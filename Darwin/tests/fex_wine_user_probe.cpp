// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>

static unsigned sentinel = 0x13572468;
static bool created;
static constexpr WCHAR class_name[] = L"FexGuestWindowPointerProbe";
static constexpr char payload[] = "guest copy data";

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
  case WM_NCCREATE: {
    const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    created = cs->lpCreateParams == &sentinel && cs->hInstance == GetModuleHandleW(nullptr);
    return created;
  }
  case WM_APP + 5:
    return wparam == 0x87654321 && lparam == 0x12345678 ? 0x2468 : 0;
  case WM_COPYDATA: {
    const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
    return data->dwData == 0x12345678 && data->cbData == sizeof(payload) &&
      !std::memcmp(data->lpData, payload, sizeof(payload)) ? 0x1357 : 0;
  }
  default:
    return DefWindowProcW(window, message, wparam, lparam);
  }
}

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "Win32 check failed at line %d: %s (error %lu)\n", __LINE__, #condition, GetLastError()); \
  return 1; } } while (0)

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":%u}\n", unsigned(sizeof(void*) * 8));
  std::fflush(stdout);
  MSG message {};
  PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
  CHECK(PostThreadMessageW(GetCurrentThreadId(), WM_APP + 1, 0x1234, 0x5678));
  CHECK(GetMessageW(&message, nullptr, WM_APP + 1, WM_APP + 1) == 1);
  CHECK(message.message == WM_APP + 1 && message.wParam == 0x1234 && message.lParam == 0x5678);
  CHECK(PostThreadMessageW(GetCurrentThreadId(), WM_APP + 2, 0x5678, 0x1234));
  CHECK(PeekMessageW(&message, nullptr, WM_APP + 2, WM_APP + 2, PM_REMOVE));
  CHECK(message.message == WM_APP + 2 && message.wParam == 0x5678 && message.lParam == 0x1234);

  WNDCLASSEXW wc {sizeof(wc)};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = class_name;
  CHECK(RegisterClassExW(&wc));
  WNDCLASSEXW output_class {sizeof(output_class)};
  CHECK(GetClassInfoExW(wc.hInstance, class_name, &output_class));
  CHECK(output_class.lpfnWndProc == window_proc && output_class.hInstance == wc.hInstance);
  const HWND window = CreateWindowExW(0, class_name, L"initial", 0, 0, 0, 1, 1, HWND_MESSAGE, nullptr, wc.hInstance, &sentinel);
  CHECK(window && created);
  CHECK(SendMessageW(window, WM_APP + 5, 0x87654321, 0x12345678) == 0x2468);
  COPYDATASTRUCT data {0x12345678, sizeof(payload), const_cast<char*>(payload)};
  CHECK(SendMessageW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data)) == 0x1357);
  constexpr WCHAR text[] = L"guest UTF-16 buffer";
  CHECK(SendMessageW(window, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text)));
  struct { unsigned before; WCHAR text[64]; unsigned after; } caption {0x12345678, {}, 0x87654321};
  CHECK(SendMessageW(window, WM_GETTEXT, 64, reinterpret_cast<LPARAM>(caption.text)) == std::wcslen(text));
  CHECK(!std::wcscmp(caption.text, text) && caption.before == 0x12345678 && caption.after == 0x87654321);
  RECT rectangle {-1, -1, -1, -1};
  CHECK(GetClientRect(window, &rectangle));
  CHECK(rectangle.left == 0 && rectangle.top == 0 && rectangle.right >= 0 && rectangle.bottom >= 0);

  const HDC dc = CreateCompatibleDC(nullptr);
  CHECK(dc);
  BITMAPINFO info {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = 2;
  info.bmiHeader.biHeight = -2;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  const HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  CHECK(bitmap && bits);
  constexpr unsigned pixels[] = {0x00112233, 0x00445566, 0x00778899, 0x00aabbcc};
  std::memcpy(bits, pixels, sizeof(pixels));
  DIBSECTION section {};
  CHECK(GetObjectW(bitmap, sizeof(section), &section) == sizeof(section));
  CHECK(section.dsBm.bmBits == bits);
  struct { unsigned before; unsigned pixels[4]; unsigned after; } copy {0x12345678, {}, 0x87654321};
  CHECK(GetDIBits(dc, bitmap, 0, 2, copy.pixels, &info, DIB_RGB_COLORS) == 2);
  CHECK(!std::memcmp(copy.pixels, pixels, sizeof(pixels)) && copy.before == 0x12345678 && copy.after == 0x87654321);
  FONTSIGNATURE signature {};
  CHECK(GetTextCharsetInfo(dc, &signature, 0) != GDI_ERROR);
  const HRGN region = CreateRectRgn(1, 2, 3, 4);
  CHECK(region);
  struct { RGNDATAHEADER header; RECT rect; } region_data {};
  CHECK(GetRegionData(region, sizeof(region_data), reinterpret_cast<RGNDATA*>(&region_data)) == sizeof(region_data));
  CHECK(region_data.header.nCount == 1 && region_data.rect.left == 1 && region_data.rect.top == 2 &&
        region_data.rect.right == 3 && region_data.rect.bottom == 4);
  CHECK(DeleteObject(region) && DeleteObject(bitmap) && DeleteDC(dc));
  CHECK(DestroyWindow(window) && UnregisterClassW(class_name, wc.hInstance));
  std::printf("{\"wine_user_checks\":\"PASS\",\"cases\":11}\n");
}
