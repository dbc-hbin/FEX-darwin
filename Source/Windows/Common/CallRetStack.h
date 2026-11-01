// SPDX-License-Identifier: MIT
#pragma once

#include <FEXCore/Core/Context.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/Debug/InternalThreadState.h>

#include <algorithm>
#include <atomic>

namespace FEX::Windows::CallRetStack {
// Wine exposes 4KB guest pages, but Darwin ARM64 commits/protects 16KB host pages.
// Keep each guard on a separate host page so cache drift faults inside our range.
constexpr uint64_t GuardPageSize = 16384;

// Only a window around the default location is committed. Accesses outside of it fault and grow it, so invalidation
// only has to zero the initial window and decommit whatever it grew by, instead of decommitting the whole stack.
constexpr uint64_t WindowBelowDefault = 48 * 1024;
constexpr uint64_t WindowAboveDefault = 16 * 1024;
constexpr uint64_t GrowSize = 256 * 1024;

struct CallRetStackInfo {
  uint64_t AllocationBase;
  uint64_t AllocationEnd;
  uint64_t DefaultLocation;
};

// Committed extent of a thread's call-ret stack. Grown by the owning thread's fault handler and reset by invalidations,
// which may run on any thread and concurrently with each other. No lock is taken, so a suspended owner thread can never
// block an invalidating thread.
struct State {
  std::atomic<uint64_t> Low {};
  std::atomic<uint64_t> High {};
};

CallRetStackInfo GetInfoThread(FEXCore::Core::InternalThreadState* Thread) {
  uint64_t Base = reinterpret_cast<uint64_t>(Thread->CallRetStackBase);
  // Leave some room from the base for the default location to allow for underflows without constant exceptions
  return {Base - GuardPageSize, Base + FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE + GuardPageSize,
          Base + FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE / 4};
}

static bool Commit(uint64_t Begin, uint64_t End) {
  return ::VirtualAlloc(reinterpret_cast<void*>(Begin), End - Begin, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

// Each entry is cleared by a single 16-byte aligned stp, which FEAT_LSE2 makes single-copy atomic like the JIT's ldp.
// A concurrently running owner thread thus never sees a guest RIP paired with a cleared host address or vice versa.
static void ZeroEntries(uint64_t Begin, uint64_t End) {
  for (; Begin != End; Begin += 64) {
    asm volatile("stp xzr, xzr, [%0]\n"
                 "stp xzr, xzr, [%0, #16]\n"
                 "stp xzr, xzr, [%0, #32]\n"
                 "stp xzr, xzr, [%0, #48]" ::"r"(Begin)
                 : "memory");
  }
}

void InitializeThread(FEXCore::Core::InternalThreadState* Thread, State& Stack) {
  // Reserve the call-ret stack with guard pages on both sides
  const void* CallRetStackAlloc = ::VirtualAlloc(nullptr, FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE + 2 * GuardPageSize,
                                                 MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS);

  FEXCore::Allocator::VirtualName("FEXMem_CallRetStacks", CallRetStackAlloc,
                                  FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE + 2 * GuardPageSize);
  FEXCore::Allocator::VirtualTHPControl(CallRetStackAlloc, FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE + 2 * GuardPageSize,
                                        FEXCore::Allocator::THPControl::Disable);

  Thread->CallRetStackBase = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(CallRetStackAlloc) + GuardPageSize);
  const auto Base = reinterpret_cast<uint64_t>(Thread->CallRetStackBase);
  const auto DefaultLocation = GetInfoThread(Thread).DefaultLocation;

  // The first host page stays committed so the stack base is always accessible.
  Commit(Base, Base + GuardPageSize);
  Stack.Low = DefaultLocation - WindowBelowDefault;
  Stack.High = DefaultLocation + WindowAboveDefault;
  Commit(Stack.Low, Stack.High);

  Thread->CurrentFrame->State.callret_sp = DefaultLocation;
}

void DestroyThread(FEXCore::Core::InternalThreadState* Thread) {
  auto CallRetStackInfo = GetInfoThread(Thread);
  ::VirtualFree(reinterpret_cast<void*>(CallRetStackInfo.AllocationBase), 0, MEM_RELEASE);
}

// Clears every entry while the owning thread may be running. Afterwards each committed page is either zeroed or
// freshly committed, and entries outside the committed window can only be reached through a fault that commits zeroes.
void Invalidate(FEXCore::Core::InternalThreadState* Thread, State& Stack) {
  const auto Base = reinterpret_cast<uint64_t>(Thread->CallRetStackBase);
  const auto DefaultLocation = GetInfoThread(Thread).DefaultLocation;
  const auto InitialLow = DefaultLocation - WindowBelowDefault;
  const auto InitialHigh = DefaultLocation + WindowAboveDefault;

  // The first page never faults, so a stale call-ret SP that still points into it can keep writing there; always clear it.
  ZeroEntries(Base, Base + GuardPageSize);
  ZeroEntries(InitialLow, InitialHigh);
  // A grow that publishes after these exchanges undoes itself, see HandleAccessViolation.
  if (const auto Low = Stack.Low.exchange(InitialLow); Low < InitialLow) {
    ::VirtualFree(reinterpret_cast<void*>(Low), InitialLow - Low, MEM_DECOMMIT);
  }
  if (const auto High = Stack.High.exchange(InitialHigh); High > InitialHigh) {
    ::VirtualFree(reinterpret_cast<void*>(InitialHigh), High - InitialHigh, MEM_DECOMMIT);
  }
}

// Commits [Begin, End) and publishes Bound = NewBound. Only the owning thread grows, so a failed exchange means an
// invalidation reset Bound meanwhile. The pages then escaped its decommit, so decommit them again; the owner is inside
// this handler and cannot have written to them, and the retried access faults and grows anew.
static void Grow(std::atomic<uint64_t>& Bound, uint64_t Expected, uint64_t NewBound, uint64_t Begin, uint64_t End) {
  if (Commit(Begin, End) && !Bound.compare_exchange_strong(Expected, NewBound)) {
    ::VirtualFree(reinterpret_cast<void*>(Begin), End - Begin, MEM_DECOMMIT);
  }
}

bool HandleAccessViolation(FEXCore::Core::InternalThreadState* Thread, State& Stack, uint64_t Address, uint64_t& CallRetSPReg) {
  auto CallRetStackInfo = GetInfoThread(Thread);
  if (Address < CallRetStackInfo.AllocationBase || Address >= CallRetStackInfo.AllocationEnd) {
    return false;
  }

  const auto Base = reinterpret_cast<uint64_t>(Thread->CallRetStackBase);
  const auto End = Base + FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE;
  const auto Low = Stack.Low.load();
  const auto High = Stack.High.load();
  if (Address >= Base + GuardPageSize && Address < Low) {
    // The first page is always committed, so the window never needs to reach below it.
    const auto NewLow = std::max(Address & ~(GrowSize - 1), Base + GuardPageSize);
    Grow(Stack.Low, Low, NewLow, NewLow, Low);
    return true;
  }
  if (Address >= High && Address < End) {
    const auto NewHigh = std::min((Address & ~(GrowSize - 1)) + GrowSize, End);
    Grow(Stack.High, High, NewHigh, High, NewHigh);
    return true;
  }

  LogMan::Msg::DFmt("Call-ret stack inbalance: {:X}", Address);
  CallRetSPReg = CallRetStackInfo.DefaultLocation;
  return true;
}
} // namespace FEX::Windows::CallRetStack
