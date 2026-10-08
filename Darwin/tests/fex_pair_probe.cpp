// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cpuid.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "Pair check failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static uintptr_t fault_pc, resume_pc, fault_address;
static unsigned faults;
static bool writing;
static LONG CALLBACK handler(EXCEPTION_POINTERS* exception) {
  const auto* record = exception->ExceptionRecord;
  if (reinterpret_cast<uintptr_t>(record->ExceptionAddress) != fault_pc ||
      exception->ContextRecord->Rip != fault_pc || record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      record->NumberParameters != 2 || record->ExceptionInformation[0] != unsigned(writing) ||
      record->ExceptionInformation[1] != fault_address || faults++) {
    std::fprintf(stderr, "Wrong pair fault: code=%lx pc=%p access=%llu address=%llx\n",
      record->ExceptionCode, record->ExceptionAddress, record->ExceptionInformation[0], record->ExceptionInformation[1]);
    return EXCEPTION_CONTINUE_SEARCH;
  }
  exception->ContextRecord->Rip = resume_pc;
  return EXCEPTION_CONTINUE_EXECUTION;
}

// Keep the segment change, access and restoration in one asm statement. No Windows call uses the temporary GS base.
template<bool Store, int Displacement>
static void transfer(void* segment, uint32_t offset, void* data) {
  uintptr_t saved_gs;
  if constexpr (Store) {
    asm volatile("rdgsbase %[saved]; wrgsbase %[base]; vmovdqu (%[data]), %%ymm0;"
                 "vmovdqu %%ymm0, %%gs:%c[disp](%%ecx); wrgsbase %[saved]; vzeroupper"
      : [saved] "=&r"(saved_gs) : [base] "r"(segment), "c"(offset), [data] "r"(data), [disp] "i"(Displacement)
      : "ymm0", "memory");
  } else {
    asm volatile("rdgsbase %[saved]; wrgsbase %[base]; vmovdqu %%gs:%c[disp](%%ecx), %%ymm0;"
                 "vmovdqu %%ymm0, (%[data]); wrgsbase %[saved]; vzeroupper"
      : [saved] "=&r"(saved_gs) : [base] "r"(segment), "c"(offset), [data] "r"(data), [disp] "i"(Displacement)
      : "ymm0", "memory");
  }
}

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":64}\n");
  std::fflush(stdout);
  unsigned a, b, c, d;
  __cpuid(1, a, b, c, d);
  CHECK(c & (1U << 28));
  constexpr size_t span = 0x100000000ULL;
  auto* window = static_cast<unsigned char*>(VirtualAlloc(nullptr, span + 0x10000, MEM_RESERVE, PAGE_NOACCESS));
  CHECK(window);
  CHECK(VirtualAlloc(window, 0x10000, MEM_COMMIT, PAGE_READWRITE) == window);
  CHECK(VirtualAlloc(window + span - 0x10000, 0x20000, MEM_COMMIT, PAGE_READWRITE) == window + span - 0x10000);
  auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
  CHECK(code);
  const auto veh = AddVectoredExceptionHandler(1, handler);
  CHECK(veh);
  alignas(32) unsigned char expected[32], actual[32];
  for (unsigned i = 0; i < 32; ++i) expected[i] = 3 + i * 7;
  unsigned completed = 0;
  for (unsigned store = 0; store < 2; ++store) {
    for (unsigned negative = 0; negative < 2; ++negative) {
      // Native Darwin reserves the first 4GB. The fault must use the wrapped EA,
      // not a 64-bit address produced by adding the pair displacement afterwards.
      const uint32_t offset = negative ? 0x10 : 0xfffffff0;
      const unsigned char displacement = negative ? 0xc0 : 0x40;
      const unsigned char program[] = {0xc5, 0xfe, 0x6f, 0x02,
        0x67, 0xc5, 0xfe, static_cast<unsigned char>(store ? 0x7f : 0x6f), 0x41, displacement,
        0xc5, 0xfe, 0x7f, 0x02, 0xc5, 0xf8, 0x77, 0xc3};
      std::memcpy(code, program, sizeof(program));
      fault_pc = reinterpret_cast<uintptr_t>(code + 4);
      resume_pc = reinterpret_cast<uintptr_t>(code + 10);
      fault_address = negative ? 0xffffffd0 : 0x30;
      writing = store;
      faults = 0;
      std::memcpy(actual, expected, sizeof(actual));
      CHECK(FlushInstructionCache(GetCurrentProcess(), code, sizeof(program)));
      reinterpret_cast<void (*)(uint32_t, void*)>(code)(offset, actual);
      CHECK(faults == 1 && !std::memcmp(actual, expected, sizeof(actual)));
      ++completed;
    }
    for (unsigned which = 0; which < 3; ++which) {
      const uint32_t offset = which == 0 ? 0xfffffff0 : which == 1 ? 0x10 : 0xffffffb0;
      const uint32_t target = which == 0 ? 0x30 : which == 1 ? 0xffffffd0 : 0xfffffff0;
      std::memset(window, 0xa5, 32);
      std::memset(actual, 0, sizeof(actual));
      std::memcpy(window + target, store ? actual : expected, 32);
      uintptr_t original_gs, restored_gs;
      asm volatile("rdgsbase %0" : "=r"(original_gs));
      if (store) {
        if (which == 1) transfer<true, -64>(window, offset, expected);
        else transfer<true, 64>(window, offset, expected);
      } else {
        if (which == 1) transfer<false, -64>(window, offset, actual);
        else transfer<false, 64>(window, offset, actual);
      }
      asm volatile("rdgsbase %0" : "=r"(restored_gs));
      CHECK(restored_gs == original_gs);
      if (store) std::memcpy(actual, window + target, sizeof(actual));
      CHECK(!std::memcmp(actual, expected, sizeof(actual)));
      for (unsigned i = 0; i < 32; ++i) CHECK(window[i] == 0xa5);
      ++completed;
    }
  }
  CHECK(RemoveVectoredExceptionHandler(veh));
  CHECK(VirtualFree(code, 0, MEM_RELEASE));
  CHECK(VirtualFree(window, 0, MEM_RELEASE));
  std::printf("{\"pair_checks\":\"PASS\",\"cases\":%u}\n", completed);
}
