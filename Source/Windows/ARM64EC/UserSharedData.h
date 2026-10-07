// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

namespace FEX::Windows::UserSharedData {
constexpr uint64_t GuestBase = 0x7ffe0000;
constexpr uint64_t PageSize = 0x1000;

// Decode only the ordinary A64 loads emitted by the non-SVE Windows JIT.
// Change an address register only when the instruction explicitly writes it back.
inline bool HandleLoad(const uint8_t* Backing, uint32_t Insn, uint64_t Fault, uint64_t* X, uint64_t SP,
                       void* Vectors) {
  if (!Backing || Fault - GuestBase >= PageSize) {
    return false;
  }
  const unsigned Rn = (Insn >> 5) & 31;
  const unsigned Rt = Insn & 31;
  const unsigned Size = Insn >> 30;
  const bool Vector = (Insn >> 26) & 1;
  const unsigned Opc = (Insn >> 22) & 3;
  uint64_t Address = Rn == 31 ? SP : X[Rn];
  uint64_t WritebackAddress = Address;
  bool Writeback = false;
  unsigned Bytes = 1U << Size;
  unsigned Count = 1;
  unsigned Rt2 = 0;
  bool Signed = false;
  bool Word = Size < 3;
  bool Acquire = false;
  int Lane = -1;
  unsigned Replicate = 0;

  if ((Insn & 0xbfff0000) == 0x0d400000) {
    // LD1 lane / LD1R, without writeback or multiple structure registers.
    const unsigned Op = (Insn >> 13) & 7;
    const unsigned S = (Insn >> 12) & 1;
    const unsigned Element = (Insn >> 10) & 3;
    const unsigned Q = Insn >> 30;
    if (Op == 0) { Bytes = 1; Lane = Q * 8 + S * 4 + Element; }
    else if (Op == 2 && !(Element & 1)) { Bytes = 2; Lane = Q * 4 + S * 2 + (Element >> 1); }
    else if (Op == 4 && Element == 0) { Bytes = 4; Lane = Q * 2 + S; }
    else if (Op == 4 && Element == 1 && !S) { Bytes = 8; Lane = Q; }
    else if (Op == 6 && !S) { Bytes = 1U << Element; Replicate = Q ? 16 : 8; }
    else return false;
  } else if ((Insn & 0x3ffffc00) == 0x08dffc00 || (Insn & 0x3ffffc00) == 0x38bfc000) {
    // LDAR / LDAPR (including byte and halfword forms).
    Acquire = true;
  } else if ((Insn & 0x3fe00c00) == 0x19400000) {
    // LDAPUR, the RCpc immediate form used for TSO.
    Address += static_cast<int64_t>(static_cast<int32_t>((Insn >> 12) & 511) - ((Insn & (1U << 20)) ? 512 : 0));
    Acquire = true;
  } else if ((Insn & 0x3a000000) == 0x28000000) {
    // LDP (including REP MOVS post-index) / LDNP.
    const unsigned Mode = (Insn >> 23) & 3;
    if (!(Insn & (1U << 22))) {
      return false;
    }
    if (Vector) {
      if (Size == 3) return false;
      Bytes = 4U << Size;
    } else {
      if (Size == 3 || (Size == 1 && Mode == 0)) return false;
      Bytes = Size == 2 ? 8 : 4;
      Signed = Size == 1; // LDPSW
      Word = Size == 0;
    }
    const int Offset = static_cast<int>((Insn >> 15) & 127) - ((Insn & (1U << 21)) ? 128 : 0);
    WritebackAddress = Address + static_cast<int64_t>(Offset) * Bytes;
    Writeback = Mode & 1;
    if (Mode != 1) Address = WritebackAddress;
    Count = 2;
    Rt2 = (Insn >> 10) & 31;
    if (Rt == Rt2) return false;
  } else if ((Insn & 0x3b000000) == 0x39000000 || (Insn & 0x3b000000) == 0x38000000) {
    if (Vector) {
      if (!(Opc & 1) || (Opc == 3 && Size != 0)) return false;
      if (Opc == 3) Bytes = 16;
    } else {
      if (Opc == 0 || (Opc == 2 && Size == 3) || (Opc == 3 && Size >= 2)) return false;
      Signed = Opc >= 2;
      Word = Opc == 3 || (Opc == 1 && Size < 3);
    }
    if (Insn & (1U << 24)) {
      Address += ((Insn >> 10) & 4095) * Bytes;
    } else if ((Insn & 0x00200c00) == 0x00200800) {
      const unsigned Rm = (Insn >> 16) & 31;
      uint64_t Offset = Rm == 31 ? 0 : X[Rm];
      switch ((Insn >> 13) & 7) {
      case 2: Offset = static_cast<uint32_t>(Offset); break;
      case 3: break;
      case 6: Offset = static_cast<int64_t>(static_cast<int32_t>(Offset)); break;
      case 7: break;
      default: return false;
      }
      Address += Offset * ((Insn & (1U << 12)) ? Bytes : 1);
    } else if (!(Insn & (1U << 21)) && ((Insn >> 10) & 3) != 2) {
      const unsigned Mode = (Insn >> 10) & 3;
      WritebackAddress = Address + static_cast<int64_t>(static_cast<int32_t>((Insn >> 12) & 511) - ((Insn & (1U << 20)) ? 512 : 0));
      Writeback = Mode & 1;
      if (Mode != 1) Address = WritebackAddress;
    } else {
      return false; // Unprivileged and reserved forms.
    }
  } else {
    return false;
  }

  // SP is passed by value; integer destination/base overlap has constrained writeback.
  if (Writeback && (Rn == 31 || (!Vector && (Rn == Rt || (Count == 2 && Rn == Rt2))))) return false;
  const uint64_t Offset = Address - GuestBase;
  if (Offset >= PageSize || Bytes * Count > PageSize - Offset || Fault < Address || Fault - Address >= Bytes * Count) {
    return false;
  }
  auto* V = static_cast<uint8_t*>(Vectors);
  for (unsigned I = 0; I < Count; ++I) {
    const unsigned Dst = I ? Rt2 : Rt;
    const auto* Source = Backing + Offset + I * Bytes;
    if (Vector) {
      if (Lane >= 0) {
        std::memcpy(V + Dst * 16 + Lane * Bytes, Source, Bytes);
      } else {
        std::memset(V + Dst * 16, 0, 16);
        for (unsigned B = 0; B < (Replicate ? Replicate : Bytes); B += Bytes) {
          std::memcpy(V + Dst * 16 + B, Source, Bytes);
        }
      }
    } else {
      uint64_t Value = 0;
      switch (Bytes) {
      case 1: Value = *Source; break;
      case 2: { uint16_t T; std::memcpy(&T, Source, 2); Value = T; break; }
      case 4: { uint32_t T; std::memcpy(&T, Source, 4); Value = T; break; }
      case 8: std::memcpy(&Value, Source, 8); break;
      }
      if (Signed) {
        const unsigned Shift = 64 - Bytes * 8;
        Value = static_cast<int64_t>(Value << Shift) >> Shift;
      }
      if (Dst != 31) X[Dst] = Word ? static_cast<uint32_t>(Value) : Value;
    }
  }
  if (Writeback) X[Rn] = WritebackAddress;
  if (Acquire) std::atomic_thread_fence(std::memory_order_acquire);
  return true;
}
} // namespace FEX::Windows::UserSharedData
