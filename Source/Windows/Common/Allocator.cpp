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
namespace {
  // libsystem_pthread's pthread_jit_write_protect_np toggles W^X in user space: it reads a variant byte and the
  // precomputed RW/RX register values from the Darwin commpage, then writes an implementation-defined system register.
  // Doing the same here avoids a PE-to-unix transition per JIT write scope. The layout is undocumented, so it is only
  // used after a startup self-test against Wine's unix call, which remains the fallback.
  constexpr uintptr_t CommPageJitVariant = 0xF'FFFF'C10C;
  constexpr uintptr_t CommPageJitRW = 0xF'FFFF'C110;
  constexpr uintptr_t CommPageJitRX = 0xF'FFFF'C118;
  uint64_t JitRW, JitRX;

  // Variant 1 uses S3_4_C15_C2_7; variants 2 and 3 use S3_6_C15_C1_5.
  template<int Variant>
  uint64_t ReadJitRegister() {
    uint64_t Value;
    if constexpr (Variant == 1) {
      __asm volatile("mrs %0, S3_4_C15_C2_7" : "=r"(Value));
    } else {
      __asm volatile("mrs %0, S3_6_C15_C1_5" : "=r"(Value));
    }
    return Value;
  }

  // Returns the previous mode like __wine_jit_write_protect. Prior stores to JIT code are ordered before the mode
  // change and the isb makes it take effect immediately, matching libpthread.
  template<int Variant>
  uint32_t InlineJitWriteProtect(uint32_t Enable) {
    const uint64_t Previous = ReadJitRegister<Variant>();
    const uint64_t Value = Enable ? JitRX : JitRW;
    if constexpr (Variant == 1) {
      __asm volatile("dmb ishst\n\tmsr S3_4_C15_C2_7, %0\n\tisb" ::"r"(Value) : "memory");
    } else {
      __asm volatile("dmb ishst\n\tmsr S3_6_C15_C1_5, %0\n\tisb" ::"r"(Value) : "memory");
    }
    return Previous != JitRW;
  }

  template<int Variant>
  bool SelfTest(FEXCore::Allocator::VirtualJitWriteProtectPtr UnixCall) {
    constexpr auto Inline = InlineJitWriteProtect<Variant>;
    // The unix call must produce exactly the commpage values, and the inline toggle must reproduce them and report the
    // previous mode the same way. Both paths finish write-protected, which is the state every scope restores to.
    UnixCall(0);
    const bool UnixRW = ReadJitRegister<Variant>() == JitRW;
    UnixCall(1);
    const bool UnixRX = ReadJitRegister<Variant>() == JitRX;
    const bool InlineRW = Inline(0) == 1 && ReadJitRegister<Variant>() == JitRW;
    const bool InlineNested = Inline(0) == 0 && ReadJitRegister<Variant>() == JitRW;
    const bool InlineRX = Inline(1) == 0 && ReadJitRegister<Variant>() == JitRX;
    const bool InlineIdle = Inline(1) == 1 && ReadJitRegister<Variant>() == JitRX;
    return UnixRW && UnixRX && InlineRW && InlineNested && InlineRX && InlineIdle;
  }

  FEXCore::Allocator::VirtualJitWriteProtectPtr SelectJitWriteProtect(FEXCore::Allocator::VirtualJitWriteProtectPtr UnixCall) {
    // Scopes restore the mode they observed, so only take over from a write-protected thread.
    if (!UnixCall) {
      return nullptr;
    }
    const uint32_t Mode = UnixCall(2);
    if (Mode == UINT32_MAX) {
      return nullptr;
    }
    if (Mode != 1) {
      return UnixCall;
    }
    const uint8_t Variant = *reinterpret_cast<const volatile uint8_t*>(CommPageJitVariant);
    JitRW = *reinterpret_cast<const volatile uint64_t*>(CommPageJitRW);
    JitRX = *reinterpret_cast<const volatile uint64_t*>(CommPageJitRX);
    if (JitRW == 0 || JitRX == 0 || JitRW == JitRX) {
      return UnixCall;
    }
    if (Variant == 1 && SelfTest<1>(UnixCall)) {
      return InlineJitWriteProtect<1>;
    }
    if ((Variant == 2 || Variant == 3) && SelfTest<2>(UnixCall)) {
      return InlineJitWriteProtect<2>;
    }
    UnixCall(1);
    return UnixCall;
  }
} // namespace

void SetupHooks(HMODULE ntdll) {
  FEXCore::Allocator::HookPtrs Ptrs {};

  const auto JitWriteProtect = reinterpret_cast<FEXCore::Allocator::VirtualJitWriteProtectPtr>(
    GetProcAddress(ntdll, "__wine_jit_write_protect"));
  Ptrs = {
    .VirtualName = UnixLib::VirtualName,
    .VirtualTHPControl = UnixLib::VirtualTHPControl,
    .VirtualJitWriteProtect = SelectJitWriteProtect(JitWriteProtect),
    .VirtualNativeJit = reinterpret_cast<FEXCore::Allocator::VirtualNativeJitPtr>(
      GetProcAddress(ntdll, "__wine_allocate_native_jit")),
  };

  SYSTEM_INFO system_info {};
  GetSystemInfo(&system_info);
  FEXCore::Allocator::SetupHooks(system_info.dwPageSize, Ptrs);
}
} // namespace FEX::Windows::Allocator
