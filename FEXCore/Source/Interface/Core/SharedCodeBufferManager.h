// SPDX-License-Identifier: MIT
/*
$info$
category: code buffer ~ Thread shared code buffer management
tags: backend|shared
$end_info$
*/
#pragma once

#include <FEXCore/fextl/memory.h>
#include <FEXCore/fextl/vector.h>
#include <FEXCore/Utils/MathUtils.h>
#include <FEXCore/Utils/SignalScopeGuards.h>
#include <FEXCore/Utils/TypeDefines.h>

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace FEXCore {
struct GuestToHostMap;
}

namespace FEXCore::CPU {
struct CodeBufferRegion {
  explicit CodeBufferRegion(size_t Size, const CodeBufferRegion* Previous = nullptr);
  ~CodeBufferRegion();

  const CodeBufferRegion* const Previous;

  // Atomically allocate a fixed size buffer out of the current allocated codebuffer.
  // Lockless because it's just a linear allocator.
  struct CodeBufferAllocation {
    const uint8_t* BufferBase;
    uint8_t* BufferAllocationOffset;
  };

  CodeBufferAllocation TryAllocate(size_t Size) {
    Size = FEXCore::AlignUp(Size, 16);
    auto ExpectedOffset = CodeBufferOffset.load(std::memory_order_relaxed);
    while (Size <= static_cast<size_t>(CodeBufferEnd - ExpectedOffset)) {
      if (CodeBufferOffset.compare_exchange_weak(ExpectedOffset, ExpectedOffset + Size, std::memory_order_relaxed)) {
        return {Ptr, ExpectedOffset};
      }
    }
    return {};
  }

  // Returns the total number of bytes available for storing code
  size_t UsableSize() const {
    return AllocatedSize - GuardSize;
  }

  // Returns the full size of the buffer, including the guard page.
  size_t TotalAllocationSize() const {
    return AllocatedSize;
  }

  // Returns the num of bytes currently allocated from the allocator.
  size_t AllocatedSpaceUsed() const {
    return CodeBufferOffset - Ptr;
  }

  // Trivially reset the allocator.
  void Reset() {
    CodeBufferOffset = Ptr;
  }

  // Returns the base of the buffer.
  uint8_t* GetBufferBase() const {
    return Ptr;
  }

private:
  uint8_t* Ptr;
  uint8_t* CodeBufferEnd;
  size_t AllocatedSize; // including guard page; see UsableSize()
  size_t GuardSize {FEXCore::Utils::FEX_PAGE_SIZE};

  // Code buffer allocation information.
  std::atomic<uint8_t*> CodeBufferOffset {};
};

// All regions in a generation share one block map and retire together. A thread or signal
// handler pinning the generation keeps all its branch targets and return addresses alive.
struct CodeBuffer : CodeBufferRegion {
  explicit CodeBuffer(size_t Size);
  ~CodeBuffer();

  fextl::unique_ptr<GuestToHostMap> LookupCache;

  CodeBufferAllocation AtomicAllocateBuffer(size_t Size) {
    return ActiveRegion.load(std::memory_order_acquire)->TryAllocate(Size);
  }
  CodeBufferAllocation GrowAndAllocate(size_t Size, const CodeBufferRegion*& Added);
  bool Contains(uintptr_t Address) const {
    for (const auto* Region = ActiveRegion.load(std::memory_order_acquire); Region; Region = Region->Previous) {
      if (Address - reinterpret_cast<uintptr_t>(Region->GetBufferBase()) < Region->UsableSize()) {
        return true;
      }
    }
    return false;
  }

private:
  std::atomic<CodeBufferRegion*> ActiveRegion {this};
  std::mutex GrowthMutex;
  fextl::vector<fextl::unique_ptr<CodeBufferRegion>> AdditionalRegions;
  size_t GenerationBytes;
};

/**
 * A manager that coordinates access to the CodeBuffer used for compiling new code across threads.
 *
 * The CodeBuffer is managed as a partially persistent data structure:
 * - Exactly one CodeBuffer is now designated as "active", which means data can be appended to it
 * - Lossy modifications to the active CodeBuffer will not invalidate any data in use by other threads (which is what enables save CodeBuffer sharing across threads)
 * - Instead, such lossy modifications trigger a new "version" of the data in the modifying thread. Old versions of the CodeBuffer persist as read-only data for use by the other threads.
 * - The other threads can update their version of the CodeBuffer. This will decrease the reference count and eventually trigger deallocation of the old version
 */
class SharedCodeBufferManager {
public:
  virtual ~SharedCodeBufferManager() = default;

  // Get the CodeBuffer that was most recently allocated.
  // This is the only CodeBuffer that data may be written to.
  fextl::shared_ptr<CodeBuffer> GetLatest();
  bool IsLatest(const CodeBuffer* Buffer) const {
    return Published.load(std::memory_order_acquire) == Buffer;
  }

  // Allocate a new CodeBuffer with geometric growth up to an internal maximum.
  // Subsequent calls to GetLatest will point to the returned buffer.
  fextl::shared_ptr<CodeBuffer> StartLargerCodeBuffer();

  // Allocate a new CodeBuffer with maximum internal size.
  // Subsequent calls to GetLatest will point to the returned buffer.
  fextl::shared_ptr<CodeBuffer> StartMaximalCodeBuffer();

  virtual void OnCodeBufferAllocated(const std::shared_ptr<CodeBuffer>&) {};

private:
  std::mutex GenerationMutex;
  std::atomic<CodeBuffer*> Published {};
  fextl::shared_ptr<CodeBuffer> Latest;

  fextl::shared_ptr<CodeBuffer> AllocateNew(size_t Size);
};
} // namespace FEXCore::CPU
