// SPDX-License-Identifier: MIT
/*
$info$
tags: backend|arm64
$end_info$
*/

#include "Interface/Context/Context.h"
#include "Interface/Core/Dispatcher/Dispatcher.h"
#include "Interface/Core/JIT/JITClass.h"

namespace FEXCore::CPU {
DEF_OP(CASPair) {
  auto Op = IROp->C<IR::IROp_CASPair>();
  LOGMAN_THROW_A_FMT(IROp->ElementSize == IR::OpSize::i32Bit || IROp->ElementSize == IR::OpSize::i64Bit, "Wrong element size");
  // Size is the size of each pair element
  auto Dst0 = GetReg(Op->OutLo);
  auto Dst1 = GetReg(Op->OutHi);
  auto Expected0 = GetReg(Op->ExpectedLo);
  auto Expected1 = GetReg(Op->ExpectedHi);
  auto Desired0 = GetReg(Op->DesiredLo);
  auto Desired1 = GetReg(Op->DesiredHi);
  auto MemSrc = GetReg(Op->Addr);

  const auto EmitSize = IROp->ElementSize == IR::OpSize::i64Bit ? ARMEmitter::Size::i64Bit : ARMEmitter::Size::i32Bit;

  // LL/SC on every host, including LSE ones: under contention caspal costs about 3x more than this loop on
  // Apple cores. The loop always stores, writing the observed value back on mismatch, as x86 locked CMPXCHG
  // does; that keeps the line exclusive. The trailing dmb ish makes it a full barrier. Comparing with
  // eor/orr/cbnz leaves NZCV untouched, so a fault on the store still exposes the exact guest flags.
  // This instruction sequence must be synced with HandleCASPair_LLSC.
  ARMEmitter::BackwardLabel LoopTop;
  ARMEmitter::ForwardLabel LoopNotExpected;
  ARMEmitter::ForwardLabel LoopDone;
  (void)Bind(&LoopTop);
  ldxp(EmitSize, TMP2, TMP3, MemSrc);
  eor(EmitSize, TMP4, TMP2, Expected0);
  eor(EmitSize, TMP1, TMP3, Expected1);
  orr(EmitSize, TMP4, TMP4, TMP1);
  (void)cbnz(EmitSize, TMP4, &LoopNotExpected);
  stlxp(EmitSize, TMP4, Desired0, Desired1, MemSrc);
  (void)cbnz(ARMEmitter::Size::i32Bit, TMP4, &LoopTop);
  (void)b(&LoopDone);
  (void)Bind(&LoopNotExpected);
  stlxp(EmitSize, TMP4, TMP2, TMP3, MemSrc);
  (void)cbnz(ARMEmitter::Size::i32Bit, TMP4, &LoopTop);
  (void)Bind(&LoopDone);
  dmb(ARMEmitter::BarrierScope::ISH);
  mov(EmitSize, Dst0, TMP2.R());
  mov(EmitSize, Dst1, TMP3.R());
}

DEF_OP(CAS) {
  auto Op = IROp->C<IR::IROp_CAS>();
  const auto EmitSize = ConvertSize(IROp);
  const auto SubEmitSize = ConvertSubRegSize8(IROp->Size);
  // DataSrc = *Src1
  // if (DataSrc == Src3) { *Src1 == Src2; } Src2 = DataSrc
  // This will write to memory! Careful!

  auto Expected = GetReg(Op->Expected);
  auto Desired = GetReg(Op->Desired);
  auto MemSrc = GetReg(Op->Addr);
  auto Dst = GetReg(Node);

  // LL/SC on every host, including LSE ones; see CASPair for why. sub/cbnz leaves NZCV untouched.
  // This instruction sequence must be synced with HandleCAS_LLSC.
  ARMEmitter::BackwardLabel LoopTop;
  ARMEmitter::ForwardLabel LoopNotExpected;
  ARMEmitter::ForwardLabel LoopDone;
  (void)Bind(&LoopTop);
  ldxr(SubEmitSize, TMP2, MemSrc);
  if (IROp->Size == IR::OpSize::i8Bit) {
    sub(EmitSize, TMP3, TMP2, Expected, ARMEmitter::ExtendedType::UXTB, 0);
  } else if (IROp->Size == IR::OpSize::i16Bit) {
    sub(EmitSize, TMP3, TMP2, Expected, ARMEmitter::ExtendedType::UXTH, 0);
  } else {
    sub(EmitSize, TMP3, TMP2, Expected);
  }
  (void)cbnz(EmitSize, TMP3, &LoopNotExpected);
  stlxr(SubEmitSize, TMP3, Desired, MemSrc);
  (void)cbnz(ARMEmitter::Size::i32Bit, TMP3, &LoopTop);
  (void)b(&LoopDone);
  (void)Bind(&LoopNotExpected);
  stlxr(SubEmitSize, TMP3, TMP2, MemSrc);
  (void)cbnz(ARMEmitter::Size::i32Bit, TMP3, &LoopTop);
  (void)Bind(&LoopDone);
  dmb(ARMEmitter::BarrierScope::ISH);
  mov(EmitSize, Dst, TMP2.R());
}

DEF_OP(AtomicSwap) {
  auto Op = IROp->C<IR::IROp_AtomicSwap>();
  const auto OpSize = IROp->Size;
  LOGMAN_THROW_A_FMT(
    OpSize == IR::OpSize::i64Bit || OpSize == IR::OpSize::i32Bit || OpSize == IR::OpSize::i16Bit || OpSize == IR::OpSize::i8Bit, "Unexpecte"
                                                                                                                                 "d CAS "
                                                                                                                                 "size");

  auto MemSrc = GetReg(Op->Addr);
  auto Src = GetReg(Op->Value);

  const auto EmitSize = ConvertSize(IROp);
  const auto SubEmitSize = OpSize == IR::OpSize::i64Bit ? ARMEmitter::SubRegSize::i64Bit :
                           OpSize == IR::OpSize::i32Bit ? ARMEmitter::SubRegSize::i32Bit :
                           OpSize == IR::OpSize::i16Bit ? ARMEmitter::SubRegSize::i16Bit :
                                                          ARMEmitter::SubRegSize::i8Bit;

  if (CTX->HostFeatures.SupportsAtomics) {
    ldswpal(SubEmitSize, Src, GetReg(Node), MemSrc);
  } else {
    ARMEmitter::BackwardLabel LoopTop;
    (void)Bind(&LoopTop);
    ldaxr(SubEmitSize, TMP2, MemSrc);
    stlxr(SubEmitSize, TMP4, Src, MemSrc);
    (void)cbnz(EmitSize, TMP4, &LoopTop);
    ubfm(EmitSize, GetReg(Node), TMP2, 0, IR::OpSizeAsBits(OpSize) - 1);
  }
}

DEF_OP(AtomicFetchAdd) {
  auto Op = IROp->C<IR::IROp_AtomicFetchAdd>();
  const auto EmitSize = ConvertSize(IROp);
  const auto SubEmitSize = ConvertSubRegSize8(IROp->Size);

  auto MemSrc = GetReg(Op->Addr);
  auto Src = GetReg(Op->Value);

  if (CTX->HostFeatures.SupportsAtomics) {
    ldaddal(SubEmitSize, Src, GetReg(Node), MemSrc);
  } else {
    ARMEmitter::BackwardLabel LoopTop;
    (void)Bind(&LoopTop);
    ldaxr(SubEmitSize, TMP2, MemSrc);
    add(EmitSize, TMP3, TMP2, Src);
    stlxr(SubEmitSize, TMP4, TMP3, MemSrc);
    (void)cbnz(EmitSize, TMP4, &LoopTop);
    mov(EmitSize, GetReg(Node), TMP2.R());
  }
}

DEF_OP(AtomicFetchSub) {
  auto Op = IROp->C<IR::IROp_AtomicFetchSub>();
  const auto EmitSize = ConvertSize(IROp);
  const auto SubEmitSize = ConvertSubRegSize8(IROp->Size);

  auto MemSrc = GetReg(Op->Addr);
  auto Src = GetReg(Op->Value);

  if (CTX->HostFeatures.SupportsAtomics) {
    neg(EmitSize, TMP2, Src);
    ldaddal(SubEmitSize, TMP2, GetReg(Node), MemSrc);
  } else {
    ARMEmitter::BackwardLabel LoopTop;
    (void)Bind(&LoopTop);
    ldaxr(SubEmitSize, TMP2, MemSrc);
    sub(EmitSize, TMP3, TMP2, Src);
    stlxr(SubEmitSize, TMP4, TMP3, MemSrc);
    (void)cbnz(EmitSize, TMP4, &LoopTop);
    mov(EmitSize, GetReg(Node), TMP2.R());
  }
}

DEF_OP(AtomicFetchAnd) {
  auto Op = IROp->C<IR::IROp_AtomicFetchAnd>();
  const auto EmitSize = ConvertSize(IROp);
  const auto SubEmitSize = ConvertSubRegSize8(IROp->Size);

  auto MemSrc = GetReg(Op->Addr);
  auto Src = GetReg(Op->Value);

  if (CTX->HostFeatures.SupportsAtomics) {
    mvn(EmitSize, TMP2, Src);
    ldclral(SubEmitSize, TMP2, GetReg(Node), MemSrc);
  } else {
    ARMEmitter::BackwardLabel LoopTop;
    (void)Bind(&LoopTop);
    ldaxr(SubEmitSize, TMP2, MemSrc);
    and_(EmitSize, TMP3, TMP2, Src);
    stlxr(SubEmitSize, TMP4, TMP3, MemSrc);
    (void)cbnz(EmitSize, TMP4, &LoopTop);
    mov(EmitSize, GetReg(Node), TMP2.R());
  }
}

DEF_OP(AtomicFetchCLR) {
  auto Op = IROp->C<IR::IROp_AtomicFetchCLR>();
  const auto EmitSize = ConvertSize(IROp);
  const auto SubEmitSize = ConvertSubRegSize8(IROp->Size);

  auto MemSrc = GetReg(Op->Addr);
  auto Src = GetReg(Op->Value);

  if (CTX->HostFeatures.SupportsAtomics) {
    ldclral(SubEmitSize, Src, GetReg(Node), MemSrc);
  } else {
    ARMEmitter::BackwardLabel LoopTop;
    (void)Bind(&LoopTop);
    ldaxr(SubEmitSize, TMP2, MemSrc);
    bic(EmitSize, TMP3, TMP2, Src);
    stlxr(SubEmitSize, TMP4, TMP3, MemSrc);
    (void)cbnz(EmitSize, TMP4, &LoopTop);
    mov(EmitSize, GetReg(Node), TMP2.R());
  }
}

DEF_OP(AtomicFetchOr) {
  auto Op = IROp->C<IR::IROp_AtomicFetchOr>();
  const auto EmitSize = ConvertSize(IROp);
  const auto SubEmitSize = ConvertSubRegSize8(IROp->Size);

  auto MemSrc = GetReg(Op->Addr);
  auto Src = GetReg(Op->Value);

  if (CTX->HostFeatures.SupportsAtomics) {
    ldsetal(SubEmitSize, Src, GetReg(Node), MemSrc);
  } else {
    ARMEmitter::BackwardLabel LoopTop;
    (void)Bind(&LoopTop);
    ldaxr(SubEmitSize, TMP2, MemSrc);
    orr(EmitSize, TMP3, TMP2, Src);
    stlxr(SubEmitSize, TMP4, TMP3, MemSrc);
    (void)cbnz(EmitSize, TMP4, &LoopTop);
    mov(EmitSize, GetReg(Node), TMP2.R());
  }
}

DEF_OP(AtomicFetchXor) {
  auto Op = IROp->C<IR::IROp_AtomicFetchXor>();
  const auto EmitSize = ConvertSize(IROp);
  const auto SubEmitSize = ConvertSubRegSize8(IROp->Size);

  auto MemSrc = GetReg(Op->Addr);
  auto Src = GetReg(Op->Value);

  if (CTX->HostFeatures.SupportsAtomics) {
    ldeoral(SubEmitSize, Src, GetReg(Node), MemSrc);
  } else {
    ARMEmitter::BackwardLabel LoopTop;
    (void)Bind(&LoopTop);
    ldaxr(SubEmitSize, TMP2, MemSrc);
    eor(EmitSize, TMP3, TMP2, Src);
    stlxr(SubEmitSize, TMP4, TMP3, MemSrc);
    (void)cbnz(EmitSize, TMP4, &LoopTop);
    mov(EmitSize, GetReg(Node), TMP2.R());
  }
}

DEF_OP(AtomicFetchNeg) {
  auto Op = IROp->C<IR::IROp_AtomicFetchNeg>();
  const auto EmitSize = ConvertSize(IROp);
  const auto SubEmitSize = ConvertSubRegSize8(IROp->Size);

  auto MemSrc = GetReg(Op->Addr);

  if (CTX->HostFeatures.SupportsAtomics) {
    // Use a CAS loop to avoid needing to emulate unaligned LLSC atomics
    ldr(SubEmitSize, TMP2, MemSrc);
    ARMEmitter::BackwardLabel LoopTop;
    (void)Bind(&LoopTop);
    mov(EmitSize, TMP4, TMP2);
    neg(EmitSize, TMP3, TMP2);
    casal(SubEmitSize, TMP2, TMP3, MemSrc);
    sub(EmitSize, TMP3, TMP2, TMP4);
    (void)cbnz(EmitSize, TMP3, &LoopTop);
    mov(EmitSize, GetReg(Node), TMP2.R());
  } else {
    ARMEmitter::BackwardLabel LoopTop;
    (void)Bind(&LoopTop);
    ldaxr(SubEmitSize, TMP2, MemSrc);
    neg(EmitSize, TMP3, TMP2);
    stlxr(SubEmitSize, TMP4, TMP3, MemSrc);
    (void)cbnz(EmitSize, TMP4, &LoopTop);
    mov(EmitSize, GetReg(Node), TMP2.R());
  }
}

DEF_OP(TelemetrySetValue) {
#ifndef FEX_DISABLE_TELEMETRY
  auto Op = IROp->C<IR::IROp_TelemetrySetValue>();
  auto Src = GetReg(Op->Value);

  ldr(TMP2, STATE_PTR_IDX(CpuStateFrame, Pointers.TelemetryValueAddresses, Op->TelemetryValueIndex));

  // Cortex fuses cmp+cset.
  cmp(ARMEmitter::Size::i32Bit, Src, 0);
  cset(ARMEmitter::Size::i32Bit, TMP1, ARMEmitter::Condition::CC_NE);

  if (CTX->HostFeatures.SupportsAtomics) {
    stsetl(ARMEmitter::SubRegSize::i64Bit, TMP1, TMP2);
  } else {
    ARMEmitter::BackwardLabel LoopTop;
    (void)Bind(&LoopTop);
    ldaxr(ARMEmitter::SubRegSize::i64Bit, TMP3, TMP2);
    orr(ARMEmitter::Size::i32Bit, TMP3, TMP3, Src);
    stlxr(ARMEmitter::SubRegSize::i64Bit, TMP4, TMP3, TMP2);
    (void)cbnz(ARMEmitter::Size::i32Bit, TMP4, &LoopTop);
  }
#endif
}

} // namespace FEXCore::CPU
