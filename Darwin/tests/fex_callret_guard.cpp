// SPDX-License-Identifier: MIT
// Compile this x64 PE against the selected FEX snapshot's FEXCore/include and fmt/include.
// Run through native ARM64 Wine/FEX; VirtualQuery checks the actual emulator allocation.
#include <FEXCore/Debug/InternalThreadState.h>
#include <windows.h>
#include <intrin.h>
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
  // Windows ARM64EC thread ABI: TEB.ChpeV2CpuAreaInfo, then EmulatorData[1].
  const auto teb = __readgsqword(0x30);
  const auto area = *reinterpret_cast<const uintptr_t*>(teb + 0x1788);
  assert(area);
  const auto* thread = *reinterpret_cast<FEXCore::Core::InternalThreadState* const*>(area + 0x38);
  assert(thread);
  const auto stack = reinterpret_cast<uintptr_t>(thread->CallRetStackBase);
  constexpr size_t host_page = 16384;
  assert(stack % host_page == 0);

  MEMORY_BASIC_INFORMATION lower {}, data {}, upper {};
  assert(VirtualQuery(reinterpret_cast<const void*>(stack - host_page), &lower, sizeof(lower)) == sizeof(lower));
  assert(VirtualQuery(reinterpret_cast<const void*>(stack), &data, sizeof(data)) == sizeof(data));
  assert(VirtualQuery(reinterpret_cast<const void*>(stack + thread->CALLRET_STACK_SIZE), &upper, sizeof(upper)) == sizeof(upper));
  assert(lower.State == MEM_RESERVE && lower.RegionSize >= host_page);
  assert(data.State == MEM_COMMIT && data.Protect == PAGE_READWRITE);
  assert(upper.State == MEM_RESERVE && upper.RegionSize >= host_page);
  std::puts("FEX_CALLRET_GUARD_PASS lower=reserved16k data=rw upper=reserved16k");
}
