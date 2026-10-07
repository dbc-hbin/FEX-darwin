// Compile as a standalone Windows executable; run with a non-UTF8 process ACP.
// Includes the production traversal/deletion code, not a reimplementation.
#include "../../FEXCore/Source/Utils/FileUtils.cpp"
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

namespace FEXCore::Allocator {
void* malloc(size_t Size) { return std::malloc(Size); }
void free(void* Ptr) { std::free(Ptr); }
void* aligned_alloc(size_t Alignment, size_t Size) { return _aligned_malloc(Size, Alignment); }
void aligned_free(void* Ptr) { _aligned_free(Ptr); }
}

int main() {
  if (GetACP() == CP_UTF8) {
    std::fprintf(stderr, "Run this regression with a non-UTF8 ACP.\n");
    return 1;
  }
  wchar_t Temp[MAX_PATH] {}, Unique[MAX_PATH] {};
  if (!GetTempPathW(MAX_PATH, Temp) || !GetTempFileNameW(Temp, L"fex", 0, Unique) ||
      !DeleteFileW(Unique) || !CreateDirectoryW(Unique, nullptr)) return 1;
  const std::wstring Base = Unique;
  const std::wstring Target = Base + L"/caf\u00e9";
  const char Name[] = "caf\xc3\xa9";
  wchar_t ANSIName[32] {};
  if (!MultiByteToWideChar(CP_ACP, 0, Name, -1, ANSIName, 32)) return 1;
  const std::wstring Sibling = Base + L"/" + ANSIName;
  if (Target == Sibling || !CreateDirectoryW(Target.c_str(), nullptr) || !CreateDirectoryW(Sibling.c_str(), nullptr)) return 1;
  const std::wstring Nested = Target + L"/\u65e5\u672c\U0001f680";
  const std::wstring File = Nested + L"/\u00e9.bin";
  if (!CreateDirectoryW(Nested.c_str(), nullptr)) return 1;
  HANDLE Handle = CreateFileW(File.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
  if (Handle == INVALID_HANDLE_VALUE || !CloseHandle(Handle)) return 1;
  // A directory reparse point must not delete the sibling's contents.
  const std::wstring Keep = Sibling + L"/keep.bin";
  Handle = CreateFileW(Keep.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
  if (Handle == INVALID_HANDLE_VALUE || !CloseHandle(Handle)) return 1;
  const std::wstring Link = Target + L"/link";
  const bool HasLink = CreateSymbolicLinkW(Link.c_str(), Sibling.c_str(),
                                          SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
  int Bytes = WideCharToMultiByte(CP_UTF8, 0, Unique, -1, nullptr, 0, nullptr, nullptr);
  std::vector<char> Buffer(Bytes);
  if (!Bytes || !WideCharToMultiByte(CP_UTF8, 0, Unique, -1, Buffer.data(), Bytes, nullptr, nullptr)) return 1;
  const fextl::string Root(Buffer.data());
  struct Capture {
    const fextl::string& Root;
    std::string_view Name;
    bool Found {}, Removed {};
  } Data {Root, Name};
  FEXCore::FileUtils::WalkDirectory(Root, +[](std::string_view Entry, bool IsDirectory, const void* Opaque) {
    auto& Data = *const_cast<Capture*>(static_cast<const Capture*>(Opaque));
    if (Entry == Data.Name && IsDirectory) {
      Data.Found = true;
      Data.Removed = FEXCore::FileUtils::RecursiveRemoveDirectory(Data.Root + "/" + fextl::string(Entry));
    }
  }, &Data);
  const bool Passed = Data.Found && Data.Removed && GetFileAttributesW(Target.c_str()) == INVALID_FILE_ATTRIBUTES &&
                      GetFileAttributesW(Keep.c_str()) != INVALID_FILE_ATTRIBUTES &&
                      !FEXCore::FileUtils::RecursiveRemoveDirectory(Root + "/\xff");
  const bool Cleaned = FEXCore::FileUtils::RecursiveRemoveDirectory(Root);
  std::printf("{\"unicode_directory\":\"%s\",\"acp\":%u,\"reparse_checked\":%s}\n",
              Passed && Cleaned ? "PASS" : "FAIL", GetACP(), HasLink ? "true" : "false");
  return Passed && Cleaned ? 0 : 1;
}
