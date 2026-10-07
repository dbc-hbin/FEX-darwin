// Same anonymous lookup key and footprint, different live opcode hashes.
// Run with FEX_DISKCACHE=1 and an explicit small FEX_DISKCACHEMEMORYSIZE.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":%u}\n", static_cast<unsigned>(sizeof(void*) * 8));
  char image[MAX_PATH] {};
  if (!GetModuleFileNameA(nullptr, image, MAX_PATH)) {
    return 1;
  }
  std::printf("FEX_PROBE_IMAGE:%s\n", image);
  auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (!code) {
    std::fprintf(stderr, "VirtualAlloc failed: %lu\n", GetLastError());
    return 1;
  }
  using Function = int (*)();
  const auto function = reinterpret_cast<Function>(code);
  unsigned checks = 0;
  for (unsigned pass = 0; pass < 3; ++pass) {
    for (unsigned index = 0; index < 256; ++index) {
      const unsigned pattern = pass == 1 ? 255 - index : (index * 73) & 255;
      DWORD previous;
      if (!VirtualProtect(code, 4096, PAGE_READWRITE, &previous)) {
        std::fprintf(stderr, "VirtualProtect writable failed: %lu\n", GetLastError());
        return 1;
      }
      std::memset(code, 0x90, 64);
      code[64] = 0xb8; // mov eax, 100
      const std::uint32_t initial = 100;
      std::memcpy(code + 65, &initial, sizeof(initial));
      int expected = 100;
      for (unsigned bit = 0; bit < 8; ++bit) {
        const bool subtract = (pattern & (1u << bit)) != 0;
        code[69 + bit * 3] = 0x83;
        code[70 + bit * 3] = subtract ? 0xe8 : 0xc0; // sub/add eax, 1
        code[71 + bit * 3] = 1;
        expected += subtract ? -1 : 1;
      }
      code[93] = 0xc3;
      if (!VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &previous) ||
          !FlushInstructionCache(GetCurrentProcess(), code, 94)) {
        std::fprintf(stderr, "Publishing code failed: %lu\n", GetLastError());
        return 1;
      }
      for (unsigned repeat = 0; repeat < 2; ++repeat) {
        const int actual = function();
        if (actual != expected) {
          std::fprintf(stderr, "Cache lookup mismatch: pass=%u pattern=%u expected=%d actual=%d\n",
                       pass, pattern, expected, actual);
          return 1;
        }
        ++checks;
      }
    }
  }
  if (!VirtualFree(code, 0, MEM_RELEASE)) {
    std::fprintf(stderr, "VirtualFree failed: %lu\n", GetLastError());
    return 1;
  }
  std::printf("{\"cache_checks\":\"PASS\",\"cases\":%u}\n", checks);
  return 0;
}
