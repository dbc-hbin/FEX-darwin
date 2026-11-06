// SPDX-License-Identifier: MIT
#pragma once

#include <FEXCore/Utils/IntervalList.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <string_view>

namespace FEXCore::Core {
struct InternalThreadState;
}

namespace FEXCore::Context {
class Context;
}

namespace FEX::Windows {
/**
 * @brief Handles SMC and regular code invalidation
 */
class InvalidationTracker {
public:
  InvalidationTracker(FEXCore::Context::Context& CTX, const std::unordered_map<DWORD, FEXCore::Core::InternalThreadState*>& Threads);
  void HandleMemoryProtectionNotification(uint64_t Address, uint64_t Size, ULONG Prot);
  void HandleProcessExecuteFlagsChange(ULONG Flags);
  void HandleImageMap(std::string_view Name, uint64_t Address);
  struct ContainingSection {
    uint64_t SectionStart;
    uint64_t SectionSize;
    bool Image;
  };
  // Returns the allocation containing Address, with a zero size if Address is not mapped.
  ContainingSection QueryContainingSection(uint64_t Address);
  void InvalidateSection(const ContainingSection& Section, bool Free);
  void InvalidateAlignedInterval(uint64_t Address, uint64_t Size, bool Free);
  void ReprotectRWXIntervals(uint64_t Address, uint64_t Size);
  bool HandleRWXAccessViolation(FEXCore::Core::InternalThreadState* Thread, uint64_t HostPC, uint64_t FaultAddress);

  // Unprotects any RWX intervals in the input interval and invalidates code
  // NOTE: CodeInvalidationMutex must be locked when calling this, and if true is returned, kept locked until the write ends.
  bool BeginUntrackedWriteLocked(uint64_t Address, uint64_t Size);

  FEXCore::HLE::ExecutableRangeInfo QueryExecutableRange(uint64_t Address);

  // Per-thread state carried from the Before to the After callback of one memory notification.
  struct PendingMemoryChange {
    bool Locked {};
    ContainingSection Unmapped {}; // Section of an unlocked unmap
  };

  // Brackets an alloc/protect/free notification. `Mutex` is the frontend lock that serializes executable tracking.
  // It is held across the change only when the change can alter executable tracking: the new protection is effectively
  // executable, or the range intersects an executable interval. Other changes take no lock unless EndMemoryChange finds
  // that a locked change may have raced with them, or that their range became executable, and then resync the range.
  // A null `Pending` (cross-process work, or a thread without frontend state) defers all handling to EndMemoryChange.
  // Prot is PAGE_NOACCESS for frees.
  template<typename MutexType>
  void BeginMemoryChange(MutexType& Mutex, PendingMemoryChange* Pending, uint64_t Address, uint64_t Size, ULONG Prot) {
    if (Pending && MayAffectExecutable(Address, Size, Prot)) {
      BeginLockedChange(Mutex, Pending);
    }
  }

  // Address and Size are the final range reported by the After callback. Apply performs the tracking update of a
  // successful locked change.
  template<typename MutexType, typename ApplyType>
  void EndMemoryChange(MutexType& Mutex, PendingMemoryChange* Pending, uint64_t Address, uint64_t Size, ULONG Prot, bool Succeeded,
                       ApplyType&& Apply) {
    if (Pending && Pending->Locked) {
      if (Succeeded) {
        Apply();
      }
      EndLockedChange(Mutex, Pending);
    } else if (Succeeded) {
      ResyncUnlockedChange(Mutex, Address, Size, Prot);
    }
  }

  // Brackets an unmap notification like Begin/EndMemoryChange. Images and sections intersecting executable intervals
  // are invalidated and passed to `ImageUnmap` in the Before callback, with `Mutex` held until the After callback.
  template<typename MutexType, typename ImageUnmapType>
  void BeginUnmap(MutexType& Mutex, PendingMemoryChange* Pending, uint64_t Address, ImageUnmapType&& ImageUnmap) {
    const auto Section = QueryContainingSection(Address);
    if (Pending && !Section.Image && !(Section.SectionSize && MayAffectExecutable(Section.SectionStart, Section.SectionSize, PAGE_NOACCESS))) {
      Pending->Unmapped = Section;
      return;
    }

    if (Pending) {
      BeginLockedChange(Mutex, Pending);
    } else {
      Mutex.lock();
    }
    if (Section.SectionSize) {
      InvalidateSection(Section, true);
      ImageUnmap(Section.SectionStart, Section.SectionSize);
    }
    if (!Pending) {
      Mutex.unlock();
    }
  }

  template<typename MutexType>
  void EndUnmap(MutexType& Mutex, PendingMemoryChange* Pending, bool Succeeded) {
    if (!Pending) {
      return;
    }
    if (Pending->Locked) {
      EndLockedChange(Mutex, Pending);
      return;
    }
    const auto Section = std::exchange(Pending->Unmapped, {});
    if (Succeeded && Section.SectionSize) {
      ResyncUnlockedChange(Mutex, Section.SectionStart, Section.SectionSize, PAGE_NOACCESS);
    }
  }

private:
  // Whether applying Prot to the page-aligned range can change executable tracking. A null Address (chosen by the
  // kernel) checks only Prot, and a zero Size checks the first page.
  bool MayAffectExecutable(uint64_t Address, uint64_t Size, ULONG Prot);

  template<typename MutexType>
  void BeginLockedChange(MutexType& Mutex, PendingMemoryChange* Pending) {
    Mutex.lock();
    LockedChangesInFlight.fetch_add(1);
    Pending->Locked = true;
  }

  template<typename MutexType>
  void EndLockedChange(MutexType& Mutex, PendingMemoryChange* Pending) {
    Pending->Locked = false;
    LockedChangesInFlight.fetch_sub(1);
    Mutex.unlock();
  }

  // Called after an unlocked change completed in the kernel. A locked change whose Before callback ran before that
  // point either is still in flight, so its After callback may record a protection the unlocked change overwrote, or
  // has already updated the interval lists. Reading the in-flight count before the lists observes one of the two, and a
  // locked change that starts later also reaches the kernel later, so its own update is final.
  template<typename MutexType>
  void ResyncUnlockedChange(MutexType& Mutex, uint64_t Address, uint64_t Size, ULONG Prot) {
    if (!LockedChangesInFlight.load() && !MayAffectExecutable(Address, Size, Prot)) {
      return;
    }
    std::scoped_lock Lock(Mutex);
    if (MayAffectExecutable(Address, Size, Prot)) {
      ResyncRange(Address, Size);
    }
  }

  // Re-derives executable tracking for the range from its current protection, then invalidates the range.
  // Executable non-writable pages keep their RWX tracking, as in HandleMemoryProtectionNotification.
  // NOTE: Must be called with the frontend lock passed to EndMemoryChange held.
  void ResyncRange(uint64_t Address, uint64_t Size);

  void DetectMonoBackpatcherBlock(FEXCore::Core::InternalThreadState* Thread, uint64_t HostPC);
  void DisableSMCDetection();
  void InvalidateIntervalInternal(uint64_t Address, uint64_t Size);
  // NOTE: This assumed CodeInvalidationMutex is locked by the caller
  void InvalidateIntervalInternalLocked(uint64_t Address, uint64_t Size);

  // NOTE: If ForWriteLocked is true then this assumes CodeInvalidationMutex is locked by the caller,
  // and any code in the range will be invalidated before protection as RWX, otherwise protects as RX if false.
  bool ProtectRWXIntervalsInternal(uint64_t Address, uint64_t Size, bool ForWriteLocked);

  // Returns the correct protection for trapping (removing write) or untrapping (restoring write) an RWX interval.
  // For DEP-promoted regions (originally non-exec), uses PAGE_READONLY/PAGE_READWRITE instead of PAGE_EXECUTE_READ/PAGE_EXECUTE_READWRITE.
  // NOTE: Must be called with IntervalsLock held.
  ULONG GetTrapProt(uint64_t Address) const;
  ULONG GetUntrapProt(uint64_t Address) const;

  FEXCore::IntervalList<uint64_t> XIntervals;
  FEXCore::IntervalList<uint64_t> RWXIntervals;
  std::shared_mutex IntervalsLock;
  FEXCore::Context::Context& CTX;
  const std::unordered_map<DWORD, FEXCore::Core::InternalThreadState*>& Threads;
  bool SMCDetectionDisabled {false};                    // Protected by IntervalsLock
  bool WritableCodeValidated {false};
  bool DEPDisabled {false};                             // Protected by IntervalsLock
  FEXCore::IntervalList<uint64_t> DEPPromotedIntervals; // Protected by IntervalsLock

  // Locked memory notifications between their Before and After callbacks
  std::atomic<uint32_t> LockedChangesInFlight {0};

  bool MonoBackpatcherDetectionPending {false};
  uint64_t MonoBase {0};
  uint64_t MonoEnd {0};
};
} // namespace FEX::Windows
