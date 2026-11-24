// RUN: %clang --target=riscv32-tt-elf -mcpu=tt-bh-tensix -### -c %s \
// RUN:   -mtt-tensix-replay-buffer-size=16 2>&1 | FileCheck %s --check-prefix=SIZE
// SIZE: "-cc1"{{.*}} "-mtt-tensix-replay-buffer-size=16"

// RUN: not %clang --target=riscv32-tt-elf -mcpu=tt-bh-tensix -### -c %s \
// RUN:   -mtt-tensix-replay-buffer-size=33 2>&1 | FileCheck %s --check-prefix=BAD
// BAD: error: invalid integral value '33' in '-mtt-tensix-replay-buffer-size=33'

// RUN: %clang --target=riscv32-tt-elf -mcpu=tt-bh-tensix -### -c %s \
// RUN:   -mno-tt-tensix-optimize-replay 2>&1 | FileCheck %s --check-prefix=OFF
// OFF: "-target-feature" "-tt-tensix-optimize-replay"
