// SPDX-License-Identifier: MIT
// llvm-mingw {x86_64,i686}-w64-mingw32-clang++ -std=c++17 -O2 -msse4.2
//   -fno-vectorize -fno-slp-vectorize -static fex_pcmp_probe.cpp -o fex_pcmp_probe.exe
#include <nmmintrin.h>
#include <windows.h>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

struct Result {
  int index;
  uint8_t mask[16];
  int flags; // CF, ZF, SF, OF, and (!CF && !ZF), respectively.
};

static int element(const uint8_t* data, int i, int mode) {
  if (mode & 1) {
    uint16_t word;
    std::memcpy(&word, data + 2 * i, 2);
    return (mode & 2) ? static_cast<int16_t>(word) : word;
  }
  return (mode & 2) ? static_cast<int8_t>(data[i]) : data[i];
}

static int length(const uint8_t* data, int raw, int mode, bool explicit_length) {
  const int n = (mode & 1) ? 8 : 16;
  if (explicit_length) {
    const int64_t absolute = raw < 0 ? -static_cast<int64_t>(raw) : raw;
    return absolute < n ? static_cast<int>(absolute) : n;
  }
  int i = 0;
  while (i < n && element(data, i, mode) != 0) ++i;
  return i;
}

// Scalar Intel SDM validity/aggregation rules, independent of the FEX vector lowering.
__attribute__((noinline)) static Result reference(const uint8_t* a, int raw_a, const uint8_t* b, int raw_b,
                                                 int mode, bool explicit_length) {
  const int n = (mode & 1) ? 8 : 16;
  const int la = length(a, raw_a, mode, explicit_length);
  const int lb = length(b, raw_b, mode, explicit_length);
  unsigned bits = 0;
  for (int j = 0; j < n; ++j) {
    bool match = false;
    switch ((mode >> 2) & 3) {
    case 0:
      for (int i = 0; i < la && j < lb; ++i) match |= element(a, i, mode) == element(b, j, mode);
      break;
    case 1:
      for (int i = 0; i + 1 < la && j < lb; i += 2)
        match |= element(a, i, mode) <= element(b, j, mode) && element(b, j, mode) <= element(a, i + 1, mode);
      break;
    case 2:
      match = (j >= la && j >= lb) || (j < la && j < lb && element(a, j, mode) == element(b, j, mode));
      break;
    case 3:
      match = true;
      // A full RHS permits a partial match at its last lanes; a short RHS does not.
      for (int i = 0; i < la && i + j < n; ++i)
        match &= i + j < lb && element(a, i, mode) == element(b, i + j, mode);
      break;
    }
    bits |= static_cast<unsigned>(match) << j;
  }
  const int polarity = (mode >> 4) & 3;
  if (polarity == 1) bits ^= (1U << n) - 1;
  if (polarity == 3) bits ^= (1U << lb) - 1;
  Result result {};
  result.index = n;
  for (int i = 0; i < n; ++i) {
    if ((bits >> i) & 1) {
      if (result.index == n || (mode & 64)) result.index = i;
      if (mode & 64) {
        result.mask[i * ((mode & 1) ? 2 : 1)] = 0xff;
        if (mode & 1) result.mask[2 * i + 1] = 0xff;
      }
    }
  }
  if (!(mode & 64)) {
    result.mask[0] = static_cast<uint8_t>(bits);
    result.mask[1] = static_cast<uint8_t>(bits >> 8);
  }
  result.flags = (bits != 0) | ((lb < n) << 1) | ((la < n) << 2) | ((bits & 1) << 3) | ((bits == 0 && lb == n) << 4);
  return result;
}

template<int Mode>
__attribute__((noinline)) static Result actual(const uint8_t* a, int la, const uint8_t* b, int lb, bool explicit_length) {
  const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
  const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
  Result result {};
  __m128i mask;
  if (explicit_length) {
    result.index = _mm_cmpestri(va, la, vb, lb, Mode);
    mask = _mm_cmpestrm(va, la, vb, lb, Mode);
    result.flags = _mm_cmpestrc(va, la, vb, lb, Mode) | (_mm_cmpestrz(va, la, vb, lb, Mode) << 1) |
                   (_mm_cmpestrs(va, la, vb, lb, Mode) << 2) | (_mm_cmpestro(va, la, vb, lb, Mode) << 3) |
                   (_mm_cmpestra(va, la, vb, lb, Mode) << 4);
  } else {
    result.index = _mm_cmpistri(va, vb, Mode);
    mask = _mm_cmpistrm(va, vb, Mode);
    result.flags = _mm_cmpistrc(va, vb, Mode) | (_mm_cmpistrz(va, vb, Mode) << 1) |
                   (_mm_cmpistrs(va, vb, Mode) << 2) | (_mm_cmpistro(va, vb, Mode) << 3) |
                   (_mm_cmpistra(va, vb, Mode) << 4);
  }
  _mm_storeu_si128(reinterpret_cast<__m128i*>(result.mask), mask);
  return result;
}

using Probe = Result (*)(const uint8_t*, int, const uint8_t*, int, bool);
template<size_t... Modes>
static int run(std::index_sequence<Modes...>) {
  const Probe probes[] = {actual<static_cast<int>(Modes)>...};
  const int lengths[] = {0, 1, 2, 3, 7, 8, 9, 15, 16, 17, -1, -7, -8, -16, -17, INT_MIN, INT_MAX};
  unsigned checks = 0;
  uint32_t random = 0x12345678;
  for (int sample = 0; sample < 48; ++sample) {
    uint8_t a[16], b[16];
    for (int i = 0; i < 16; ++i) {
      random ^= random << 13; random ^= random >> 17; random ^= random << 5;
      a[i] = static_cast<uint8_t>(random);
      b[i] = static_cast<uint8_t>(random >> 8);
    }
    // Empty, equal, absent, repeated, range sign boundaries, and end-of-vector substrings.
    if (sample == 0) std::memset(a, 0, 16), std::memset(b, 0, 16);
    if (sample == 1) std::memcpy(b, a, 16);
    if (sample == 2) std::memset(a, 1, 16), std::memset(b, 2, 16);
    if (sample == 3) std::memset(a, 0xff, 16), std::memset(b, 0xff, 16);
    if (sample == 4) {
      const uint8_t ranges[16] = {0xf0, 0x10, 0xf0, 0xff, 0x10, 0, 0x80, 0xff, 1, 0, 0xff, 0x7f, 0, 0, 0, 0};
      const uint8_t values[16] = {0x7f, 0x80, 0x11, 0xef, 0xf0, 0x10, 0, 1, 0, 0x80, 0xff, 0x7f, 0xf0, 0xff, 0x10, 0};
      std::memcpy(a, ranges, 16); std::memcpy(b, values, 16);
    }
    if (sample >= 5 && sample < 21) {
      std::memset(a, 'a', 16); std::memset(b, 'b', 16);
      std::memcpy(b + sample - 5, a, 21 - sample);
    }
    if (sample >= 21 && sample < 37) a[sample - 21] = b[36 - sample] = 0;
    for (int mode = 0; mode < 256; ++mode) {
      for (int explicit_length = 0; explicit_length < 2; ++explicit_length) {
        const int count = explicit_length ? static_cast<int>(sizeof(lengths) / sizeof(lengths[0])) : 1;
        for (int ia = 0; ia < count; ++ia) {
          for (int ib = 0; ib < count; ++ib) {
            const Result want = reference(a, lengths[ia], b, lengths[ib], mode, explicit_length);
            const Result got = probes[mode](a, lengths[ia], b, lengths[ib], explicit_length);
            if (got.index != want.index || got.flags != want.flags || std::memcmp(got.mask, want.mask, 16)) {
              std::printf("FEX_PCMP_FAIL sample=%d mode=0x%02x explicit=%d la=%d lb=%d index=%d/%d flags=%x/%x\n",
                          sample, mode, explicit_length, lengths[ia], lengths[ib], got.index, want.index, got.flags, want.flags);
              std::printf("mask got/want:");
              for (int i = 0; i < 16; ++i) std::printf(" %02x/%02x", got.mask[i], want.mask[i]);
              std::puts("");
              return 1;
            }
            ++checks;
          }
        }
      }
    }
  }
  std::printf("FEX_PCMP_PASS checks=%u modes=256 operations=pcmpestri,pcmpestrm,pcmpistri,pcmpistrm\n", checks);
  std::printf("{\"pcmp_checks\":\"PASS\",\"cases\":%u}\n", checks);
  return 0;
}

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":%u}\n", static_cast<unsigned>(sizeof(void*) * 8));
  char image[32768];
  const DWORD size = GetModuleFileNameA(nullptr, image, sizeof(image));
  if (!size || size >= sizeof(image)) {
    std::puts("FEX_PCMP_FAIL executable_identity");
    return 1;
  }
  std::printf("FEX_PROBE_IMAGE:%s\n", image);
  std::fflush(stdout);
  return run(std::make_index_sequence<256> {});
}
