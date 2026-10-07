// SPDX-License-Identifier: MIT
#pragma once

#include <FEXCore/Core/Context.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/Debug/InternalThreadState.h>

namespace FEX::Windows::CallRetStack {
// Wine exposes 4KB guest pages, but Darwin ARM64 commits/protects 16KB host pages.
// Keep each guard on a separate host page so cache drift faults inside our range.
constexpr uint64_t GuardPageSize = 16384;

struct CallRetStackInfo {
  uint64_t AllocationBase;
  uint64_t AllocationEnd;
  uint64_t DefaultLocation;
};

CallRetStackInfo GetInfoThread(FEXCore::Core::InternalThreadState* Thread) {
  uint64_t Base = reinterpret_cast<uint64_t>(Thread->CallRetStackBase);
  // Leave some room from the base for the default location to allow for underflows without constant exceptions
  return {Base - GuardPageSize, Base + FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE + GuardPageSize,
          Base + FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE / 4};
}

void InitializeThread(FEXCore::Core::InternalThreadState* Thread) {
  // Allocate the call-ret stack with guard pages on both sides
  const void* CallRetStackAlloc = ::VirtualAlloc(nullptr, FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE + 2 * GuardPageSize,
                                                 MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS);

  FEXCore::Allocator::VirtualName("FEXMem_CallRetStacks", CallRetStackAlloc,
                                  FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE + 2 * GuardPageSize);
  FEXCore::Allocator::VirtualTHPControl(CallRetStackAlloc, FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE + 2 * GuardPageSize,
                                        FEXCore::Allocator::THPControl::Disable);

  Thread->CallRetStackBase = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(CallRetStackAlloc) + GuardPageSize);
  ::VirtualAlloc(Thread->CallRetStackBase, FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE, MEM_COMMIT, PAGE_READWRITE);

  Thread->CurrentFrame->State.callret_sp = GetInfoThread(Thread).DefaultLocation;
}

void DestroyThread(FEXCore::Core::InternalThreadState* Thread) {
  auto CallRetStackInfo = GetInfoThread(Thread);
  ::VirtualFree(reinterpret_cast<void*>(CallRetStackInfo.AllocationBase), 0, MEM_RELEASE);
}

bool HandleAccessViolation(FEXCore::Core::InternalThreadState* Thread, uint64_t Address, uint64_t& CallRetSPReg) {
  auto CallRetStackInfo = GetInfoThread(Thread);
  if (Address >= CallRetStackInfo.AllocationBase && Address < CallRetStackInfo.AllocationEnd) {
    LogMan::Msg::DFmt("Call-ret stack inbalance: {:X}", Address);
    CallRetSPReg = CallRetStackInfo.DefaultLocation;
    return true;
  }
  return false;
}
} // namespace FEX::Windows::CallRetStack
