// RUN: %clang_cc1 -triple riscv32-tt-elf -O2 -emit-llvm -o - %s | FileCheck %s

// CHECK: define {{.*}}void @size(){{.*}} [[SIZE:#[0-9]+]]
__attribute__((optimize("Os"))) void size(void) {}

// CHECK: define {{.*}}void @no_jt(){{.*}} [[NOJT:#[0-9]+]]
__attribute__((optimize("no-jump-tables"))) void no_jt(void) {}

// Unknown options are ignored.
// CHECK: define {{.*}}void @other(){{.*}} [[OTHER:#[0-9]+]]
[[gnu::optimize("unroll-loops")]] void other(void) {}

// CHECK: attributes [[SIZE]] = { {{.*}}optsize
// CHECK: attributes [[NOJT]] = { {{.*}}"no-jump-tables"="true"
// CHECK-NOT: attributes [[OTHER]] = { {{.*}}optsize
