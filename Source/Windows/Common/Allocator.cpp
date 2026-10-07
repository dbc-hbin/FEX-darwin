// SPDX-License-Identifier: MIT
#include <FEXCore/Utils/AllocatorHooks.h>
#include <FEXCore/Utils/TypeDefines.h>
#include "Windows/Common/FEXUnixLib.h"

#include <array>
#include <chrono>
#include <libloaderapi.h>
#include <sysinfoapi.h>
#include <synchapi.h>
#include <windef.h>
#include <winternl.h>
#include <winnt.h>
#include <wine/debug.h>

namespace FEX::Windows::Allocator {
void SetupHooks(HMODULE ntdll) {
  FEXCore::Allocator::HookPtrs Ptrs {};

  const auto JitWriteProtect = reinterpret_cast<FEXCore::Allocator::VirtualJitWriteProtectPtr>(
    GetProcAddress(ntdll, "__wine_jit_write_protect"));
  Ptrs = {
    .VirtualName = UnixLib::VirtualName,
    .VirtualTHPControl = UnixLib::VirtualTHPControl,
    .VirtualJitWriteProtect = JitWriteProtect && JitWriteProtect(2) != UINT32_MAX ? JitWriteProtect : nullptr,
    .VirtualNativeJit = reinterpret_cast<FEXCore::Allocator::VirtualNativeJitPtr>(
      GetProcAddress(ntdll, "__wine_allocate_native_jit")),
  };

  SYSTEM_INFO system_info {};
  GetSystemInfo(&system_info);
  FEXCore::Allocator::SetupHooks(system_info.dwPageSize, Ptrs);
}
} // namespace FEX::Windows::Allocator
