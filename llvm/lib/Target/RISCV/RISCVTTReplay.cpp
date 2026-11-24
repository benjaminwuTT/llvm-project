//===- RISCVTTReplay.cpp - Use the Tensix replay buffer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A sequence of Tensix instructions that repeats within a basic block is
// recorded into the Tensix replay buffer the first time it executes, and later
// copies are replaced with a single TTREPLAY. This follows the replay pass of
// the GCC SFPI toolchain: matching sequences are grown one instruction at a
// time, and the buffer is filled greedily with the most profitable sequence.
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVInstrInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsRISCV.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "riscv-tt-replay"

namespace {

// Shorter sequences do not pay for the TTREPLAY instructions.
constexpr unsigned MinSequence = 4;
constexpr unsigned MaxBufferSize = 32;
constexpr unsigned ReplayOpcode = 0x04;

struct ReplayInsn {
  MachineInstr *MI;
  unsigned Hash;
  // How often each GPR the instruction reads had been written; a synthesized
  // instruction is only the same if it reads the same values.
  SmallVector<unsigned, 2> Ages;
  // Whether the next instruction cannot extend a sequence ending here.
  bool MustEnd = true;
};

// A half-open range of instructions or buffer entries.
struct Span {
  unsigned Begin;
  unsigned End;
};

// A sequence and its non-overlapping copies ("clones"), in program order.
struct Sequence {
  unsigned Parent;
  unsigned Hash;
  unsigned Length;
  SmallVector<Span, 4> Clones;
};

struct Slot {
  unsigned Start;
  unsigned Size;
};

enum class UserReplay { None, Playback, FixedCapture, VariableCapture };

class TTReplay {
  const RISCVInstrInfo &TII;
  bool IsBH;
  SmallVector<Slot, 4> FreeSlots;
  SmallVector<ReplayInsn, 64> Region;
  std::vector<Sequence> List;
  DenseMap<unsigned, SmallVector<unsigned, 1>> Map;

public:
  TTReplay(const RISCVSubtarget &ST)
      : TII(*ST.getInstrInfo()), IsBH(ST.hasVendorXTTTensixBH()) {}
  bool run(MachineFunction &MF, unsigned BufferSize);

private:
  bool findFreeSlots(MachineFunction &MF, unsigned BufferSize);
  bool processRegion();
  void extend(unsigned Parent, unsigned Length, unsigned Begin, unsigned End);
  unsigned buildSequences(unsigned MaxLength);
  void replace(const Sequence &Seq, unsigned Start);
};

} // namespace

static bool isTensix(const MachineInstr &MI) {
  return RISCVII::isTensix(MI.getDesc().TSFlags);
}

// The number of instructions a REPLAY with this Count field records or issues.
// Current silicon treats 0 as 32 on Wormhole and 64 on Blackhole, and Wormhole
// uses only the low five bits.
static unsigned getReplayCount(uint64_t Count, bool IsBH) {
  Count &= 0x3f;
  if (Count == 0)
    return IsBH ? 64 : 32;
  return !IsBH && Count > 32 ? Count - 32 : Count;
}

// Classify an explicit replay instruction, setting the buffer index it starts
// at and the number of instructions it records or issues.
static UserReplay getUserReplay(const MachineInstr &MI, bool IsBH,
                                unsigned &Start, unsigned &Len) {
  uint32_t Word;
  switch (MI.getOpcode()) {
  case RISCV::TT_TTREPLAY:
    Start = MI.getOperand(0).getImm() & 0x1f;
    Len = getReplayCount(MI.getOperand(1).getImm(), IsBH);
    return MI.getOperand(3).getImm() ? UserReplay::FixedCapture
                                     : UserReplay::Playback;
  case RISCV::TT_TTINSN:
    Word = MI.getOperand(0).getImm();
    break;
  case RISCV::PseudoTTSynthRaw:
    // A replay with a run-time length.
    Word = MI.getOperand(3).getImm();
    if (Word >> 24 != ReplayOpcode)
      return UserReplay::None;
    Start = (Word >> 14) & 0x1f;
    Len = 0;
    return Word & 1 ? UserReplay::VariableCapture : UserReplay::Playback;
  default:
    return UserReplay::None;
  }
  if (Word >> 24 != ReplayOpcode)
    return UserReplay::None;
  Start = (Word >> 14) & 0x1f;
  Len = getReplayCount(Word >> 4, IsBH);
  return Word & 1 ? UserReplay::FixedCapture : UserReplay::Playback;
}

static bool isGPR(Register Reg) {
  return Reg.isPhysical() && RISCV::GPRRegClass.contains(Reg);
}

static void ageDefs(const MachineInstr &MI, unsigned (&Ages)[32]) {
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isRegMask()) {
      for (unsigned I = 1; I != 32; ++I)
        if (MO.clobbersPhysReg(RISCV::X0 + I))
          ++Ages[I];
    } else if (MO.isReg() && MO.isDef() && isGPR(MO.getReg())) {
      ++Ages[MO.getReg() - RISCV::X0];
    }
  }
}

static bool isSameInsn(const ReplayInsn &A, const ReplayInsn &B) {
  if (A.MI->getOpcode() != B.MI->getOpcode() ||
      A.MI->getNumOperands() != B.MI->getNumOperands() || A.Ages != B.Ages)
    return false;
  for (unsigned I = 0, E = A.MI->getNumOperands(); I != E; ++I)
    if (!A.MI->getOperand(I).isIdenticalTo(B.MI->getOperand(I)))
      return false;
  return true;
}

// Integer instructions that do not affect the Tensix instruction stream; they
// can stay where they are inside a recorded or replayed sequence.
static bool isTransparent(const MachineInstr &MI) {
  return !isTensix(MI) && !MI.isTerminator() && !MI.isCall() &&
         !MI.isInlineAsm() && !MI.isMetaInstruction() && !MI.mayLoadOrStore() &&
         !MI.hasUnmodeledSideEffects() &&
         !MI.getFlag(MachineInstr::FrameSetup) &&
         !MI.getFlag(MachineInstr::FrameDestroy);
}

static void removeSpan(SmallVectorImpl<Span> &Free, Span Used) {
  if (Used.Begin >= Used.End)
    return;
  SmallVector<Span, 4> Rest;
  for (Span S : Free) {
    if (S.End <= Used.Begin || S.Begin >= Used.End) {
      Rest.push_back(S);
      continue;
    }
    if (S.Begin < Used.Begin)
      Rest.push_back({S.Begin, Used.Begin});
    if (S.End > Used.End)
      Rest.push_back({Used.End, S.End});
  }
  Free.assign(Rest);
}

// Remove the entries used by a replay of Len instructions from Start, or by
// one of run-time length if Len is 0. Buffer indices wrap around.
static void removeReplay(SmallVectorImpl<Span> &Free, unsigned Start,
                         unsigned Len) {
  if (!Len || Start + Len <= MaxBufferSize) {
    removeSpan(Free, {Start, Len ? Start + Len : MaxBufferSize});
    return;
  }
  removeSpan(Free, {Start, MaxBufferSize});
  removeSpan(Free, {0, std::min(Start + Len - MaxBufferSize, MaxBufferSize)});
}

// Remove the entries used by an explicit replay in IR.
static void removeIRReplay(SmallVectorImpl<Span> &Free, const IntrinsicInst &II,
                           bool IsBH) {
  auto ConstArg = [&](unsigned I) -> std::optional<uint64_t> {
    if (auto *C = dyn_cast<ConstantInt>(II.getArgOperand(I)))
      return C->getZExtValue();
    return std::nullopt;
  };
  std::optional<uint64_t> Word;
  switch (II.getIntrinsicID()) {
  case Intrinsic::riscv_tt_ttreplay: {
    std::optional<uint64_t> Start = ConstArg(2), Len = ConstArg(1);
    if (!Start)
      return removeSpan(Free, {0, MaxBufferSize});
    return removeReplay(Free, *Start & 0x1f,
                        Len ? getReplayCount(*Len, IsBH) : 0);
  }
  case Intrinsic::riscv_tt_ttinsn:
    Word = ConstArg(1);
    if (Word && *Word >> 24 == ReplayOpcode)
      removeReplay(Free, (*Word >> 14) & 0x1f,
                   getReplayCount(*Word >> 4, IsBH));
    return;
  case Intrinsic::riscv_tt_synth_void:
    // A replay with a run-time length, after RISCVTTLowering.
    Word = ConstArg(3);
    if (Word && *Word >> 24 == ReplayOpcode)
      removeReplay(Free, (*Word >> 14) & 0x1f, 0);
    return;
  default:
    return;
  }
}

// Remove the entries used by explicit replay instructions from the buffer.
// Returns false if the function must be left alone.
bool TTReplay::findFreeSlots(MachineFunction &MF, unsigned BufferSize) {
  SmallVector<Span, 4> Free = {{0, BufferSize}};
  // The buffer is shared by all functions, and entries recorded in one may be
  // replayed elsewhere (for example by the MOP), so reserve them module-wide.
  for (const Function &F : *MF.getFunction().getParent())
    for (const Instruction &I : instructions(F))
      if (auto *II = dyn_cast<IntrinsicInst>(&I))
        removeIRReplay(Free, *II, IsBH);
  for (MachineBasicBlock &MBB : MF) {
    unsigned Shadow = 0;
    for (MachineInstr &MI : MBB) {
      if (!isTensix(MI))
        continue;
      unsigned Start, Len;
      UserReplay Kind = getUserReplay(MI, IsBH, Start, Len);
      if (Kind == UserReplay::None) {
        if (Shadow)
          --Shadow;
        continue;
      }
      // Capturing a replay or capture is not understood.
      if (Shadow)
        return false;
      if (Kind == UserReplay::FixedCapture)
        Shadow = Len;
      removeReplay(Free, Start, Kind == UserReplay::VariableCapture ? 0 : Len);
    }
    // A capture that continues into another block.
    if (Shadow)
      return false;
  }

  FreeSlots.clear();
  for (Span S : Free)
    if (S.End - S.Begin >= MinSequence)
      FreeSlots.push_back({S.Begin, S.End - S.Begin});
  llvm::stable_sort(
      FreeSlots, [](const Slot &A, const Slot &B) { return A.Size > B.Size; });
  return !FreeSlots.empty();
}

void TTReplay::extend(unsigned Parent, unsigned Length, unsigned Begin,
                      unsigned End) {
  const ReplayInsn &Insn = Region[End - 1];
  unsigned Hash =
      Parent ? unsigned(hash_combine(List[Parent].Hash, Insn.Hash)) : Insn.Hash;
  SmallVector<unsigned, 1> &Candidates = Map[Hash];
  for (unsigned Ix : Candidates) {
    Sequence &Seq = List[Ix];
    // The parents match, so only the last instructions need comparing.
    if (Seq.Parent != Parent ||
        !isSameInsn(Region[Seq.Clones.front().End - 1], Insn))
      continue;
    Seq.Clones.push_back({Begin, End});
    return;
  }
  Candidates.push_back(List.size());
  List.push_back({Parent, Hash, Length, {{Begin, End}}});
}

// Build all sequences that occur more than once, shortest first. Returns the
// index of the first one at least MinSequence long. This is quadratic.
unsigned TTReplay::buildSequences(unsigned MaxLength) {
  List.clear();
  List.push_back({0, 0, 0, {}});
  Map.clear();
  for (unsigned I = 0, E = Region.size(); I != E; ++I)
    extend(0, 1, I, I + 1);
  unsigned FirstUseful = List.size();

  unsigned From = 1, Length = 1;
  while (Length++ < MaxLength && From != List.size()) {
    Map.clear();
    unsigned SeqEnd = List.size();
    for (unsigned SeqIx = From; SeqIx != SeqEnd; ++SeqIx) {
      // List grows in here, so do not hold references into it.
      for (unsigned CloneIx = 0, CloneEnd = List[SeqIx].Clones.size();
           CloneEnd > 1 && CloneIx != CloneEnd; ++CloneIx) {
        Span S = List[SeqIx].Clones[CloneIx];
        if (!Region[S.End - 1].MustEnd)
          extend(SeqIx, Length, S.Begin, S.End + 1);
      }
    }
    if (Length < MinSequence)
      FirstUseful = List.size();
    From = SeqEnd;
  }
  return FirstUseful;
}

// Record the first clone of Seq into the buffer at Start and replay the
// others.
void TTReplay::replace(const Sequence &Seq, unsigned Start) {
  unsigned Length = Seq.Length;
  MachineInstr &First = *Region[Seq.Clones.front().Begin].MI;
  BuildMI(*First.getParent(), First, First.getDebugLoc(),
          TII.get(RISCV::TT_TTREPLAY))
      .addImm(Start)
      .addImm(Length)
      .addImm(1)
      .addImm(1);
  // Keep the instruction cache from fusing the capture with the instructions
  // it records, which can hang Blackhole when the capture is in a loop.
  BuildMI(*First.getParent(), First, First.getDebugLoc(), TII.get(RISCV::ADDI),
          RISCV::X0)
      .addReg(RISCV::X0)
      .addImm(0);
  LLVM_DEBUG(dbgs() << "Recording " << Seq.Clones.size() << " x " << Length
                    << " at " << Start << " from " << First);

  for (const Span &Clone : drop_begin(Seq.Clones)) {
    MachineInstr &Last = *Region[Clone.End - 1].MI;
    auto MIB = BuildMI(*Last.getParent(), std::next(Last.getIterator()),
                       Last.getDebugLoc(), TII.get(RISCV::TT_TTREPLAY))
                   .addImm(Start)
                   .addImm(Length)
                   .addImm(0)
                   .addImm(0);
    // The replay writes what the removed instructions wrote.
    SmallSet<std::pair<Register, bool>, 8> Seen;
    for (unsigned I = Clone.Begin; I != Clone.End; ++I) {
      MachineInstr *MI = Region[I].MI;
      for (const MachineOperand &MO : MI->operands())
        if (MO.isReg() && MO.getReg() &&
            Seen.insert({MO.getReg(), MO.isDef()}).second)
          MIB.addReg(MO.getReg(),
                     RegState::Implicit |
                         (MO.isDef() ? RegState::Define : RegState::Undef));
      MI->eraseFromParent();
    }
  }
}

bool TTReplay::processRegion() {
  if (Region.size() < 2 * MinSequence)
    return false;
  unsigned FirstUseful = buildSequences(FreeSlots.front().Size);

  // Keep the non-overlapping clones of sequences that still repeat.
  SmallVector<Sequence *, 16> Active;
  for (unsigned I = FirstUseful, E = List.size(); I != E; ++I) {
    Sequence &Seq = List[I];
    unsigned Bound = 0;
    llvm::erase_if(Seq.Clones, [&](const Span &S) {
      if (Bound > S.Begin)
        return true;
      Bound = S.End;
      return false;
    });
    if (Seq.Clones.size() > 1)
      Active.push_back(&Seq);
  }

  bool Changed = false;
  SmallVector<Slot, 4> Slots = FreeSlots;
  while (!Active.empty()) {
    // Pick the biggest saving, preferring longer sequences.
    Sequence *Best = nullptr;
    unsigned BestSaving = 0;
    for (Sequence *Seq : Active) {
      if (Seq->Length > Slots.front().Size)
        break;
      unsigned Saving = (Seq->Clones.size() - 1) * (Seq->Length - 1) - 1;
      if (BestSaving < Saving ||
          (BestSaving == Saving && Best && Best->Length < Seq->Length)) {
        BestSaving = Saving;
        Best = Seq;
      }
    }
    if (!Best)
      break;

    // Use the smallest slot it fits in.
    unsigned SlotIx = 0;
    for (unsigned I = 1; I != Slots.size() && Slots[I].Size >= Best->Length;
         ++I)
      SlotIx = I;
    replace(*Best, Slots[SlotIx].Start);
    Changed = true;
    Slots[SlotIx].Start += Best->Length;
    Slots[SlotIx].Size -= Best->Length;
    if (Slots[SlotIx].Size < MinSequence)
      Slots.erase(Slots.begin() + SlotIx);
    if (Slots.empty())
      break;

    // Drop clones overlapping those just replaced, and sequences that no
    // longer repeat or fit.
    SmallVector<Sequence *, 16> Kept;
    for (Sequence *Seq : Active) {
      if (Seq->Length > Slots.front().Size)
        break;
      if (Seq == Best)
        continue;
      const Span *Used = Best->Clones.begin(), *UsedEnd = Best->Clones.end();
      llvm::erase_if(Seq->Clones, [&](const Span &S) {
        while (Used != UsedEnd && Used->End <= S.Begin)
          ++Used;
        return Used != UsedEnd && Used->Begin < S.End;
      });
      if (Seq->Clones.size() > 1)
        Kept.push_back(Seq);
    }
    Active = std::move(Kept);
  }
  return Changed;
}

bool TTReplay::run(MachineFunction &MF, unsigned BufferSize) {
  if (!findFreeSlots(MF, BufferSize))
    return false;

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    unsigned Ages[32] = {};
    unsigned Shadow = 0;
    bool MayContinue = false;
    Region.clear();
    // Sequences do not extend across memory accesses or other side effects,
    // and are only matched between calls, which may use the buffer
    // themselves.
    auto Flush = [&] {
      Changed |= processRegion();
      Region.clear();
      MayContinue = false;
    };
    for (MachineInstr &MI : make_early_inc_range(MBB)) {
      if (MI.isDebugInstr())
        continue;
      if (MI.isCall() || MI.isInlineAsm()) {
        Flush();
      } else if (!isTensix(MI)) {
        if (!MI.isMetaInstruction() && !isTransparent(MI))
          MayContinue = false;
      } else {
        unsigned Start, Len;
        UserReplay Kind = getUserReplay(MI, IsBH, Start, Len);
        // The rest of the block may be captured by an unknown length replay.
        if (Kind == UserReplay::VariableCapture)
          break;
        if (Kind != UserReplay::None) {
          Flush();
          if (Kind == UserReplay::FixedCapture)
            Shadow = Len;
        } else if (Shadow) {
          --Shadow;
        } else if (MI.getOpcode() == RISCV::TT_TTINSN ||
                   (MI.getOpcode() == RISCV::PseudoTTSynthRaw &&
                    !MI.getOperand(3).getImm())) {
          // A raw instruction might be a replay.
          Flush();
        } else {
          if (MayContinue)
            Region.back().MustEnd = false;
          ReplayInsn Insn{&MI, unsigned(MI.getOpcode()), {}};
          for (const MachineOperand &MO : MI.operands()) {
            Insn.Hash = unsigned(hash_combine(Insn.Hash, MO));
            if (MO.isReg() && MO.isUse() && isGPR(MO.getReg()))
              Insn.Ages.push_back(Ages[MO.getReg() - RISCV::X0]);
          }
          Region.push_back(std::move(Insn));
          MayContinue = true;
        }
      }
      ageDefs(MI, Ages);
    }
    Flush();
  }
  return Changed;
}

bool llvm::optimizeTTReplay(MachineFunction &MF) {
  const Function &F = MF.getFunction();
  unsigned BufferSize = MaxBufferSize;
  if (Attribute A = F.getFnAttribute("riscv-tt-replay-buffer-size");
      A.isValid())
    A.getValueAsString().getAsInteger(10, BufferSize);
  BufferSize = std::min(BufferSize, MaxBufferSize);
  if (BufferSize < MinSequence)
    return false;
  return TTReplay(MF.getSubtarget<RISCVSubtarget>()).run(MF, BufferSize);
}
