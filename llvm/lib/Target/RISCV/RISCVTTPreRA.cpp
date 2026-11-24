//===- RISCVTTPreRA.cpp - Fold Tensix constant register reads -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Reads of the Tensix constant LREGs (L8-L15) are selected as copies to
// virtual registers, or ordered pseudos for the programmable ones. Ordinary
// SFPU source operands can name those registers directly, so this pass
// rewrites such uses and drops the reads, saving both an allocatable LREG and
// an SFPMOV.
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVInstrInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "riscv-tt-pre-ra"
#define PASS_NAME "RISC-V Tenstorrent constant register folding"

namespace {
class RISCVTTPreRA : public MachineFunctionPass {
public:
  static char ID;

  RISCVTTPreRA() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return PASS_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
} // namespace

char RISCVTTPreRA::ID = 0;

INITIALIZE_PASS(RISCVTTPreRA, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createRISCVTTPreRAPass() { return new RISCVTTPreRA(); }

// L12-L14 (and L11) may be rewritten by SFPCONFIG.
static bool isFixedConstant(MCRegister Reg) {
  return Reg == RISCV::L8 || Reg == RISCV::L9 || Reg == RISCV::L10 ||
         Reg == RISCV::L15;
}

// Constant registers that may be named in a VD field: an instruction with VD
// 12-15 writes a LOADMACRO template instead of executing.
static bool isHardwiredConstant(MCRegister Reg) {
  return Reg == RISCV::L8 || Reg == RISCV::L9 || Reg == RISCV::L10;
}

// The hardwired constant register that virtual register Reg copies, if any.
static MCRegister getHardwiredConstantSource(Register Reg,
                                             const MachineRegisterInfo &MRI) {
  if (!Reg.isVirtual())
    return MCRegister();
  const MachineInstr *Def = MRI.getVRegDef(Reg);
  if (!Def)
    return MCRegister();
  MCRegister Src;
  if (Def->getOpcode() == RISCV::PseudoTTReadLReg)
    Src = RISCV::L0 + Def->getOperand(1).getImm();
  else if (Def->isCopy() && Def->getOperand(1).getReg().isPhysical())
    Src = Def->getOperand(1).getReg().asMCReg();
  return isHardwiredConstant(Src) ? Src : MCRegister();
}

// The first operand of the CC-only SFPGT/SFPLE is their VD field.
static bool writesOperand(const MachineInstr &MI, unsigned OpNo) {
  return OpNo == 0 && (MI.getOpcode() == RISCV::TT_SFPGT_CC ||
                       MI.getOpcode() == RISCV::TT_SFPLE_CC);
}

static bool mayWriteConstant(const MachineInstr &MI) {
  return MI.isCall() || MI.isInlineAsm() ||
         MI.getOpcode() == RISCV::TT_SFPCONFIG ||
         MI.getOpcode() == RISCV::TT_TTINSN ||
         MI.getOpcode() == RISCV::PseudoTTSynthRaw;
}

// SFPSTORE's source is in the VD field, where L12-L15 select a LOADMACRO
// template write instead of a store.
static bool isTemplateIfConstant(const MachineInstr &MI, unsigned OpNo) {
  return OpNo == 0 && (MI.getOpcode() == RISCV::TT_SFPSTORE_WH ||
                       MI.getOpcode() == RISCV::TT_SFPSTORE_BH);
}

// Whether only re-reads of LREGs separate Read from User.
static bool isReadNextTo(const MachineInstr &Read, const MachineInstr &User) {
  for (const MachineInstr *MI = Read.getNextNode(); MI;
       MI = MI->getNextNode()) {
    if (MI == &User)
      return true;
    if (MI->getOpcode() != RISCV::PseudoTTReadLReg)
      return false;
  }
  return false;
}

static bool comesBefore(const MachineInstr &A, const MachineInstr &B) {
  const MachineBasicBlock &MBB = *A.getParent();
  for (MachineBasicBlock::const_iterator I = std::next(A.getIterator());
       I != MBB.end(); ++I)
    if (&*I == &B)
      return true;
  return false;
}

// Whether the constant register read by Copy may have been rewritten by the
// time User executes.
static bool mayBeClobbered(const MachineInstr &Copy, const MachineInstr &User,
                           ArrayRef<const MachineInstr *> Writers) {
  const MachineBasicBlock *CopyMBB = Copy.getParent();
  if (User.getParent() == CopyMBB && comesBefore(Copy, User)) {
    for (const MachineInstr *W : Writers)
      if (W->getParent() == CopyMBB && comesBefore(Copy, *W) &&
          comesBefore(*W, User))
        return true;
    return false;
  }
  // Paths that pass through the copy again read the register again, so only
  // writers on a path from the copy to User that avoids CopyMBB matter.
  auto ReachableFrom = [&](const MachineBasicBlock *From) {
    SmallPtrSet<const MachineBasicBlock *, 16> Reachable;
    SmallVector<const MachineBasicBlock *, 16> Worklist(From->successors());
    while (!Worklist.empty()) {
      const MachineBasicBlock *MBB = Worklist.pop_back_val();
      if (Reachable.insert(MBB).second && MBB != CopyMBB)
        append_range(Worklist, MBB->successors());
    }
    return Reachable;
  };
  auto FromCopy = ReachableFrom(CopyMBB);
  const MachineBasicBlock *UserMBB = User.getParent();
  return any_of(Writers, [&](const MachineInstr *W) {
    const MachineBasicBlock *WMBB = W->getParent();
    if (!FromCopy.contains(WMBB) && !(WMBB == CopyMBB && comesBefore(Copy, *W)))
      return false;
    if (WMBB == UserMBB && comesBefore(*W, User))
      return true;
    return ReachableFrom(WMBB).contains(UserMBB);
  });
}

bool RISCVTTPreRA::runOnMachineFunction(MachineFunction &MF) {
  if (!MF.getSubtarget<RISCVSubtarget>().hasVendorXTTTensix())
    return false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  SmallVector<const MachineInstr *, 8> Writers;
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      if (mayWriteConstant(MI))
        Writers.push_back(&MI);

  bool Changed = false;
  // Keep values read from or written to a numbered LREG in that LREG where
  // possible, as code mixing these with raw instructions expects; writing
  // back what was just read is a no-op.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : make_early_inc_range(MBB)) {
      bool IsRead = MI.getOpcode() == RISCV::PseudoTTReadLReg;
      if (!IsRead && MI.getOpcode() != RISCV::PseudoTTWriteLReg)
        continue;
      int64_t LReg = MI.getOperand(1).getImm();
      Register VReg = MI.getOperand(0).getReg();
      if (LReg >= 8 || !VReg.isVirtual())
        continue;
      MRI.setSimpleHint(VReg, RISCV::L0 + LReg);
      MachineInstr *Def = IsRead ? nullptr : MRI.getVRegDef(VReg);
      if (Def && Def->getOpcode() == RISCV::PseudoTTReadLReg &&
          Def->getOperand(1).getImm() == LReg && MRI.hasOneNonDBGUse(VReg)) {
        MI.eraseFromParent();
        Def->eraseFromParent();
        Changed = true;
      }
    }
  }

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &Copy : make_early_inc_range(MBB)) {
      Register Src;
      if (Copy.isCopy() && Copy.getOperand(0).getReg().isVirtual())
        Src = Copy.getOperand(1).getReg();
      else if (Copy.getOpcode() == RISCV::PseudoTTReadLReg)
        Src = RISCV::L0 + Copy.getOperand(1).getImm();
      if (!Src.isPhysical() || Src < RISCV::L8 || Src > RISCV::L15)
        continue;
      Register Dst = Copy.getOperand(0).getReg();
      SmallDenseMap<const MachineInstr *, Register, 4> ReReads;
      for (MachineOperand &MO :
           make_early_inc_range(MRI.use_nodbg_operands(Dst))) {
        MachineInstr &User = *MO.getParent();
        if (User.isDebugInstr() || User.isPHI() ||
            (!isFixedConstant(Src) && mayBeClobbered(Copy, User, Writers)))
          continue;
        unsigned OpNo = MO.getOperandNo();
        bool CanUsePhysReg =
            RISCVII::isTensix(User.getDesc().TSFlags) && !MO.isTied() &&
            User.getOpcode() != RISCV::PseudoTTSynth &&
            User.getOpcode() != RISCV::PseudoTTSynthStore &&
            OpNo < User.getDesc().getNumOperands() &&
            (!writesOperand(User, OpNo) || isHardwiredConstant(Src)) &&
            (!isTemplateIfConstant(User, OpNo) || Src < RISCV::L12) &&
            User.getDesc().operands()[OpNo].RegClass == RISCV::LRSRegClassID;
        if (CanUsePhysReg) {
          MO.setReg(Src);
          MO.setIsKill(false);
        } else if (!isReadNextTo(Copy, User) ||
                   (Copy.isCopy() && isTemplateIfConstant(User, OpNo))) {
          // Re-read the register next to its user rather than keeping an
          // allocatable LREG live (possibly across calls, which clobber all).
          // Unlike a COPY, the pseudo is neither moved nor coalesced.
          Register &NewReg = ReReads[&User];
          if (!NewReg) {
            NewReg = MRI.cloneVirtualRegister(Dst);
            BuildMI(*User.getParent(), User, Copy.getDebugLoc(),
                    TII->get(RISCV::PseudoTTReadLReg), NewReg)
                .addImm(Src - RISCV::L0);
          }
          MO.setReg(NewReg);
        } else {
          continue;
        }
        Changed = true;
      }
      if (MRI.use_nodbg_empty(Dst)) {
        Copy.eraseFromParent();
        Changed = true;
      }
    }
  }

  // An SFPSWAP operand that holds a fixed constant and whose result is unused
  // can name the constant register; the hardware discards the write.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : make_early_inc_range(MBB)) {
      if (MI.getOpcode() != RISCV::TT_SFPSWAP)
        continue;
      for (unsigned Side = 0; Side != 2; ++Side) {
        Register Def = MI.getOperand(Side).getReg();
        Register In = MI.getOperand(2 + Side).getReg();
        MCRegister Const = getHardwiredConstantSource(In, MRI);
        if (!Const || !MRI.use_nodbg_empty(Def))
          continue;
        Register Other = MI.getOperand(1 - Side).getReg();
        Register OtherIn = MI.getOperand(3 - Side).getReg();
        auto MIB = BuildMI(
            MBB, MI, MI.getDebugLoc(),
            TII->get(Side == 0 ? RISCV::TT_SFPSWAP_CD : RISCV::TT_SFPSWAP_CS),
            Other);
        if (Side == 0)
          MIB.addReg(Const).addReg(OtherIn);
        else
          MIB.addReg(OtherIn).addReg(Const);
        MIB.addImm(MI.getOperand(4).getImm());
        MI.eraseFromParent();
        if (MachineInstr *Read = MRI.getVRegDef(In);
            Read && MRI.use_nodbg_empty(In))
          Read->eraseFromParent();
        Changed = true;
        break;
      }
    }
  }
  return Changed;
}
