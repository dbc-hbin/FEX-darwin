// SPDX-License-Identifier: MIT
// Keep the i686 linker's non-LAA default: the native guest-window allocations
// below require the first and last 64KiB to remain unclaimed (checked at entry).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

constexpr size_t Page = 0x4000;
constexpr size_t Arena = 3 * Page;
constexpr uintptr_t FlagMask = 0xcd5; // OF, DF, SF, ZF, AF, PF, CF.
constexpr uintptr_t ArithmeticFlags = 0x8d7;
constexpr uintptr_t Fill = sizeof(uintptr_t) == 8 ? uintptr_t(0x78695a4b3c2d1e0fULL) : uintptr_t(0x3c2d1e0f);
static const char* case_name = "setup";
static unsigned completed;

[[noreturn]] static void fail(const char* condition, unsigned line) {
  std::fprintf(stderr, "REP check failed: case=%s line=%u: %s\n", case_name, line, condition);
  std::fflush(stderr);
  ExitProcess(1);
}
#define CHECK(condition) do { if (!(condition)) fail(#condition, __LINE__); } while (0)

struct Registers { uintptr_t si, di, cx, flags; };
static Registers result;
struct Fault {
  uintptr_t pc, si, di, cx, flags, address;
  unsigned access, seen;
  void* page;
  unsigned char* source;
  unsigned width, done;
  bool backward, mutate;
};
static Fault fault;

static LONG CALLBACK handler(EXCEPTION_POINTERS* exception) {
  // Windows/C++ ABI requires a clear *actual* DF, even for a saved backward REP.
  // Do this before any C++ library or Windows call; ContextRecord is untouched.
  asm volatile("cld" ::: "cc");
  const auto& record = *exception->ExceptionRecord;
  const auto& context = *exception->ContextRecord;
#ifdef _WIN64
  const Registers saved {uintptr_t(context.Rsi), uintptr_t(context.Rdi), uintptr_t(context.Rcx), context.EFlags};
  const uintptr_t pc = context.Rip;
#else
  const Registers saved {context.Esi, context.Edi, context.Ecx, context.EFlags};
  const uintptr_t pc = context.Eip;
#endif
  CHECK(fault.page && !fault.seen);
  CHECK(record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION);
  CHECK(record.NumberParameters == 2);
  CHECK(reinterpret_cast<uintptr_t>(record.ExceptionAddress) == fault.pc && pc == fault.pc);
  CHECK(record.ExceptionInformation[0] == fault.access);
  CHECK(record.ExceptionInformation[1] == fault.address);
  CHECK(saved.si == fault.si && saved.di == fault.di && saved.cx == fault.cx);
  CHECK((saved.flags & FlagMask) == (fault.flags & FlagMask));
  ++fault.seen;
  DWORD old;
  CHECK(VirtualProtect(fault.page, Page, PAGE_READWRITE, &old));
  CHECK(old == PAGE_NOACCESS);
  if (fault.mutate) {
    for (unsigned i = 0; i < fault.done; ++i) {
      auto* copied = fault.backward ? fault.source - i * fault.width : fault.source + i * fault.width;
      for (unsigned j = 0; j < fault.width; ++j) copied[j] ^= 0xff;
    }
  }
  // The exact saved instruction pointer is retained. No advance, register repair,
  // event timing, or retry loop can hide a broken architectural restart.
  return EXCEPTION_CONTINUE_EXECUTION;
}

// Each snippet has a fixed 128-byte bound, no loops except the instruction under
// test, and no C++ callbacks while DF is set. Immediate inputs avoid ABI argument
// registers and preserve the caller's nonvolatile SI/DI on both Windows ABIs.
static unsigned char* code;
static size_t used;
static void byte(unsigned char value) { CHECK(used < 128); code[used++] = value; }
static void immediate(uintptr_t value) {
  CHECK(used + sizeof(value) <= 128);
  std::memcpy(code + used, &value, sizeof(value));
  used += sizeof(value);
}
static void load(unsigned reg, uintptr_t value) {
#ifdef _WIN64
  byte(0x48);
#endif
  byte(0xb8 + reg);
  immediate(value);
}
static void store(unsigned modrm, unsigned offset) {
#ifdef _WIN64
  byte(0x48);
#endif
  byte(0x89); byte(modrm); byte(offset);
}
static uintptr_t execute(bool stos, unsigned width, bool backward, uintptr_t si,
                         uintptr_t di, uintptr_t cx, bool address_override = false,
                         bool repeat = true, bool segment = false) {
  used = 0;
  byte(0x56); byte(0x57); // push SI, DI
  load(0, Fill); load(6, si); load(7, di); load(1, cx);
  load(2, reinterpret_cast<uintptr_t>(&result));
  // push imm32; popf[q]. All six arithmetic flags are intentionally set, so
  // internal pointer/count arithmetic must not leak its flags to guest state.
  byte(0x68);
  const uint32_t flags = ArithmeticFlags | (backward ? 0x400 : 0);
  CHECK(used + sizeof(flags) <= 128);
  std::memcpy(code + used, &flags, sizeof(flags)); used += sizeof(flags);
  byte(0x9d);
  const auto pc = reinterpret_cast<uintptr_t>(code + used);
  if (segment) {
#ifdef _WIN64
    byte(0x65); // GS: Windows x64 TEB
#else
    byte(0x64); // FS: Windows x86 TEB
#endif
  }
  if (address_override) byte(0x67);
  if (repeat) byte(0xf3);
  if (width == 2) byte(0x66);
#ifdef _WIN64
  if (width == 8) byte(0x48);
#endif
  byte(stos ? (width == 1 ? 0xaa : 0xab) : (width == 1 ? 0xa4 : 0xa5));
  byte(0x9c); byte(0x58); // pushf[q]; pop AX
  store(0x42, 3 * sizeof(uintptr_t));
  store(0x72, 0); store(0x7a, sizeof(uintptr_t)); store(0x4a, 2 * sizeof(uintptr_t));
  byte(0xfc); byte(0x5f); byte(0x5e); byte(0xc3); // cld; restore; ret
  result = {};
  if (fault.page) fault.pc = pc;
  CHECK(FlushInstructionCache(GetCurrentProcess(), code, used));
  reinterpret_cast<void (*)()>(code)();
  if ((result.flags & FlagMask) != (flags & FlagMask))
    std::fprintf(stderr, "REP flags after=%llx expected=%x\n", static_cast<unsigned long long>(result.flags), flags);
  CHECK((result.flags & FlagMask) == (flags & FlagMask));
  return pc;
}

static uintptr_t advance(uintptr_t value, unsigned width, unsigned count, bool backward) {
  return backward ? value - width * count : value + width * count;
}
static void registers(bool stos, uintptr_t si, uintptr_t di, uintptr_t cx) {
  CHECK(result.si == si);
  CHECK(result.di == di);
  CHECK(result.cx == cx);
  if (stos) CHECK(result.si == si); // STOS must not touch its unused source register.
}

static void normal_cases(unsigned char* source, unsigned char* dest, unsigned char* expected) {
  char name[128];
#ifdef _WIN64
  constexpr unsigned max_width = 8;
#else
  constexpr unsigned max_width = 4;
#endif
  for (unsigned width = 1; width <= max_width; width *= 2) {
    for (bool backward : {false, true}) {
      // Aligned faults follow four elements; a split element faults after three.
      for (unsigned split = 0; split < (width == 1 ? 1U : 2U); ++split) {
        const unsigned done = split ? 3 : 4;
        // MOVS source read and destination write faults; STOS destination faults.
        for (unsigned kind = 0; kind < 3; ++kind) {
          const bool stos = kind == 2, source_fault = kind == 0;
          std::snprintf(name, sizeof(name), "%s%u-%s-%s-fault%s", stos ? "stos" : "movs", width * 8,
                        backward ? "backward" : "forward", source_fault ? "read" : "write", split ? "-split" : "");
          case_name = name;
          for (size_t i = 0; i < Arena; ++i) source[i] = static_cast<unsigned char>(i * 17 + i / 256 + 3);
          std::memset(dest, 0xa5, Arena); std::memcpy(expected, dest, Arena);
          const ptrdiff_t start = backward ? ptrdiff_t(3 * width) - split : -ptrdiff_t(4 * width) + split;
          auto* s = source + Page + start;
          auto* d = dest + Page + start;
          for (unsigned i = 0; i < 8; ++i) {
            const auto offset = advance(reinterpret_cast<uintptr_t>(d), width, i, backward) - reinterpret_cast<uintptr_t>(dest);
            const auto from = advance(reinterpret_cast<uintptr_t>(s), width, i, backward);
            std::memcpy(expected + offset, stos ? static_cast<const void*>(&Fill) : reinterpret_cast<const void*>(from), width);
          }
          void* locked = (source_fault ? source : dest) + (backward ? 0 : Page);
          DWORD old;
          CHECK(VirtualProtect(locked, Page, PAGE_NOACCESS, &old));
          fault = {0, stos ? reinterpret_cast<uintptr_t>(s) : advance(reinterpret_cast<uintptr_t>(s), width, done, backward),
                   advance(reinterpret_cast<uintptr_t>(d), width, done, backward), 8 - done,
                   ArithmeticFlags | (backward ? 0x400U : 0U),
                   split ? reinterpret_cast<uintptr_t>(locked) + (backward ? Page - 1 : 0)
                         : advance(reinterpret_cast<uintptr_t>(source_fault ? s : d), width, done, backward),
                   source_fault ? 0U : 1U, 0, locked, s, width, done, backward, !stos};
          execute(stos, width, backward, reinterpret_cast<uintptr_t>(s), reinterpret_cast<uintptr_t>(d), 8);
          CHECK(fault.seen == 1);
          registers(stos, stos ? reinterpret_cast<uintptr_t>(s) : advance(reinterpret_cast<uintptr_t>(s), width, 8, backward),
                    advance(reinterpret_cast<uintptr_t>(d), width, 8, backward), 0);
          CHECK(!std::memcmp(dest, expected, Arena));
          fault = {}; ++completed;
        }
      }
      // A memcpy/memmove substitution is not valid: scalar MOVS can propagate
      // the just-written bytes on overlap, in either direction.
      std::snprintf(name, sizeof(name), "movs%u-%s-overlap", width * 8, backward ? "backward" : "forward");
      case_name = name;
      for (size_t i = 0; i < Arena; ++i) dest[i] = static_cast<unsigned char>(i * 29 + 7);
      std::memcpy(expected, dest, Arena);
      auto* s = dest + 256;
      auto* d = backward ? s - 1 : s + 1;
      for (unsigned i = 0; i < 8; ++i) {
        unsigned char element[8];
        const auto so = advance(reinterpret_cast<uintptr_t>(s), width, i, backward) - reinterpret_cast<uintptr_t>(dest);
        const auto ds = advance(reinterpret_cast<uintptr_t>(d), width, i, backward) - reinterpret_cast<uintptr_t>(dest);
        std::memcpy(element, expected + so, width); std::memcpy(expected + ds, element, width);
      }
      execute(false, width, backward, reinterpret_cast<uintptr_t>(s), reinterpret_cast<uintptr_t>(d), 8);
      registers(false, advance(reinterpret_cast<uintptr_t>(s), width, 8, backward), advance(reinterpret_cast<uintptr_t>(d), width, 8, backward), 0);
      CHECK(!std::memcmp(dest, expected, Arena)); ++completed;
      for (bool stos : {false, true}) {
        std::snprintf(name, sizeof(name), "%s%u-%s-zero", stos ? "stos" : "movs", width * 8, backward ? "backward" : "forward");
        case_name = name;
        DWORD old;
        CHECK(VirtualProtect(source, Page, PAGE_NOACCESS, &old));
        CHECK(VirtualProtect(dest, Page, PAGE_NOACCESS, &old));
        const auto si = reinterpret_cast<uintptr_t>(source), di = reinterpret_cast<uintptr_t>(dest);
        execute(stos, width, backward, si, di, 0);
        registers(stos, si, di, 0);
#ifdef _WIN64
        // Address-size 32 examines only ECX. Zero iterations must not zero the
        // upper halves of any register, even though ordinary updates do.
        // The low offsets are in Windows' permanently inaccessible null 64KiB.
        const uintptr_t high_si = 0x1234567800000010ULL;
        const uintptr_t high_di = 0x2345678900000020ULL;
        constexpr uintptr_t high_cx = 0x3456789a00000000ULL;
        execute(stos, width, backward, high_si, high_di, high_cx, true);
        registers(stos, high_si, high_di, high_cx); ++completed;
#else
        const uintptr_t high_si = 0x13570010U, high_di = 0x24680020U, high_cx = 0x369a0000U;
        execute(stos, width, backward, high_si, high_di, high_cx, true);
        registers(stos, high_si, high_di, high_cx); ++completed;
#endif
        CHECK(VirtualProtect(source, Page, PAGE_READWRITE, &old));
        CHECK(VirtualProtect(dest, Page, PAGE_READWRITE, &old));
        CHECK(!std::memcmp(dest, expected, Arena)); ++completed;
      }
    }
  }
}

#ifndef _WIN64
using Query64 = LONG (WINAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
using Allocate64 = LONG (WINAPI*)(HANDLE, uint64_t*, uint64_t, uint64_t*, ULONG, ULONG);
using Read64 = LONG (WINAPI*)(HANDLE, uint64_t, void*, uint64_t, uint64_t*);
using Write64 = LONG (WINAPI*)(HANDLE, uint64_t, const void*, uint64_t, uint64_t*);
static Read64 read64;
static Write64 write64;
static uint64_t guest_base;

static size_t boundary_index(uint32_t offset, bool address16) {
  if (address16) return uint16_t(offset);
  CHECK(offset < 0x10000 || offset >= 0xffff0000U);
  return offset < 0x10000 ? offset : 0x10000 + (offset - 0xffff0000U);
}

static void boundary_cases() {
  case_name = "native-window-setup";
  const auto* image = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
  CHECK(!(nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE));
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto query = reinterpret_cast<Query64>(GetProcAddress(ntdll, "NtWow64QueryInformationProcess64"));
  const auto allocate = reinterpret_cast<Allocate64>(GetProcAddress(ntdll, "NtWow64AllocateVirtualMemory64"));
  read64 = reinterpret_cast<Read64>(GetProcAddress(ntdll, "NtWow64ReadVirtualMemory64"));
  write64 = reinterpret_cast<Write64>(GetProcAddress(ntdll, "NtWow64WriteVirtualMemory64"));
  CHECK(query && allocate && read64 && write64);
  alignas(8) uint64_t info[6] {};
  CHECK(!query(GetCurrentProcess(), 0, info, sizeof(info), nullptr));
  guest_base = info[1] & ~0xffffffffULL;
  CHECK(guest_base);
  uint32_t sentinel = 0x12345678, copied = 0;
  CHECK(!read64(GetCurrentProcess(), guest_base + reinterpret_cast<uintptr_t>(&sentinel), &copied, sizeof(copied), nullptr));
  CHECK(copied == sentinel);
  for (uint64_t offset : {0ULL, 0xffff0000ULL}) {
    uint64_t address = guest_base + offset, size = 0x10000;
    const auto status = allocate(GetCurrentProcess(), &address, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (status) std::fprintf(stderr, "REP native allocation offset=%llx status=%lx\n", offset, static_cast<unsigned long>(status));
    CHECK(!status && address == guest_base + offset && size == 0x10000);
  }
  static unsigned char expected[0x20000], actual[0x20000];
  char name[128];
  for (bool address16 : {false, true}) {
    for (unsigned width : {1U, 2U, 4U}) {
      for (bool backward : {false, true}) {
        for (bool stos : {false, true}) {
          std::snprintf(name, sizeof(name), "%s%u-address%u-%s-wrap", stos ? "stos" : "movs", width * 8,
                        address16 ? 16 : 32, backward ? "backward" : "forward");
          case_name = name;
          for (size_t i = 0; i < sizeof(expected); ++i) expected[i] = static_cast<unsigned char>(i * 31 + i / 128 + 5);
          CHECK(!write64(GetCurrentProcess(), guest_base, expected, 0x10000, nullptr));
          CHECK(!write64(GetCurrentProcess(), guest_base + 0xffff0000ULL, expected + 0x10000, 0x10000, nullptr));
          const uint32_t mask = address16 ? 0xffff : 0xffffffffU;
          const uint32_t slo = (backward ? 3 * width : uint32_t(-int(4 * width))) & mask;
          const uint32_t dlo = (backward ? 2 * width : uint32_t(-int(3 * width))) & mask;
          const uintptr_t si = address16 ? 0x13570000U | slo : slo;
          const uintptr_t di = address16 ? 0x24680000U | dlo : dlo;
          const uintptr_t cx = address16 ? 0x369a0008U : 8;
          for (unsigned i = 0; i < 8; ++i) {
            unsigned char element[8];
            const uint32_t so = advance(slo, width, i, backward) & mask;
            const uint32_t ds = advance(dlo, width, i, backward) & mask;
            if (stos) std::memcpy(element, &Fill, width);
            else for (unsigned j = 0; j < width; ++j) element[j] = expected[boundary_index((so + j) & mask, address16)];
            for (unsigned j = 0; j < width; ++j) expected[boundary_index((ds + j) & mask, address16)] = element[j];
          }
          execute(stos, width, backward, si, di, cx, address16);
          const uintptr_t end_si = stos ? si : (address16 ? (si & 0xffff0000U) | (advance(slo, width, 8, backward) & mask) : advance(slo, width, 8, backward));
          const uintptr_t end_di = address16 ? (di & 0xffff0000U) | (advance(dlo, width, 8, backward) & mask) : advance(dlo, width, 8, backward);
          registers(stos, end_si, end_di, address16 ? cx & 0xffff0000U : 0);
          CHECK(!read64(GetCurrentProcess(), guest_base, actual, 0x10000, nullptr));
          CHECK(!read64(GetCurrentProcess(), guest_base + 0xffff0000ULL, actual + 0x10000, 0x10000, nullptr));
          CHECK(!std::memcmp(actual, expected, sizeof(expected))); ++completed;
        }
      }
    }
  }
}
#endif

static void segment_cases() {
  uintptr_t self = 0;
#ifdef _WIN64
  constexpr uintptr_t offset = 0x30;
  asm volatile("movq %%gs:0x30, %0" : "=r"(self));
#else
  constexpr uintptr_t offset = 0x18;
  asm volatile("movl %%fs:0x18, %0" : "=r"(self));
#endif
  CHECK(self);
  for (bool repeat : {false, true}) {
    for (bool backward : {false, true}) {
      case_name = repeat ? "segment-movs-rep1" : "segment-movs-nonrep";
      uintptr_t copied = 0;
      const auto di = reinterpret_cast<uintptr_t>(&copied);
      const uintptr_t count = repeat ? 1 : 0x12345678;
      execute(false, sizeof(uintptr_t), backward, offset, di, count, false, repeat, true);
      registers(false, advance(offset, sizeof(uintptr_t), 1, backward), advance(di, sizeof(uintptr_t), 1, backward), repeat ? 0 : count);
      CHECK(copied == self); ++completed;
    }
  }
}

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":%u}\n", unsigned(sizeof(void*) * 8));
  std::fflush(stdout);
  code = static_cast<unsigned char*>(VirtualAlloc(nullptr, Page, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
  auto* source = static_cast<unsigned char*>(VirtualAlloc(nullptr, Arena, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  auto* dest = static_cast<unsigned char*>(VirtualAlloc(nullptr, Arena, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  auto* expected = static_cast<unsigned char*>(std::malloc(Arena));
  CHECK(code && source && dest && expected);
  CHECK(!(reinterpret_cast<uintptr_t>(source) & (Page - 1)) && !(reinterpret_cast<uintptr_t>(dest) & (Page - 1)));
  const auto veh = AddVectoredExceptionHandler(1, handler);
  CHECK(veh);
  normal_cases(source, dest, expected);
#ifndef _WIN64
  boundary_cases();
#endif
  segment_cases();
  case_name = "cleanup";
  CHECK(RemoveVectoredExceptionHandler(veh));
  CHECK(VirtualFree(code, 0, MEM_RELEASE));
  CHECK(VirtualFree(source, 0, MEM_RELEASE));
  CHECK(VirtualFree(dest, 0, MEM_RELEASE));
  std::free(expected);
  std::printf("{\"rep_checks\":\"PASS\",\"cases\":%u}\n", completed);
}
