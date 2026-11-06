# Stock LLVM as a drop-in for SFPI (tt-lab-private BRISC firmware)

This directory has the same layout as an SFPI release (`compiler/bin/riscv-tt-elf-*`), but
the toolchain inside is LLVM main (`9af38dfa80c8`) with a small patch. tt-lab-private builds
with it unchanged:

    ./make.py --env SFPI_PATH=$HOME/tt-llvm :build

## tt-lab-private changes

On `main` (f5bcef3c), none: the firmware sources, `rules.py` and the flags (including
`-mcpu=tt-bh`, `-Wl,--no-warn-rwx-segments`) are used as they are.

On glm-5.3-flash, clang needs a few source fixes (uncommitted in that branch's working tree)
and `--env RISCV_OPT=-Os`, because at `-O2` the GLM images exceed `TT_FIRMWARE_END`.

## LLVM changes (branch `tt-lab-clang`)

- `llvm/lib/Target/RISCV/AsmParser/RISCVAsmParser.cpp`: the `.ttinsn VALUE` directive,
  matching SFPI binutils. It rotates the value left by 2 bits and emits it as one 32-bit word.
  This is the only Tensix-specific feature the firmware uses; every `TTI_*` macro expands to
  it. Unlike SFPI gas it is not gated on an `xtttensixbh` ISA extension, and it accepts
  negative 32-bit values, because clang prints `"n"` asm operands as signed.
- `llvm/lib/Target/RISCV/RISCVProcessors.td`: the `-mcpu=tt-bh` CPU (RV32IM + Zaamo, Zba,
  Zbb, no scheduling model). It only matters for tuning, because tt-lab passes an explicit
  `-march`.
  Its tuning inlines fixed-size copies up to 48 bytes and fills up to 64 bytes, at `-Os`
  too, as GCC does. Without that, LLVM calls `memcpy` for struct copies over 32 bytes, and
  the firmware has no libc (this broke `brisc-glm-batch` on the glm-5.3-flash branch).
- `lld/ELF/Options.td`: lld ignores `--[no-]warn-rwx-segments` (GNU ld 2.39+) instead of
  rejecting it.

## Toolchain-side files (not source changes)

- `compiler/bin/riscv-tt-elf-g++` -> `clang`, `riscv-tt-elf-objcopy` -> `llvm-objcopy`,
  `riscv-tt-elf-objdump` -> `llvm-objdump`.
- `compiler/bin/riscv-tt-elf.cfg` (copy in this directory): a clang config file. Clang
  picks it up from the `riscv-tt-elf-` program-name prefix. It sets
  `--target=riscv32-unknown-elf`,
  `-fuse-ld=lld` and `-fno-math-errno`. Without the last one, clang turns `__builtin_fmaf`
  into a call to `fmaf` on bare-metal ELF targets, and the link fails because there is no
  libm. GCC emits `fmadd.s` here.
- `compiler/include/{c++/v1,riscv32-unknown-unknown-elf/c++/v1}`: headers-only libc++ for
  riscv32 bare metal (threads, localization etc. off). The firmware includes `<bit>` for
  `std::bit_cast`, and stock bare-metal clang ships no C++ library headers.

## Rebuilding

    cd ~/llvm-project
    cmake -G Ninja -S llvm -B build -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON \
      -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_TARGETS_TO_BUILD=RISCV \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLLVM_USE_LINKER=lld \
      -DCMAKE_INSTALL_PREFIX=$HOME/tt-llvm/compiler -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON \
      -DLLVM_DISTRIBUTION_COMPONENTS="clang;lld;clang-resource-headers;llvm-objcopy;llvm-objdump"
    ninja -C build install-distribution

    C=$HOME/tt-llvm/compiler/bin
    cmake -G Ninja -S runtimes -B build-libcxx-rv32 -DLLVM_ENABLE_RUNTIMES=libcxx \
      -DLLVM_DEFAULT_TARGET_TRIPLE=riscv32-unknown-elf -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$HOME/tt-llvm/compiler -DCMAKE_SYSTEM_NAME=Generic \
      -DCMAKE_C_COMPILER=$C/clang -DCMAKE_CXX_COMPILER=$C/clang++ \
      -DCMAKE_C_COMPILER_TARGET=riscv32-unknown-elf \
      -DCMAKE_CXX_COMPILER_TARGET=riscv32-unknown-elf \
      -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON \
      -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXX_ENABLE_THREADS=OFF \
      -DLIBCXX_ENABLE_MONOTONIC_CLOCK=OFF -DLIBCXX_ENABLE_LOCALIZATION=OFF \
      -DLIBCXX_ENABLE_FILESYSTEM=OFF -DLIBCXX_ENABLE_RANDOM_DEVICE=OFF \
      -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF -DLIBCXX_ENABLE_UNICODE=OFF \
      -DLIBCXX_ENABLE_EXCEPTIONS=OFF -DLIBCXX_ENABLE_RTTI=OFF -DLIBCXX_CXX_ABI=none \
      -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_TESTS=OFF
    ninja -C build-libcxx-rv32 install-cxx-headers

Then recreate the three symlinks in `compiler/bin` and copy `tt-lab/riscv-tt-elf.cfg` there.
