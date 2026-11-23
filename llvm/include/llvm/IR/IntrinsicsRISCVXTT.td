//===- IntrinsicsRISCVXTT.td - Tenstorrent Tensix intrinsics -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Tenstorrent Tensix (SFPU) intrinsics. See
// llvm/docs/RISCV/TenstorrentTensix.rst for the execution model.
//
// SFPU instructions only write the vector lanes enabled by the condition code
// (CC), so every value producing intrinsic takes the previous value of its
// destination as its first operand; poison means the old value is dead. The
// CC itself is modelled as inaccessible memory: CC changing intrinsics write
// it and everything else reads it. Immediate operands are plain i32 operands
// because sfpi passes them through inline functions; the backend synthesizes
// the instruction at run time (through the instruction buffer pointer
// operand) when an immediate is not a constant.
//
//===----------------------------------------------------------------------===//

defvar llvm_xtt_vec_ty = llvm_v32i32_ty;

let TargetPrefix = "riscv" in {

// Reads the CC.
class RISCVTTIntrinsic<list<LLVMType> ret, list<LLVMType> args>
    : DefaultAttrsIntrinsic<ret, args, [IntrReadMem, IntrInaccessibleMemOnly]>;

// Reads and may write the CC, or touches other Tensix state.
class RISCVTTStateIntrinsic<list<LLVMType> ret, list<LLVMType> args>
    : DefaultAttrsIntrinsic<ret, args, [IntrInaccessibleMemOnly]>;

// Arbitrary Tensix instruction: may touch any memory.
class RISCVTTVolatileIntrinsic<list<LLVMType> args>
    : DefaultAttrsIntrinsic<[], args, [IntrHasSideEffects]>;

// (old, a, mod)
class RISCVTTUnary
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_i32_ty]>;
class RISCVTTUnaryCC
    : RISCVTTStateIntrinsic<[llvm_xtt_vec_ty],
                            [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_i32_ty]>;
// (old, a, b)
class RISCVTTBinaryNoMod
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty]>;
// (old, a, b, mod)
class RISCVTTBinary
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty,
                        llvm_i32_ty]>;
class RISCVTTBinaryCC
    : RISCVTTStateIntrinsic<[llvm_xtt_vec_ty],
                            [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty,
                             llvm_i32_ty]>;
// (old, iptr, a, imm, mod)
class RISCVTTImm
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_ptr_ty, llvm_xtt_vec_ty,
                        llvm_i32_ty, llvm_i32_ty]>;
class RISCVTTImmCC
    : RISCVTTStateIntrinsic<[llvm_xtt_vec_ty],
                            [llvm_xtt_vec_ty, llvm_ptr_ty, llvm_xtt_vec_ty,
                             llvm_i32_ty, llvm_i32_ty]>;

def int_riscv_tt_sfploadi
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty]>;
// Load a 32-bit constant, using the cheapest SFPLOADI sequence.
def int_riscv_tt_sfpxloadi
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty]>;
// (old, iptr, addr, mod, addr_mode)
def int_riscv_tt_sfpload
    : RISCVTTStateIntrinsic<[llvm_xtt_vec_ty],
                            [llvm_xtt_vec_ty, llvm_ptr_ty, llvm_i32_ty,
                             llvm_i32_ty, llvm_i32_ty]>;
// (iptr, value, addr, mod, addr_mode)
def int_riscv_tt_sfpstore
    : RISCVTTStateIntrinsic<[], [llvm_ptr_ty, llvm_xtt_vec_ty, llvm_i32_ty,
                                 llvm_i32_ty, llvm_i32_ty]>;

def int_riscv_tt_sfpmov : RISCVTTUnary;
def int_riscv_tt_sfpexexp : RISCVTTUnaryCC;
def int_riscv_tt_sfpexman : RISCVTTUnary;
def int_riscv_tt_sfpabs : RISCVTTUnary;
def int_riscv_tt_sfplz : RISCVTTUnaryCC;
def int_riscv_tt_sfpcast : RISCVTTUnary;
def int_riscv_tt_sfparecip : RISCVTTUnary;
def int_riscv_tt_sfpnot
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty], [llvm_xtt_vec_ty, llvm_xtt_vec_ty]>;
def int_riscv_tt_sfpand : RISCVTTBinaryNoMod;
def int_riscv_tt_sfpor : RISCVTTBinaryNoMod;
def int_riscv_tt_sfpxor : RISCVTTBinaryNoMod;
def int_riscv_tt_sfpshft_v : RISCVTTBinary;
def int_riscv_tt_sfpshft_i : RISCVTTImm;
def int_riscv_tt_sfpiadd_v : RISCVTTBinaryCC;
def int_riscv_tt_sfpiadd_i : RISCVTTImmCC;
def int_riscv_tt_sfpmul : RISCVTTBinary;
def int_riscv_tt_sfpadd : RISCVTTBinary;
def int_riscv_tt_sfpmul24 : RISCVTTBinary;
def int_riscv_tt_sfpmuli : RISCVTTImm;
def int_riscv_tt_sfpaddi : RISCVTTImm;
def int_riscv_tt_sfpmad
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty,
                        llvm_xtt_vec_ty, llvm_i32_ty]>;
def int_riscv_tt_sfpsetexp_v : RISCVTTBinary;
def int_riscv_tt_sfpsetexp_i : RISCVTTImm;
def int_riscv_tt_sfpsetman_v : RISCVTTBinary;
def int_riscv_tt_sfpsetman_i : RISCVTTImm;
def int_riscv_tt_sfpsetsgn_v : RISCVTTBinary;
def int_riscv_tt_sfpsetsgn_i : RISCVTTImm;
def int_riscv_tt_sfpdivp2 : RISCVTTImm;
def int_riscv_tt_sfpgt : RISCVTTBinaryCC;
def int_riscv_tt_sfple : RISCVTTBinaryCC;
// (old, a, mod, rnd)
def int_riscv_tt_sfpstochrnd
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_i32_ty,
                        llvm_i32_ty]>;
// (old, a, descale, mod, rnd)
def int_riscv_tt_sfpstochrnd_descale_v
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty,
                        llvm_i32_ty, llvm_i32_ty]>;
// (old, iptr, a, descale, mod, rnd)
def int_riscv_tt_sfpstochrnd_descale_i
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_ptr_ty, llvm_xtt_vec_ty,
                        llvm_i32_ty, llvm_i32_ty, llvm_i32_ty]>;
// (old, l0, l1, l2, l3, mod)
def int_riscv_tt_sfplut
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty,
                        llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_i32_ty]>;
def int_riscv_tt_sfplutfp32_3r
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty,
                        llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_i32_ty]>;
def int_riscv_tt_sfplutfp32_6r
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty,
                        llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty,
                        llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_i32_ty]>;
def int_riscv_tt_sfpswap
    : RISCVTTIntrinsic<[llvm_xtt_vec_ty, llvm_xtt_vec_ty],
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_i32_ty]>;
def int_riscv_tt_sfptransp
    : RISCVTTIntrinsic<!listsplat(llvm_xtt_vec_ty, 8),
                       !listsplat(llvm_xtt_vec_ty, 8)>;
def int_riscv_tt_sfpshft2_copy4
    : RISCVTTIntrinsic<!listsplat(llvm_xtt_vec_ty, 4),
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty,
                        llvm_i32_ty]>;
def int_riscv_tt_sfpshft2_subvec_copy4
    : RISCVTTIntrinsic<!listsplat(llvm_xtt_vec_ty, 4),
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty,
                        llvm_xtt_vec_ty, llvm_i32_ty]>;
def int_riscv_tt_sfpshft2_subvec_shfl1_copy4
    : RISCVTTIntrinsic<!listsplat(llvm_xtt_vec_ty, 4),
                       [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_xtt_vec_ty,
                        llvm_xtt_vec_ty, llvm_i32_ty]>;
def int_riscv_tt_sfpshft2_subvec_shfl1 : RISCVTTUnary;

// Read or write a specific LREG, e.g. a constant register.
def int_riscv_tt_sfpreadlreg
    : RISCVTTStateIntrinsic<[llvm_xtt_vec_ty], [llvm_i32_ty]>;
def int_riscv_tt_sfpwritelreg
    : RISCVTTStateIntrinsic<[], [llvm_xtt_vec_ty, llvm_i32_ty]>;
// (old, config)
def int_riscv_tt_sfpreadconfig
    : RISCVTTStateIntrinsic<[llvm_xtt_vec_ty], [llvm_xtt_vec_ty, llvm_i32_ty]>;
// (value, mod, dest)
def int_riscv_tt_sfpwriteconfig_v
    : RISCVTTStateIntrinsic<[], [llvm_xtt_vec_ty, llvm_i32_ty, llvm_i32_ty]>;
// (iptr, imm, mod, dest)
def int_riscv_tt_sfpwriteconfig_i
    : RISCVTTStateIntrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty,
                                 llvm_i32_ty]>;

// Condition code manipulation.
def int_riscv_tt_sfpsetcc
    : RISCVTTStateIntrinsic<[], [llvm_xtt_vec_ty, llvm_i32_ty]>;
def int_riscv_tt_sfpencc : RISCVTTStateIntrinsic<[], [llvm_i32_ty, llvm_i32_ty]>;
def int_riscv_tt_sfpcompc : RISCVTTStateIntrinsic<[], []>;
def int_riscv_tt_sfppushc : RISCVTTStateIntrinsic<[], [llvm_i32_ty]>;
def int_riscv_tt_sfppopc : RISCVTTStateIntrinsic<[], [llvm_i32_ty]>;

// Structured predication, lowered by RISCVTTLowerPredication. sfpxcmp and
// sfpxlogic build a boolean tree, sfpxcond applies it to the CC and sfpxpred
// marks the v_if/v_elseif/v_else/v_endif structure.
def int_riscv_tt_sfpxcmp
    : DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_i32_ty],
                            [IntrReadMem, IntrInaccessibleMemOnly]>;
def int_riscv_tt_sfpxlogic
    : DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
// (mod, dep, cond)
def int_riscv_tt_sfpxcond
    : RISCVTTStateIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty]>;
def int_riscv_tt_sfpxpred
    : RISCVTTStateIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty]>;

// Predicated assignment: (old, new). Folded into the producer of new where
// possible, otherwise a masked move.
def int_riscv_tt_sfpassign_lv : RISCVTTIntrinsic<[llvm_xtt_vec_ty],
                                                 [llvm_xtt_vec_ty,
                                                  llvm_xtt_vec_ty]>;

// Run-time synthesized instructions, created by RISCVTTLowering:
// (old, src, value, iptr, static opcode, register field shifts).
def int_riscv_tt_synth
    : RISCVTTStateIntrinsic<[llvm_xtt_vec_ty],
                            [llvm_xtt_vec_ty, llvm_xtt_vec_ty, llvm_i32_ty,
                             llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty]>;
// (src, value, iptr, static opcode, register field shifts); src may be poison.
def int_riscv_tt_synth_void
    : RISCVTTStateIntrinsic<[], [llvm_xtt_vec_ty, llvm_i32_ty, llvm_ptr_ty,
                                 llvm_i32_ty, llvm_i32_ty]>;

def int_riscv_tt_sfpnop : RISCVTTStateIntrinsic<[], []>;
// (iptr, insn, flags, dst_mask, src_mask)
def int_riscv_tt_ttinsn
    : RISCVTTVolatileIntrinsic<[llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty,
                                llvm_i32_ty, llvm_i32_ty]>;
// (iptr, len, start, exec_while_load, load)
def int_riscv_tt_ttreplay
    : RISCVTTVolatileIntrinsic<[llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty,
                                llvm_i32_ty, llvm_i32_ty]>;
def int_riscv_tt_ttincrwc
    : RISCVTTStateIntrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                                 llvm_i32_ty]>;

} // TargetPrefix = "riscv"
