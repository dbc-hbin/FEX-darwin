// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cpuid.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

struct alignas(64) Area { unsigned char bytes[832]; };
static Area seed, source, output;
static unsigned cases;
static constexpr uint32_t normal_mxcsr = 0x1f80;
static uintptr_t unreadable_header;
static unsigned header_faults;

static LONG CALLBACK header_fault(EXCEPTION_POINTERS* exception) {
  const auto& record = *exception->ExceptionRecord;
  const auto* pc = reinterpret_cast<const unsigned char*>(exception->ContextRecord->Rip);
  if (record.ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record.NumberParameters != 2 ||
      record.ExceptionInformation[0] != 0 || record.ExceptionInformation[1] != unreadable_header + 512 ||
      pc[0] != 0x0f || pc[1] != 0xae || pc[2] != 0x29) return EXCEPTION_CONTINUE_SEARCH;
  ++header_faults;
  exception->ContextRecord->Rip += 3; // xrstor [rcx]
  return EXCEPTION_CONTINUE_EXECUTION;
}

static void initialize(Area& area, bool target) {
  const uint16_t cw = target ? 0x0b7f : 0x077f;
  const uint16_t sw = (target ? 3 : 5) << 11;
  const uint32_t mxcsr = target ? 0x5f80 : 0x3f80;
  const uint64_t bv = 7;
  std::memcpy(area.bytes, &cw, 2);
  std::memcpy(area.bytes + 2, &sw, 2);
  area.bytes[4] = 0xff;
  std::memcpy(area.bytes + 24, &mxcsr, 4);
  std::memcpy(area.bytes + 512, &bv, 8);
  for (unsigned i = 0; i < 8; ++i) {
    const uint64_t mantissa = (target ? 0xf000000000000000ULL : 0xa800000000000000ULL) + (uint64_t(i) << 56);
    const uint16_t exponent = 0x4001;
    std::memcpy(area.bytes + 32 + i * 16, &mantissa, 8);
    std::memcpy(area.bytes + 40 + i * 16, &exponent, 2);
  }
  for (unsigned i = 0; i < 256; ++i) {
    area.bytes[160 + i] = (target ? 0x81 : 0x11) + i % 29;
    area.bytes[576 + i] = (target ? 0xc1 : 0x41) + i % 31;
  }
}

static bool check(const unsigned char* image, uint32_t request, uint32_t high, uint64_t bv) {
  // No C or ABI call can clobber vectors between seeding, XRSTOR and capture.
  asm volatile(
    "movl $7, %%eax; xorl %%edx, %%edx; xrstor (%[seed])\n\t"
    "movl %[request], %%eax; movl %[high], %%edx; xrstor (%[image])\n\t"
    "movl $7, %%eax; xorl %%edx, %%edx; xsave (%[output])\n\t"
    "fninit; ldmxcsr %[normal]"
    : : [seed] "r"(&seed), [image] "r"(image), [output] "r"(&output),
        [request] "r"(request), [high] "r"(high), [normal] "m"(normal_mxcsr)
    : "rax", "rdx", "memory", "cc", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)",
      "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
      "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
  const auto* x87 = request & 1 ? source.bytes : seed.bytes;
  const bool default_x87 = (request & 1) && !(bv & 1);
  const uint16_t initial_cw = 0x037f;
  if (default_x87) {
    if (std::memcmp(output.bytes, &initial_cw, 2) || output.bytes[2] || output.bytes[3] || output.bytes[4]) return false;
  } else {
    if (std::memcmp(output.bytes, x87, 5)) return false;
    for (unsigned i = 0; i < 8; ++i)
      if (std::memcmp(output.bytes + 32 + 16 * i, x87 + 32 + 16 * i, 10)) return false;
  }
  for (unsigned component = 1; component < 3; ++component) {
    const unsigned offset = component == 1 ? 160 : 576;
    for (unsigned i = 0; i < 256; ++i) {
      const auto expected = !(request & (1U << component)) ? seed.bytes[offset + i]
        : (bv & (1U << component)) ? source.bytes[offset + i] : 0;
      if (output.bytes[offset + i] != expected) return false;
    }
  }
  // MXCSR is loaded whenever SSE or AVX is requested, independently of XSTATE_BV.
  if (std::memcmp(output.bytes + 24, (request & 6 ? source.bytes : seed.bytes) + 24, 4)) return false;
  ++cases;
  return true;
}

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":64}\n");
  std::fflush(stdout);
  unsigned a, b, c, d;
  __cpuid(1, a, b, c, d);
  if ((c & (7U << 26)) != (7U << 26)) return 2;
  uint32_t xcr, high;
  asm volatile("xgetbv" : "=a"(xcr), "=d"(high) : "c"(0));
  if ((xcr & 7) != 7) return 3;
  initialize(seed, false);
  initialize(source, true);
  for (uint64_t bv = 0; bv < 8; ++bv) {
    std::memcpy(source.bytes + 512, &bv, 8);
    for (unsigned test = 0; test < 10; ++test) {
      const uint32_t request = test < 8 ? test : test == 8 ? 0 : 0xffffffff;
      const uint32_t high = test >= 8 ? 0xffffffff : 0;
      if (!check(source.bytes, request, high, bv)) {
        std::fprintf(stderr, "XRSTOR mismatch request=%08x:%08x XSTATE_BV=%llu\n", high, request, static_cast<unsigned long long>(bv));
        return 1;
      }
    }
  }
  constexpr size_t page = 0x10000;
  auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 2 * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  if (!memory) return 4;
  DWORD old;
  if (!VirtualProtect(memory + page, page, PAGE_NOACCESS, &old)) return 5;
  auto* truncated = memory + page - 576;
  for (uint64_t bv : {uint64_t(0), uint64_t(7)}) {
    std::memcpy(source.bytes + 512, &bv, 8);
    std::memcpy(truncated, source.bytes, 576);
    for (uint32_t request = 0; request < 8; ++request) {
      if ((request & 4) && (bv & 4)) continue;
      if (!check(truncated, request, 0, bv)) return 6;
    }
  }
  unreadable_header = reinterpret_cast<uintptr_t>(memory + page);
  const auto handler = AddVectoredExceptionHandler(1, header_fault);
  if (!handler) return 7;
  asm volatile("xorl %%eax, %%eax; xorl %%edx, %%edx; xrstor (%%rcx)"
               : : "c"(unreadable_header) : "rax", "rdx", "cc", "memory");
  if (header_faults != 1 || !RemoveVectoredExceptionHandler(handler)) return 8;
  ++cases;
  if (!VirtualFree(memory, 0, MEM_RELEASE)) return 9;
  std::printf("{\"xstate_checks\":\"PASS\",\"cases\":%u}\n", cases);
}
