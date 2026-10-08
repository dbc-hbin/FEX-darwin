#!/usr/bin/env python3
"""Exercise production cache pruning and FOZ identification on a temporary filesystem."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "FEXCore/Source/Interface/Core/DiskCache.cpp").read_text()


def body(token: str) -> str:
    start = source.index(token)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


constants = source[source.index("    enum { FOSSILIZE_COMPRESSION"):source.index("    struct __attribute__((packed)) mesa_index")]
harness = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fmt/format.h>
namespace fs = std::filesystem;
namespace fextl {
using namespace std;
namespace fmt = ::fmt;
}
bool PruningEnabled = true;
#define FEXCORE_PROFILE_SCOPED(...)
#define FEX_CONFIG_OPT(name, ...) const bool name = PruningEnabled
namespace FEXCore {
namespace File {
enum class FileModes { READ = 1, WRITE = 2, CREATE = 4 };
FileModes operator|(FileModes A, FileModes B) { return FileModes(int(A) | int(B)); }
// Real file I/O; only the platform wrapper is replaced in this host harness.
class File {
  int FD;
public:
  File(const char* Path, FileModes Modes, bool) : FD(open(Path, int(Modes) == 1 ? O_RDONLY : O_RDWR | O_CREAT, 0600)) {}
  ~File() { if (FD != -1) close(FD); }
  bool IsValid() const { return FD != -1; }
  ssize_t Size() { struct stat S {}; return fstat(FD, &S) ? -1 : S.st_size; }
  ssize_t PRead(void* Data, size_t Size, uint64_t Offset) { return pread(FD, Data, Size, Offset); }
  ssize_t PWrite(const void* Data, size_t Size, uint64_t Offset) { return pwrite(FD, Data, Size, Offset); }
  bool Lock(unsigned) { assert(false); return false; }
  void Unlock() { assert(false); }
};
}
namespace FileUtils {
template<class Callback>
void WalkDirectory(std::string_view Path, Callback Visit, const void* Data) {
  if (fs::is_symlink(Path) || !fs::is_directory(Path)) return;
  std::vector<fs::directory_entry> Entries;
  for (const auto& Entry : fs::directory_iterator(Path)) Entries.push_back(Entry);
  for (const auto& Entry : Entries) Visit(Entry.path().filename().string(), Entry.is_directory(), Data);
}
bool RecursiveRemoveDirectory(const std::string& Path) { fs::remove_all(Path); return true; }
}
namespace DiskCache {
namespace MesaFOZ {
''' + constants + r'''
}
class FOZFile {
  std::string FileName;
  bool ReadOnly {};
  std::unique_ptr<File::File> FD;
  static constexpr unsigned OPEN_LOCK_TIMEOUT_MS = 100;
public:
  bool Open(const std::string&, bool);
};
''' + body("  bool FOZFile::Open(") + body("  static inline void PruneStaleEntries(") + r'''
}
}
int main(int argc, char** argv) {
  assert(argc == 2);
  const fs::path Root = argv[1];
  const std::string Current = "0000000000000000", Stale = "1111111111111111";
  const std::string Mixed = "2222222222222222", Bad = "3333333333333333", Linked = "4444444444444444";
  const std::string DB = "RWCacheDB_abcdef0123456789.foz";
  const std::string Index = "RWCacheDB_abcdef0123456789_idx.foz";
  const auto Put = [&](const fs::path& Path, bool Valid) {
    fs::create_directories(Path.parent_path());
    std::ofstream File(Path, std::ios::binary);
    if (Valid) File.write(reinterpret_cast<const char*>(FEXCore::DiskCache::MesaFOZ::stream_reference_magic_and_version), FOZ_REF_MAGIC_SIZE);
    else File << "not a FEX database";
  };
  for (const auto& Bucket : {Current, Stale, Mixed}) {
    Put(Root / Bucket / DB, true);
    Put(Root / Bucket / Index, true);
  }
  Put(Root / "logs" / "keep.txt", false);
  Put(Root / "not-a-bucket" / DB, true);
  Put(Root / Mixed / "keep.txt", false);
  Put(Root / Mixed / "nested" / DB, true);
  Put(Root / Mixed / "RWCacheDB_invalid.foz", true);
  Put(Root / Mixed / "RWCacheDB_abcdef0123456789.foz.backup", true);
  Put(Root / Bad / DB, false);
  Put(Root / "outside" / DB, true);
  fs::create_directories(Root / "5555555555555555"); // An empty hash-shaped directory is not evidence of ownership.
  fs::create_directory_symlink(Root / "outside", Root / Linked);
  fs::create_symlink(Root / "outside" / DB, Root / Mixed / "RWCacheDB_9999999999999999.foz");
  const auto FIFO = Root / Mixed / "RWCacheDB_8888888888888888.foz";
  assert(mkfifo(FIFO.c_str(), 0600) == 0);
  fs::create_symlink(FIFO, Root / Mixed / "RWCacheDB_7777777777777777.foz");

  PruningEnabled = false;
  FEXCore::DiskCache::PruneStaleEntries(Root.string(), Current);
  assert(fs::exists(Root / Stale / DB));
  PruningEnabled = true;
  FEXCore::DiskCache::PruneStaleEntries(Root.string(), Current);
  assert(!fs::exists(Root / Stale));
  assert(fs::exists(Root / Current / DB) && fs::exists(Root / Current / Index));
  assert(fs::exists(Root / "logs" / "keep.txt") && fs::exists(Root / "not-a-bucket" / DB));
  assert(!fs::exists(Root / Mixed / DB) && !fs::exists(Root / Mixed / Index));
  assert(fs::exists(Root / Mixed / "keep.txt") && fs::exists(Root / Mixed / "nested" / DB));
  assert(fs::exists(Root / Mixed / "RWCacheDB_invalid.foz"));
  assert(fs::exists(Root / Mixed / "RWCacheDB_abcdef0123456789.foz.backup"));
  assert(fs::exists(Root / Bad / DB) && fs::exists(Root / "outside" / DB));
  assert(fs::is_directory(Root / "5555555555555555"));
  assert(fs::is_fifo(FIFO) && fs::is_symlink(Root / Mixed / "RWCacheDB_7777777777777777.foz"));
  FEXCore::DiskCache::PruneStaleEntries(Root.string(), Current);
  assert(fs::exists(Root / Mixed / "keep.txt") && fs::exists(Root / "outside" / DB));
  std::puts("disk-cache pruning: PASS (owned files only, current bucket, foreign files, invalid FOZ, symlinks, disabled)");
}
'''

with tempfile.TemporaryDirectory(prefix="fex-prune-", dir=root / "build/local") as directory:
    path = Path(directory)
    (path / "probe.cpp").write_text(harness)
    subprocess.run(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-DFMT_HEADER_ONLY", "-I" + str(root / "External/fmt/include"),
                    str(path / "probe.cpp"), "-o", str(path / "probe")], check=True)
    subprocess.run([str(path / "probe"), str(path / "cache-caf\u00e9")], check=True, timeout=15)
