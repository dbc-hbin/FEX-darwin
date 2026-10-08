// SPDX-License-Identifier: MIT
// Adjacent writable 4-KiB pages must not hide writes to translated code on a 16-KiB host.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":%u}\n", unsigned(sizeof(void*) * 8));
  std::fflush(stdout);
  auto* memory = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 16384, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
  if (!memory) return 1;
  constexpr unsigned offsets[] = {0, 4096, 12288};
  for (unsigned offset : offsets) {
    const std::uint8_t code[] = {0xb8, 33, 0, 0, 0, 0xc3};
    std::memcpy(memory + offset, code, sizeof(code));
  }
  unsigned cases = 0;
  for (unsigned round = 0; round < 3; ++round) {
    for (unsigned offset : offsets) {
      const auto function = reinterpret_cast<int (*)()>(memory + offset);
      const int expected = 33 + round;
      const int actual = function();
      if (actual != expected) {
        std::fprintf(stderr, "SMC mismatch: round=%u offset=%u expected=%d actual=%d\n", round, offset, expected, actual);
        VirtualFree(memory, 0, MEM_RELEASE);
        return 1;
      }
      ++cases;
      // x86 code publication does not require the Windows instruction-cache flush API.
      *reinterpret_cast<volatile std::uint8_t*>(memory + offset + 1) = expected + 1;
    }
  }
  // A backward branch splits an already decoded block. Its validation policy must survive.
  const std::uint8_t loop[] = {0xb8, 0, 0, 0, 0, 0xb9, 2, 0, 0, 0, 0x83, 0xc0, 1, 0xff, 0xc9, 0x75, 0xf9, 0xc3};
  std::memcpy(memory + 1024, loop, sizeof(loop));
  const auto function = reinterpret_cast<int (*)()>(memory + 1024);
  for (unsigned value = 1; value <= 3; ++value) {
    *reinterpret_cast<volatile std::uint8_t*>(memory + 1024 + 12) = value;
    if (function() != int(value * 2)) {
      std::fprintf(stderr, "SMC split-block mismatch: value=%u\n", value);
      VirtualFree(memory, 0, MEM_RELEASE);
      return 1;
    }
    ++cases;
  }
  // The invalidation helper must preserve the second fastcall argument in EDX/RDX.
  const std::uint8_t argument_code[] = {0x8d, 0x42, 1, 0xc3};
  std::memcpy(memory + 2048, argument_code, sizeof(argument_code));
  const auto argument_function = reinterpret_cast<int (__fastcall *)(int, int)>(memory + 2048);
  for (unsigned value = 1; value <= 3; ++value) {
    *reinterpret_cast<volatile std::uint8_t*>(memory + 2048 + 2) = value;
    if (argument_function(0, 19) != int(19 + value)) {
      std::fprintf(stderr, "SMC argument preservation mismatch: value=%u\n", value);
      VirtualFree(memory, 0, MEM_RELEASE);
      return 1;
    }
    ++cases;
  }
  if (!VirtualFree(memory, 0, MEM_RELEASE)) return 1;
  std::printf("{\"smc_checks\":\"PASS\",\"cases\":%u}\n", cases);
}
