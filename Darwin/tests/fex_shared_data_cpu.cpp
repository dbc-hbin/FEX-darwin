// SPDX-License-Identifier: MIT
// clang++ -std=c++20 -Wall -Wextra -Werror -I<patched-source>/Source/Windows/ARM64EC
//   Darwin/tests/fex_shared_data_cpu.cpp -o <output> && <output>
#include "UserSharedData.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace FEX::Windows::UserSharedData;
int main() {
  std::array<uint8_t, PageSize> Page;
  for (unsigned I = 0; I < Page.size(); ++I) Page[I] = (I * 37 + 11) & 255;
  std::array<uint64_t, 31> X;
  std::array<std::array<uint8_t, 16>, 32> V;
  unsigned Checks = 0;
  const auto Check = [&](uint32_t Insn, unsigned Bytes, unsigned Count, bool Vector, int Offset, int Writeback = 0) {
    for (unsigned I = 0; I < X.size(); ++I) X[I] = 0x1234567800000000ULL + I;
    X[1] = GuestBase + 0x100;
    X[2] = Offset;
    for (auto& R : V) R.fill(0xa5);
    const auto BeforeX = X;
    const auto BeforeV = V;
    assert(HandleLoad(Page.data(), Insn, GuestBase + 0x100 + Offset, X.data(), 0, V.data()));
    for (unsigned I = 0; I < X.size(); ++I) {
      if (I == 1) { assert(X[I] == BeforeX[I] + Writeback); continue; }
      if (!Vector && (I == 3 || (Count == 2 && I == 4))) continue;
      assert(X[I] == BeforeX[I]);
    }
    for (unsigned I = 0; I < V.size(); ++I) {
      if (Vector && (I == 3 || (Count == 2 && I == 4))) continue;
      assert(V[I] == BeforeV[I]);
    }
    for (unsigned I = 0; I < Count; ++I) {
      if (Vector) {
        assert(!std::memcmp(V[3 + I].data(), Page.data() + 0x100 + Offset + I * Bytes, Bytes));
        for (unsigned B = Bytes; B < 16; ++B) assert(V[3 + I][B] == 0);
      } else {
        uint64_t Expected = 0;
        std::memcpy(&Expected, Page.data() + 0x100 + Offset + I * Bytes, Bytes);
        assert(X[3 + I] == Expected);
      }
    }
    ++Checks;
  };
  for (unsigned Size = 0; Size < 4; ++Size) {
    const unsigned Bytes = 1U << Size;
    Check(0x39400023 | (Size << 30) | (3 << 10), Bytes, 1, false, Bytes * 3);
    Check(0x38400023 | (Size << 30) | (7 << 12), Bytes, 1, false, 7);
    Check(0x38626823 | (Size << 30), Bytes, 1, false, 7); // register X2
    Check(0x08dffc23 | (Size << 30), Bytes, 1, false, 0);
    Check(0x38bfc023 | (Size << 30), Bytes, 1, false, 0);
    Check(0x19400023 | (Size << 30) | (7 << 12), Bytes, 1, false, 7);
    // REP MOVS emits LDRB/LDRH/LDR post-index in both directions.
    for (int Increment : {static_cast<int>(Bytes), -static_cast<int>(Bytes), 0}) {
      const uint32_t Imm = (static_cast<uint32_t>(Increment) & 511) << 12;
      Check(0x38400423 | (Size << 30) | Imm, Bytes, 1, false, 0, Increment);
      Check(0x38400c23 | (Size << 30) | Imm, Bytes, 1, false, Increment, Increment);
    }
  }
  for (unsigned Size = 0; Size < 5; ++Size) {
    const unsigned Bytes = 1U << Size;
    const uint32_t Base = Size == 4 ? 0x3dc00023 : 0x3d400023 | (Size << 30);
    Check(Base | (3 << 10), Bytes, 1, true, Bytes * 3);
    Check((Base & ~0x01000000) | (7 << 12), Bytes, 1, true, 7);
    Check((Base & ~0x01000000) | 0x00226800, Bytes, 1, true, 7);
    for (int Increment : {static_cast<int>(Bytes), -static_cast<int>(Bytes)}) {
      const uint32_t Imm = (static_cast<uint32_t>(Increment) & 511) << 12;
      Check((Base & ~0x01000000) | 0x400 | Imm, Bytes, 1, true, 0, Increment);
      Check((Base & ~0x01000000) | 0xc00 | Imm, Bytes, 1, true, Increment, Increment);
    }
  }
  for (unsigned Size = 0; Size < 3; ++Size) {
    Check(0x2d401023 | (Size << 30), 4U << Size, 2, true, 0);
    // The 32-byte REP MOVS path uses LDP Q post-index.
    const unsigned Bytes = 4U << Size;
    for (int Increment : {static_cast<int>(Bytes * 2), -static_cast<int>(Bytes * 2)}) {
      const uint32_t Imm = (static_cast<uint32_t>(Increment / static_cast<int>(Bytes)) & 127) << 15;
      Check(0x2cc01023 | (Size << 30) | Imm, Bytes, 2, true, 0, Increment);
      Check(0x2dc01023 | (Size << 30) | Imm, Bytes, 2, true, Increment, Increment);
    }
  }
  Check(0x29401023, 4, 2, false, 0);
  Check(0xa9401023, 8, 2, false, 0);
  for (unsigned Size : {0U, 2U}) {
    const unsigned Bytes = Size == 2 ? 8 : 4;
    for (int Increment : {static_cast<int>(Bytes * 2), -static_cast<int>(Bytes * 2)}) {
      const uint32_t Imm = (static_cast<uint32_t>(Increment / static_cast<int>(Bytes)) & 127) << 15;
      Check(0x28c01023 | (Size << 30) | Imm, Bytes, 2, false, 0, Increment);
      Check(0x29c01023 | (Size << 30) | Imm, Bytes, 2, false, Increment, Increment);
    }
  }
  for (unsigned Size = 0; Size < 4; ++Size) {
    const unsigned Bytes = 1U << Size;
    for (unsigned Lane = 0; Lane < 16 / Bytes; ++Lane) {
      X[1] = GuestBase;
      for (auto& R : V) R.fill(0xa5);
      const unsigned Index = Lane * Bytes;
      uint32_t Insn = 0x0d400023 | ((Index >> 3) << 30);
      if (Size == 0) Insn |= (Index & 7) << 10;
      if (Size == 1) Insn |= 0x4000 | ((Index & 7) << 10);
      if (Size == 2) Insn |= 0x8000 | ((Index & 7) << 10);
      if (Size == 3) Insn |= 0x8400;
      const auto SavedX = X;
      const auto SavedV = V;
      assert(HandleLoad(Page.data(), Insn, GuestBase, X.data(), 0, V.data()));
      assert(X == SavedX);
      for (unsigned R = 0; R < 32; ++R)
        for (unsigned B = 0; B < 16; ++B)
          assert(V[R][B] == ((R == 3 && B >= Index && B < Index + Bytes) ? Page[B - Index] : SavedV[R][B]));
      ++Checks;
    }
    for (unsigned Q = 0; Q < 2; ++Q) {
      for (auto& R : V) R.fill(0xa5);
      assert(HandleLoad(Page.data(), 0x0d40c023 | (Q << 30) | (Size << 10), GuestBase, X.data(), 0, V.data()));
      for (unsigned B = 0; B < 16; ++B) assert(V[3][B] == (B < (Q ? 16U : 8U) ? Page[B % Bytes] : 0));
      ++Checks;
    }
  }
  // Failure is transactional, including page-crossing loads and all stores.
  X.fill(GuestBase + PageSize - 1);
  for (auto& R : V) R.fill(0xa5);
  const auto BeforeX = X;
  const auto BeforeV = V;
  for (uint32_t Insn : {0xf9400023U, 0xf9000023U, 0x3d800023U, 0xa9001023U, 0xd503201fU,
                        0xf8408423U, 0xf85f8423U, 0xf8408c23U, 0xf85f8c23U,
                        0xacc11023U, 0xacff1023U, 0xadc11023U, 0xadff1023U}) {
    assert(!HandleLoad(Page.data(), Insn, GuestBase + PageSize - 1, X.data(), 0, V.data()));
    assert(X == BeforeX && V == BeforeV);
  }
  assert(!HandleLoad(nullptr, 0x39400023, GuestBase, X.data(), 0, V.data()));
  assert(!HandleLoad(Page.data(), 0x39400023, GuestBase - 1, X.data(), 0, V.data()));
  // Reject writeback SP, integer destination/base overlap, stores and reserved encodings
  // even when the entire access would otherwise fit in the page.
  X.fill(GuestBase + 0x100);
  const auto InvalidX = X;
  const auto InvalidV = V;
  for (uint32_t Insn : {0x38401421U, 0x38400c21U, 0xa8c11021U, 0xa8c10423U,
                        0x384017e3U, 0x38400fe3U, 0xacc117e3U, 0xadc017e3U,
                        0x38001423U, 0x38001c23U, 0xac811023U, 0xad811023U,
                        0x38400823U, 0x38600423U, 0xf8c00423U, 0x7cc00423U,
                        0xecc01023U, 0x68401023U, 0xacc10c23U}) {
    assert(!HandleLoad(Page.data(), Insn, GuestBase + 0x100, X.data(), GuestBase + 0x100, V.data()));
    assert(X == InvalidX && V == InvalidV);
  }
  // Post-index may move outside the page: only the loaded range must fit.
  X[1] = GuestBase;
  assert(HandleLoad(Page.data(), 0x385ff423, GuestBase, X.data(), 0, V.data()));
  assert(X[1] == GuestBase - 1 && X[3] == Page[0]);
  // A fault in the second vector completes both loads; V1 does not alias base X1.
  X[1] = GuestBase + 0x100;
  assert(HandleLoad(Page.data(), 0xacc11021, GuestBase + 0x11f, X.data(), 0, V.data()));
  assert(X[1] == GuestBase + 0x120);
  assert(!std::memcmp(V[1].data(), Page.data() + 0x100, 16));
  assert(!std::memcmp(V[4].data(), Page.data() + 0x110, 16));
  // Signed loads, negative displacement, register extension, SP and discard Rt.
  X[1] = GuestBase + 1;
  Page[0] = 0x80;
  assert(HandleLoad(Page.data(), 0x389ff023, GuestBase, X.data(), 0, V.data()));
  assert(X[3] == 0xffffffffffffff80ULL && X[1] == GuestBase + 1);
  X[2] = 0xffffffff;
  assert(HandleLoad(Page.data(), 0x3862c823, GuestBase, X.data(), 0, V.data()));
  assert(X[3] == 0x80);
  assert(HandleLoad(Page.data(), 0x394003e3, GuestBase, X.data(), GuestBase, V.data()));
  const auto DiscardX = X;
  assert(HandleLoad(Page.data(), 0x3940003f, GuestBase + 1, X.data(), 0, V.data()));
  assert(X == DiscardX);
  std::printf("PASS: %u scalar/vector/addressing checks; pre/post-index writeback, signed/SP/discard, register preservation, stores and boundaries\n", Checks);
}
