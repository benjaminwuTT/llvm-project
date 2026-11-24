// RUN: %clang_cc1 -triple riscv32-tt-elf -emit-llvm -o - %s \
// RUN:   -mtt-tensix-replay-buffer-size=16 | FileCheck %s
// RUN: %clang_cc1 -triple riscv32-tt-elf -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=DEFAULT

// CHECK: define{{.*}} void @f() [[ATTR:#[0-9]+]]
// CHECK: attributes [[ATTR]] = {{.*}}"riscv-tt-replay-buffer-size"="16"
// DEFAULT-NOT: riscv-tt-replay-buffer-size
void f(void) {}
