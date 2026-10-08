// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static uintptr_t fault_pc, resume_pc;
static unsigned faults;
static LONG CALLBACK handler(EXCEPTION_POINTERS* exception) {
  const auto* record = exception->ExceptionRecord;
  if (reinterpret_cast<uintptr_t>(record->ExceptionAddress) != fault_pc ||
      exception->ContextRecord->Eip != fault_pc || record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      record->NumberParameters != 2 || record->ExceptionInformation[0] != 0 ||
      record->ExceptionInformation[1] != 0xffffffffU || faults++) {
    std::fprintf(stderr, "Wrong boundary fault: code=%lx pc=%p access=%lx address=%lx\n",
      record->ExceptionCode, record->ExceptionAddress, record->ExceptionInformation[0], record->ExceptionInformation[1]);
    return EXCEPTION_CONTINUE_SEARCH;
  }
  exception->ContextRecord->Eip = resume_pc;
  return EXCEPTION_CONTINUE_EXECUTION;
}

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "Boundary check failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":%u}\n", unsigned(sizeof(void*) * 8));
  std::fflush(stdout);
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto query = reinterpret_cast<LONG (WINAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*)>(GetProcAddress(ntdll, "NtWow64QueryInformationProcess64"));
  const auto allocate = reinterpret_cast<LONG (WINAPI*)(HANDLE, uint64_t*, uint64_t, uint64_t*, ULONG, ULONG)>(GetProcAddress(ntdll, "NtWow64AllocateVirtualMemory64"));
  const auto read_memory = reinterpret_cast<LONG (WINAPI*)(HANDLE, uint64_t, void*, uint64_t, uint64_t*)>(GetProcAddress(ntdll, "NtWow64ReadVirtualMemory64"));
  const auto write_memory = reinterpret_cast<LONG (WINAPI*)(HANDLE, uint64_t, const void*, uint64_t, uint64_t*)>(GetProcAddress(ntdll, "NtWow64WriteVirtualMemory64"));
  CHECK(query && allocate && read_memory && write_memory);
  alignas(8) uint64_t info[6] {};
  CHECK(!query(GetCurrentProcess(), 0, info, sizeof(info), nullptr));
  const uint64_t base = info[1] & ~0xffffffffULL;
  CHECK(base);
  unsigned sentinel = 0x12345678, copied = 0;
  CHECK(!read_memory(GetCurrentProcess(), base + reinterpret_cast<uintptr_t>(&sentinel), &copied, sizeof(copied), nullptr));
  CHECK(copied == sentinel);
  // This PE stays non-LAA: Wine keeps ordinary WoW64 allocations below 2GB,
  // leaving the final 64KiB free for these explicit native-width allocations.
  for (const uint64_t offset : {0ULL, 0xffff0000ULL}) {
    uint64_t address = base + offset, size = 0x10000;
    const auto status = allocate(GetCurrentProcess(), &address, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (status) std::fprintf(stderr, "Boundary allocation offset=%llx status=%lx\n", offset, static_cast<unsigned long>(status));
    CHECK(!status && address == base + offset && size == 0x10000);
  }
  std::printf("Boundary host window=%llx; first and last 64KiB committed\n", base);
  std::fflush(stdout);
  auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
  CHECK(code);
  const auto veh = AddVectoredExceptionHandler(1, handler);
  CHECK(veh);
  struct Case { const char* name; unsigned char instruction[3]; unsigned length; uint32_t address; };
  const Case cases[] = {
    {"fld80", {0xdb, 0x29}, 2, 0xfffffff8},
    {"fstp80", {0xdb, 0x39}, 2, 0xfffffff8},
    {"fldenv32", {0xd9, 0x21}, 2, 0xfffffff0},
    {"fnstenv32", {0xd9, 0x31}, 2, 0xfffffff0},
    {"fldenv16", {0x66, 0xd9, 0x21}, 3, 0xfffffff8},
    {"fnstenv16", {0x66, 0xd9, 0x31}, 3, 0xfffffff8},
    {"frstor32", {0xdd, 0x21}, 2, 0xffffffc0},
    {"fnsave32", {0xdd, 0x31}, 2, 0xffffffc0},
    {"frstor16", {0x66, 0xdd, 0x21}, 3, 0xffffffc0},
    {"fnsave16", {0x66, 0xdd, 0x31}, 3, 0xffffffc0},
    {"fxsave", {0x0f, 0xae, 0x01}, 3, 0xfffffe10},
    {"fxrstor", {0x0f, 0xae, 0x09}, 3, 0xfffffe10},
  };
  unsigned completed = 0;
  for (const auto& test : cases) {
    unsigned char expected[1024], first[1024], last[1024];
    std::memset(expected, 0xa5, sizeof(expected));
    CHECK(!write_memory(GetCurrentProcess(), base, expected, sizeof(expected), nullptr));
    CHECK(!write_memory(GetCurrentProcess(), base + 0xfffffc00, expected, sizeof(expected), nullptr));
    alignas(16) unsigned char states[1024] {};
    // fastcall ECX is the operand; EDX is the two-image state buffer.
    constexpr unsigned char before[] = {0xdb, 0xe3, 0xd9, 0xe8, 0x0f, 0xae, 0x02};
    constexpr unsigned char after[] = {0x0f, 0xae, 0x82, 0x00, 0x02, 0x00, 0x00, 0xdb, 0xe3, 0xc3};
    std::memcpy(code, before, sizeof(before));
    std::memcpy(code + sizeof(before), test.instruction, test.length);
    std::memcpy(code + sizeof(before) + test.length, after, sizeof(after));
    fault_pc = reinterpret_cast<uintptr_t>(code + sizeof(before));
    resume_pc = fault_pc + test.length;
    faults = 0;
    CHECK(FlushInstructionCache(GetCurrentProcess(), code, sizeof(before) + test.length + sizeof(after)));
    std::printf("Boundary case=%s\n", test.name);
    std::fflush(stdout);
    reinterpret_cast<void (__fastcall*)(uintptr_t, void*)>(code)(test.address, states);
    CHECK(faults == 1);
    // FCW, FSW (including TOP), FTW and all eight 80-bit slots must survive the fault.
    CHECK(!std::memcmp(states, states + 512, 5));
    for (unsigned i = 0; i < 8; ++i) CHECK(!std::memcmp(states + 32 + i * 16, states + 544 + i * 16, 10));
    CHECK(!read_memory(GetCurrentProcess(), base, first, sizeof(first), nullptr));
    CHECK(!read_memory(GetCurrentProcess(), base + 0xfffffc00, last, sizeof(last), nullptr));
    CHECK(!std::memcmp(first, expected, sizeof(first)) && !std::memcmp(last, expected, sizeof(last)));
    ++completed;
  }
  // The last legal ten-byte operand must not be rejected by the upper-bound check.
  const unsigned char one[10] = {0, 0, 0, 0, 0, 0, 0, 0x80, 0xff, 0x3f};
  CHECK(!write_memory(GetCurrentProcess(), base + 0xfffffff6, one, sizeof(one), nullptr));
  unsigned char roundtrip[10] {};
  const uintptr_t address = 0xfffffff6;
  asm volatile("fninit; fldt (%1); fstpt %0; fninit" : "=m"(roundtrip) : "r"(address) : "st", "memory");
  CHECK(!std::memcmp(roundtrip, one, sizeof(one)));
  ++completed;
  CHECK(RemoveVectoredExceptionHandler(veh));
  CHECK(VirtualFree(code, 0, MEM_RELEASE));
  std::printf("{\"boundary_checks\":\"PASS\",\"cases\":%u}\n", completed);
}
