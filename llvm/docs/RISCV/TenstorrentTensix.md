# Tenstorrent Tensix Support

```{contents}
:local:
```

## Introduction

Tenstorrent's Wormhole and Blackhole chips contain Tensix cores. Each Tensix
core has several small RV32IM "baby" cores; those that drive the Tensix
coprocessor (the TRISCs) can place coprocessor instructions directly in their
instruction stream. The most important coprocessor unit for compilers is the
SFPU, a 32-lane SIMD unit with eight general purpose vector registers.

Tenstorrent's GCC based [SFPI](https://github.com/tenstorrent/sfpi) toolchain
exposes the SFPU to C++ through `__builtin_rvtt_*` builtins, which the SFPI
headers (`sfpi.h`) wrap in vector classes and a `v_if`/`v_else` predication
syntax. LLVM implements the same builtins, assembler syntax and target options
so that the SFPI headers and existing kernels (for example in
[tt-metal](https://github.com/tenstorrent/tt-metal)) compile unchanged.

The support consists of:

* The `XTTTensixWH`, `XTTTensixBH` and `XTTZbkb` vendor extensions and the
  `tt-wh`, `tt-wh-tensix`, `tt-bh` and `tt-bh-tensix` CPUs.
* The `tt` triple vendor (`riscv32-tt-elf`), which selects GCC compatible type
  layout and toolchain discovery.
* Assembler and disassembler support for the SFPU instructions, `ttnop`,
  `ttreplay`, `ttincrwc` and the `.ttinsn` directive.
* Clang builtins, the `__xtt_vector` types, and the GCC options used by SFPI
  users (`-mtt-*`, `-ftt-*`).
* Code generation: the `RISCVTTLowering` IR pass, instruction selection, and
  the `RISCVTTPreRA` and `RISCVTTPostRA` machine passes.

## Instruction encoding

A Tensix instruction is a 32-bit word, `opcode[31:24]` followed by 24 operand
bits. In the RISC-V instruction stream it is rotated left by two bits, so its
low two bits are never `0b11` and it cannot be mistaken for a 32-bit RISC-V
instruction. The cores have no compressed instruction support, which makes
these encodings free; the disassembler decodes them as 32-bit Tensix
instructions whenever a Tensix extension is enabled.

The TableGen definitions in `RISCVInstrInfoXTT.td` describe the unrotated word
in a field `Enc` and map it onto `Inst`. Instructions are spelled as the vendor
assembler spells them (`sfpmad L0, L1, L2, L3, 0`), with the SFPU registers
named `L0`-`L15`. `L0`-`L7` are allocatable; `L8`-`L15` hold constants
(0.8373, 0.0, 1.0, -1.0, three programmable values and the lane id) and can
only be read.

`.ttinsn <word>` emits an arbitrary unrotated instruction word and is what the
compiler uses for `__builtin_rvtt_ttinsn` with a constant word.

## Execution model

Every SFPU instruction is predicated by a per-lane condition code (CC). Lanes
that are disabled keep the previous value of the destination register, so
from the compiler's point of view every instruction that writes an LREG also
reads it. The CC is changed by compare instructions (`sfpsetcc`, `sfpiadd`,
`sfpgt`, ...), and saved and restored with `sfppushc`/`sfppopc`.

LLVM models this as follows:

* `__xtt_vector` is a 32 x i32 vector. Value producing intrinsics take the old
  destination value as their first operand (`poison` when it is dead). During
  instruction selection it becomes an operand tied to the destination, the
  same way RVV passthru operands work.
* The CC is modelled as inaccessible memory. Intrinsics that change it write
  inaccessible memory, all other SFPU intrinsics read it. This keeps
  computations inside their predication region and stops value numbering from
  merging computations done under different predicates, while still allowing
  dead code elimination and CSE within a region.
* All Tensix machine instructions have unmodelled side effects, so machine
  passes never reorder them. This matches the GCC implementation.
* Register copies use `sfpmov ..., 2`, which copies all lanes regardless of the
  CC. LREGs cannot be spilled; running out of LREGs is a hard error, as in GCC.

## Lowering of the SFPI builtins

The SFPI headers use a few builtins that do not correspond to instructions.
`RISCVTTLowering`, an IR pass run at the start of code generation (after
inlining, when most operands have become constants), lowers them:

* `sfpxloadi` loads a 32-bit constant. It becomes a constant register read, a
  single `sfploadi` (bfloat16, fp16, signed or unsigned 16-bit form) or a pair
  of loads, whichever is cheapest.
* `sfpxcmp` and `sfpxlogic` build a boolean tree of comparisons that
  `sfpxcond` applies to the CC. The tree is expanded at the `sfpxcond`:
  comparisons AND into the CC, `!` inverts a comparison, and `||` is done with
  De Morgan's law and `sfpcompc`. Comparisons use `sfpgt`/`sfple` for floats on
  Blackhole, the sign of a difference computed with `sfpiadd` (or `sfpmad` for
  Wormhole floats) otherwise, and `sfpsetcc` when comparing against zero.
* The `v_if` macros emit `sfppushc`/`sfppopc` themselves. Because all lanes
  are enabled on function entry, the outermost push is unnecessary and the
  outermost pop becomes `sfpencc 3, 10`. An inner region without an else whose
  pop is immediately followed by its parent's pop needs no push either
  (`-mtt-tensix-optimize-cc`).
* `sfpassign_lv(old, new)` is a predicated assignment. It is dropped when all
  lanes are known to be enabled, `old` is undefined, or the variable's
  unpredicated definition is under the same CC; it is folded into the
  instruction that computes `new` when possible, and otherwise selected as a
  CC-predicated `sfpmov`. As in GCC, functions are assumed to be entered and
  to return with all lanes enabled, so calls keep that state.
* Peephole combines (`-mtt-tensix-optimize-combine`) form `sfpmad` from a
  multiply and an add, use the immediate forms `sfpmuli`, `sfpaddi` and
  `sfpiadd`, and fold negation into Blackhole multiply, add and MAD modifiers,
  also when the negated value has several uses. Like GCC, a product plus a
  bfloat16 constant uses `sfpaddi` instead of holding the constant in an LREG.
* Builtins whose immediate operand is not a compile-time constant cannot be
  encoded in the instruction stream. The instruction word is built in a GPR
  and stored to the instruction buffer (the `iptr` operand, normally
  `ckernel::instrn_buffer`). The pass computes the immediate part in IR; the
  register fields are only known after register allocation and are filled in
  by `RISCVTTPostRA`.

## Machine passes

`RISCVTTPreRA` replaces copies from the constant registers with direct uses,
saving allocatable registers. A programmable constant register is only read
directly when no `sfpconfig` can rewrite it in between. A constant register
is only named in an instruction's VD field (the first operand of the CC-only
`sfpgt`/`sfple`, or an `sfpswap` with a constant operand whose result is
unused, as in `max(x, 0.0f)`) when it is `L8`-`L10`. With the default lane
configuration an instruction whose VD field is 12 or more does not execute at
all; it overwrites an `SFPLOADMACRO` instruction template instead. For the
same reason `L12`-`L15` are copied to an allocatable register before an
`sfpstore`. 32-bit
constants are selected as a single pseudo and split into their two `sfploadi`
halves after register allocation, so the halves are never shared or split by
the allocator.

`RISCVTTPostRA` runs just before emission and

* expands the run-time synthesized instructions,
* inserts an `sfpnop` after instructions with a result latency (`sfpmad`,
  `sfpmul`, `sfpadd`, `sfpmuli`, `sfpaddi`, `sfplut*`, `sfpmul24`) when the next
  Tensix instruction reads the result. Wormhole needs this for every reader;
  Blackhole has an interlock that only misses some readers,
* on Wormhole, inserts an `sfpnop` between an `sfpswap` and a following
  `sfpstochrnd`, `sfpshft2`, half-register `sfploadi`/`sfpload` or write through
  the index in `L7`, whose write can collide with the swap's second write. GCC
  does not (tt-metal#67142),
* uses the replay buffer for repeated instruction sequences
  (`-mtt-tensix-optimize-replay`, see below),
* pads inline `ebreak` with 8 nops (`-mtt-fix-whbhebreak`), and
* on Wormhole, reads back byte and halfword stores before a later load can
  overtake them (`-mtt-fix-whraw`).

### Replay buffer

Tensix cores have a 32 entry replay buffer: `ttreplay start, len, 1, 1`
records (and executes) the next `len` Tensix instructions, and
`ttreplay start, len, 0, 0` issues them again. As in GCC, a sequence of at
least four Tensix instructions that repeats within a basic block, typically an
unrolled loop body, is recorded at its first occurrence and later copies are
replaced by a replay. Sequences are grown one instruction at a time and the
buffer is filled greedily with the sequence that saves the most instructions.
Instructions with run-time immediates take part when the GPR they read holds
the same value. The recording `ttreplay` is followed by a RISC-V `nop`: in a
loop, a recording `ttreplay` directly followed by the instructions it records
has been seen to hang Blackhole depending only on the alignment of the code
(GCC emitted the same instructions at a different address and did not hang).
The cause is not known. It is not the documented instruction gathering
erratum, as tt-metal's firmware disables gathering by default.

Buffer entries named by explicit replays anywhere in the module (`ttreplay`,
or a raw `.ttinsn` replay word such as `TTI_REPLAY`) are not used, because
the buffer is shared: LLK code records entries in one function that the MOP
replays later. With LTO, as tt-metal builds kernels, this covers the whole
program. A replay with a run-time length reserves the rest of the buffer and,
in its own function, ends the search in its block. Copies are only matched between calls and inline assembly, which may
use the buffer themselves. Integer instructions without memory accesses or
side effects do not end a sequence and stay in place. The number of entries
available can be limited with `-mtt-tensix-replay-buffer-size=`, which becomes
the `"riscv-tt-replay-buffer-size"` function attribute so that it also applies
to LTO code generation.

## Toolchain integration

The `tt` vendor makes `int32_t` a `long`, as in GCC's newlib configuration,
which the SFPI headers check for. The driver finds a GCC installation laid out
as `riscv-tt-elf` and picks the `wh-ilp32` or `bh-ilp32` multilib from
`-mcpu`. A drop-in replacement for `riscv-tt-elf-g++` is a link to `clang`
plus a `riscv-tt-elf.cfg` configuration file next to it:

```
--target=riscv32-tt-elf
--gcc-toolchain=<CFGDIR>/..
-fuse-ld=lld
-Wno-invalid-constexpr
```

`-Wno-invalid-constexpr` is needed because the SFPI headers declare
`constexpr` functions that call builtins, which GCC accepts in C++17.

The following GCC options are accepted for compatibility:

| Option | Behavior |
|---|---|
| `-mtt-fix-whbhebreak`, `-mtt-fix-whraw`, `-mtt-fix-whfence`, `-mtt-tensix-optimize-{cc,combine,replay}` | Subtarget features with the same defaults as GCC |
| `-mtt-tensix-replay-buffer-size=<n>` | Replay buffer entries available to the replay optimization |
| `-mtt-optimize-{extend,hll}`, `-mtt-tensix-optimize-dce`, `-mtt-fix-qsr*` | Ignored |
| `-ftt-nttp`, `-ftt-consteval`, `-ftt-constinit` | Allow class type template parameters, `consteval` and `constinit` before C++20 |
| `-ftt-no-dyninit` | `-Werror=global-constructors` |
| `-flto=<jobs>`, `-flto-partition=` | Full LTO |
| `-Wstack-usage=`, `-Werror=stack-usage=` | `-Wframe-larger-than=` |

The GCC attributes `optimize("...")`, `rvtt_l1_ptr`, `rvtt_reg_ptr` and
`noclone` are accepted. `optimize` maps `-Os`/`-Oz` and `no-jump-tables` onto
the corresponding function attributes; the others are ignored.

## Differences from the GCC implementation

* Register allocation differs from GCC's. Hand-tuned kernels that fill all
  eight LREGs with GCC fit with LLVM as well (all of tt-metal's LLK tests), but
  a kernel can still need more LREGs than with GCC, which is a hard error.
* `__builtin_rvtt_sfpreadlreg`/`sfpwritelreg` of `L0`-`L7` copy from/to the
  register at that point. As with GCC nothing reserves the register in between;
  the allocator is only hinted to keep such values in their register.

* The replay optimization does not match copies of a sequence across calls,
  reserves the entries used by explicit replays (including raw `TTI_REPLAY`
  words) in the whole module rather than only the function, and does not let
  integer instructions end a sequence, and puts a `nop` after the recording
  `ttreplay`; GCC does none of these. Without the
  module-wide reservation, tt-metal's SDPA kernels overwrite entries the pack
  MOP replays.
* The Wormhole `fence` workaround (`-mtt-fix-whfence`) is not implemented.
* Quasar is not supported.
* GCC shares the materialization of a synthesized instruction's opcode between
  uses; LLVM materializes it at each use.
* On Blackhole, GCC compares floats for equality with two `sfple`. `sfple`
  uses a total order in which `-0.0 < +0.0`, so `+0.0` and `-0.0` compare
  unequal (and a NaN equals itself); LLVM does the same to match it.
