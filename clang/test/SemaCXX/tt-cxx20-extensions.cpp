// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -fsyntax-only -verify=cxx17 %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -ftt-nttp -ftt-consteval -ftt-constinit \
// RUN:   -fsyntax-only -verify=tt %s
// tt-no-diagnostics

struct S {
  int V;
};
template <S X> int get() { return X.V; } // cxx17-error {{a non-type template parameter cannot have type 'S' before C++20}}

consteval int five() { return 5; } // cxx17-error {{unknown type name 'consteval'}}
constinit int I = 3; // cxx17-error {{unknown type name 'constinit'}}
