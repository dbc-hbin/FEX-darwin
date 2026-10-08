// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":%u}\n", unsigned(sizeof(void*) * 8));
  std::fflush(stdout);
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto debug_write = reinterpret_cast<int (WINAPI*)(const char*, unsigned)>(GetProcAddress(ntdll, "__wine_dbg_write"));
  const auto handle_to_fd = reinterpret_cast<LONG (__cdecl*)(HANDLE, unsigned, int*, unsigned*)>(GetProcAddress(ntdll, "wine_server_handle_to_fd"));
  const auto fd_to_handle = reinterpret_cast<LONG (__cdecl*)(int, unsigned, unsigned, HANDLE*)>(GetProcAddress(ntdll, "wine_server_fd_to_handle"));
  if (!debug_write || !handle_to_fd || !fd_to_handle) return 1;
  constexpr char message[] = "FEX_WINE_UNIX_STRING\n";
  if (debug_write(message, sizeof(message) - 1) != sizeof(message) - 1) return 1;

  const HANDLE file = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 1;
  struct {
    unsigned before;
    int fd;
    unsigned options;
    unsigned after;
  } output {0x12345678, -1, ~0U, 0x87654321};
  const auto status = handle_to_fd(file, GENERIC_READ, &output.fd, &output.options);
  if (status || output.fd < 0 || output.options == ~0U || output.before != 0x12345678 || output.after != 0x87654321) {
    std::fprintf(stderr, "Unix fd output failed: status=%lx fd=%d options=%x\n", static_cast<unsigned long>(status), output.fd, output.options);
    CloseHandle(file);
    return 1;
  }
  HANDLE duplicate = INVALID_HANDLE_VALUE;
  if (fd_to_handle(output.fd, GENERIC_READ, 0, &duplicate) || duplicate == INVALID_HANDLE_VALUE || !duplicate) {
    CloseHandle(file);
    return 1;
  }
  if (GetFileType(duplicate) != GetFileType(file)) return 1;
  if (!CloseHandle(duplicate) || !CloseHandle(file)) return 1;
  std::printf("{\"wine_unix_checks\":\"PASS\",\"cases\":3}\n");
}
