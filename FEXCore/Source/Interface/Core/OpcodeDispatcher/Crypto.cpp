// SPDX-License-Identifier: MIT
/*
$info$
tags: frontend|x86-to-ir, opcodes|dispatcher-implementations
desc: Handles x86/64 Crypto instructions to IR
$end_info$
*/

#include "Interface/Core/X86Tables/X86Tables.h"

#include <FEXCore/Utils/LogManager.h>
#include "Interface/Core/OpcodeDispatcher.h"

#include <cstdint>

namespace FEXCore::IR {
class OrderedNode;

#define OpcodeArgs [[maybe_unused]] FEXCore::X86Tables::DecodedOp Op

void OpDispatchBuilder::SHA1NEXTEOp(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsSHA) {
    UnimplementedOp(Op);
    return;
  }
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);

  Ref Result {};
  if (CTX->HostFeatures.SupportsSVE128) {
    auto ZeroVec = LoadZeroVector(OpSize::i128Bit);
    auto Tmp = _VInsElement(OpSize::i128Bit, OpSize::i32Bit, 3, 3, ZeroVec, Dest);
    auto Xar = _VXar(OpSize::i128Bit, OpSize::i32Bit, ZeroVec, Tmp, 2);
    Result = _VAdd(OpSize::i128Bit, OpSize::i32Bit, Src, Xar);
  } else {
    // ARMv8 SHA1 extension provides a `SHA1H` instruction which does a fixed rotate by 30.
    // This only operates on element 0 rather than element 3. We don't have the luxury of rewriting the x86 SHA algorithm to take advantage of this.
    // Move the element to zero, rotate, and then move back (Using duplicates).
    // Saves one instruction versus that path that doesn't support SHA extension.
    auto Duplicated = _VDupElement(OpSize::i128Bit, OpSize::i32Bit, Dest, 3);
    auto Sha1HRotated = _VSha1H(Duplicated);
    auto RotatedNode = _VDupElement(OpSize::i128Bit, OpSize::i32Bit, Sha1HRotated, 0);
    auto Tmp = _VAdd(OpSize::i128Bit, OpSize::i32Bit, Src, RotatedNode);
    Result = _VInsElement(OpSize::i128Bit, OpSize::i32Bit, 3, 3, Src, Tmp);
  }

  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
}

void OpDispatchBuilder::SHA1MSG1Op(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsSHA) {
    UnimplementedOp(Op);
    return;
  }
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);

  Ref NewVec = _VExtr(OpSize::i128Bit, OpSize::i64Bit, Dest, Src, 1);

  // [W0, W1, W2, W3] ^ [W2, W3, W4, W5]
  Ref Result = _VXor(OpSize::i128Bit, Dest, NewVec);

  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
}

void OpDispatchBuilder::SHA1MSG2Op(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsSHA) {
    UnimplementedOp(Op);
    return;
  }
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);

  // ARM SHA1 mostly matches x86 semantics, except the input and outputs are both flipped from elements 0,1,2,3 to 3,2,1,0.
  auto Src1 = SHADataShuffle(Dest);
  auto Src2 = SHADataShuffle(Src);

  // The result is swizzled differently than expected
  auto Result = SHADataShuffle(_VSha1SU1(Src1, Src2));

  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
}

void OpDispatchBuilder::SHA1RNDS4Op(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsSHA) {
    UnimplementedOp(Op);
    return;
  }
  const uint64_t Imm8 = Op->Src[1].Literal() & 0b11;
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);

  Ref Result {};
  Ref ConstantVector {};
  switch (Imm8) {
  case 0:
    ConstantVector = LoadAndCacheNamedVectorConstant(OpSize::i128Bit, FEXCore::IR::NamedVectorConstant::NAMED_VECTOR_SHA1RNDS_K0);
    break;
  case 1:
    ConstantVector = LoadAndCacheNamedVectorConstant(OpSize::i128Bit, FEXCore::IR::NamedVectorConstant::NAMED_VECTOR_SHA1RNDS_K1);
    break;
  case 2:
    ConstantVector = LoadAndCacheNamedVectorConstant(OpSize::i128Bit, FEXCore::IR::NamedVectorConstant::NAMED_VECTOR_SHA1RNDS_K2);
    break;
  case 3:
    ConstantVector = LoadAndCacheNamedVectorConstant(OpSize::i128Bit, FEXCore::IR::NamedVectorConstant::NAMED_VECTOR_SHA1RNDS_K3);
    break;
  }

  const auto ZeroRegister = LoadZeroVector(OpSize::i128Bit);

  Ref Src1 = SHADataShuffle(Dest);
  Ref Src2 = SHADataShuffle(Src);
  Src2 = _VAdd(OpSize::i128Bit, OpSize::i32Bit, Src2, ConstantVector);

  switch (Imm8) {
  case 0: Result = SHADataShuffle(_VSha1C(Src1, ZeroRegister, Src2)); break;
  case 2: Result = SHADataShuffle(_VSha1M(Src1, ZeroRegister, Src2)); break;
  case 1:
  case 3: Result = SHADataShuffle(_VSha1P(Src1, ZeroRegister, Src2)); break;
  }

  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
}

void OpDispatchBuilder::SHA256MSG1Op(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsSHA) {
    UnimplementedOp(Op);
    return;
  }
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);

  auto Result = _VSha256U0(Dest, Src);

  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
}

void OpDispatchBuilder::SHA256MSG2Op(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsSHA) {
    UnimplementedOp(Op);
    return;
  }
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);

  auto Src1 = _VExtr(OpSize::i128Bit, OpSize::i32Bit, Dest, Dest, 3);
  auto DupDst = _VDupElement(OpSize::i128Bit, OpSize::i32Bit, Dest, 3);
  auto Src2 = _VZip2(OpSize::i128Bit, OpSize::i64Bit, DupDst, Src);

  auto Result = _VSha256U1(Src1, Src2);

  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
}

void OpDispatchBuilder::SHA256RNDS2Op(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsSHA) {
    UnimplementedOp(Op);
    return;
  }
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  // Hardcoded to XMM0
  auto XMM0 = LoadXMMRegister(0);

  auto shuffle_abcd = [this](Ref Src1, Ref Src2) -> Ref {
    // Generates a suitable SHA256 `abcd` configuration from x86 format.
    auto Tmp = _VZip2(OpSize::i128Bit, OpSize::i64Bit, Src2, Src1);
    return _VRev64(OpSize::i128Bit, OpSize::i32Bit, Tmp);
  };

  auto shuffle_efgh = [this](Ref Src1, Ref Src2) -> Ref {
    // Generates a suitable SHA256 `efgh` configuration from x86 format.
    auto Tmp = _VZip(OpSize::i128Bit, OpSize::i64Bit, Src2, Src1);
    return _VRev64(OpSize::i128Bit, OpSize::i32Bit, Tmp);
  };

  // ARM64 SHA256H/SHA256H2 run four rounds; x86 SHA256RNDS2 runs two. Lanes 2-3 of the
  // ARM64 output hold the state after rounds 1-2, which only consume key lanes 0-1, so the
  // full xmm0 can be passed as the key. Lanes 0-1 then hold rounds 3-4 using key lanes 2-3.
  //
  // That lets us fold the OpenSSL/Intel idiom:
  //   sha256rnds2 X, Y
  //   pshufd xmm0, xmm0, 0xe (or psrldq xmm0, 8)
  //   sha256rnds2 Y, X
  // The second instruction is exactly rounds 3-4 of the first instruction's ARM64 result when,
  // within the same IR block:
  //   - the second Src (X) is the first Dest and still holds the first result,
  //   - the second Dest (Y) is the first Src and was not written since,
  //   - xmm0 was written exactly once since, by an 8-byte VExtr of the previous xmm0.
  // A per-instruction SRA flush means operands are reloaded, so this tracks register writes
  // instead of SSA identity. All tracking is reset by full register-cache flushes.
  const bool DestIsXMM = Op->Dest.IsGPR() && Op->Dest.Data.GPR.GPR >= X86State::REG_XMM_0 && Op->Dest.Data.GPR.GPR <= X86State::REG_XMM_15;
  const bool SrcIsXMM =
    Op->Src[0].IsGPR() && Op->Src[0].Data.GPR.GPR >= X86State::REG_XMM_0 && Op->Src[0].Data.GPR.GPR <= X86State::REG_XMM_15;
  const uint8_t DestReg = DestIsXMM ? Op->Dest.Data.GPR.GPR - X86State::REG_XMM_0 : 0;
  const uint8_t SrcReg = SrcIsXMM ? Op->Src[0].Data.GPR.GPR - X86State::REG_XMM_0 : 0;
  auto& Pending = RegCache.SHA256Rounds;

  if (Pending.A && Pending.Block == GetCurrentBlock() && DestIsXMM && SrcIsXMM && SrcReg == Pending.DestReg && DestReg == Pending.SrcReg &&
      RegCache.WriteCount[FPR0Index + SrcReg] == Pending.DestCount && RegCache.WriteCount[FPR0Index + DestReg] == Pending.SrcCount &&
      RegCache.WriteCount[FPR0Index] == Pending.KeyCount + 1) {
    const auto* KeyShift = GetOpHeader(WrapNode(RegCache.LastDef[FPR0Index]));
    if (KeyShift->Op == IR::OP_VEXTR && KeyShift->Size == OpSize::i128Bit) {
      const auto* Extr = KeyShift->C<IR::IROp_VExtr>();
      const auto* Upper = GetOpHeader(Extr->VectorUpper);
      if (Extr->Index * IR::OpSizeToSize(KeyShift->ElementSize) == 8 && Upper->Op == IR::OP_LOADREGISTER &&
          Upper->C<IR::IROp_LoadRegister>()->Class == RegClass::FPR && Upper->C<IR::IROp_LoadRegister>()->Reg == 0) {
        Ref A = Pending.A;
        Ref B = Pending.B;
        Pending.A = nullptr;
        StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, shuffle_efgh(A, B));
        return;
      }
    }
  }

  auto ABCD = shuffle_abcd(Dest, Src);
  auto EFGH = shuffle_efgh(Dest, Src);

  auto A = _VSha256H(ABCD, EFGH, XMM0);
  auto B = _VSha256H2(EFGH, ABCD, XMM0);
  auto Result = shuffle_abcd(A, B);

  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);

  // Record this instruction as a possible first half. Aliased operands or xmm0 operands
  // would make the second half's inputs differ from what the fold assumes.
  Pending.A = nullptr;
  if (DestIsXMM && SrcIsXMM && DestReg != SrcReg && DestReg != 0 && SrcReg != 0) {
    Pending.A = A;
    Pending.B = B;
    Pending.Block = GetCurrentBlock();
    Pending.DestReg = DestReg;
    Pending.SrcReg = SrcReg;
    Pending.DestCount = RegCache.WriteCount[FPR0Index + DestReg];
    Pending.SrcCount = RegCache.WriteCount[FPR0Index + SrcReg];
    Pending.KeyCount = RegCache.WriteCount[FPR0Index];
  }
}

void OpDispatchBuilder::AESImcOp(OpcodeArgs, bool IsAVX) {
  if (!CTX->HostFeatures.SupportsAES) {
    UnimplementedOp(Op);
    return;
  }
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  Ref Result = _VAESImc(Src);

  if (IsAVX) {
    StoreResultFPR(Op, Result);
  } else {
    StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
  }
}

void OpDispatchBuilder::AESEncOp(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsAES) {
    UnimplementedOp(Op);
    return;
  }
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  Ref Result = _VAESEnc(OpSize::i128Bit, Dest, Src, LoadZeroVector(OpSize::i128Bit));
  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
}

void OpDispatchBuilder::VAESEncOp(OpcodeArgs) {
  const auto DstSize = OpSizeFromDst(Op);
  const auto Is256Bit = DstSize == OpSize::i256Bit;

  Ref State = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  Ref Key = LoadSourceFPR(Op, Op->Src[1], Op->Flags);
  Ref ZeroVec = LoadZeroVector(DstSize);

  Ref Result {};
  if (Is256Bit) {
    // TODO: Handle as one operation once vixl supports it.
    auto UpperState = _VDupElement(DstSize, OpSize::i128Bit, State, 1);
    auto UpperKey = _VDupElement(DstSize, OpSize::i128Bit, Key, 1);

    auto Lower = _VAESEnc(OpSize::i128Bit, State, Key, ZeroVec);
    auto Upper = _VAESEnc(OpSize::i128Bit, UpperState, UpperKey, ZeroVec);

    Result = _VInsElement(DstSize, OpSize::i128Bit, 1, 0, Lower, Upper);
  } else {
    Result = _VAESEnc(DstSize, State, Key, ZeroVec);
  }

  StoreResultFPR(Op, Result);
}

void OpDispatchBuilder::AESEncLastOp(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsAES) {
    UnimplementedOp(Op);
    return;
  }
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  Ref Result = _VAESEncLast(OpSize::i128Bit, Dest, Src, LoadZeroVector(OpSize::i128Bit));
  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
}

void OpDispatchBuilder::VAESEncLastOp(OpcodeArgs) {
  const auto DstSize = OpSizeFromDst(Op);
  const auto Is256Bit = DstSize == OpSize::i256Bit;

  Ref State = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  Ref Key = LoadSourceFPR(Op, Op->Src[1], Op->Flags);
  Ref ZeroVec = LoadZeroVector(DstSize);

  Ref Result {};
  if (Is256Bit) {
    // TODO: Handle as one operation once vixl supports it.
    auto UpperState = _VDupElement(DstSize, OpSize::i128Bit, State, 1);
    auto UpperKey = _VDupElement(DstSize, OpSize::i128Bit, Key, 1);

    auto Lower = _VAESEncLast(OpSize::i128Bit, State, Key, ZeroVec);
    auto Upper = _VAESEncLast(OpSize::i128Bit, UpperState, UpperKey, ZeroVec);

    Result = _VInsElement(DstSize, OpSize::i128Bit, 1, 0, Lower, Upper);
  } else {
    Result = _VAESEncLast(DstSize, State, Key, ZeroVec);
  }

  StoreResultFPR(Op, Result);
}

void OpDispatchBuilder::AESDecOp(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsAES) {
    UnimplementedOp(Op);
    return;
  }
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  Ref Result = _VAESDec(OpSize::i128Bit, Dest, Src, LoadZeroVector(OpSize::i128Bit));
  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
}

void OpDispatchBuilder::VAESDecOp(OpcodeArgs) {
  const auto DstSize = OpSizeFromDst(Op);
  const auto Is256Bit = DstSize == OpSize::i256Bit;

  Ref State = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  Ref Key = LoadSourceFPR(Op, Op->Src[1], Op->Flags);
  Ref ZeroVec = LoadZeroVector(DstSize);

  Ref Result {};
  if (Is256Bit) {
    // TODO: Handle as one operation once vixl supports it.
    auto UpperState = _VDupElement(DstSize, OpSize::i128Bit, State, 1);
    auto UpperKey = _VDupElement(DstSize, OpSize::i128Bit, Key, 1);

    auto Lower = _VAESDec(OpSize::i128Bit, State, Key, ZeroVec);
    auto Upper = _VAESDec(OpSize::i128Bit, UpperState, UpperKey, ZeroVec);

    Result = _VInsElement(DstSize, OpSize::i128Bit, 1, 0, Lower, Upper);
  } else {
    Result = _VAESDec(DstSize, State, Key, ZeroVec);
  }

  StoreResultFPR(Op, Result);
}

void OpDispatchBuilder::AESDecLastOp(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsAES) {
    UnimplementedOp(Op);
    return;
  }
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  Ref Result = _VAESDecLast(OpSize::i128Bit, Dest, Src, LoadZeroVector(OpSize::i128Bit));
  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
}

void OpDispatchBuilder::VAESDecLastOp(OpcodeArgs) {
  const auto DstSize = OpSizeFromDst(Op);
  const auto Is256Bit = DstSize == OpSize::i256Bit;

  Ref State = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  Ref Key = LoadSourceFPR(Op, Op->Src[1], Op->Flags);
  Ref ZeroVec = LoadZeroVector(DstSize);

  Ref Result {};
  if (Is256Bit) {
    // TODO: Handle as one operation once vixl supports it.
    auto UpperState = _VDupElement(DstSize, OpSize::i128Bit, State, 1);
    auto UpperKey = _VDupElement(DstSize, OpSize::i128Bit, Key, 1);

    auto Lower = _VAESDecLast(OpSize::i128Bit, State, Key, ZeroVec);
    auto Upper = _VAESDecLast(OpSize::i128Bit, UpperState, UpperKey, ZeroVec);

    Result = _VInsElement(DstSize, OpSize::i128Bit, 1, 0, Lower, Upper);
  } else {
    Result = _VAESDecLast(DstSize, State, Key, ZeroVec);
  }

  StoreResultFPR(Op, Result);
}

Ref OpDispatchBuilder::AESKeyGenAssistImpl(OpcodeArgs) {
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  const uint64_t RCON = Op->Src[1].Literal();

  auto KeyGenSwizzle = LoadAndCacheNamedVectorConstant(OpSize::i128Bit, NAMED_VECTOR_AESKEYGENASSIST_SWIZZLE);
  return _VAESKeyGenAssist(Src, KeyGenSwizzle, LoadZeroVector(OpSize::i128Bit), RCON);
}

void OpDispatchBuilder::AESKeyGenAssist(OpcodeArgs, bool IsAVX) {
  if (!CTX->HostFeatures.SupportsAES) {
    UnimplementedOp(Op);
    return;
  }

  Ref Result = AESKeyGenAssistImpl(Op);

  if (IsAVX) {
    StoreResultFPR(Op, Result);
  } else {
    StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
  }
}

void OpDispatchBuilder::PCLMULQDQOp(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsPMULL_128Bit) {
    UnimplementedOp(Op);
    return;
  }
  Ref Dest = LoadSourceFPR(Op, Op->Dest, Op->Flags);
  Ref Src = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  const auto Selector = static_cast<uint8_t>(Op->Src[1].Literal());

  auto Result = _PCLMUL(OpSize::i128Bit, Dest, Src, Selector & 0b1'0001);
  StoreResult_WithAVXInsert(VectorOpType::SSE, RegClass::FPR, Op, Result);
}

void OpDispatchBuilder::VPCLMULQDQOp(OpcodeArgs) {
  if (!CTX->HostFeatures.SupportsPMULL_128Bit) {
    UnimplementedOp(Op);
    return;
  }
  const auto DstSize = OpSizeFromDst(Op);

  Ref Src1 = LoadSourceFPR(Op, Op->Src[0], Op->Flags);
  Ref Src2 = LoadSourceFPR(Op, Op->Src[1], Op->Flags);
  const auto Selector = static_cast<uint8_t>(Op->Src[2].Literal());

  Ref Res = _PCLMUL(DstSize, Src1, Src2, Selector & 0b1'0001);
  StoreResultFPR(Op, Res);
}

} // namespace FEXCore::IR
