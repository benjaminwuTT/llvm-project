// RUN: %clang_cc1 -triple riscv32-tt-elf -target-feature +xtttensixbh -O1 \
// RUN:   -emit-llvm -o - %s | FileCheck %s

typedef volatile unsigned long *iptr;

// CHECK-LABEL: @mad(
// CHECK: [[A:%.*]] = tail call <32 x i32> @llvm.riscv.tt.sfpload(<32 x i32> poison, ptr {{.*}}, i32 0, i32 0, i32 7)
// CHECK: [[R:%.*]] = tail call <32 x i32> @llvm.riscv.tt.sfpmad(<32 x i32> poison, <32 x i32> [[A]], <32 x i32> [[A]], <32 x i32> [[A]], i32 0)
// CHECK: call void @llvm.riscv.tt.sfpstore(ptr {{.*}}, <32 x i32> [[R]], i32 2, i32 0, i32 7)
void mad(iptr ip) {
  __xtt_vector a = __builtin_rvtt_sfpload(ip, 0, 0, 0, 0, 7);
  __xtt_vector r = __builtin_rvtt_sfpmad(a, a, a, 0);
  __builtin_rvtt_sfpstore(ip, r, 2, 0, 0, 0, 7);
}

// The old value of an _lv builtin becomes the first operand.
// CHECK-LABEL: @live(
// CHECK: call <32 x i32> @llvm.riscv.tt.sfpiadd.i(<32 x i32> [[OLD:%.*]], ptr {{.*}}, <32 x i32> [[SRC:%.*]], i32 5, i32 4)
// CHECK: call <32 x i32> @llvm.riscv.tt.sfpassign.lv(<32 x i32> {{.*}}, <32 x i32> {{.*}})
__xtt_vector live(iptr ip, __xtt_vector old, __xtt_vector src) {
  __xtt_vector v = __builtin_rvtt_sfpiadd_i_lv(ip, old, src, 5, 0, 0, 4);
  return __builtin_rvtt_sfpassign_lv(v, src);
}

// CHECK-LABEL: @cond(
// CHECK: [[C:%.*]] = tail call i32 @llvm.riscv.tt.sfpxcmp(<32 x i32> {{.*}}, <32 x i32> {{.*}}, i32 48)
// CHECK: call i32 @llvm.riscv.tt.sfpxcond(i32 0, i32 0, i32 [[C]])
// CHECK: call void @llvm.riscv.tt.ttinsn(ptr {{.*}}, i32 33554432, i32 0, i32 0, i32 0)
void cond(iptr ip, __xtt_vector a) {
  __builtin_rvtt_sfpxcond(0, 0, __builtin_rvtt_sfpxcmp(a, a, 48));
  __builtin_rvtt_ttinsn(ip, 0x02000000, 0, 0, 0);
}

// Multi-register results are concatenated and selected with shuffles, which
// fold away.
// CHECK-LABEL: @swap(
// CHECK: [[S:%.*]] = tail call { <32 x i32>, <32 x i32> } @llvm.riscv.tt.sfpswap(
// CHECK-NEXT: extractvalue { <32 x i32>, <32 x i32> } [[S]], 1
__xtt_vector swap(__xtt_vector a, __xtt_vector b) {
  __xtt_vector2 p = __builtin_rvtt_sfpswap(a, b, 1);
  return __builtin_rvtt_sfpselect2(p, 1);
}
