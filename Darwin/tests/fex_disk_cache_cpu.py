#!/usr/bin/env python3
"""Run cache publication/writer/pruning bodies on the host without Wine.

The harness compiles the production bodies, not a copy of the duplicate logic.
Only disk I/O and the worker queue are replaced, so failure and eviction are
explicit synchronous events. Run: python3 Darwin/tests/fex_disk_cache_cpu.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "FEXCore/Source/Interface/Core/DiskCache.cpp").read_text()
header = (root / "FEXCore/include/FEXCore/Core/DiskCache.h").read_text()


def body(text: str, token: str) -> str:
    start = text.index(token)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


# Production structs and bodies require only standard containers and xxhash.
structs = header[header.index("  struct MemoryLRUKey {"):header.index("  struct __attribute__((packed)) BlobFixedHeader")]
lookup = body(source, "  IndexEntry* DiskCache::LookupLocked(")
prune = body(source, "  struct DiskCache::PruneMemoryLRUWorkItem final") + ";"
writer = body(source, "  struct DiskCache::CacheStoreWorkItem final") + ";"
publication = body(source[source.index("    NewEntry.GuestExtents = ExactGuestCodeExtents;"):], "    {\n      std::lock_guard Guard(IndexLock);")
extra = body(source, "  struct __attribute__((packed)) IndexExtraBlobHeader") + ";"
common = body(source, "    struct __attribute__((packed)) mesa_index_db_file_entry") + ";"

harness = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>
#define XXH_INLINE_ALL
#include <xxhash.h>
namespace fextl = std;
#define LOGMAN_THROW_A_FMT(condition, ...) assert(condition)
namespace FEXCore { namespace DiskCache {
namespace MesaFOZ {
struct foz_payload_key { uint8_t bytes[40]; };
''' + common + r'''
}
''' + extra + r'''
class IndexedDB {
public:
  bool Success = false;
  unsigned Writes = 0;
  bool StoreCacheBlob(const MesaFOZ::foz_payload_key&, uint64_t, std::span<const uint8_t>,
                      MesaFOZ::mesa_index_db_file_entry& Header, std::span<const uint8_t>) {
    ++Writes;
    Header.cache_db_file_offset = 123;
    return Success;
  }
};
''' + structs + r'''
class WorkQueueThread {
public:
  struct WorkItem { virtual ~WorkItem() = default; virtual void Run() = 0; };
  std::vector<std::unique_ptr<WorkItem>> Queue;
  void QueueWork(std::unique_ptr<WorkItem> Work) { Queue.push_back(std::move(Work)); }
  void Drain() { while (!Queue.empty()) { auto Work = std::move(Queue.front()); Queue.erase(Queue.begin()); Work->Run(); } }
};
class DiskCache {
public:
  std::map<uint64_t, IndexCacheHead> Index;
  std::mutex IndexLock, MemoryLRULock;
  std::list<MemoryLRUKey> MemoryLRU;
  std::atomic<uint64_t> MemoryLRUCurrentSize {};
  uint64_t MemoryLRUMaxSize = 0, MemoryLRUEvictThreshold = 0;
  static constexpr unsigned LOOKUP_KEY_MAX_BUCKET_DEPTH = 8;
  std::unique_ptr<WorkQueueThread> Writer = std::make_unique<WorkQueueThread>();
  struct CacheStoreWorkItem;
  struct PruneMemoryLRUWorkItem;
  IndexEntry* LookupLocked(uint64_t, const XXH128_hash_t&, uint64_t);
  bool Publish(uint64_t LookupKey, uint64_t GuestFootprint, XXH128_hash_t Hash,
               const std::shared_ptr<std::vector<uint8_t>>& BlobRef, bool& Queued) {
    struct { XXH128_hash_t GuestHash; } Header {Hash};
    IndexEntry NewEntry {nullptr, 0, static_cast<uint32_t>(BlobRef->size()), 1, Hash, BlobRef, {0, 1}, {}};
''' + publication + r'''
    Queued = true;
    return true;
  }
};
''' + lookup + prune + writer + r'''
} }
using namespace FEXCore::DiskCache;
int main() {
  DiskCache Cache;
  IndexedDB DB;
  const XXH128_hash_t Hash {11, 22};
  const uint64_t Key = 33, Footprint = 44;
  const auto Store = [&](uint64_t FP, XXH128_hash_t H, bool Disk = true) {
    auto Blob = std::make_shared<std::vector<uint8_t>>(64, 0xa5);
    bool Queued = false;
    assert(Cache.Publish(Key, FP, H, Blob, Queued));
    if (Queued) {
      IndexExtraBlobHeader Header {H, FP, 1, 2};
      std::vector<uint8_t> IndexBlob(sizeof(Header));
      std::memcpy(IndexBlob.data(), &Header, sizeof(Header));
      Cache.Writer->QueueWork(std::make_unique<DiskCache::CacheStoreWorkItem>(
        &Cache, &DB, MesaFOZ::foz_payload_key {}, Key, Blob, std::move(IndexBlob), Disk));
    }
    return Queued;
  };
  // In-flight duplicates must not queue another writer. Failure with zero memory
  // budget makes the entry dead; repopulation must reuse the existing index slot.
  assert(Store(Footprint, Hash));
  assert(!Store(Footprint, Hash));
  auto* Entry = Cache.LookupLocked(Key, Hash, Footprint);
  std::weak_ptr<std::vector<uint8_t>> PendingBlob = Entry->MemoryBlob;
  auto Work = std::move(Cache.Writer->Queue.front());
  Cache.Writer->Queue.clear();
  Work->Run();
  assert(Entry && !Entry->DB && !Entry->MemoryBlob && !Entry->LRUEntry);
  assert(!PendingBlob.expired()); // The writer still owns its input after index reset.
  Work.reset();
  assert(PendingBlob.expired());
  assert(Store(Footprint, Hash));
  DB.Success = true;
  Cache.Writer->Drain();
  assert(DB.Writes == 2 && Entry->DB == &DB && Entry->Offset == 123);
  assert(!Store(Footprint, Hash));

  // The same opcode hash with a different extent footprint is a separate entry.
  assert(Store(Footprint + 1, Hash, false));
  Cache.MemoryLRUMaxSize = 64;
  Cache.Writer->Drain();
  auto* MemoryEntry = Cache.LookupLocked(Key, Hash, Footprint + 1);
  assert(MemoryEntry && MemoryEntry != Entry && MemoryEntry->MemoryBlob && MemoryEntry->LRUEntry);
  assert(Cache.MemoryLRU.size() == 1 && Cache.MemoryLRUCurrentSize == 64);
  Cache.MemoryLRUMaxSize = 0;
  DiskCache::PruneMemoryLRUWorkItem Prune(&Cache);
  auto Reader = MemoryEntry->MemoryBlob;
  Prune.Run(); // A reader owns the blob: eviction is forbidden.
  assert(MemoryEntry->MemoryBlob && Cache.MemoryLRUCurrentSize == 64);
  Reader.reset();
  Prune.Run(); // Explicit eviction signal, no sleep or polling.
  assert(!MemoryEntry->MemoryBlob && !MemoryEntry->LRUEntry && Cache.MemoryLRU.empty());
  assert(Cache.MemoryLRUCurrentSize == 0);
  for (unsigned Retry = 0; Retry < 3; ++Retry) {
    assert(Store(Footprint + 1, Hash, false));
    Cache.MemoryLRUMaxSize = 64;
    Cache.Writer->Drain();
    assert(Cache.LookupLocked(Key, Hash, Footprint + 1) == MemoryEntry);
    assert(MemoryEntry->MemoryBlob && Cache.MemoryLRU.size() == 1 && Cache.MemoryLRUCurrentSize == 64);
    Cache.MemoryLRUMaxSize = 0;
    Prune.Run();
    assert(!MemoryEntry->MemoryBlob && Cache.MemoryLRU.empty() && Cache.MemoryLRUCurrentSize == 0);
  }
  assert(Cache.Index.at(Key).MoreEntries->size() == 1);
  std::puts("disk-cache CPU regression: PASS (failure retry, exact identity, live duplicate, eviction repopulation)");
}
'''

with tempfile.TemporaryDirectory(prefix="fex-cache-", dir=root / "build/local") as directory:
    path = Path(directory)
    (path / "probe.cpp").write_text(harness)
    subprocess.run(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I" + str(root / "External/xxhash"), str(path / "probe.cpp"), "-o", str(path / "probe")], check=True)
    subprocess.run([str(path / "probe")], check=True)
