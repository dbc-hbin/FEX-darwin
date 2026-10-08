// SPDX-License-Identifier: MIT
// Build: x86_64-w64-mingw32-clang++ -std=c++20 -O2 -static -mavx2
//        -fno-vectorize -fno-slp-vectorize fex_memory_perf.cpp -o memory-perf.exe
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

static constexpr unsigned Repetitions = 5;
static constexpr size_t Capacity = 65536;
alignas(64) static uint8_t source[Capacity], destination[Capacity];
alignas(32) static uint64_t indices[4], masks[4], initial[4], gathered[4], remaining[4];

static void fail(const char* workload) {
  std::fprintf(stderr, "FAIL: %s exact result\n", workload);
  std::exit(1);
}

__attribute__((noinline)) static void rep(unsigned operation, size_t bytes, unsigned iterations) {
  for (unsigned i = 0; i < iterations; ++i) {
    void* dst = destination;
    const void* src = source;
    size_t count = operation & 1 ? bytes / 8 : bytes;
    const uint64_t value = 0x193b5d7fa1c3e507ULL;
    switch (operation) {
    case 0: asm volatile("cld; rep movsb" : "+D"(dst), "+S"(src), "+c"(count) :: "memory", "cc"); break;
    case 1: asm volatile("cld; rep movsq" : "+D"(dst), "+S"(src), "+c"(count) :: "memory", "cc"); break;
    case 2: asm volatile("cld; rep stosb" : "+D"(dst), "+c"(count) : "a"(value) : "memory", "cc"); break;
    case 3: asm volatile("cld; rep stosq" : "+D"(dst), "+c"(count) : "a"(value) : "memory", "cc"); break;
    }
  }
}

__attribute__((noinline)) static void gather(bool qword, unsigned iterations) {
  for (unsigned i = 0; i < iterations; ++i) {
    if (qword) {
      asm volatile("vmovdqu (%1), %%ymm0; vmovdqu (%2), %%ymm1; vmovdqu (%3), %%ymm2; "
                   "vpgatherqq %%ymm2, (%0,%%ymm1,8), %%ymm0; "
                   "vmovdqu %%ymm0, (%4); vmovdqu %%ymm2, (%5)"
                   :: "r"(source), "r"(initial), "r"(indices), "r"(masks), "r"(gathered), "r"(remaining)
                   : "ymm0", "ymm1", "ymm2", "memory");
    } else {
      asm volatile("vmovdqu (%1), %%ymm0; vmovdqu (%2), %%ymm1; vmovdqu (%3), %%ymm2; "
                   "vpgatherdd %%ymm2, (%0,%%ymm1,4), %%ymm0; "
                   "vmovdqu %%ymm0, (%4); vmovdqu %%ymm2, (%5)"
                   :: "r"(source), "r"(initial), "r"(indices), "r"(masks), "r"(gathered), "r"(remaining)
                   : "ymm0", "ymm1", "ymm2", "memory");
    }
  }
  asm volatile("vzeroupper" ::: "memory");
}

static void verify_rep(unsigned operation, size_t bytes) {
  const uint64_t value = 0x193b5d7fa1c3e507ULL;
  for (size_t i = 0; i < Capacity; ++i) {
    uint8_t expected = i >= bytes ? 0xcc : operation < 2 ? source[i]
      : operation == 2 ? uint8_t(value) : uint8_t(value >> ((i % 8) * 8));
    if (destination[i] != expected) fail("rep");
  }
}

static void verify_gather(bool qword, unsigned active) {
  const unsigned width = qword ? 8 : 4;
  for (unsigned lane = 0; lane < 32 / width; ++lane) {
    uint64_t index = 0;
    std::memcpy(&index, reinterpret_cast<uint8_t*>(indices) + lane * width, width);
    const uint8_t* expected = active & (1U << lane) ? source + index * width
      : reinterpret_cast<uint8_t*>(initial) + lane * width;
    if (std::memcmp(reinterpret_cast<uint8_t*>(gathered) + lane * width, expected, width)) fail("gather");
  }
  for (uint64_t mask : remaining) if (mask) fail("gather mask");
}

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":%zu}\n", sizeof(void*) * 8);
  LARGE_INTEGER frequency, begin, end;
  if (!QueryPerformanceFrequency(&frequency)) return 2;
  // Write every byte before measurement, including output and all gather working sets.
  for (size_t i = 0; i < Capacity; ++i) source[i] = uint8_t(i * 37 + i / 251);
  std::memset(destination, 0xcc, sizeof(destination));
  std::memset(initial, 0xa5, sizeof(initial));
  std::memset(gathered, 0, sizeof(gathered));
  std::memset(remaining, 0, sizeof(remaining));
  const char* names[] = {"rep_movsb", "rep_movsq", "rep_stosb", "rep_stosq"};
  for (unsigned operation = 0; operation < 4; ++operation) {
    for (size_t bytes : {size_t(64), size_t(4096), Capacity}) {
      std::memset(destination, 0xcc, sizeof(destination));
      rep(operation, bytes, unsigned(2 * 1024 * 1024 / bytes));
      verify_rep(operation, bytes);
      const unsigned iterations = unsigned(128 * 1024 * 1024 / bytes);
      for (unsigned repetition = 0; repetition < Repetitions; ++repetition) {
        std::memset(destination, 0xcc, sizeof(destination));
        QueryPerformanceCounter(&begin);
        rep(operation, bytes, iterations);
        QueryPerformanceCounter(&end);
        verify_rep(operation, bytes);
        double seconds = double(end.QuadPart - begin.QuadPart) / double(frequency.QuadPart);
        std::printf("{\"workload\":\"%s_%zu\",\"repetition\":%u,\"iterations\":%u,\"seconds\":%.9f,\"MiB_s\":%.3f}\n",
                    names[operation], bytes, repetition, iterations, seconds, bytes * double(iterations) / 1048576 / seconds);
      }
    }
  }
  for (bool qword : {false, true}) {
    const unsigned lanes = qword ? 4 : 8, width = qword ? 8 : 4;
    const unsigned patterns[] = {(1U << lanes) - 1, 1, 1U << (lanes - 1), qword ? 0x9U : 0x81U};
    const char* pattern_names[] = {"dense", "low", "high", "sparse"};
    for (unsigned scattered = 0; scattered < 2; ++scattered) {
      for (unsigned pattern = 0; pattern < 4; ++pattern) {
        std::memset(indices, 0, sizeof(indices));
        std::memset(masks, 0, sizeof(masks));
        for (unsigned lane = 0; lane < lanes; ++lane) {
          uint64_t index = scattered ? (lane * 997 + 13) % (Capacity / width) : lane;
          uint64_t mask = patterns[pattern] & (1U << lane) ? UINT64_MAX : 0;
          std::memcpy(reinterpret_cast<uint8_t*>(indices) + lane * width, &index, width);
          std::memcpy(reinterpret_cast<uint8_t*>(masks) + lane * width, &mask, width);
        }
        gather(qword, 2048);
        verify_gather(qword, patterns[pattern]);
        constexpr unsigned iterations = 4000000;
        for (unsigned repetition = 0; repetition < Repetitions; ++repetition) {
          QueryPerformanceCounter(&begin);
          gather(qword, iterations);
          QueryPerformanceCounter(&end);
          verify_gather(qword, patterns[pattern]);
          double seconds = double(end.QuadPart - begin.QuadPart) / double(frequency.QuadPart);
          std::printf("{\"workload\":\"gather_%s_%s_%s\",\"repetition\":%u,\"iterations\":%u,\"seconds\":%.9f,\"M_gathers_s\":%.3f}\n",
                      qword ? "qq" : "dd", pattern_names[pattern], scattered ? "scattered" : "contiguous",
                      repetition, iterations, seconds, iterations / seconds / 1e6);
        }
      }
    }
  }
  std::puts("{\"memory_perf\":\"PASS\",\"workloads\":28,\"repetitions\":5}");
}
