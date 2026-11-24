//===- RISCVTTPostRA.cpp - Tenstorrent late code fixups ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Late fixups for Tenstorrent baby cores, run just before emission:
//
//  * Tensix instructions with run-time immediates are built in a GPR, now that
//    their register fields are known, and pushed to the instruction buffer.
//  * An SFPNOP is inserted after SFPU instructions with a result latency when
//    the next Tensix instruction reads the result and the hardware does not
//    interlock (Wormhole) or the interlock is broken (some Blackhole readers).
//  * Repeated Tensix instruction sequences use the replay buffer
//    (RISCVTTReplay.cpp).
//  * Inline "ebreak" is padded with nops (Wormhole/Blackhole erratum).
//  * Wormhole short stores are followed by a dummy load of the same address
//    before any later load can bypass them.
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVInstrInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "riscv-tt-post-ra"
#define PASS_NAME "RISC-V Tenstorrent late fixups"

namespace {

class RISCVTTPostRAImpl {
  const RISCVSubtarget &ST;
  const RISCVInstrInfo &TII;
  const TargetRegisterInfo &TRI;

public:
  RISCVTTPostRAImpl(const RISCVSubtarget &ST)
      : ST(ST), TII(*ST.getInstrInfo()), TRI(*ST.getRegisterInfo()) {}
  bool run(MachineFunction &MF);

private:
  bool insertSFPNops(MachineFunction &MF);
  bool expandSynth(MachineFunction &MF);
  bool fixEbreak(MachineFunction &MF);
  bool fixShortStores(MachineFunction &MF);
  void expandLUT(MachineInstr &MI);
};

} // namespace

static bool isTensix(const MachineInstr &MI) {
  return RISCVII::isTensix(MI.getDesc().TSFlags);
}

// The opcode and modifier fields of a run-time synthesized instruction, or 0.
static unsigned getSynthOp(const MachineInstr &MI, unsigned &Mod) {
  unsigned WordOp;
  switch (MI.getOpcode()) {
  case RISCV::PseudoTTSynth:
    WordOp = 6;
    break;
  case RISCV::PseudoTTSynthStore:
    WordOp = 4;
    break;
  case RISCV::PseudoTTSynthRaw:
    WordOp = 3;
    break;
  default:
    return 0;
  }
  uint64_t Word = MI.getOperand(WordOp).getImm();
  Mod = Word & 0xf;
  return Word >> 24;
}

// SFPU instructions whose result is not available to the next instruction.
static bool hasDynamicDelay(const MachineInstr &MI) {
  unsigned Mod;
  switch (getSynthOp(MI, Mod)) {
  case 0x74: // SFPMULI
  case 0x75: // SFPADDI
    return true;
  }
  switch (MI.getOpcode()) {
  case RISCV::TT_SFPADD:
  case RISCV::TT_SFPMUL:
  case RISCV::TT_SFPMAD:
  case RISCV::TT_SFPADDI:
  case RISCV::TT_SFPMULI:
  case RISCV::TT_SFPLUT:
  case RISCV::TT_SFPLUTFP32:
  case RISCV::TT_SFPMUL24:
    return true;
  default:
    return false;
  }
}

// Instructions that must always be followed by an SFPNOP.
static bool hasStaticDelay(const MachineInstr &MI) {
  if (MI.getOpcode() != RISCV::TT_SFPSHFT2)
    return false;
  int64_t Mod = MI.getOperand(4).getImm();
  return Mod >= 2 && Mod <= 4;
}

// Blackhole readers that do not see a pending result.
static bool missesBHInterlock(const MachineInstr &MI) {
  unsigned Mod;
  switch (getSynthOp(MI, Mod)) {
  case 0x79: // SFPIADD
    return !(Mod & 1);
  case 0x7a: // SFPSHFT
    return !(Mod & 4);
  case 0x91: // SFPCONFIG
    return true;
  }
  switch (MI.getOpcode()) {
  case RISCV::TT_SFPIADD:
    return !(MI.getOperand(4).getImm() & 1);
  case RISCV::TT_SFPSHFT:
    return !(MI.getOperand(4).getImm() & 4);
  case RISCV::TT_SFPAND_BH:
  case RISCV::TT_SFPOR_BH:
  case RISCV::TT_SFPCONFIG:
  case RISCV::TT_SFPSWAP:
  case RISCV::TT_SFPSWAP_CD:
  case RISCV::TT_SFPSWAP_CS:
  case RISCV::TT_SFPSHFT2:
    return true;
  default:
    return false;
  }
}

// Collect the Tensix instructions that may execute next, following
// fallthrough and branches.
static void findNextTensix(MachineBasicBlock::iterator I,
                           MachineBasicBlock *MBB,
                           SmallPtrSetImpl<MachineBasicBlock *> &Visited,
                           SmallVectorImpl<MachineInstr *> &Result) {
  for (; I != MBB->end(); ++I) {
    if (!isTensix(*I) || I->getOpcode() == RISCV::TT_TTINSN ||
        I->getOpcode() == RISCV::PseudoTTSynthRaw)
      continue;
    Result.push_back(&*I);
    return;
  }
  for (MachineBasicBlock *Succ : MBB->successors())
    if (Visited.insert(Succ).second)
      findNextTensix(Succ->begin(), Succ, Visited, Result);
}

bool RISCVTTPostRAImpl::insertSFPNops(MachineFunction &MF) {
  bool IsBH = ST.hasVendorXTTTensixBH();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      bool Static = hasStaticDelay(MI);
      if (!Static && !hasDynamicDelay(MI))
        continue;
      SmallPtrSet<MachineBasicBlock *, 8> Visited;
      SmallVector<MachineInstr *, 4> Next;
      findNextTensix(std::next(MI.getIterator()), &MBB, Visited, Next);
      bool NeedNop = false;
      for (MachineInstr *N : Next) {
        if (N->getOpcode() == RISCV::TT_SFPNOP)
          continue;
        if (Static) {
          NeedNop = true;
          break;
        }
        Register Dst = MI.getOperand(0).getReg();
        if (N->readsRegister(Dst, &TRI) && (!IsBH || missesBHInterlock(*N))) {
          NeedNop = true;
          break;
        }
      }
      if (!NeedNop)
        continue;
      BuildMI(MBB, std::next(MI.getIterator()), MI.getDebugLoc(),
              TII.get(RISCV::TT_SFPNOP));
      Changed = true;
    }
  }
  return Changed;
}

// The fp32 table lookup with three registers takes its destination index
// from L7.
void RISCVTTPostRAImpl::expandLUT(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();
  int64_t Mod = MI.getOperand(0).getImm();
  if (MI.getOpcode() == RISCV::PseudoTTSFPLUT) {
    BuildMI(MBB, MI, DL, TII.get(RISCV::TT_SFPLUT), RISCV::L3)
        .addReg(RISCV::L3)
        .addImm(Mod);
  } else {
    if (MI.getOpcode() == RISCV::PseudoTTSFPLUTFP32_3R)
      BuildMI(MBB, MI, DL, TII.get(RISCV::TT_SFPLOADI), RISCV::L7)
          .addReg(RISCV::L7, RegState::Undef)
          .addImm(3)
          .addImm(2);
    BuildMI(MBB, MI, DL, TII.get(RISCV::TT_SFPLUTFP32), RISCV::L3)
        .addReg(RISCV::L3)
        .addImm(Mod);
  }
  MI.eraseFromParent();
}

// li tmp, opcode | register fields; add tmp, tmp, value; sw tmp, 0(iptr)
bool RISCVTTPostRAImpl::expandSynth(MachineFunction &MF) {
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : make_early_inc_range(MBB)) {
      unsigned Opc = MI.getOpcode();
      if (Opc != RISCV::PseudoTTSynth && Opc != RISCV::PseudoTTSynthStore &&
          Opc != RISCV::PseudoTTSynthRaw)
        continue;
      const DebugLoc &DL = MI.getDebugLoc();
      unsigned First = Opc == RISCV::PseudoTTSynth ? 1 : 0;
      Register Tmp = MI.getOperand(First).getReg();
      uint64_t Word;
      Register Val, IPtr;
      if (Opc == RISCV::PseudoTTSynthRaw) {
        Val = MI.getOperand(1).getReg();
        IPtr = MI.getOperand(2).getReg();
        Word = MI.getOperand(3).getImm();
      } else {
        unsigned Base = Opc == RISCV::PseudoTTSynth ? 3 : 1;
        Register Src = MI.getOperand(Base).getReg();
        Val = MI.getOperand(Base + 1).getReg();
        IPtr = MI.getOperand(Base + 2).getReg();
        Word = MI.getOperand(Base + 3).getImm();
        uint64_t Fields = MI.getOperand(Base + 4).getImm();
        unsigned DstShift = Fields & 0xff, SrcShift = (Fields >> 8) & 0xff;
        if (Opc == RISCV::PseudoTTSynth && DstShift != 0xff)
          Word |= uint64_t(TRI.getEncodingValue(MI.getOperand(0).getReg()))
                  << DstShift;
        if (SrcShift != 0xff)
          Word |= uint64_t(TRI.getEncodingValue(Src)) << SrcShift;
      }
      Register Pushed = Val;
      if (Word) {
        TII.movImm(MBB, MI, DL, Tmp, uint32_t(Word));
        BuildMI(MBB, MI, DL, TII.get(RISCV::ADD), Tmp).addReg(Tmp).addReg(Val);
        Pushed = Tmp;
      }
      BuildMI(MBB, MI, DL, TII.get(RISCV::SW))
          .addReg(Pushed)
          .addReg(IPtr)
          .addImm(0);
      MI.eraseFromParent();
      Changed = true;
    }
  }
  return Changed;
}

bool RISCVTTPostRAImpl::fixEbreak(MachineFunction &MF) {
  constexpr unsigned NumNops = 8;
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!MI.isInlineAsm() ||
          StringRef(MI.getOperand(0).getSymbolName()).trim() != "ebreak")
        continue;
      for (unsigned I = 0; I < NumNops; ++I)
        BuildMI(MBB, std::next(MI.getIterator()), MI.getDebugLoc(),
                TII.get(RISCV::ADDI), RISCV::X0)
            .addReg(RISCV::X0)
            .addImm(0);
      Changed = true;
    }
  }
  return Changed;
}

// Wormhole can let a later load bypass an earlier byte or halfword store to
// the same word. Reading the stored location back makes the store complete.
bool RISCVTTPostRAImpl::fixShortStores(MachineFunction &MF) {
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    MachineInstr *Pending = nullptr;
    auto Flush = [&](MachineBasicBlock::iterator Where) {
      unsigned LoadOpc =
          Pending->getOpcode() == RISCV::SB ? RISCV::LBU : RISCV::LHU;
      BuildMI(MBB, Where, Pending->getDebugLoc(), TII.get(LoadOpc), RISCV::X0)
          .add(Pending->getOperand(1))
          .add(Pending->getOperand(2))
          .cloneMemRefs(*Pending);
      Pending = nullptr;
      Changed = true;
    };
    for (MachineInstr &MI : MBB) {
      bool IsShortStore =
          MI.getOpcode() == RISCV::SB || MI.getOpcode() == RISCV::SH;
      if (Pending &&
          (IsShortStore || MI.isCall() || MI.mayLoad() || MI.isTerminator() ||
           MI.modifiesRegister(Pending->getOperand(1).getReg(), &TRI)))
        Flush(MI.getIterator());
      if (IsShortStore && MI.getOperand(1).isReg())
        Pending = &MI;
    }
    if (Pending)
      Flush(MBB.end());
  }
  return Changed;
}

bool RISCVTTPostRAImpl::run(MachineFunction &MF) {
  bool Changed = false;
  if (ST.hasVendorXTTTensix()) {
    for (MachineBasicBlock &MBB : MF)
      for (MachineInstr &MI : make_early_inc_range(MBB))
        if (MI.getOpcode() == RISCV::PseudoTTReadLReg ||
            MI.getOpcode() == RISCV::PseudoTTWriteLReg) {
          bool IsRead = MI.getOpcode() == RISCV::PseudoTTReadLReg;
          Register LReg = RISCV::L0 + MI.getOperand(1).getImm();
          Register Dst = IsRead ? MI.getOperand(0).getReg() : LReg;
          Register Src = IsRead ? LReg : MI.getOperand(0).getReg();
          if (Dst != Src)
            BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCV::TT_SFPMOV), Dst)
                .addReg(Dst, RegState::Undef)
                .addReg(Src, RegState::Undef)
                .addImm(2);
          MI.eraseFromParent();
          Changed = true;
        } else if (MI.getOpcode() == RISCV::PseudoTTLoadI32) {
          Register Dst = MI.getOperand(0).getReg();
          uint32_t Value = MI.getOperand(1).getImm();
          BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCV::TT_SFPLOADI), Dst)
              .addReg(Dst, RegState::Undef)
              .addImm(Value & 0xffff)
              .addImm(2);
          BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCV::TT_SFPLOADI), Dst)
              .addReg(Dst)
              .addImm(Value >> 16)
              .addImm(8);
          MI.eraseFromParent();
          Changed = true;
        } else if (MI.getOpcode() == RISCV::PseudoTTSFPLUT ||
                   MI.getOpcode() == RISCV::PseudoTTSFPLUTFP32_3R ||
                   MI.getOpcode() == RISCV::PseudoTTSFPLUTFP32_6R) {
          expandLUT(MI);
          Changed = true;
        }
    Changed |= insertSFPNops(MF);
    if (ST.hasTTTensixOptimizeReplay() &&
        MF.getTarget().getOptLevel() != CodeGenOptLevel::None &&
        !MF.getFunction().hasOptNone())
      Changed |= optimizeTTReplay(MF);
    Changed |= expandSynth(MF);
  }
  if (ST.hasTTFixWHBHEbreak())
    Changed |= fixEbreak(MF);
  if (ST.hasTTFixWHRaw())
    Changed |= fixShortStores(MF);
  return Changed;
}

namespace {
class RISCVTTPostRALegacy : public MachineFunctionPass {
public:
  static char ID;

  RISCVTTPostRALegacy() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    return RISCVTTPostRAImpl(MF.getSubtarget<RISCVSubtarget>()).run(MF);
  }

  StringRef getPassName() const override { return PASS_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
} // namespace

char RISCVTTPostRALegacy::ID = 0;

INITIALIZE_PASS(RISCVTTPostRALegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createRISCVTTPostRALegacyPass() {
  return new RISCVTTPostRALegacy();
}

PreservedAnalyses RISCVTTPostRAPass::run(MachineFunction &MF,
                                         MachineFunctionAnalysisManager &) {
  if (!RISCVTTPostRAImpl(MF.getSubtarget<RISCVSubtarget>()).run(MF))
    return PreservedAnalyses::all();
  return getMachineFunctionPassPreservedAnalyses();
}
