//==- BuiltinsRISCVXTT.td - RISC-V Tenstorrent Builtin database --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Tenstorrent Tensix (SFPU) builtin function database.
// The names and prototypes match the GCC-based SFPI toolchain.
//
//===----------------------------------------------------------------------===//

class RISCVTTBuiltin<string prototype, string features> : TargetBuiltin {
  let Spellings = ["__builtin_rvtt_" # NAME];
  let Prototype = prototype;
  let Features = features;
}

let Attributes = [NoThrow] in {
def synth_opcode : RISCVTTBuiltin<"unsigned int(unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def synth_add2 : RISCVTTBuiltin<"unsigned int(unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def lreg_pressure : RISCVTTBuiltin<"void(unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpxlogic : RISCVTTBuiltin<"unsigned int(unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpxpred : RISCVTTBuiltin<"unsigned int(unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpxcond : RISCVTTBuiltin<"unsigned int(unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpxcmp : RISCVTTBuiltin<"unsigned int(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpxloadi : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, unsigned int, unsigned int, unsigned int, int)", "xtttensixwh|xtttensixbh">;
def sfpnop : RISCVTTBuiltin<"void()", "xtttensixwh|xtttensixbh">;
def sfpreadlreg : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpwritelreg : RISCVTTBuiltin<"void(_Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpnovalue : RISCVTTBuiltin<"_Vector<32, unsigned int>()", "xtttensixwh|xtttensixbh">;
def sfpselect2 : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<64, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpselect4 : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<128, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpselect8 : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<256, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpconcat2 : RISCVTTBuiltin<"_Vector<64, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfpsetcc : RISCVTTBuiltin<"void(_Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpencc : RISCVTTBuiltin<"void(unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpcompc : RISCVTTBuiltin<"void()", "xtttensixwh|xtttensixbh">;
def sfppushc : RISCVTTBuiltin<"void(unsigned int)", "xtttensixwh|xtttensixbh">;
def sfppopc : RISCVTTBuiltin<"void(unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpassign : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfpassign_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfploadi : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfploadi_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpmov : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpmov_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpexexp : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpexexp_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpexman : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpexman_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpabs : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpabs_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfplz : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfplz_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpand : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfpand_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfpor : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfpor_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfpxor : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfpxor_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfpnot : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfpnot_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfpshft_v : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpshft_v_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpshft_i : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpshft_i_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfplut : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfplutfp32_3r : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfplutfp32_6r : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpswap : RISCVTTBuiltin<"_Vector<64, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfptransp : RISCVTTBuiltin<"_Vector<256, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>)", "xtttensixwh|xtttensixbh">;
def sfpshft2_copy4 : RISCVTTBuiltin<"_Vector<128, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpshft2_subvec_copy4 : RISCVTTBuiltin<"_Vector<128, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpshft2_subvec_shfl1_copy4 : RISCVTTBuiltin<"_Vector<128, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpshft2_subvec_shfl1 : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpshft2_subvec_shfl1_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpload : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpload_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpstore : RISCVTTBuiltin<"void(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpiadd_v : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpiadd_v_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpiadd_i : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpiadd_i_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpmul : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpmul_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpmuli : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpmuli_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpadd : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpadd_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpaddi : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpaddi_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetexp_v : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetexp_v_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetexp_i : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetexp_i_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetman_v : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetman_v_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetman_i : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetman_i_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetsgn_v : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetsgn_v_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetsgn_i : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpsetsgn_i_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpmad : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpmad_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpdivp2 : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpdivp2_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpcast : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpcast_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpstochrnd : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpstochrnd_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpstochrnd_descale_v : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpstochrnd_descale_v_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpstochrnd_descale_i : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpstochrnd_descale_i_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned long int volatile *, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpwriteconfig_v : RISCVTTBuiltin<"void(_Vector<32, unsigned int>, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def sfpwriteconfig_i : RISCVTTBuiltin<"void(unsigned long int volatile *, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def ttinsn : RISCVTTBuiltin<"void(unsigned long int volatile *, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def ttreplay : RISCVTTBuiltin<"void(unsigned long int volatile *, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int)", "xtttensixwh|xtttensixbh">;
def ttincrwc : RISCVTTBuiltin<"void(int, int, int, int)", "xtttensixwh|xtttensixbh">;
def sfpreadconfig : RISCVTTBuiltin<"_Vector<32, unsigned int>(unsigned int)", "xtttensixbh">;
def sfpreadconfig_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, unsigned int)", "xtttensixbh">;
def sfpgt : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixbh">;
def sfpgt_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixbh">;
def sfple : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixbh">;
def sfple_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixbh">;
def sfpmul24 : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixbh">;
def sfpmul24_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixbh">;
def sfparecip : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, unsigned int)", "xtttensixbh">;
def sfparecip_lv : RISCVTTBuiltin<"_Vector<32, unsigned int>(_Vector<32, unsigned int>, _Vector<32, unsigned int>, unsigned int)", "xtttensixbh">;
} // Attributes = [NoThrow]
