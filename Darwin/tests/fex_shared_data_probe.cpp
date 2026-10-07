// SPDX-License-Identifier: MIT
// Build as an ordinary x86_64 Windows executable; no relocated-pointer accessor.
#include <windows.h>
#include <winternl.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <immintrin.h>

static volatile LONG WriteFaults;
static LONG CALLBACK WriteHandler(EXCEPTION_POINTERS* E) {
  const auto* R = E->ExceptionRecord;
  if (R->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || R->NumberParameters < 2 ||
      R->ExceptionInformation[0] != 1 || R->ExceptionInformation[1] != 0x7ffe0260 ||
      E->ContextRecord->Rax != 0x7ffe0260) return EXCEPTION_CONTINUE_SEARCH;
  E->ContextRecord->Rip += 3; // c6 00 01: movb $1,(%rax)
  InterlockedIncrement(&WriteFaults);
  return EXCEPTION_CONTINUE_EXECUTION;
}

int main() {
  std::puts("FEX_SHARED_DATA_ENTER");
  std::fflush(stdout);
  RTL_OSVERSIONINFOW Version {};
  Version.dwOSVersionInfoSize = sizeof(Version);
  using GetVersion = NTSTATUS (WINAPI *)(PRTL_OSVERSIONINFOW);
  const auto Get = reinterpret_cast<GetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
  if (!Get || Get(&Version)) return 1;
  uint32_t ConstantMajor;
  asm volatile("movl 0x7ffe026c, %0" : "=r"(ConstantMajor) : : "memory");
  uintptr_t Base = 0x7ffe0000;
  const uintptr_t Before = Base;
  uint32_t Major, Minor, Build;
  uint16_t Build16;
  uint8_t Build8;
  uint64_t VersionPair;
  __m128i Vector;
  asm volatile("movl 0x26c(%1), %0" : "=r"(Major) : "r"(Base) : "memory");
  asm volatile("movl 0x270(%1), %0" : "=r"(Minor) : "r"(Base) : "memory");
  asm volatile("movl 0x260(%1), %0" : "=r"(Build) : "r"(Base) : "memory");
  asm volatile("movw 0x260(%1), %0" : "=r"(Build16) : "r"(Base) : "memory");
  asm volatile("movb 0x260(%1), %0" : "=q"(Build8) : "r"(Base) : "memory");
  asm volatile("movq 0x26c(%1), %0" : "=r"(VersionPair) : "r"(Base) : "memory");
  asm volatile("movdqu 0x260(%1), %0" : "=x"(Vector) : "r"(Base) : "memory");
  uint32_t Words[4];
  std::memcpy(Words, &Vector, sizeof(Words));
  if (Base != Before || Major != ConstantMajor || Major != Version.dwMajorVersion || Minor != Version.dwMinorVersion ||
      (Build & 0xffff) != Version.dwBuildNumber || Build16 != (Build & 0xffff) || Build8 != (Build & 255) ||
      VersionPair != (uint64_t(Minor) << 32 | Major) || Words[0] != Build || Words[3] != Major) return 2;
  // The live shared clock must have valid KSYSTEM_TIME high/low/high coherence.
  uint32_t High1, Low, High2;
  do {
    High1 = *reinterpret_cast<volatile uint32_t*>(0x7ffe0018);
    Low = *reinterpret_cast<volatile uint32_t*>(0x7ffe0014);
    High2 = *reinterpret_cast<volatile uint32_t*>(0x7ffe001c);
  } while (High1 != High2);
  if (!(uint64_t(High1) << 32 | Low)) return 3;
  const auto Handler = AddVectoredExceptionHandler(1, WriteHandler);
  if (!Handler) return 4;
  uintptr_t WriteAddress = 0x7ffe0260;
  asm volatile(".byte 0xc6, 0x00, 0x01" : "+a"(WriteAddress) : : "memory");
  RemoveVectoredExceptionHandler(Handler);
  const uint32_t AfterWrite = *reinterpret_cast<volatile uint32_t*>(0x7ffe0260);
  if (WriteFaults != 1 || WriteAddress != 0x7ffe0260 || AfterWrite != Build) {
    std::fprintf(stderr, "FEX_SHARED_DATA_WRITE_FAIL faults=%ld address=0x%llx build=0x%x expected=0x%x\n",
                 WriteFaults, static_cast<unsigned long long>(WriteAddress), AfterWrite, Build);
    return 5;
  }
  std::printf("PASS: fixed-address constant/register scalar 8/16/32/64, SSE128, clock and read-only AV; Windows %u.%u build %u\n",
              Major, Minor, Build & 0xffff);
  return 0;
}
