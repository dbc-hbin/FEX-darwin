// SPDX-License-Identifier: MIT
// Compile against patched FEX headers with -D_WIN32 to exercise the Windows hook branch.
#include <FEXCore/Utils/JitWriteScope.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace FEXCore::Allocator {
VirtualJitWriteProtectPtr VirtualJitWriteProtect {};
}

static uint32_t mode = 1;
static uint32_t calls;

static uint32_t protect(uint32_t enable) {
  assert(enable <= 1);
  const uint32_t previous = mode;
  mode = enable;
  ++calls;
  return previous;
}

int main() {
  using FEXCore::Allocator::JitWriteScope;
  static_assert(!std::is_copy_constructible_v<JitWriteScope>);
  static_assert(!std::is_copy_assignable_v<JitWriteScope>);
  {
    JitWriteScope inactive;
    assert(mode == 1 && calls == 0);
  }
  FEXCore::Allocator::VirtualJitWriteProtect = protect;
  {
    JitWriteScope outer;
    assert(mode == 0 && calls == 1);
    {
      JitWriteScope inner;
      assert(mode == 0 && calls == 2);
    }
    assert(mode == 0 && calls == 3);
  }
  assert(mode == 1 && calls == 4);
  mode = 0;
  {
    JitWriteScope already_writable;
    assert(mode == 0 && calls == 5);
  }
  assert(mode == 0 && calls == 6);
  FEXCore::Allocator::VirtualJitWriteProtect = nullptr;
  std::puts("FEX_JIT_WRITE_SCOPE_PASS nested_restore=ok inactive=ok already_writable=ok");
}
