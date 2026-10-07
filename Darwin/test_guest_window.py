#!/usr/bin/env python3
"""Compile the production guest-window branches with the real A64 emitter and check fault boundaries."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def body(source: str, marker: str) -> str:
    start = source.index(marker) + len(marker)
    depth = 1
    for end in range(start, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            return source[start:end]
    raise ValueError(f"Unclosed body: {marker}")


class GuestWindowTests(unittest.TestCase):
    def test_emitted_push_fault_boundary_and_invalidation_ranges(self):
        memory = (ROOT / "FEXCore/Source/Interface/Core/JIT/MemoryOps.cpp").read_text()
        push = body(body(memory, "DEF_OP(Push) {"), "if (GuestBase) {")
        pair = body(body(memory, "DEF_OP(PushTwo) {"), "if (GuestBase) {")
        translate = body(memory, "ARMEmitter::Register Arm64JITCore::ApplyGuestBase(ARMEmitter::Register GuestReg, ARMEmitter::Register Tmp) {")
        invalidation = body((ROOT / "Source/Windows/Common/InvalidationTracker.cpp").read_text(),
                            "void InvalidationTracker::InvalidateIntervalInternalLocked(uint64_t Address, uint64_t Size) {")
        source = r'''
#include <CodeEmitter/Emitter.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
#include <utility>
REGISTERS
struct Emitter : ARMEmitter::Emitter {
  using ARMEmitter::Emitter::Emitter;
  uint64_t GuestBase = 1ULL << 36;
  ARMEmitter::Register ApplyGuestBase(ARMEmitter::Register GuestReg, ARMEmitter::Register Tmp = REG_GUEST_ADDR_TMP.R()) { TRANSLATE }
  void Push(unsigned ValueSize, ARMEmitter::Register Src, ARMEmitter::Register AddrSrc, ARMEmitter::Register Dst) { PUSH }
  void PushTwo(unsigned ValueSize, ARMEmitter::Register Src1, ARMEmitter::Register Src2, ARMEmitter::Register Dst) { PAIR }
};
struct Recorder {
  unsigned CodeCalls{}, ThreadCalls{};
  uint64_t Start{}, Length{};
  void InvalidateCodeBuffersCodeRange(uint64_t Address, uint64_t Size) { ++CodeCalls; Start = Address; Length = Size; }
  void InvalidateThreadCachedCodeRange(unsigned, uint64_t Address, uint64_t Size) {
    ++ThreadCalls; assert(Address == Start && Size == Length);
  }
};
struct Tracker {
  uint64_t GuestBase = 1ULL << 36;
  Recorder CTX;
  std::array<std::pair<unsigned, unsigned>, 2> Threads{{{1, 1}, {2, 2}}};
  void InvalidateIntervalInternalLocked(uint64_t Address, uint64_t Size) { INVALIDATION }
};
int main() {
  for (bool Pair : {false, true}) {
    for (unsigned Size : {1U, 2U, 4U, 8U}) {
      if (Pair && Size < 4) continue;
      for (bool Alias : {false, true}) {
        std::array<uint32_t, 64> Code{};
        Emitter E(reinterpret_cast<uint8_t*>(Code.data()), sizeof(Code));
        const auto ESP = ARMEmitter::Reg::r4;
        const auto Src = Alias ? ESP : ARMEmitter::Reg::r5;
        if (Pair) E.PushTwo(Size, Src, ESP, ESP); else E.Push(Size, Src, ESP, ESP);
        const auto Count = E.GetCursorOffset() / 4;
        assert(Count >= 3);
        // Every instruction before the store writes only scratch registers. A store fault
        // therefore leaves architectural ESP intact even with coalesced/aliased operands.
        for (size_t I = 0; I + 2 < Count; ++I) assert((Code[I] & 31) != ESP.Idx());
        const auto Store = Code[Count - 2];
        assert((Store & 31) == Src.Idx());
        if (Pair) {
          assert((Store & 0x3bc00000) == 0x29000000); // STP offset, no writeback
          assert(((Store >> 10) & 31) == ESP.Idx());
        } else {
          assert((Store & 0x3fe00c00) == 0x38200800); // STR register offset
          assert(((Store >> 16) & 31) == TMP3.Idx());
        }
        const auto Commit = Code[Count - 1];
        assert((Commit & 0xffe0ffe0) == 0x2a0003e0); // MOV Wd, Wm, zero-extends ESP
        assert((Commit & 31) == ESP.Idx() && ((Commit >> 16) & 31) == TMP3.Idx());
        assert((Code[0] & 0xff000000) == 0x51000000); // SUB Wtmp, Wesp, #size (32-bit wrap)
        assert(((Code[0] >> 10) & 4095) == Size * (Pair ? 2 : 1));
      }
    }
  }
  const uint64_t Base = 1ULL << 36, Window = 1ULL << 32;
  for (auto [Address, Size, Start, Length] : std::array<std::array<uint64_t, 4>, 6>{{
    {0, std::numeric_limits<uint64_t>::max(), 0, Window},
    {Base - 4096, 8192, 0, 4096}, {Base + 16, 32, 16, 32},
    {Base + Window - 16, 32, Window - 16, 16},
    {Base - 4096, 4096, 0, 0}, {Base + Window, 4096, 0, 0}}}) {
    Tracker T;
    T.InvalidateIntervalInternalLocked(Address, Size);
    assert(T.CTX.CodeCalls == (Length ? 1U : 0U));
    assert(T.CTX.ThreadCalls == (Length ? 2U : 0U));
    if (Length) assert(T.CTX.Start == Start && T.CTX.Length == Length);
  }
  Tracker Identity;
  Identity.GuestBase = 0;
  Identity.InvalidateIntervalInternalLocked(123, 456);
  assert(Identity.CTX.Start == 123 && Identity.CTX.Length == 456 && Identity.CTX.ThreadCalls == 2);
  std::puts("PASS: emitted PUSH/PUSH2 fault boundaries, aliasing, 32-bit wrap, full/clipped/identity invalidation");
}
'''
        for marker, implementation in (("TRANSLATE", translate), ("PUSH", push), ("PAIR", pair), ("INVALIDATION", invalidation)):
            source = source.replace("{ " + marker + " }", "{" + implementation + "}")
        registers = (ROOT / "FEXCore/Source/Interface/Core/ArchHelpers/Arm64Emitter.h").read_text()
        source = source.replace("REGISTERS", "\n".join(re.search(r"constexpr auto " + name + r" = [^;]+;", registers)[0]
                                                     for name in ("TMP3", "REG_GUEST_BASE", "REG_GUEST_ADDR_TMP")))
        with tempfile.TemporaryDirectory(prefix="guest-window-", dir=ROOT / "build/local") as directory:
            executable = str(Path(directory) / "check")
            # The emitter uses no allocated vectors here; bypass the Linux/Windows allocator on macOS.
            vector = Path(directory) / "FEXCore/fextl/vector.h"
            vector.parent.mkdir(parents=True)
            vector.write_text("#pragma once\n#include <vector>\nnamespace fextl { template<class T> using vector = std::vector<T>; }\n")
            command = ["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                       "-I" + directory,
                       *["-I" + str(ROOT / path) for path in ("FEXCore/include", "FEXCore/Source", "CodeEmitter", "FEXHeaderUtils", "External/fmt/include")],
                       "-x", "c++", "-", "-o", executable]
            subprocess.run(command, input=source, text=True, check=True)
            subprocess.run([executable], check=True)


if __name__ == "__main__":
    unittest.main()
