// RUN: %clang_cc1 -triple riscv32-tt-elf -fsyntax-only -verify=riscv %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fsyntax-only -verify=x86 %s

static __attribute__((noinline, noclone)) void f(int x) { (void)x; } // x86-warning {{unknown attribute 'noclone' ignored}}
[[gnu::noclone]] void g(void); // x86-warning {{unknown attribute 'gnu::noclone' ignored}}
int v __attribute__((noclone)); // riscv-warning {{'noclone' attribute only applies to functions}} \
                                // x86-warning {{unknown attribute 'noclone' ignored}}

void h(void) { f(1); }
