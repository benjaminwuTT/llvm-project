// RUN: %clang_cc1 -triple riscv32-tt-elf -E -dM < /dev/null \
// RUN:   | FileCheck -match-full-lines %s
// RUN: %clang_cc1 -triple riscv32-unknown-elf -E -dM < /dev/null \
// RUN:   | FileCheck -match-full-lines -check-prefix=GENERIC %s

// Tenstorrent's GCC toolchain makes the 32-bit stdint types long.
// CHECK: #define __INT32_TYPE__ long int
// CHECK: #define __UINT32_TYPE__ long unsigned int
// GENERIC: #define __INT32_TYPE__ int
