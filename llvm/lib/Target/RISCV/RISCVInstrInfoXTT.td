//===-- RISCVInstrInfoXTT.td - Tenstorrent Tensix instructions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the Tenstorrent Tensix coprocessor instructions that may
// appear in the instruction stream of a Tensix baby core (XTTTensixWH and
// XTTTensixBH). See llvm/docs/RISCV/TenstorrentTensix.rst for an overview.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Operand and SDNode definitions.
//===----------------------------------------------------------------------===//

def tt_uimm10 : RISCVUImmOp<10>;
def tt_uimm12 : RISCVUImmOp<12>;
def tt_simm4 : RISCVSImmOp<4>;
def tt_simm13 : RISCVSImmOp<13>;
def tt_simm14 : RISCVSImmOp<14>;

// SFPLOADI accepts both signed and unsigned 16-bit immediates.
def TTImm16AsmOperand : AsmOperandClass {
  let Name = "TTImm16";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidTTImm16";
}
def tt_imm16 : RISCVOp {
  let ParserMatchClass = TTImm16AsmOperand;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmOperand<16>";
  let OperandType = "OPERAND_UIMM16";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isInt<16>(Imm) || isUInt<16>(Imm);
  }];
}

// A full unswizzled Tensix instruction word, printed as a signed value.
def tt_insnword : Operand<XLenVT> {
  let EncoderMethod = "getImmOpValue";
}

//===----------------------------------------------------------------------===//
// Instruction formats.
//===----------------------------------------------------------------------===//

// Tensix instructions are 32-bit words {opcode[31:24], operands[23:0]} that
// are rotated left by two bits when placed in the RISC-V instruction stream.
class TTInst<bits<8> opc, dag outs, dag ins, string opcodestr, string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatOther> {
  bits<32> Enc;
  let Enc{31-24} = opc;
  let Inst{31-2} = Enc{29-0};
  let Inst{1-0} = Enc{31-30};
  let hasSideEffects = 1;
  let mayLoad = 0;
  let mayStore = 0;
  let IsTensix = 1;
}

// Every SFPU instruction that writes an LREG only writes the lanes enabled by
// the condition code, so the old destination value is always an input.
class TTSFPInst<bits<8> opc, dag outs, dag ins, string opcodestr,
                string argstr>
    : TTInst<opc, outs, ins, opcodestr, argstr> {
  let Constraints = "$ld = $ld_in";
}

// ld = op(ls) under mod.
class TTSFPUnary<bits<8> opc, string opcodestr>
    : TTSFPInst<opc, (outs LRD:$ld), (ins LRD:$ld_in, LRS:$ls, uimm4:$mod),
                opcodestr, "$ld, $ls, $mod"> {
  bits<4> ld;
  bits<4> ls;
  bits<4> mod;
  let Enc{23-12} = 0;
  let Enc{11-8} = ls;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
}

// ld = op(ld, ls).
class TTSFPBinary<bits<8> opc, string opcodestr>
    : TTSFPInst<opc, (outs LRD:$ld), (ins LRD:$ld_in, LRS:$ls),
                opcodestr, "$ld, $ls"> {
  bits<4> ld;
  bits<4> ls;
  let Enc{23-12} = 0;
  let Enc{11-8} = ls;
  let Enc{7-4} = ld;
  let Enc{3-0} = 0;
}

// ld = op(ld, ls, imm12) under mod.
class TTSFPImm12<bits<8> opc, string opcodestr, Operand immop>
    : TTSFPInst<opc, (outs LRD:$ld),
                (ins LRD:$ld_in, LRS:$ls, immop:$imm, uimm4:$mod),
                opcodestr, "$ld, $ls, $imm, $mod"> {
  bits<4> ld;
  bits<4> ls;
  bits<12> imm;
  bits<4> mod;
  let Enc{23-12} = imm;
  let Enc{11-8} = ls;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
}

// ld = op(ld, imm16) under mod.
class TTSFPImm16<bits<8> opc, string opcodestr>
    : TTSFPInst<opc, (outs LRD:$ld), (ins LRD:$ld_in, uimm16:$imm, uimm4:$mod),
                opcodestr, "$ld, $imm, $mod"> {
  bits<4> ld;
  bits<16> imm;
  bits<4> mod;
  let Enc{23-8} = imm;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
}

// ld = la * lb + lc under mod; MUL and ADD hardwire one source.
class TTSFPMad<bits<8> opc, string opcodestr, dag ins, string argstr>
    : TTSFPInst<opc, (outs LRD:$ld), ins, opcodestr, argstr> {
  bits<4> ld;
  bits<4> la;
  bits<4> lb;
  bits<4> lc;
  bits<4> mod;
  let Enc{23-20} = 0;
  let Enc{19-16} = la;
  let Enc{15-12} = lb;
  let Enc{11-8} = lc;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
}

class TTSFPNoOperands<bits<8> opc, string opcodestr>
    : TTInst<opc, (outs), (ins), opcodestr, ""> {
  let Enc{23-0} = 0;
}

class TTSFPModOnly<bits<8> opc, string opcodestr>
    : TTInst<opc, (outs), (ins uimm4:$mod), opcodestr, "$mod"> {
  bits<4> mod;
  let Enc{23-4} = 0;
  let Enc{3-0} = mod;
}

//===----------------------------------------------------------------------===//
// SFPU instructions common to Wormhole and Blackhole.
//===----------------------------------------------------------------------===//

let Predicates = [HasVendorXTTTensix], DecoderNamespace = "XTT" in {
def TT_SFPLOADI
    : TTSFPInst<0x71, (outs LRD:$ld), (ins LRD:$ld_in, tt_imm16:$imm, uimm4:$mod),
                "sfploadi", "$ld, $imm, $mod"> {
  bits<4> ld;
  bits<16> imm;
  bits<4> mod;
  let Enc{23-20} = ld;
  let Enc{19-16} = mod;
  let Enc{15-0} = imm;
}

def TT_SFPLUT
    : TTSFPInst<0x73, (outs LRD:$ld), (ins LRD:$ld_in, uimm4:$mod),
                "sfplut", "$ld, $mod"> {
  bits<4> ld;
  bits<4> mod;
  let Enc{23-20} = ld;
  let Enc{19-16} = mod;
  let Enc{15-0} = 0;
  let Uses = [L0, L1, L2];
}

def TT_SFPMULI : TTSFPImm16<0x74, "sfpmuli">;
def TT_SFPADDI : TTSFPImm16<0x75, "sfpaddi">;
def TT_SFPDIVP2 : TTSFPImm12<0x76, "sfpdivp2", simm12>;
def TT_SFPEXEXP : TTSFPUnary<0x77, "sfpexexp">;
def TT_SFPEXMAN : TTSFPUnary<0x78, "sfpexman">;
def TT_SFPIADD : TTSFPImm12<0x79, "sfpiadd", simm12>;
def TT_SFPSHFT : TTSFPImm12<0x7a, "sfpshft", simm12>;

def TT_SFPSETCC
    : TTInst<0x7b, (outs), (ins LRS:$ls, tt_uimm12:$imm, uimm4:$mod),
             "sfpsetcc", "$ls, $imm, $mod"> {
  bits<4> ls;
  bits<12> imm;
  bits<4> mod;
  let Enc{23-12} = imm;
  let Enc{11-8} = ls;
  let Enc{7-4} = 0;
  let Enc{3-0} = mod;
}

def TT_SFPMOV : TTSFPUnary<0x7c, "sfpmov">;
def TT_SFPABS : TTSFPUnary<0x7d, "sfpabs">;
def TT_SFPNOT : TTSFPBinary<0x80, "sfpnot">;
def TT_SFPLZ : TTSFPUnary<0x81, "sfplz">;
def TT_SFPSETEXP : TTSFPImm12<0x82, "sfpsetexp", tt_uimm12>;
def TT_SFPSETMAN : TTSFPImm12<0x83, "sfpsetman", tt_uimm12>;

def TT_SFPMAD
    : TTSFPMad<0x84, "sfpmad",
               (ins LRD:$ld_in, LRS:$la, LRS:$lb, LRS:$lc, uimm4:$mod),
               "$ld, $la, $lb, $lc, $mod">;
let la = 10 in
def TT_SFPADD
    : TTSFPMad<0x85, "sfpadd", (ins LRD:$ld_in, LRS:$lb, LRS:$lc, uimm4:$mod),
               "$ld, $lb, $lc, $mod">;
let lc = 9 in
def TT_SFPMUL
    : TTSFPMad<0x86, "sfpmul", (ins LRD:$ld_in, LRS:$la, LRS:$lb, uimm4:$mod),
               "$ld, $la, $lb, $mod">;

def TT_SFPPUSHC : TTSFPModOnly<0x87, "sfppushc">;
def TT_SFPPOPC : TTSFPModOnly<0x88, "sfppopc">;
def TT_SFPSETSGN : TTSFPImm12<0x89, "sfpsetsgn", tt_uimm12>;

def TT_SFPENCC
    : TTInst<0x8a, (outs), (ins tt_uimm12:$imm, uimm4:$mod),
             "sfpencc", "$imm, $mod"> {
  bits<12> imm;
  bits<4> mod;
  let Enc{23-12} = imm;
  let Enc{11-4} = 0;
  let Enc{3-0} = mod;
}

def TT_SFPCOMPC : TTSFPNoOperands<0x8b, "sfpcompc">;
let Defs = [L0, L1, L2, L3, L4, L5, L6, L7],
    Uses = [L0, L1, L2, L3, L4, L5, L6, L7] in
def TT_SFPTRANSP : TTSFPNoOperands<0x8c, "sfptransp">;
def TT_SFPXOR : TTSFPBinary<0x8d, "sfpxor">;
def TT_SFPNOP : TTSFPNoOperands<0x8f, "sfpnop">;

def TT_SFPCONFIG
    : TTInst<0x91, (outs), (ins uimm4:$dest, uimm16:$imm, uimm4:$mod),
             "sfpconfig", "$dest, $imm, $mod"> {
  bits<4> dest;
  bits<16> imm;
  bits<4> mod;
  let Enc{23-8} = imm;
  let Enc{7-4} = dest;
  let Enc{3-0} = mod;
}

def TT_SFPSWAP
    : TTInst<0x92, (outs LRD:$ld, LRD:$ls),
             (ins LRD:$ld_in, LRD:$ls_in, uimm4:$mod),
             "sfpswap", "$ld, $ls, $mod"> {
  bits<4> ld;
  bits<4> ls;
  bits<4> mod;
  let Enc{23-12} = 0;
  let Enc{11-8} = ls;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
  let Constraints = "$ld = $ld_in,$ls = $ls_in";
}

// SFPSWAP with one operand in a read-only constant register, whose result is
// discarded by the hardware.
let isCodeGenOnly = 1 in {
def TT_SFPSWAP_CD
    : TTInst<0x92, (outs LRD:$ls), (ins LRS:$ld, LRD:$ls_in, uimm4:$mod),
             "sfpswap", "$ld, $ls, $mod"> {
  bits<4> ld;
  bits<4> ls;
  bits<4> mod;
  let Enc{23-12} = 0;
  let Enc{11-8} = ls;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
  let Constraints = "$ls = $ls_in";
}
def TT_SFPSWAP_CS
    : TTInst<0x92, (outs LRD:$ld), (ins LRD:$ld_in, LRS:$ls, uimm4:$mod),
             "sfpswap", "$ld, $ls, $mod"> {
  bits<4> ld;
  bits<4> ls;
  bits<4> mod;
  let Enc{23-12} = 0;
  let Enc{11-8} = ls;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
  let Constraints = "$ld = $ld_in";
}
}

def TT_SFPSHFT2 : TTSFPImm12<0x94, "sfpshft2", simm12>;

def TT_SFPLUTFP32
    : TTSFPInst<0x95, (outs LRD:$ld), (ins LRD:$ld_in, uimm4:$mod),
                "sfplutfp32", "$ld, $mod"> {
  bits<4> ld;
  bits<4> mod;
  let Enc{23-8} = 0;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
  let Uses = [L0, L1, L2, L4, L5, L6];
}

def TT_TTNOP : TTSFPNoOperands<0x02, "ttnop">;

def TT_TTREPLAY
    : TTInst<0x04, (outs), (ins tt_uimm10:$start, tt_uimm10:$len, uimm3:$exec,
                                uimm1:$load),
             "ttreplay", "$start, $len, $exec, $load"> {
  bits<10> start;
  bits<10> len;
  bits<3> exec;
  bits<1> load;
  let Enc{23-14} = start;
  let Enc{13-4} = len;
  let Enc{3-1} = exec;
  let Enc{0} = load;
}

def TT_TTINCRWC
    : TTInst<0x38, (outs), (ins simm6:$cr, tt_simm4:$d, tt_simm4:$b,
                                tt_simm4:$a),
             "ttincrwc", "$cr, $d, $b, $a"> {
  bits<6> cr;
  bits<4> d;
  bits<4> b;
  bits<4> a;
  let Enc{23-18} = cr;
  let Enc{17-14} = d;
  let Enc{13-10} = b;
  let Enc{9-6} = a;
  let Enc{5-0} = 0;
}

// A raw Tensix instruction, emitted by the .ttinsn directive.
let isCodeGenOnly = 1 in
def TT_TTINSN
    : TTInst<0, (outs), (ins tt_insnword:$word), ".ttinsn", "$word"> {
  bits<32> word;
  let Enc = word;
  let mayLoad = 1;
  let mayStore = 1;
}
} // Predicates = [HasVendorXTTTensix], DecoderNamespace = "XTT"

//===----------------------------------------------------------------------===//
// Wormhole specific SFPU instructions.
//===----------------------------------------------------------------------===//

let Predicates = [HasVendorXTTTensixWH], DecoderNamespace = "XTTWH" in {
def TT_SFPLOAD_WH
    : TTSFPInst<0x70, (outs LRD:$ld),
                (ins LRD:$ld_in, tt_simm14:$off, uimm4:$mod, uimm2:$amode),
                "sfpload", "$ld, $off, $mod, $amode"> {
  bits<4> ld;
  bits<14> off;
  bits<4> mod;
  bits<2> amode;
  let Enc{23-20} = ld;
  let Enc{19-16} = mod;
  let Enc{15-14} = amode;
  let Enc{13-0} = off;
}

def TT_SFPSTORE_WH
    : TTInst<0x72, (outs), (ins LRS:$ls, tt_simm14:$off, uimm4:$mod,
                                uimm2:$amode),
             "sfpstore", "$ls, $off, $mod, $amode"> {
  bits<4> ls;
  bits<14> off;
  bits<4> mod;
  bits<2> amode;
  let Enc{23-20} = ls;
  let Enc{19-16} = mod;
  let Enc{15-14} = amode;
  let Enc{13-0} = off;
}

def TT_SFPAND_WH : TTSFPBinary<0x7e, "sfpand">;
def TT_SFPOR_WH : TTSFPBinary<0x7f, "sfpor">;
def TT_SFPCAST_WH : TTSFPUnary<0x90, "sfpcast">;

def TT_SFPSTOCHRND_WH
    : TTSFPInst<0x8e, (outs LRD:$ld),
                (ins LRD:$ld_in, LRS:$lb, LRS:$lc, uimm5:$imm, uimm4:$mod,
                     uimm1:$rnd),
                "sfpstochrnd", "$ld, $lb, $lc, $imm, $mod, $rnd"> {
  bits<4> ld;
  bits<4> lb;
  bits<4> lc;
  bits<5> imm;
  bits<4> mod;
  bits<1> rnd;
  let Enc{23-22} = 0;
  let Enc{21} = rnd;
  let Enc{20-16} = imm;
  let Enc{15-12} = lb;
  let Enc{11-8} = lc;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
}
} // Predicates = [HasVendorXTTTensixWH], DecoderNamespace = "XTTWH"

//===----------------------------------------------------------------------===//
// Blackhole specific SFPU instructions.
//===----------------------------------------------------------------------===//

let Predicates = [HasVendorXTTTensixBH], DecoderNamespace = "XTTBH" in {
def TT_SFPLOAD_BH
    : TTSFPInst<0x70, (outs LRD:$ld),
                (ins LRD:$ld_in, tt_simm13:$off, uimm4:$mod, uimm3:$amode),
                "sfpload", "$ld, $off, $mod, $amode"> {
  bits<4> ld;
  bits<13> off;
  bits<4> mod;
  bits<3> amode;
  let Enc{23-20} = ld;
  let Enc{19-16} = mod;
  let Enc{15-13} = amode;
  let Enc{12-0} = off;
}

def TT_SFPSTORE_BH
    : TTInst<0x72, (outs), (ins LRS:$ls, tt_simm13:$off, uimm4:$mod,
                                uimm3:$amode),
             "sfpstore", "$ls, $off, $mod, $amode"> {
  bits<4> ls;
  bits<13> off;
  bits<4> mod;
  bits<3> amode;
  let Enc{23-20} = ls;
  let Enc{19-16} = mod;
  let Enc{15-13} = amode;
  let Enc{12-0} = off;
}

// ld = lb op la under mod.
class TTSFPLogicBH<bits<8> opc, string opcodestr>
    : TTSFPInst<opc, (outs LRD:$ld),
                (ins LRD:$ld_in, LRS:$lb, LRS:$la, uimm4:$mod),
                opcodestr, "$ld, $lb, $la, $mod"> {
  bits<4> ld;
  bits<4> lb;
  bits<4> la;
  bits<4> mod;
  let Enc{23-16} = 0;
  let Enc{15-12} = lb;
  let Enc{11-8} = la;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
}

def TT_SFPAND_BH : TTSFPLogicBH<0x7e, "sfpand">;
def TT_SFPOR_BH : TTSFPLogicBH<0x7f, "sfpor">;
def TT_SFPCAST_BH : TTSFPUnary<0x90, "sfpcast">;

def TT_SFPSTOCHRND_BH
    : TTSFPInst<0x8e, (outs LRD:$ld),
                (ins LRD:$ld_in, LRS:$lb, LRS:$lc, uimm5:$imm, uimm4:$mod,
                     uimm2:$rnd),
                "sfpstochrnd", "$ld, $lb, $lc, $imm, $mod, $rnd"> {
  bits<4> ld;
  bits<4> lb;
  bits<4> lc;
  bits<5> imm;
  bits<4> mod;
  bits<2> rnd;
  let Enc{23} = 0;
  let Enc{22-21} = rnd;
  let Enc{20-16} = imm;
  let Enc{15-12} = lb;
  let Enc{11-8} = lc;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
}

// Compare ld against ls, writing an all-ones/zero mask and optionally the CC.
class TTSFPCmpBH<bits<8> opc, string opcodestr>
    : TTSFPInst<opc, (outs LRD:$ld),
                (ins LRD:$ld_in, LRS:$ls, uimm4:$imm, uimm4:$mod),
                opcodestr, "$ld, $ls, $imm, $mod"> {
  bits<4> ld;
  bits<4> ls;
  bits<4> imm;
  bits<4> mod;
  let Enc{23-16} = 0;
  let Enc{15-12} = imm;
  let Enc{11-8} = ls;
  let Enc{7-4} = ld;
  let Enc{3-0} = mod;
}

def TT_SFPLE : TTSFPCmpBH<0x96, "sfple">;
def TT_SFPGT : TTSFPCmpBH<0x97, "sfpgt">;

let lc = 9 in
def TT_SFPMUL24
    : TTSFPMad<0x98, "sfpmul24", (ins LRD:$ld_in, LRS:$la, LRS:$lb, uimm4:$mod),
               "$ld, $la, $lb, $mod">;

def TT_SFPARECIP : TTSFPUnary<0x99, "sfparecip">;
} // Predicates = [HasVendorXTTTensixBH], DecoderNamespace = "XTTBH"

//===----------------------------------------------------------------------===//
// Code generation only forms.
//===----------------------------------------------------------------------===//

// SFPGT/SFPLE that only update the CC; the first operand is just compared.
let Predicates = [HasVendorXTTTensixBH], isCodeGenOnly = 1 in {
class TTSFPCmpCCBH<bits<8> opc, string opcodestr>
    : TTInst<opc, (outs), (ins LRS:$la, LRS:$ls, uimm4:$imm, uimm4:$mod),
             opcodestr, "$la, $ls, $imm, $mod"> {
  bits<4> la;
  bits<4> ls;
  bits<4> imm;
  bits<4> mod;
  let Enc{23-16} = 0;
  let Enc{15-12} = imm;
  let Enc{11-8} = ls;
  let Enc{7-4} = la;
  let Enc{3-0} = mod;
}

def TT_SFPLE_CC : TTSFPCmpCCBH<0x96, "sfple">;
def TT_SFPGT_CC : TTSFPCmpCCBH<0x97, "sfpgt">;
} // Predicates = [HasVendorXTTTensixBH], isCodeGenOnly = 1

// An instruction with a run-time immediate is built in a GPR and pushed into
// the instruction buffer. The register fields are filled in after register
// allocation: $fields holds the destination field shift in bits [7:0] and the
// source field shift in bits [15:8], 0xff meaning no such field.
let Predicates = [HasVendorXTTTensix], hasSideEffects = 1, mayStore = 1,
    mayLoad = 0, IsTensix = 1 in {
def PseudoTTSynth
    : Pseudo<(outs LRD:$ld, GPR:$tmp),
             (ins LRD:$ld_in, LRS:$ls, GPR:$val, GPR:$iptr, i32imm:$opc,
                  i32imm:$fields), []> {
  let Constraints = "$ld = $ld_in,@earlyclobber $tmp";
}
def PseudoTTSynthStore
    : Pseudo<(outs GPR:$tmp),
             (ins LRS:$ls, GPR:$val, GPR:$iptr, i32imm:$opc,
                  i32imm:$fields), []> {
  let Constraints = "@earlyclobber $tmp";
}
def PseudoTTSynthRaw
    : Pseudo<(outs GPR:$tmp), (ins GPR:$val, GPR:$iptr, i32imm:$opc), []> {
  let Constraints = "@earlyclobber $tmp";
}
} // Predicates = [HasVendorXTTTensix]

// Lookup table instructions with their operands pinned to fixed LREGs; the
// result is always produced in L3.
let Predicates = [HasVendorXTTTensix], hasSideEffects = 1, mayLoad = 0,
    mayStore = 0, IsTensix = 1 in {
let Uses = [L0, L1, L2, L3], Defs = [L3] in
def PseudoTTSFPLUT : Pseudo<(outs), (ins i32imm:$mod), []>;
let Uses = [L0, L1, L2, L3], Defs = [L3, L7] in
def PseudoTTSFPLUTFP32_3R : Pseudo<(outs), (ins i32imm:$mod), []>;
let Uses = [L0, L1, L2, L3, L4, L5, L6], Defs = [L3] in
def PseudoTTSFPLUTFP32_6R : Pseudo<(outs), (ins i32imm:$mod), []>;
} // Predicates = [HasVendorXTTTensix]

// Access to an LREG by number, e.g. one written by a raw Tensix instruction.
// These are expanded after register allocation to SFPMOVs of all lanes; as in
// GCC, the allocator does not keep the register free around them.
let Predicates = [HasVendorXTTTensix], hasSideEffects = 1, mayLoad = 0,
    mayStore = 0, IsTensix = 1 in {
def PseudoTTReadLReg : Pseudo<(outs LRD:$ld), (ins i32imm:$lreg), []>;
def PseudoTTWriteLReg : Pseudo<(outs), (ins LRS:$ls, i32imm:$lreg), []>;
// A 32-bit constant, expanded after register allocation into an SFPLOADI of
// each half so that the halves are neither shared nor kept apart.
def PseudoTTLoadI32 : Pseudo<(outs LRD:$ld), (ins i32imm:$imm), []>;
}
