// SPDX-License-Identifier: MIT
#pragma once
#include <FEXCore/Utils/CompilerDefs.h>
#include <cstdint>

namespace FEXCore::Allocator {
using VirtualJitWriteProtectPtr = uint32_t (*)(uint32_t);
#ifdef _WIN32
FEX_DEFAULT_VISIBILITY extern VirtualJitWriteProtectPtr VirtualJitWriteProtect;
#endif

// Restore the previous thread mode, including nested compilation/patching scopes.
// Other threads retain their executable mapping throughout code publication.
class JitWriteScope {
public:
  JitWriteScope() {
#ifdef _WIN32
    if (VirtualJitWriteProtect) Previous = VirtualJitWriteProtect(0);
#endif
  }
  ~JitWriteScope() {
#ifdef _WIN32
    if (VirtualJitWriteProtect) VirtualJitWriteProtect(Previous);
#endif
  }
  JitWriteScope(const JitWriteScope&) = delete;
  JitWriteScope& operator=(const JitWriteScope&) = delete;
private:
  uint32_t Previous {1};
};
} // namespace FEXCore::Allocator
