//===- RISCVTTLowering.cpp - Lower Tenstorrent Tensix pseudo intrinsics ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers the structured Tensix SFPU intrinsics produced by the sfpi
// headers into intrinsics that map onto single instructions:
//
//  * sfpxloadi becomes the cheapest SFPLOADI sequence or a constant register.
//  * sfpxcmp/sfpxlogic trees are expanded into condition code (CC) setting
//    instructions at their sfpxcond, and the sfpxpred markers are dropped.
//  * Redundant outermost SFPPUSHC/SFPPOPC pairs are removed.
//  * sfpassign_lv whose old value is provably dead is removed, and the rest
//    are folded into the instruction producing the new value when possible.
//  * A few peephole combines, e.g. SFPMUL + SFPADD into SFPMAD.
//
// See llvm/docs/RISCV/TenstorrentTensix.rst for the execution model.
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVTargetMachine.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsRISCV.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

#define DEBUG_TYPE "riscv-tt-lowering"
#define PASS_NAME "RISC-V Tenstorrent Tensix lowering"

namespace {

// Operand encodings shared with the sfpi headers (sfpi_constants.h).
enum : unsigned {
  XPredEnd = 0,
  XLogicAnd = 0,
  XLogicOr = 1,
  XLogicNot = 2,
  XLogicNearby = 3,
  CmpLT = 0,
  CmpGE = 1,
  CmpEQ = 2,
  CmpNE = 3,
  CmpGT = 4,
  CmpLE = 5,
  TypeUInt = 0,
  TypeInt = 1,
  TypeSMag = 2,
  TypeFloat = 3,
  SetCCLT0 = 0,
  SetCCNE0 = 2,
  SetCCGTE0 = 4,
  SetCCEQ0 = 6,
  IAddImm = 1,
  IAdd2sComp = 2,
  IAddCCLT0 = 0,
  IAddCCNone = 4,
  IAddCCGTE0 = 8,
  LoadIFloatB = 0,
  LoadIFloatA = 1,
  LoadIUShort = 2,
  LoadIShort = 4,
  LoadIUpper = 8,
  LoadILower = 10,
  GTLESetCC = 1,
  EnCCEIRI = 10,
  CRegZero = 9,
  CRegOne = 10,
  CRegNegOne = 11,
  CReg0P8373 = 8,
};

static unsigned getID(const Value *V) {
  if (const auto *II = dyn_cast_if_present<IntrinsicInst>(V))
    return II->getIntrinsicID();
  return Intrinsic::not_intrinsic;
}

static std::optional<uint64_t> getConstArg(const CallBase *CB, unsigned Idx) {
  if (const auto *C = dyn_cast<ConstantInt>(CB->getArgOperand(Idx)))
    return C->getZExtValue();
  return std::nullopt;
}

// Whether a call may change the condition code.
static bool writesCC(const Instruction &I) {
  const auto *CB = dyn_cast<CallBase>(&I);
  if (!CB)
    return false;
  auto ModHas = [&](unsigned Idx, unsigned Mask) {
    std::optional<uint64_t> Mod = getConstArg(CB, Idx);
    return !Mod || (*Mod & Mask);
  };
  switch (getID(CB)) {
  case Intrinsic::riscv_tt_sfpsetcc:
  case Intrinsic::riscv_tt_sfpencc:
  case Intrinsic::riscv_tt_sfpcompc:
  case Intrinsic::riscv_tt_sfppopc:
  case Intrinsic::riscv_tt_sfpxcond:
    return true;
  case Intrinsic::riscv_tt_sfpiadd_v:
    return !ModHas(3, IAddCCNone) || ModHas(3, IAddCCGTE0);
  case Intrinsic::riscv_tt_sfpiadd_i:
    return !ModHas(4, IAddCCNone) || ModHas(4, IAddCCGTE0);
  case Intrinsic::riscv_tt_sfpexexp:
  case Intrinsic::riscv_tt_sfplz:
    return ModHas(2, 0xa);
  case Intrinsic::riscv_tt_sfpgt:
  case Intrinsic::riscv_tt_sfple:
    return ModHas(3, 0x3);
  case Intrinsic::not_intrinsic:
    return !CB->isInlineAsm() && !CB->onlyAccessesInaccessibleMemory() &&
           !CB->doesNotAccessMemory();
  default:
    return false;
  }
}

static bool noCCChangeBetween(const Instruction *From, const Instruction *To) {
  if (From->getParent() != To->getParent() || !From->comesBefore(To))
    return false;
  for (const Instruction *I = From->getNextNode(); I != To;
       I = I->getNextNode())
    if (writesCC(*I) || getID(I) == Intrinsic::riscv_tt_sfppushc)
      return false;
  return true;
}

static bool isTTIntrinsic(const Instruction &I) {
  if (const auto *II = dyn_cast<IntrinsicInst>(&I)) {
    StringRef Name = Intrinsic::getBaseName(II->getIntrinsicID());
    return Name.starts_with("llvm.riscv.tt.");
  }
  return false;
}

class RISCVTTLoweringImpl {
  Function &F;
  const RISCVSubtarget &ST;
  bool IsBH;
  FixedVectorType *VecTy;

public:
  RISCVTTLoweringImpl(Function &F, const RISCVSubtarget &ST)
      : F(F), ST(ST), IsBH(ST.hasVendorXTTTensixBH()),
        VecTy(FixedVectorType::get(Type::getInt32Ty(F.getContext()), 32)) {}
  bool run();

private:
  Value *poison() const { return PoisonValue::get(VecTy); }
  Value *nullIPtr() const {
    return ConstantPointerNull::get(PointerType::getUnqual(F.getContext()));
  }
  CallInst *emit(IRBuilder<> &B, Intrinsic::ID ID, ArrayRef<Value *> Args) {
    return B.CreateCall(Intrinsic::getOrInsertDeclaration(F.getParent(), ID),
                        Args);
  }
  Value *creg(IRBuilder<> &B, unsigned Idx) {
    return emit(B, Intrinsic::riscv_tt_sfpreadlreg, {B.getInt32(Idx)});
  }
  void error(const Instruction *I, const Twine &Msg) {
    F.getContext().diagnose(
        DiagnosticInfoUnsupported(F, Msg, I->getDebugLoc()));
  }

  bool lowerXLoadI();
  Value *emitLoadImm(IRBuilder<> &B, Value *Old, Value *IPtr, Value *Imm,
                     int Bits);

  bool lowerPredication();
  bool expandCond(IRBuilder<> &B, Value *Node, bool Negate);
  void expandCmp(IRBuilder<> &B, CallInst *Cmp, bool Negate);
  bool optimizeCC();

  bool lowerDynamicImms();

  bool elideLiveAssigns();
  bool foldLiveAssigns();
  bool combine();
};

} // namespace

//===----------------------------------------------------------------------===//
// Constant loads.
//===----------------------------------------------------------------------===//

static bool isFP16a(uint32_t V) {
  unsigned Exp = (V >> 23) & 0xff;
  return (V & 0x1fff) == 0 && Exp > 112 && Exp < 143;
}

static uint32_t toFP16a(uint32_t V) {
  unsigned Exp = ((V >> 23) & 0xff) - 112;
  return ((V >> 16) & 0x8000) | (Exp << 10) | ((V >> 13) & 0x3ff);
}

Value *RISCVTTLoweringImpl::emitLoadImm(IRBuilder<> &B, Value *Old, Value *IPtr,
                                        Value *Imm, int Bits) {
  auto LoadI = [&](Value *PassThru, Value *Imm16, unsigned Mod) {
    return emit(B, Intrinsic::riscv_tt_sfploadi,
                {PassThru, IPtr, Imm16, B.getInt32(Mod)});
  };
  auto *CI = dyn_cast<ConstantInt>(Imm);
  if (!CI) {
    // A run-time value: narrow loads when the type allows, else two halves.
    if (Bits >= 0 && Bits < 16)
      return LoadI(Old, B.CreateAnd(Imm, 0xffff), LoadIShort);
    if (Bits < 0 && Bits >= -16)
      return LoadI(Old, B.CreateAnd(Imm, 0xffff), LoadIUShort);
    Value *Lo = LoadI(Old, B.CreateAnd(Imm, 0xffff), LoadIUShort);
    return LoadI(Lo, B.CreateLShr(Imm, 16), LoadIUpper);
  }

  uint32_t V = CI->getZExtValue();
  bool Unpredicated = isa<UndefValue>(Old);
  if (Unpredicated) {
    if (V == 0)
      return creg(B, CRegZero);
    if (V == 0x3f800000)
      return creg(B, CRegOne);
    if (V == 0xbf800000)
      return creg(B, CRegNegOne);
    if (V == 0x3f56594b)
      return creg(B, CReg0P8373);
  }
  if ((V & 0xffff) == 0)
    return LoadI(Old, B.getInt32(V >> 16), LoadIFloatB);
  if ((V >> 16) == 0)
    return LoadI(Old, B.getInt32(V), LoadIUShort);
  if ((V >> 15) == 0x1ffff)
    return LoadI(Old, B.getInt32(V & 0xffff), LoadIShort);
  if (isFP16a(V))
    return LoadI(Old, B.getInt32(toFP16a(V)), LoadIFloatA);
  Value *Lo = LoadI(Old, B.getInt32(V & 0xffff), LoadIUShort);
  return LoadI(Lo, B.getInt32(V >> 16), LoadIUpper);
}

bool RISCVTTLoweringImpl::lowerXLoadI() {
  bool Changed = false;
  for (Instruction &I : make_early_inc_range(instructions(F))) {
    if (getID(&I) != Intrinsic::riscv_tt_sfpxloadi)
      continue;
    auto *CB = cast<CallInst>(&I);
    IRBuilder<> B(CB);
    int Bits = int(cast<ConstantInt>(CB->getArgOperand(3))->getSExtValue());
    Value *V = emitLoadImm(B, CB->getArgOperand(0), CB->getArgOperand(1),
                           CB->getArgOperand(2), Bits);
    CB->replaceAllUsesWith(V);
    CB->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
// Predication.
//===----------------------------------------------------------------------===//

static bool isZeroVec(Value *V) {
  auto *CB = dyn_cast<CallInst>(V);
  if (!CB)
    return false;
  if (getID(CB) == Intrinsic::riscv_tt_sfpreadlreg)
    return getConstArg(CB, 0) == CRegZero;
  if (getID(CB) == Intrinsic::riscv_tt_sfploadi &&
      isa<UndefValue>(CB->getArgOperand(0))) {
    std::optional<uint64_t> Mod = getConstArg(CB, 3);
    return getConstArg(CB, 2) == 0 &&
           (Mod == LoadIFloatB || Mod == LoadIUShort || Mod == LoadIShort);
  }
  return false;
}

// The integer value of a constant vector load, if known.
static std::optional<int64_t> getIntConst(Value *V) {
  auto *CB = dyn_cast<CallInst>(V);
  if (!CB || getID(CB) != Intrinsic::riscv_tt_sfploadi ||
      !isa<UndefValue>(CB->getArgOperand(0)))
    return std::nullopt;
  std::optional<uint64_t> Imm = getConstArg(CB, 2);
  std::optional<uint64_t> Mod = getConstArg(CB, 3);
  if (!Imm || !Mod)
    return std::nullopt;
  if (*Mod == LoadIUShort)
    return int64_t(*Imm & 0xffff);
  if (*Mod == LoadIShort)
    return int64_t(int16_t(*Imm));
  return std::nullopt;
}

void RISCVTTLoweringImpl::expandCmp(IRBuilder<> &B, CallInst *Cmp,
                                    bool Negate) {
  Value *A = Cmp->getArgOperand(0);
  Value *Bv = Cmp->getArgOperand(1);
  unsigned Mod = cast<ConstantInt>(Cmp->getArgOperand(2))->getZExtValue();
  unsigned Op = Mod & 0xf;
  unsigned Type = (Mod >> 4) & 0x3;
  if (Negate)
    Op ^= 1;

  bool Ordered = Op == CmpLT || Op == CmpGE || Op == CmpGT || Op == CmpLE;
  auto GTLE = [&](bool IsGT, Value *X, Value *Y) {
    emit(B, IsGT ? Intrinsic::riscv_tt_sfpgt : Intrinsic::riscv_tt_sfple,
         {poison(), X, Y, B.getInt32(GTLESetCC)});
  };
  auto SetCC = [&](Value *X, unsigned CCMod) {
    emit(B, Intrinsic::riscv_tt_sfpsetcc, {X, B.getInt32(CCMod)});
  };

  // Blackhole compares floats and sign-magnitude values directly.
  if (IsBH && (Type == TypeFloat || Type == TypeSMag) && Ordered) {
    switch (Op) {
    case CmpLT:
      return GTLE(true, Bv, A);
    case CmpGE:
      return GTLE(false, Bv, A);
    case CmpGT:
      return GTLE(true, A, Bv);
    default:
      return GTLE(false, A, Bv);
    }
  }
  if (Type == TypeSMag && Ordered) {
    error(Cmp, "ordered comparison of vSMag values is not supported on "
               "Wormhole");
    return;
  }

  // Everything else compares the difference against zero.
  if (Op == CmpGT || Op == CmpLE) {
    std::swap(A, Bv);
    Op = Op == CmpGT ? CmpLT : CmpGE;
  }
  if ((Op == CmpEQ || Op == CmpNE) && isZeroVec(A))
    std::swap(A, Bv);

  static const unsigned SetCCMod[] = {SetCCLT0, SetCCGTE0, SetCCEQ0, SetCCNE0};
  if (isZeroVec(Bv))
    return SetCC(A, SetCCMod[Op]);

  std::optional<int64_t> C = Type == TypeFloat ? std::nullopt : getIntConst(Bv);
  bool UseImm = C && isInt<12>(-*C);
  if (IsBH && Op == CmpEQ && !UseImm) {
    GTLE(false, A, Bv);
    return GTLE(false, Bv, A);
  }

  if (Type == TypeFloat && Op != CmpEQ && Op != CmpNE) {
    // Wormhole: a - b computed as -1.0 * b + a.
    Value *Diff = emit(B, Intrinsic::riscv_tt_sfpmad,
                       {poison(), creg(B, CRegNegOne), Bv, A, B.getInt32(0)});
    return SetCC(Diff, SetCCMod[Op]);
  }

  unsigned CC = Op == CmpLT ? IAddCCLT0 : Op == CmpGE ? IAddCCGTE0 : IAddCCNone;
  Value *Diff;
  if (UseImm)
    Diff = emit(B, Intrinsic::riscv_tt_sfpiadd_i,
                {poison(), nullIPtr(), A, B.getInt32(uint32_t(-*C)),
                 B.getInt32(IAddImm | CC)});
  else
    Diff = emit(B, Intrinsic::riscv_tt_sfpiadd_v,
                {poison(), Bv, A, B.getInt32(IAdd2sComp | CC)});
  if (Op == CmpEQ || Op == CmpNE)
    SetCC(Diff, SetCCMod[Op]);
}

// Expand a boolean tree so that it is ANDed into the CC. Returns true if the
// expansion ends with a complement of the CC (a De Morgan OR).
bool RISCVTTLoweringImpl::expandCond(IRBuilder<> &B, Value *Node, bool Negate) {
  auto *CB = dyn_cast<CallInst>(Node);
  if (CB && getID(CB) == Intrinsic::riscv_tt_sfpxcmp) {
    expandCmp(B, CB, Negate);
    return false;
  }
  if (!CB || getID(CB) != Intrinsic::riscv_tt_sfpxlogic) {
    error(cast<Instruction>(B.GetInsertPoint()),
          "unsupported vector condition");
    return false;
  }

  unsigned Mod = cast<ConstantInt>(CB->getArgOperand(0))->getZExtValue();
  Value *LHS = CB->getArgOperand(1);
  Value *RHS = CB->getArgOperand(2);
  if (Mod == XLogicNot)
    return expandCond(B, LHS, !Negate);
  if (Mod == XLogicNearby)
    return expandCond(B, LHS, Negate);

  // AND is free; OR is NOT(AND(NOT, NOT)) using SFPCOMPC.
  bool IsAnd = (Mod == XLogicAnd) != Negate;
  bool SubNegate = IsAnd ? Negate : !Negate;
  expandCond(B, LHS, SubNegate);
  if (expandCond(B, RHS, SubNegate))
    error(CB, "vector condition with a negated right operand is not "
              "supported");
  if (IsAnd)
    return false;
  emit(B, Intrinsic::riscv_tt_sfpcompc, {});
  return true;
}

bool RISCVTTLoweringImpl::lowerPredication() {
  SmallVector<CallInst *, 8> Conds, Markers;
  for (Instruction &I : instructions(F)) {
    switch (getID(&I)) {
    case Intrinsic::riscv_tt_sfpxcond:
      Conds.push_back(cast<CallInst>(&I));
      [[fallthrough]];
    case Intrinsic::riscv_tt_sfpxpred:
    case Intrinsic::riscv_tt_sfpxcmp:
    case Intrinsic::riscv_tt_sfpxlogic:
      Markers.push_back(cast<CallInst>(&I));
      break;
    default:
      break;
    }
  }
  if (Markers.empty())
    return false;

  for (CallInst *Cond : Conds) {
    IRBuilder<> B(Cond);
    expandCond(B, Cond->getArgOperand(2), false);
  }
  for (CallInst *CB : Markers) {
    CB->replaceAllUsesWith(Constant::getNullValue(CB->getType()));
    CB->eraseFromParent();
  }
  return true;
}

// The CC stack is empty with all lanes enabled on function entry, so the
// outermost SFPPUSHC is unnecessary and the matching SFPPOPC can simply
// re-enable all lanes. An inner region without an else whose SFPPOPC is
// immediately followed by the outer SFPPOPC needs no push either.
bool RISCVTTLoweringImpl::optimizeCC() {
  if (!ST.hasTTTensixOptimizeCC())
    return false;

  // Only handle functions whose push/pop pairs are balanced per block.
  for (BasicBlock &BB : F) {
    int Depth = 0;
    for (Instruction &I : BB) {
      unsigned ID = getID(&I);
      if (ID == Intrinsic::riscv_tt_sfppushc) {
        if (getConstArg(cast<CallInst>(&I), 0) != 0)
          return false;
        ++Depth;
      } else if (ID == Intrinsic::riscv_tt_sfppopc) {
        if (--Depth < 0)
          return false;
      }
    }
    if (Depth != 0)
      return false;
  }

  bool Changed = false;
  for (BasicBlock &BB : F) {
    struct Region {
      CallInst *Push;
      bool SawCompC;
    };
    SmallVector<Region, 4> Stack;
    SmallVector<Instruction *, 4> ToErase;
    for (Instruction &I : make_early_inc_range(BB)) {
      unsigned ID = getID(&I);
      if (ID == Intrinsic::riscv_tt_sfpcompc && !Stack.empty()) {
        Stack.back().SawCompC = true;
      } else if (ID == Intrinsic::riscv_tt_sfppushc) {
        Stack.push_back({cast<CallInst>(&I), false});
      } else if (ID == Intrinsic::riscv_tt_sfppopc) {
        Region R = Stack.pop_back_val();
        if (Stack.empty()) {
          IRBuilder<> B(&I);
          emit(B, Intrinsic::riscv_tt_sfpencc,
               {B.getInt32(EnCCEIRI), B.getInt32(3)});
          ToErase.push_back(R.Push);
          ToErase.push_back(&I);
          continue;
        }
        if (R.SawCompC)
          continue;
        const Instruction *Next = I.getNextNode();
        while (Next && !isTTIntrinsic(*Next) && !isa<CallBase>(Next))
          Next = Next->getNextNode();
        if (Next && getID(Next) == Intrinsic::riscv_tt_sfppopc) {
          ToErase.push_back(R.Push);
          ToErase.push_back(&I);
        }
      }
    }
    for (Instruction *I : ToErase)
      I->eraseFromParent();
    Changed |= !ToErase.empty();
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
// Predicated assignments.
//===----------------------------------------------------------------------===//

// Value producing intrinsics whose first operand is the old destination value.
static bool hasPassThru(const Value *V) {
  const auto *II = dyn_cast<IntrinsicInst>(V);
  if (!II || II->arg_size() == 0 || !isTTIntrinsic(*II) ||
      II->getType() != II->getArgOperand(0)->getType() ||
      !II->getType()->isVectorTy())
    return false;
  switch (getID(II)) {
  case Intrinsic::riscv_tt_sfpassign_lv:
  case Intrinsic::riscv_tt_sfplut:
  case Intrinsic::riscv_tt_sfplutfp32_3r:
  case Intrinsic::riscv_tt_sfplutfp32_6r:
    return false;
  default:
    return true;
  }
}

// Follow the chain of predicated definitions to the unpredicated one.
static Value *getLiveRoot(Value *V) {
  while (true) {
    if (getID(V) == Intrinsic::riscv_tt_sfpassign_lv ||
        (hasPassThru(V) &&
         !isa<UndefValue>(cast<CallInst>(V)->getArgOperand(0)))) {
      V = cast<CallInst>(V)->getArgOperand(0);
      continue;
    }
    return V;
  }
}

// An old value is dead when all lanes are enabled, or when the CC has not
// changed since the unpredicated definition of the variable.
bool RISCVTTLoweringImpl::elideLiveAssigns() {
  // Forward dataflow: are all lanes known to be enabled?
  DenseMap<const BasicBlock *, bool> OutState;
  ReversePostOrderTraversal<Function *> RPOT(&F);
  auto Transfer = [](const Instruction &I, bool State,
                     SmallVectorImpl<bool> &Stack) {
    unsigned ID = getID(&I);
    if (ID == Intrinsic::riscv_tt_sfppushc) {
      Stack.push_back(State);
      return State;
    }
    if (ID == Intrinsic::riscv_tt_sfppopc)
      return Stack.empty() ? false : Stack.pop_back_val();
    if (ID == Intrinsic::riscv_tt_sfpencc) {
      const auto *CB = cast<CallInst>(&I);
      return getConstArg(CB, 0) == EnCCEIRI && getConstArg(CB, 1) == 3;
    }
    // Callees, like this function, expect and return with all lanes enabled.
    if (isa<CallBase>(I) && ID == Intrinsic::not_intrinsic)
      return State;
    return writesCC(I) ? false : State;
  };
  auto InState = [&](const BasicBlock *BB) {
    if (BB->isEntryBlock())
      return true;
    for (const BasicBlock *Pred : predecessors(BB)) {
      auto It = OutState.find(Pred);
      if (It != OutState.end() && !It->second)
        return false;
    }
    return true;
  };
  for (bool Changed = true; Changed;) {
    Changed = false;
    for (BasicBlock *BB : RPOT) {
      bool State = InState(BB);
      SmallVector<bool, 4> Stack;
      for (Instruction &I : *BB)
        State = Transfer(I, State, Stack);
      auto [It, Inserted] = OutState.try_emplace(BB, State);
      if (!Inserted && It->second != State) {
        It->second = State;
        Changed = true;
      }
    }
  }

  bool Changed = false;
  for (BasicBlock *BB : RPOT) {
    bool State = InState(BB);
    SmallVector<bool, 4> Stack;
    for (Instruction &I : make_early_inc_range(*BB)) {
      bool AllEnabled = State;
      State = Transfer(I, State, Stack);
      if (getID(&I) != Intrinsic::riscv_tt_sfpassign_lv)
        continue;
      auto *CB = cast<CallInst>(&I);
      Value *Old = CB->getArgOperand(0);
      Value *New = CB->getArgOperand(1);
      auto *Root = dyn_cast<Instruction>(getLiveRoot(Old));
      if (AllEnabled || isa<UndefValue>(Old) || Old == New ||
          (Root && noCCChangeBetween(Root, CB))) {
        CB->replaceAllUsesWith(New);
        CB->eraseFromParent();
        Changed = true;
      }
    }
  }
  return Changed;
}

// x = op(...); y = assign_lv(old, x)  ==>  y = op_lv(old, ...).
bool RISCVTTLoweringImpl::foldLiveAssigns() {
  bool Changed = false;
  for (Instruction &I : make_early_inc_range(instructions(F))) {
    if (getID(&I) != Intrinsic::riscv_tt_sfpassign_lv)
      continue;
    auto *CB = cast<CallInst>(&I);
    Value *Old = CB->getArgOperand(0);
    auto *Def = dyn_cast<CallInst>(CB->getArgOperand(1));
    if (!Def || !Def->hasOneUse() || !hasPassThru(Def) ||
        getID(Def) == Intrinsic::riscv_tt_sfpreadlreg ||
        !noCCChangeBetween(Def, CB))
      continue;
    if (auto *OldI = dyn_cast<Instruction>(Old))
      if (OldI->getParent() == Def->getParent() && !OldI->comesBefore(Def))
        continue;

    // The upper half of a 32-bit constant load predicates the pair.
    CallInst *Target = Def;
    if (getID(Def) == Intrinsic::riscv_tt_sfploadi &&
        !isa<UndefValue>(Def->getArgOperand(0))) {
      auto *Lo = dyn_cast<CallInst>(Def->getArgOperand(0));
      if (!Lo || !Lo->hasOneUse() ||
          getID(Lo) != Intrinsic::riscv_tt_sfploadi ||
          !isa<UndefValue>(Lo->getArgOperand(0)))
        continue;
      Target = Lo;
    } else if (!isa<UndefValue>(Def->getArgOperand(0))) {
      continue;
    }
    Target->setArgOperand(0, Old);
    CB->replaceAllUsesWith(Def);
    CB->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
// Run-time immediates.
//===----------------------------------------------------------------------===//

// Instructions whose immediate is not a compile-time constant are built at run
// time and pushed through the instruction buffer pointer. The register fields
// are filled in after register allocation (see RISCVTTPostRA).
bool RISCVTTLoweringImpl::lowerDynamicImms() {
  constexpr unsigned NoField = 0xff;
  bool Changed = false;
  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CB = dyn_cast<CallInst>(&I);
    if (!CB || !isTTIntrinsic(*CB))
      continue;
    unsigned ID = getID(CB);
    IRBuilder<> B(CB);
    auto Arg = [&](unsigned Idx) { return CB->getArgOperand(Idx); };
    auto ConstArg = [&](unsigned Idx) -> uint32_t {
      if (std::optional<uint64_t> V = getConstArg(CB, Idx))
        return *V;
      error(CB, "Tensix builtin argument must be a constant");
      return 0;
    };
    auto Shifted = [&](Value *Imm, uint32_t Mask, unsigned Shift) {
      return B.CreateShl(B.CreateAnd(Imm, Mask), Shift);
    };
    auto Synth = [&](Value *Old, Value *Src, Value *Val, uint32_t Opc,
                     unsigned DstShift, unsigned SrcShift) {
      Value *V = emit(B, Intrinsic::riscv_tt_synth,
                      {Old, Src ? Src : poison(), Val, Arg(1), B.getInt32(Opc),
                       B.getInt32(DstShift | SrcShift << 8)});
      CB->replaceAllUsesWith(V);
    };
    auto SynthVoid = [&](Value *Src, Value *Val, Value *IPtr, uint32_t Opc,
                         unsigned SrcShift) {
      emit(B, Intrinsic::riscv_tt_synth_void,
           {Src ? Src : poison(), Val, IPtr, B.getInt32(Opc),
            B.getInt32(NoField | SrcShift << 8)});
    };
    // Two-address forms overwrite their first source.
    auto TiedSrc = [&](Value *Old, Value *Src) -> Value * {
      if (isa<UndefValue>(Old))
        return Src;
      return emit(B, Intrinsic::riscv_tt_sfpassign_lv, {Old, Src});
    };
    uint32_t AModeShift = IsBH ? 13 : 14;
    uint32_t AddrMask = IsBH ? 0x1fff : 0x3fff;

    switch (ID) {
    default:
      continue;
    case Intrinsic::riscv_tt_sfploadi:
      if (isa<ConstantInt>(Arg(2)))
        continue;
      Synth(Arg(0), nullptr, B.CreateAnd(Arg(2), 0xffff),
            0x71u << 24 | ConstArg(3) << 16, 20, NoField);
      break;
    case Intrinsic::riscv_tt_sfpload:
      if (isa<ConstantInt>(Arg(2)))
        continue;
      Synth(Arg(0), nullptr, B.CreateAnd(Arg(2), AddrMask),
            0x70u << 24 | ConstArg(3) << 16 | ConstArg(4) << AModeShift, 20,
            NoField);
      break;
    case Intrinsic::riscv_tt_sfpstore:
      if (isa<ConstantInt>(Arg(2)))
        continue;
      SynthVoid(Arg(1), B.CreateAnd(Arg(2), AddrMask), Arg(0),
                0x72u << 24 | ConstArg(3) << 16 | ConstArg(4) << AModeShift,
                20);
      break;
    case Intrinsic::riscv_tt_sfpmuli:
    case Intrinsic::riscv_tt_sfpaddi:
      if (isa<ConstantInt>(Arg(3)))
        continue;
      Synth(TiedSrc(Arg(0), Arg(2)), nullptr, Shifted(Arg(3), 0xffff, 8),
            (ID == Intrinsic::riscv_tt_sfpmuli ? 0x74u : 0x75u) << 24 |
                ConstArg(4),
            4, NoField);
      break;
    case Intrinsic::riscv_tt_sfpsetexp_i:
    case Intrinsic::riscv_tt_sfpsetsgn_i:
    case Intrinsic::riscv_tt_sfpdivp2:
    case Intrinsic::riscv_tt_sfpshft_i: {
      if (isa<ConstantInt>(Arg(3)))
        continue;
      uint32_t Opc, Mask = 0xfff, Mod = ConstArg(4);
      if (ID == Intrinsic::riscv_tt_sfpsetexp_i) {
        Opc = 0x82;
        Mask = 0xff;
        Mod |= 1;
      } else if (ID == Intrinsic::riscv_tt_sfpsetsgn_i) {
        Opc = 0x89;
        Mask = 1;
        Mod |= 1;
      } else if (ID == Intrinsic::riscv_tt_sfpdivp2) {
        Opc = 0x76;
      } else {
        Opc = 0x7a;
        Mod |= IsBH ? 5 : 1;
      }
      Value *Val = Shifted(Arg(3), Mask, 12);
      if (ID == Intrinsic::riscv_tt_sfpshft_i && !IsBH)
        Synth(TiedSrc(Arg(0), Arg(2)), nullptr, Val, Opc << 24 | Mod, 4,
              NoField);
      else
        Synth(Arg(0), Arg(2), Val, Opc << 24 | Mod, 4, 8);
      break;
    }
    case Intrinsic::riscv_tt_sfpstochrnd_descale_i:
      if (isa<ConstantInt>(Arg(3)))
        continue;
      Synth(Arg(0), Arg(2), Shifted(Arg(3), 0x1f, 16),
            0x8eu << 24 | ConstArg(5) << 21 | CRegZero << 12 |
                (ConstArg(4) | 8),
            4, 8);
      break;
    case Intrinsic::riscv_tt_sfpiadd_i:
    case Intrinsic::riscv_tt_sfpsetman_i: {
      // Without a usable immediate field, load the value into a register.
      std::optional<uint64_t> Imm = getConstArg(CB, 3);
      bool IsIAdd = ID == Intrinsic::riscv_tt_sfpiadd_i;
      if (Imm && (IsIAdd ? isInt<12>(int32_t(*Imm)) : isUInt<12>(*Imm)))
        continue;
      Value *T = emitLoadImm(B, poison(), Arg(1), Arg(3), IsIAdd ? 31 : -32);
      Value *V = IsIAdd
                     ? emit(B, Intrinsic::riscv_tt_sfpiadd_v,
                            {Arg(0), T, Arg(2), B.getInt32(ConstArg(4) & ~1u)})
                     : emit(B, Intrinsic::riscv_tt_sfpsetman_v,
                            {Arg(0), Arg(2), T, B.getInt32(ConstArg(4) & ~1u)});
      CB->replaceAllUsesWith(V);
      break;
    }
    case Intrinsic::riscv_tt_sfpwriteconfig_i:
      if (isa<ConstantInt>(Arg(1)))
        continue;
      SynthVoid(nullptr, Shifted(Arg(1), 0xffff, 8), Arg(0),
                0x91u << 24 | ConstArg(3) << 4 | (ConstArg(2) | 1), NoField);
      break;
    case Intrinsic::riscv_tt_ttreplay:
      if (isa<ConstantInt>(Arg(1)))
        continue;
      SynthVoid(nullptr, Shifted(Arg(1), 0x3f, 4), Arg(0),
                0x04u << 24 | ConstArg(2) << 14 | ConstArg(3) << 1 |
                    ConstArg(4),
                NoField);
      break;
    case Intrinsic::riscv_tt_ttinsn:
      if (isa<ConstantInt>(Arg(1)))
        continue;
      SynthVoid(nullptr, Arg(1), Arg(0), 0, NoField);
      break;
    }
    CB->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
// Combines.
//===----------------------------------------------------------------------===//

static bool isUnpredicated(const CallInst *CB) {
  return isa<UndefValue>(CB->getArgOperand(0));
}

static bool isCReg(Value *V, unsigned Idx) {
  return getID(V) == Intrinsic::riscv_tt_sfpreadlreg &&
         getConstArg(cast<CallInst>(V), 0) == Idx;
}

// A single-use unpredicated negation (SFPMOV with mod 1), if V is one.
static Value *getNegated(Value *V) {
  auto *CB = dyn_cast<CallInst>(V);
  if (CB && getID(CB) == Intrinsic::riscv_tt_sfpmov && CB->hasOneUse() &&
      isUnpredicated(CB) && getConstArg(CB, 2) == 1)
    return CB->getArgOperand(1);
  return nullptr;
}

// The 16-bit immediate of a bfloat16 constant load.
static std::optional<uint64_t> getFP16bConst(Value *V) {
  auto *CB = dyn_cast<CallInst>(V);
  if (!CB || getID(CB) != Intrinsic::riscv_tt_sfploadi ||
      !isUnpredicated(CB) || getConstArg(CB, 3) != LoadIFloatB)
    return std::nullopt;
  return getConstArg(CB, 2);
}

bool RISCVTTLoweringImpl::combine() {
  bool Changed = false;
  auto Replace = [&](CallInst *CB, Value *V) {
    CB->replaceAllUsesWith(V);
    CB->eraseFromParent();
    Changed = true;
  };
  auto EraseIfDead = [](Value *V) {
    if (auto *I = dyn_cast<Instruction>(V))
      if (isInstructionTriviallyDead(I))
        I->eraseFromParent();
  };

  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CB = dyn_cast<CallInst>(&I);
    if (!CB)
      continue;
    unsigned ID = getID(CB);
    IRBuilder<> B(CB);

    switch (ID) {
    case Intrinsic::riscv_tt_sfpmul:
    case Intrinsic::riscv_tt_sfpadd: {
      std::optional<uint64_t> Mod = getConstArg(CB, 3);
      if (!Mod)
        break;
      // a * 1.0 => a
      if (ID == Intrinsic::riscv_tt_sfpmul && isUnpredicated(CB) && !*Mod) {
        if (isCReg(CB->getArgOperand(2), CRegOne)) {
          Replace(CB, CB->getArgOperand(1));
          continue;
        }
        if (isCReg(CB->getArgOperand(1), CRegOne)) {
          Replace(CB, CB->getArgOperand(2));
          continue;
        }
      }
      if (!IsBH || (*Mod & 1))
        break;
      // Blackhole: fold negation of an operand into the mod.
      if (Value *A = getNegated(CB->getArgOperand(1))) {
        Value *Neg = CB->getArgOperand(1);
        CB->setArgOperand(1, A);
        CB->setArgOperand(3, B.getInt32(*Mod | 1));
        EraseIfDead(Neg);
        Changed = true;
      } else if (Value *A = getNegated(CB->getArgOperand(2))) {
        Value *Neg = CB->getArgOperand(2);
        if (ID == Intrinsic::riscv_tt_sfpmul) {
          CB->setArgOperand(2, CB->getArgOperand(1));
          CB->setArgOperand(1, A);
          CB->setArgOperand(3, B.getInt32(*Mod | 1));
        } else if (!(*Mod & 2)) {
          CB->setArgOperand(2, A);
          CB->setArgOperand(3, B.getInt32(*Mod | 2));
        } else {
          break;
        }
        EraseIfDead(Neg);
        Changed = true;
      }
      break;
    }
    case Intrinsic::riscv_tt_sfpmov: {
      // Blackhole: -(a * b), -(a + b) and -(a * b + c) fold into the mod.
      auto *Src = dyn_cast<CallInst>(CB->getArgOperand(1));
      unsigned SrcID = getID(Src);
      if (!IsBH || !isUnpredicated(CB) || getConstArg(CB, 2) != 1 || !Src ||
          !Src->hasOneUse() || !isUnpredicated(Src) ||
          (SrcID != Intrinsic::riscv_tt_sfpmul &&
           SrcID != Intrinsic::riscv_tt_sfpadd &&
           SrcID != Intrinsic::riscv_tt_sfpmad) ||
          !noCCChangeBetween(Src, CB))
        break;
      unsigned ModIdx = Src->arg_size() - 1;
      if (std::optional<uint64_t> Mod = getConstArg(Src, ModIdx)) {
        unsigned Flip = SrcID == Intrinsic::riscv_tt_sfpmul ? 1 : 3;
        Src->setArgOperand(ModIdx, B.getInt32(*Mod ^ Flip));
        Replace(CB, Src);
        continue;
      }
      break;
    }
    case Intrinsic::riscv_tt_sfpiadd_v: {
      // Integer addition of a small constant uses the immediate form.
      std::optional<uint64_t> Mod = getConstArg(CB, 3);
      if (!Mod)
        break;
      Value *A = CB->getArgOperand(1), *Bv = CB->getArgOperand(2);
      bool Sub = *Mod & IAdd2sComp;
      std::optional<int64_t> CA = getIntConst(A), CBv = getIntConst(Bv);
      Value *X = nullptr;
      int64_t Imm = 0;
      if (CA && isInt<12>(Sub ? -*CA : *CA)) {
        X = Bv;
        Imm = Sub ? -*CA : *CA;
      } else if (!Sub && CBv && isInt<12>(*CBv)) {
        X = A;
        Imm = *CBv;
      }
      if (!X)
        break;
      Value *V = emit(B, Intrinsic::riscv_tt_sfpiadd_i,
                      {CB->getArgOperand(0), nullIPtr(), X,
                       B.getInt32(uint32_t(Imm)), B.getInt32(*Mod & 0xc)});
      Replace(CB, V);
      EraseIfDead(A);
      EraseIfDead(Bv);
      continue;
    }
    default:
      break;
    }
  }

  // a * b + c => SFPMAD, then a * imm and a + imm => SFPMULI and SFPADDI for
  // what remains.
  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CB = dyn_cast<CallInst>(&I);
    if (!CB || getID(CB) != Intrinsic::riscv_tt_sfpadd)
      continue;
    std::optional<uint64_t> AddMod = getConstArg(CB, 3);
    for (unsigned Idx : {1, 2}) {
      auto *Mul = dyn_cast<CallInst>(CB->getArgOperand(Idx));
      if (!AddMod || !Mul || getID(Mul) != Intrinsic::riscv_tt_sfpmul ||
          !Mul->hasOneUse() || !isUnpredicated(Mul) ||
          !noCCChangeBetween(Mul, CB))
        continue;
      std::optional<uint64_t> MulMod = getConstArg(Mul, 3);
      if (!MulMod)
        continue;
      // Like GCC, prefer SFPADDI to holding a constant addend in an LREG,
      // unless the product has a constant operand too.
      if (getFP16bConst(CB->getArgOperand(3 - Idx)) &&
          !getFP16bConst(Mul->getArgOperand(1)) &&
          !getFP16bConst(Mul->getArgOperand(2)))
        continue;
      // Mod bit 0 negates the product, bit 1 the addend.
      unsigned NegMulOperand = Idx == 1 ? *AddMod & 1 : (*AddMod >> 1) & 1;
      unsigned NegAddend = Idx == 1 ? (*AddMod >> 1) & 1 : *AddMod & 1;
      IRBuilder<> B(CB);
      Value *Mad =
          emit(B, Intrinsic::riscv_tt_sfpmad,
               {CB->getArgOperand(0), Mul->getArgOperand(1),
                Mul->getArgOperand(2), CB->getArgOperand(3 - Idx),
                B.getInt32(((*MulMod & 1) ^ NegMulOperand) | NegAddend << 1)});
      Replace(CB, Mad);
      Mul->eraseFromParent();
      break;
    }
  }
  // Blackhole: fold a negation with several uses into the mods of all of them.
  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *Neg = dyn_cast<CallInst>(&I);
    if (!IsBH || getID(Neg) != Intrinsic::riscv_tt_sfpmov ||
        getConstArg(Neg, 2) != 1 || !isUnpredicated(Neg) || Neg->use_empty())
      continue;
    // The mod bit that negates operand Idx of an add, multiply or MAD.
    auto NegBit = [](const CallInst *U, unsigned Idx) -> unsigned {
      switch (getID(U)) {
      case Intrinsic::riscv_tt_sfpmul:
        return Idx == 1 || Idx == 2 ? 1 : 0;
      case Intrinsic::riscv_tt_sfpadd:
        return Idx == 1 ? 1 : Idx == 2 ? 2 : 0;
      case Intrinsic::riscv_tt_sfpmad:
        return Idx == 1 || Idx == 2 ? 1 : Idx == 3 ? 2 : 0;
      default:
        return 0;
      }
    };
    bool AllFold = all_of(Neg->uses(), [&](const Use &U) {
      auto *UI = dyn_cast<CallInst>(U.getUser());
      return UI && NegBit(UI, U.getOperandNo()) &&
             getConstArg(UI, UI->arg_size() - 1);
    });
    if (!AllFold)
      continue;
    Value *X = Neg->getArgOperand(1);
    for (Use &U : make_early_inc_range(Neg->uses())) {
      auto *UI = cast<CallInst>(U.getUser());
      unsigned ModIdx = UI->arg_size() - 1;
      unsigned Bit = NegBit(UI, U.getOperandNo());
      UI->setArgOperand(ModIdx, ConstantInt::get(UI->getArgOperand(ModIdx)
                                                     ->getType(),
                                                 *getConstArg(UI, ModIdx) ^ Bit));
      U.set(X);
    }
    Neg->eraseFromParent();
    Changed = true;
  }

  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CB = dyn_cast<CallInst>(&I);
    unsigned ID = getID(CB);
    if (!CB ||
        (ID != Intrinsic::riscv_tt_sfpmul && ID != Intrinsic::riscv_tt_sfpadd))
      continue;
    std::optional<uint64_t> Mod = getConstArg(CB, 3);
    if (!Mod)
      continue;
    bool IsMul = ID == Intrinsic::riscv_tt_sfpmul;
    for (unsigned Idx : {2, 1}) {
      std::optional<uint64_t> Imm = getFP16bConst(CB->getArgOperand(Idx));
      // The immediate forms have no negation of the vector operand.
      unsigned NegImm = IsMul ? *Mod & 1 : (*Mod >> (Idx - 1)) & 1;
      unsigned NegVec = IsMul ? 0 : (*Mod >> (2 - Idx)) & 1;
      if (!Imm || NegVec)
        continue;
      Value *Load = CB->getArgOperand(Idx);
      IRBuilder<> B(CB);
      Value *V = emit(
          B, IsMul ? Intrinsic::riscv_tt_sfpmuli : Intrinsic::riscv_tt_sfpaddi,
          {CB->getArgOperand(0), nullIPtr(), CB->getArgOperand(3 - Idx),
           B.getInt32((*Imm ^ (NegImm << 15)) & 0xffff), B.getInt32(0)});
      Replace(CB, V);
      EraseIfDead(Load);
      break;
    }
  }
  return Changed;
}

bool RISCVTTLoweringImpl::run() {
  if (!ST.hasVendorXTTTensix())
    return false;
  bool Changed = lowerXLoadI();
  Changed |= lowerPredication();
  Changed |= optimizeCC();
  Changed |= elideLiveAssigns();
  if (ST.hasTTTensixOptimizeCombine())
    Changed |= combine();
  Changed |= foldLiveAssigns();
  Changed |= lowerDynamicImms();
  for (bool Erased = true; Erased;) {
    Erased = false;
    for (Instruction &I : make_early_inc_range(instructions(F)))
      if (isTTIntrinsic(I) && isInstructionTriviallyDead(&I)) {
        I.eraseFromParent();
        Erased = Changed = true;
      }
  }
  return Changed;
}

namespace {
class RISCVTTLoweringLegacy : public FunctionPass {
public:
  static char ID;

  RISCVTTLoweringLegacy() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override { return PASS_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<TargetPassConfig>();
  }
};
} // namespace

bool RISCVTTLoweringLegacy::runOnFunction(Function &F) {
  auto &TM = getAnalysis<TargetPassConfig>().getTM<RISCVTargetMachine>();
  return RISCVTTLoweringImpl(F, TM.getSubtarget<RISCVSubtarget>(F)).run();
}

INITIALIZE_PASS_BEGIN(RISCVTTLoweringLegacy, DEBUG_TYPE, PASS_NAME, false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(RISCVTTLoweringLegacy, DEBUG_TYPE, PASS_NAME, false, false)

char RISCVTTLoweringLegacy::ID = 0;

FunctionPass *llvm::createRISCVTTLoweringLegacyPass() {
  return new RISCVTTLoweringLegacy();
}

PreservedAnalyses RISCVTTLoweringPass::run(Function &F,
                                           FunctionAnalysisManager &FAM) {
  bool Changed =
      RISCVTTLoweringImpl(F, TM->getSubtarget<RISCVSubtarget>(F)).run();
  if (!Changed)
    return PreservedAnalyses::all();
  return PreservedAnalyses::allInSet<CFGAnalyses>();
}
