// SPDX-License-Identifier: MIT
// llvm-mingw {x86_64,i686}-w64-mingw32-clang++ -std=c++17 -O2 -msse2
//   -fno-vectorize -fno-slp-vectorize -static fex_x87_probe.cpp -o fex_x87_probe.exe
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct Extended {
  uint64_t mantissa;
  uint16_t exponent;
} __attribute__((packed));
static_assert(sizeof(Extended) == 10);

struct Classification {
  const char* name;
  Extended value;
  uint16_t flags;
};

// Raw encodings and C3:C2:C1:C0 expectations from Intel's FXAM class table.
static const Classification classifications[] = {
  {"positive_zero", {0, 0}, 0x4000},
  {"negative_zero", {0, 0x8000}, 0x4200},
  {"positive_one", {0x8000000000000000ULL, 0x3fff}, 0x0400},
  {"negative_one", {0x8000000000000000ULL, 0xbfff}, 0x0600},
  {"positive_infinity", {0x8000000000000000ULL, 0x7fff}, 0x0500},
  {"negative_infinity", {0x8000000000000000ULL, 0xffff}, 0x0700},
  {"quiet_nan", {0xc000000000000000ULL, 0x7fff}, 0x0100},
  {"negative_signaling_nan", {0x8000000000000001ULL, 0xffff}, 0x0300},
  {"denormal", {1, 0}, 0x4400},
  {"pseudo_denormal", {0x8000000000000000ULL, 0}, 0x4400},
  {"pseudo_denormal_fraction", {0x8000000000000001ULL, 0}, 0x4400},
  {"unnormal", {0x4000000000000000ULL, 0x3fff}, 0x0000},
  {"pseudo_infinity", {0, 0x7fff}, 0x0000},
  {"pseudo_nan", {0x4000000000000000ULL, 0x7fff}, 0x0000},
  {"pseudo_zero", {0, 0x3fff}, 0x0000},
  {"minimum_normal_exponent", {0x8000000000000000ULL, 1}, 0x0400},
  {"maximum_normal_exponent", {0x8000000000000000ULL, 0x7ffe}, 0x0400},
};

struct alignas(16) RoundTrip {
  uint8_t saved[512];
  uint8_t restored[512];
  double popped[8];
  double values[8];
};
static_assert(offsetof(RoundTrip, restored) == 512);
static_assert(offsetof(RoundTrip, popped) == 1024);
static_assert(offsetof(RoundTrip, values) == 1088);

int main() {
  const unsigned pointer_bits = sizeof(void*) * 8;
  const char* reduced_env = std::getenv("FEX_X87REDUCEDPRECISION");
  const bool reduced = reduced_env && std::strcmp(reduced_env, "1") == 0;
  std::printf("{\"guest_entry\":true,\"pointer_bits\":%u}\n", pointer_bits);
  std::fflush(stdout);
  unsigned cases = 0;
  unsigned failures = 0;
  unsigned skipped = 0;
  uint16_t status;
  // Leave a known positive ST(0) payload, then mark it empty; C1 is still defined.
  asm volatile("fninit; fld1; ffree %%st(0); fxam; fnstsw %%ax; fninit"
               : "=a"(status) : : "st", "memory");
  ++cases;
  if ((status & 0x4700) != 0x4100) {
    std::printf("FEX_X87_FAIL case=empty actual=%04x expected=4100\n", status & 0x4700);
    ++failures;
  }
  for (const auto& test : classifications) {
    // Reduced precision cannot preserve raw extended-only encodings or ranges.
    if (reduced && ((test.flags & 0x4500) == 0 || (test.flags & 0x4500) == 0x4400 ||
                    test.value.exponent == 1 || test.value.exponent == 0x7ffe)) {
      ++skipped;
      continue;
    }
    asm volatile("fninit; fldt %1; fxam; fnstsw %%ax; fninit"
                 : "=a"(status) : "m"(test.value) : "st", "memory");
    ++cases;
    if ((status & 0x4700) != test.flags) {
      std::printf("FEX_X87_FAIL case=%s actual=%04x expected=%04x\n", test.name, status & 0x4700, test.flags);
      ++failures;
    }
  }

  // All eight physical registers contain distinct exactly representable values.
  // FINCSTP rotates a full stack without changing its tags, exercising every TOP.
  for (unsigned top = 1; top < 8; ++top) {
    RoundTrip state {};
    const double values[] = {1.0, -2.0, 3.0, -4.0, 5.0, -6.0, 7.0, -8.0};
    std::memcpy(state.values, values, sizeof(values));
    unsigned rotations = top;
    asm volatile(
      "fninit\n\t"
      "fldl 1088(%[base]); fldl 1096(%[base]); fldl 1104(%[base]); fldl 1112(%[base])\n\t"
      "fldl 1120(%[base]); fldl 1128(%[base]); fldl 1136(%[base]); fldl 1144(%[base])\n\t"
      "1: fincstp; decl %[rotations]; jnz 1b\n\t"
      "fxsave 0(%[base]); fninit; fxrstor 0(%[base]); fxsave 512(%[base])\n\t"
      "fstpl 1024(%[base]); fstpl 1032(%[base]); fstpl 1040(%[base]); fstpl 1048(%[base])\n\t"
      "fstpl 1056(%[base]); fstpl 1064(%[base]); fstpl 1072(%[base]); fstpl 1080(%[base]); fninit"
      : [rotations] "+&c"(rotations)
      : [base] "r"(&state)
      : "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)", "cc", "memory");
    uint16_t saved_status, restored_status;
    std::memcpy(&saved_status, state.saved + 2, 2);
    std::memcpy(&restored_status, state.restored + 2, 2);
    ++cases;
    if (((saved_status >> 11) & 7) != top || ((restored_status >> 11) & 7) != top ||
        state.saved[4] != 0xff || state.restored[4] != 0xff ||
        std::memcmp(state.saved, state.restored, 2)) {
      std::printf("FEX_X87_FAIL case=restore_state top=%u saved_fsw=%04x restored_fsw=%04x tags=%02x/%02x\n",
                  top, saved_status, restored_status, state.saved[4], state.restored[4]);
      ++failures;
    }
    for (unsigned reg = 0; reg < 8; ++reg) {
      const double expected = values[7 - ((top + reg) & 7)];
      ++cases;
      // Reserved bytes in each 16-byte FXSAVE slot are not architectural data.
      if (std::memcmp(state.saved + 32 + 16 * reg, state.restored + 32 + 16 * reg, 10) ||
          state.popped[reg] != expected) {
        std::printf("FEX_X87_FAIL case=restore_register top=%u st=%u\n", top, reg);
        ++failures;
      }
    }
  }
  std::printf("FEX_X87_%s cases=%u failures=%u\n", failures ? "FAIL" : "PASS", cases, failures);
  std::printf("{\"probe\":\"x87\",\"pointer_bits\":%u,\"precision_bits\":%u,\"x87_checks\":\"%s\",\"cases\":%u,\"failures\":%u,\"skipped_extended_classes\":%u}\n",
              pointer_bits, reduced ? 64U : 80U, failures ? "FAIL" : "PASS", cases, failures, skipped);
  return failures ? 1 : 0;
}
