// SPDX-License-Identifier: MIT
/*
$info$
tags: backend|arm64
$end_info$
*/

#include "Interface/Context/Context.h"
#include "FEXCore/IR/IR.h"
#include "Interface/Core/LookupCache.h"

#include "Interface/Core/JIT/JITClass.h"

#include <FEXCore/Core/Thunks.h>
#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <FEXCore/Utils/MathUtils.h>

namespace FEXCore::CPU {

DEF_OP(CallbackReturn) {
  // spill back to CTX
  SpillStaticRegs(TMP1);

  // First we must reset the stack
  ResetStack();

  // We can now lower the ref counter again

  ldr(ARMEmitter::WReg::w2, STATE, offsetof(FEXCore::Core::CpuStateFrame, SignalHandlerRefCounter));
  sub(ARMEmitter::Size::i32Bit, ARMEmitter::Reg::r2, ARMEmitter::Reg::r2, 1);
  str(ARMEmitter::WReg::w2, STATE, offsetof(FEXCore::Core::CpuStateFrame, SignalHandlerRefCounter));

  // We need to adjust an additional 8 bytes to get back to the original "misaligned" RSP state
  ldr(ARMEmitter::XReg::x2, STATE, offsetof(FEXCore::Core::CpuStateFrame, State.gregs[X86State::REG_RSP]));
  add(ARMEmitter::Size::i64Bit, ARMEmitter::Reg::r2, ARMEmitter::Reg::r2, 8);
  str(ARMEmitter::XReg::x2, STATE, offsetof(FEXCore::Core::CpuStateFrame, State.gregs[X86State::REG_RSP]));

  PopCalleeSavedRegisters();

  // Return to the thunk
  ret();
}

DEF_OP(ExitFunction) {
  auto Op = IROp->C<IR::IROp_ExitFunction>();

  ResetStack();

  if (CTX->HostFeatures.IsInstCountCI) [[unlikely]] {
    // Emit function end marker
    udf(0x420F);
  }

  uint64_t NewRIP;

  if constexpr (Context::BLOCK_DEBUGGING) {
    // Skip block linking when BLOCK_DEBUGGING as it adds overhead and is unncessary.
    // This is a debug only feature and doesn't need caching help.
    bool IsInlineRIP = IsInlineConstant(Op->NewRIP, &NewRIP) || IsInlineEntrypointOffset(Op->NewRIP, &NewRIP);
    ARMEmitter::ForwardLabel l_ExitLink;
    if (IsInlineRIP) {
      ldr(TMP1, &l_ExitLink);
      str(TMP1, STATE, offsetof(FEXCore::Core::CpuStateFrame, State.rip));
    } else {
      auto RipReg = GetReg(Op->NewRIP);
      str(RipReg.X(), STATE, offsetof(FEXCore::Core::CpuStateFrame, State.rip));
    }
    ldr(TMP2, STATE, offsetof(FEXCore::Core::CpuStateFrame, Pointers.DispatcherLoopTop));
    br(TMP2);

    if (IsInlineRIP) {
      BindOrRestart(&l_ExitLink);
      dc64(NewRIP);
    }

    return;
  }

  if (IsInlineConstant(Op->NewRIP, &NewRIP) || IsInlineEntrypointOffset(Op->NewRIP, &NewRIP)) {
#ifdef ARCHITECTURE_arm64ec
    if (NewRIP < EC_CODE_BITMAP_MAX_ADDRESS && RtlIsEcCode(NewRIP)) {
      str(REG_CALLRET_SP, STATE_PTR(CpuStateFrame, State.callret_sp));
      add(ARMEmitter::Size::i64Bit, ARMEmitter::Reg::rsp, StaticRegisters[X86State::REG_RSP], 0);
      if (Op->PatchSiteAddress) {
        InsertGuestPatchableMove(RelocationTypes::RELOC_GUEST_PATCHABLE_RIP_MOVE, EC_CALL_CHECKER_PC_REG, NewRIP, Op->PatchSiteAddress,
                                 Op->PatchSiteSize);
      } else {
        InsertGuestRIPMove(EC_CALL_CHECKER_PC_REG, NewRIP);
      }
      ldr(TMP2, STATE_PTR(CpuStateFrame, Pointers.ExitFunctionEC));
      br(TMP2);
    } else {
#endif
      // In order to support direct branches without constantly hitting the L1 cache, we emit a call to a block linker,
      // this will compile the branch target block when it is hit and replace the branch to the linker at the callsite
      // with a direct branch to the destination block. Upon invalidation of the target block the backpatch is undone.
      //
      // In addition, to avoid needing to lookup in the cache for returns and any indirect branch prediction penalty,
      // a shadow stack of <GuestReturnRIP, HostReturnPC> pairs is maintained, acting as a first level cache for any
      // return operations. As the guest may not balance calls and returns exactly, an exception handler is expected to
      // be installed by the frontend, to reset the shadow stack to the middle of its valid bounds on overflow/underflow.
      // This shadow stack is also cleared on block invalidation operations or codebuffer switches, to ensure all pointed-to
      // host code is always valid.

      // This code will be backpatched by Arm64JITCore_ExitFunctionLink, below is an enumeration of all the possible cases.
      // Jump thunks are emitted in JIT.cpp after compilation of the entire multiblock.
      //
      // Call with known return block - unlinked
      //    00: adr TMP1, 0xC
      //    04: stp RetReg, TMP1, [SpReg, -0x10]!
      //    08: bl JmpThunk00
      //    JmpThunk00:
      //    00: b 0x8
      //    04: br TMP1
      //    08: ldr TMP1, <Shared exit linker>
      //    0c: blr TMP1
      //    10: HostCode
      //    18: GuestRIP
      //    20: CallerOffset
      //
      // Call with known return block after backpatching - linked in branch immediate range
      //    00: adr TMP1, 0xC
      //    04: stp RetReg, TMP1, [SpReg, -0x10]!
      //    08: bl HostCode                                        - MODIFIED
      //
      // Call with known return block after backpatching - linked out of range
      //    00: adr TMP1, 0xC
      //    04: stp RetReg, TMP1, [SpReg, -0x10]!
      //    08: bl JmpThunk00
      //    JmpThunk00:
      //    00: ldr TMP1, 0x10                                     - MODIFIED 2nd
      //    04: br TMP1
      //    08: ldr TMP1, <Shared exit linker>
      //    0c: blr TMP1
      //    10: HostCode                                           - MODIFIED 1st
      //    18: GuestRIP
      //    20: CallerOffset
      //
      // Jump - unlinked
      //    00: b JmpThunk00
      //    JmpThunk00:
      //    00: b 0x8
      //    04: br TMP1
      //    08: ldr TMP1, <Shared exit linker>
      //    0c: blr TMP1
      //    10: HostCode
      //    18: GuestRIP
      //    20: CallerOffset
      //
      // Jump after backpatching - linked in branch immediate range
      //    00: b HostCode                                         - MODIFIED
      //
      // Jump after backpatching - linked out of range
      //    00: b JmpThunk00
      //    JmpThunk00:
      //    00: ldr TMP1, 0x10                                     - MODIFIED 2nd
      //    04: br TMP1
      //    08: ldr TMP1, <Shared exit linker>
      //    0c: blr TMP1
      //    10: HostCode                                           - MODIFIED 1st
      //    18: GuestRIP
      //    20: CallerOffset

      ARMEmitter::ForwardLabel l_BranchHost;
      ARMEmitter::ForwardLabel l_CallReturn;
      if (Op->Hint == IR::BranchHint::Call) {
        if (!Op->CallReturnBlock.IsInvalid()) {
          auto CallReturnAddressReg = GetReg(Op->CallReturnAddress).X();
          PendingCallReturnTargetLabel = &CallReturnTargets.try_emplace(Op->CallReturnBlock.ID()).first->second;
          (void)adr(TMP1, &l_CallReturn);
          stp<ARMEmitter::IndexType::PRE>(CallReturnAddressReg, TMP1, REG_CALLRET_SP, -0x10);
        } else {
          stp<ARMEmitter::IndexType::PRE>(ARMEmitter::XReg::zr, ARMEmitter::XReg::zr, REG_CALLRET_SP, -0x10);
        }
      } else if (Op->Hint == IR::BranchHint::CheckTF) {
        ARMEmitter::ForwardLabel TFUnset;
        ldrb(TMP1, STATE_PTR(CpuStateFrame, State.flags[X86State::RFLAG_TF_RAW_LOC]));
        (void)cbz(ARMEmitter::Size::i32Bit, TMP1, &TFUnset);
        // todo do we need to account for cache patching here?
        InsertGuestRIPMove(TMP1, NewRIP);
        str(TMP1, STATE, offsetof(FEXCore::Core::CpuStateFrame, State.rip));
        ldr(TMP2, STATE, offsetof(FEXCore::Core::CpuStateFrame, Pointers.DispatcherLoopTop));
        blr(TMP2);
        (void)Bind(&TFUnset);
      }

      EmitLinkedBranch(NewRIP, Op->Hint == IR::BranchHint::Call, Op->PatchSiteAddress, Op->PatchSiteSize);
      (void)Bind(&l_CallReturn);
#ifdef ARCHITECTURE_arm64ec
    }
#endif
  } else {
    ARMEmitter::ForwardLabel SkipFullLookup;
    auto RipReg = GetReg(Op->NewRIP);

    if (Op->Hint == IR::BranchHint::Return) {
      // First try to pop from the call-ret stack, otherwise follow the normal path (but ending in a ret).
      // The hit returns straight away, so the predicted path has no taken branch before the `ret`.
      ARMEmitter::ForwardLabel CallRetMiss;
      ldp<ARMEmitter::IndexType::POST>(TMP1, TMP2, REG_CALLRET_SP, 0x10);
      sub(TMP1, TMP1, RipReg.X());
      (void)cbnz(ARMEmitter::Size::i64Bit, TMP1, &CallRetMiss);
      ret(TMP2);
      (void)Bind(&CallRetMiss);
    } else if (Op->InlineCache && !CTX->GetCodeCache().IsGeneratingCache && Op->Hint == IR::BranchHint::Call) {
      // Call inline cache: the hit runs straight through (guard, push, bl) with no taken branch, and the miss path is
      // emitted out of line by the inline cache loop at the end of CompileCode. Slot layout and patching are
      // described on the jump path below.
      //   adr  TMP4, Slot
      //   ldp  TMP1, TMP2, [TMP4]
      //   sub  TMP3, TMP1, Rip
      //   cbnz TMP3, Miss          ; out of line
      //   cbz  TMP1, Miss          ; an empty or delinked slot holds guest target 0, which must not match RIP 0
      //   <push>
      //   Site: bl Trampoline / bl HostTarget
      //   CallReturn:
      // Note: sub+cbz/cbnz used to preserve flags.
      auto& Slot = PendingInlineCacheSlots.emplace_back();
      Slot.Call = true;
      Slot.RipReg = RipReg.Idx();
      Slot.HasReturnBlock = !Op->CallReturnBlock.IsInvalid();
      Slot.CallReturnAddressReg = Slot.HasReturnBlock ? GetReg(Op->CallReturnAddress).Idx() : 0;
      adr_OrRestart(TMP4, &Slot.Label);
      ldp<ARMEmitter::IndexType::OFFSET>(TMP1, TMP2, TMP4, 0);
      sub(TMP3, TMP1, RipReg.X());
      cbnz_OrRestart(ARMEmitter::Size::i64Bit, TMP3, &Slot.Miss);
      cbz_OrRestart(ARMEmitter::Size::i64Bit, TMP1, &Slot.Miss);

      ARMEmitter::ForwardLabel l_CallReturn;
      if (Slot.HasReturnBlock) {
        PendingCallReturnTargetLabel = &CallReturnTargets.try_emplace(Op->CallReturnBlock.ID()).first->second;
      }
      EmitCallRetPush(Slot.HasReturnBlock, ARMEmitter::XRegister(Slot.CallReturnAddressReg), &l_CallReturn);
      Slot.SiteAddress = GetCursorAddress<uint64_t>();
      bl_OrRestart(&Slot.Trampoline);
      (void)Bind(&l_CallReturn);
      (void)Bind(&Slot.CallReturn);
      return;
    } else if (Op->InlineCache && !CTX->GetCodeCache().IsGeneratingCache) {
      // Slots are skipped when generating a code cache, which serializes the whole code buffer after slots were filled.
      // Per-callsite inline cache: a 32-byte {GuestTarget, HostTarget, SiteOffset | Call, PinnedGuest} slot in the block tail.
      // An empty slot (GuestTarget == 0) is filled by InlineCacheFill, which registers the slot as a block link so
      // invalidating the target clears it. A slot holding a different target falls back to the L1 lookup.
      // A hit ends at a dedicated site. InlineCacheFill patches it to a direct `b/bl HostTarget` when in range and the
      // target is the slot's first (pinned) target, which keeps monomorphic sites out of the indirect predictor; the
      // delinker restores it. Both forms are B, BL or NOP so they can be modified while other threads execute them.
      // Calls take the branch above. Note: sub+cbz/cbnz used to preserve flags.
      //   adr TMP4, Slot
      //   ldp TMP1, TMP2, [TMP4]
      //   sub TMP3, TMP1, Rip
      //   cbnz TMP3, Miss
      //   cbz TMP1, Miss      ; an empty or delinked slot holds guest target 0, which must not match RIP 0
      //   Site: nop / b HostTarget
      //         br TMP2
      //   Miss: <filler or L1 lookup>
      ARMEmitter::ForwardLabel L1Lookup;
      ARMEmitter::ForwardLabel Miss;
      auto& Slot = PendingInlineCacheSlots.emplace_back();
      Slot.Call = false;
      adr_OrRestart(TMP4, &Slot.Label);
      ldp<ARMEmitter::IndexType::OFFSET>(TMP1, TMP2, TMP4, 0);
      sub(TMP3, TMP1, RipReg.X());
      (void)cbnz(ARMEmitter::Size::i64Bit, TMP3, &Miss);
      (void)cbz(ARMEmitter::Size::i64Bit, TMP1, &Miss);
      Slot.SiteAddress = GetCursorAddress<uint64_t>();
      nop();
      br(TMP2);
      (void)Bind(&Miss);
      (void)cbnz(ARMEmitter::Size::i64Bit, TMP1, &L1Lookup);
      // Filler expects the target in State.rip and the slot in TMP4.
      str(RipReg.X(), STATE, offsetof(FEXCore::Core::CpuStateFrame, State.rip));
      ldr(TMP2, STATE, offsetof(FEXCore::Core::CpuStateFrame, Pointers.InlineCacheFiller));
      (void)b(&SkipFullLookup);
      (void)Bind(&L1Lookup);
    }

    EmitL1Lookup(RipReg.X(), &SkipFullLookup);

    (void)Bind(&SkipFullLookup);
    if (Op->Hint == IR::BranchHint::Call) {
      ARMEmitter::ForwardLabel l_CallReturn;
      const bool HasReturnBlock = !Op->CallReturnBlock.IsInvalid();
      if (HasReturnBlock) {
        PendingCallReturnTargetLabel = &CallReturnTargets.try_emplace(Op->CallReturnBlock.ID()).first->second;
      }
      EmitCallRetPush(HasReturnBlock, HasReturnBlock ? GetReg(Op->CallReturnAddress).X() : ARMEmitter::XReg::zr, &l_CallReturn);
      blr(TMP2);
      (void)Bind(&l_CallReturn);
    } else if (Op->Hint == IR::BranchHint::Return) {
      ret(TMP2);
    } else {
      br(TMP2);
    }
  }
}

DEF_OP(Jump) {
  const auto Op = IROp->C<IR::IROp_Jump>();

  PendingTargetLabel = JumpTarget(Op->TargetBlock);
}

DEF_OP(CondJump) {
  auto Op = IROp->C<IR::IROp_CondJump>();

  auto TrueTargetLabel = JumpTarget(Op->TrueBlock);

  if (Op->FromNZCV) {
    b_OrRestart(MapCC(Op->Cond), TrueTargetLabel);
  } else {
    uint64_t Const;
    const bool isConst = IsInlineConstant(Op->Cmp2, &Const);

    auto Reg = GetReg(Op->Cmp1);
    const auto Size = Op->CompareSize == IR::OpSize::i32Bit ? ARMEmitter::Size::i32Bit : ARMEmitter::Size::i64Bit;

    LOGMAN_THROW_A_FMT(IsGPR(Op->Cmp1), "CondJump: Expected GPR");
    LOGMAN_THROW_A_FMT(isConst, "CondJump: Expected constant source");

    if (Op->Cond == IR::CondClass::EQ) {
      LOGMAN_THROW_A_FMT(Const == 0, "CondJump: Expected 0 source");
      cbz_OrRestart(Size, Reg, TrueTargetLabel);
    } else if (Op->Cond == IR::CondClass::NEQ) {
      LOGMAN_THROW_A_FMT(Const == 0, "CondJump: Expected 0 source");
      cbnz_OrRestart(Size, Reg, TrueTargetLabel);
    } else if (Op->Cond == IR::CondClass::TSTZ) {
      LOGMAN_THROW_A_FMT(Const < 64, "CondJump: Expected valid bit source");
      tbz_OrRestart(Reg, Const, TrueTargetLabel);
    } else if (Op->Cond == IR::CondClass::TSTNZ) {
      LOGMAN_THROW_A_FMT(Const < 64, "CondJump: Expected valid bit source");
      tbnz_OrRestart(Reg, Const, TrueTargetLabel);
    } else {
      LOGMAN_THROW_A_FMT(false, "CondJump expected simple condition");
    }
  }

  PendingTargetLabel = JumpTarget(Op->FalseBlock);
}

DEF_OP(Syscall) {
  SpillStaticRegs(TMP1);

  // Jump to the syscall dispatch handler. We won't return after this.
  ldr(TMP1, STATE, offsetof(FEXCore::Core::CpuStateFrame, Pointers.ThreadDispatchSyscallHandler));
  br(TMP1);
}

DEF_OP(Thunk) {
  auto Op = IROp->C<IR::IROp_Thunk>();
  // Arguments are passed as follows:
  // X0: CTX
  // X1: Args (from guest stack)

  // spill to ctx before ra64 spill
  SpillStaticRegs(TMP1, {
                          .NZCV = false,
                        });

  PushDynamicRegs(TMP1);

  mov(ARMEmitter::Size::i64Bit, ARMEmitter::Reg::r0, GetReg(Op->ArgPtr));

  InsertNamedThunkRelocation(ARMEmitter::Reg::r2, Op->ThunkNameHash);
  if (!CTX->Config.DisableVixlIndirectCalls) [[unlikely]] {
    GenerateIndirectRuntimeCall<void, void*, void*>(ARMEmitter::Reg::r2);
  } else {
    blr(ARMEmitter::Reg::r2);
  }

  PopDynamicRegs();

  // load from ctx after ra64 refill
  FillStaticRegs({
    .NZCV = false,
  });
}

DEF_OP(ValidateCode) {
  auto Op = IROp->C<IR::IROp_ValidateCode>();
  auto Base = GetReg(Op->Address).X();
  int len = Op->CodeLength;

  const auto Dst = GetReg(Node);
  const auto CRC32Reg = GetReg(Op->crc);

  // Changes to TMP1
  auto WorkingReg = ARMEmitter::XReg::zr;
  auto TmpDataReg = TMP3;

  while (len >= 8) {
    ldr(TmpDataReg, Base, Op->CodeLength - len);
    crc32x(TMP1, WorkingReg, TmpDataReg);
    len -= 8;
    WorkingReg = TMP1;
  }

  while (len >= 4) {
    ldr(TmpDataReg.W(), Base, Op->CodeLength - len);
    crc32w(TMP1.W(), WorkingReg.W(), TmpDataReg.W());
    len -= 4;
    WorkingReg = TMP1;
  }

  while (len >= 2) {
    ldrh(TmpDataReg.W(), Base, Op->CodeLength - len);
    crc32h(TMP1.W(), WorkingReg.W(), TmpDataReg.W());
    len -= 2;
    WorkingReg = TMP1;
  }

  while (len >= 1) {
    ldrb(TmpDataReg.W(), Base, Op->CodeLength - len);
    crc32b(TMP1.W(), WorkingReg.W(), TmpDataReg.W());
    len -= 1;
    WorkingReg = TMP1;
  }

  sub(ARMEmitter::Size::i32Bit, Dst, WorkingReg, CRC32Reg);
}

DEF_OP(ThreadRemoveCodeEntry) {
  auto Op = IROp->C<IR::IROp_ThreadRemoveCodeEntry>();

  SpillStaticRegs(TMP1);

  // Store the new RIP to go to.
  str(GetReg(Op->NewRIP).X(), STATE, offsetof(FEXCore::Core::CpuStateFrame, State.rip));

  // Move the entry to ABI before saving state.
  mov(ARMEmitter::Size::i64Bit, ARMEmitter::Reg::r1, GetReg(Op->EntryToInvalidate));

  // Arguments are passed as follows:
  // X0: Thread
  // X1: RIPToInvalidate
  mov(ARMEmitter::Size::i64Bit, ARMEmitter::Reg::r0, STATE.R());

  // Jump to the invalidate dispatch handler. We won't return after this.
  ldr(ARMEmitter::XReg::x2, STATE, offsetof(FEXCore::Core::CpuStateFrame, Pointers.ThreadDispatchRemoveCodeEntry));
  br(ARMEmitter::XReg::x2);
}

DEF_OP(CPUID) {
  auto Op = IROp->C<IR::IROp_CPUID>();

  SpillStaticRegs(TMP1);

  LoadConstant(ARMEmitter::Size::i32Bit, TMP1, Op->InstSize);
  // Jump to the syscall dispatch handler. We won't return after this.
  ldr(TMP2, STATE, offsetof(FEXCore::Core::CpuStateFrame, Pointers.ThreadDispatchCPUIDHandler));
  br(TMP2);
}

DEF_OP(XGetBV) {
  auto Op = IROp->C<IR::IROp_XGetBV>();

  PushDynamicRegs(TMP4);
  SpillStaticRegs(TMP4);

  mov(ARMEmitter::Size::i32Bit, ARMEmitter::Reg::r1, GetReg(Op->Function));

  // x0 = CPUID Handler
  // x1 = XCR Function
  ldr(ARMEmitter::XReg::x0, STATE, offsetof(FEXCore::Core::CpuStateFrame, Pointers.CPUIDObj));
  ldr(ARMEmitter::XReg::x2, STATE, offsetof(FEXCore::Core::CpuStateFrame, Pointers.XCRFunction));
  if (!CTX->Config.DisableVixlIndirectCalls) [[unlikely]] {
    GenerateIndirectRuntimeCall<uint64_t, void*, uint32_t>(ARMEmitter::Reg::r2);
  } else {
    blr(ARMEmitter::Reg::r2);
  }

  if (!TMP_ABIARGS) {
    mov(ARMEmitter::Size::i64Bit, TMP1, ARMEmitter::Reg::r0);
  }

  FillStaticRegs();

  PopDynamicRegs();

  // Results are in x0, need to split into i32 parts
  mov(ARMEmitter::Size::i32Bit, GetReg(Op->OutEAX), TMP1);
  ubfx(ARMEmitter::Size::i64Bit, GetReg(Op->OutEDX), TMP1, 32, 32);
}

} // namespace FEXCore::CPU
