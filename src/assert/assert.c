// src/assert/assert.c   (namespace 'teko::assert')
//
// The C SEED impl of assert.tks. Each FAILS LOUD (M.1): on a false assertion ->
// tk_panic("assertion failed: <name>"). Shares tk_str + tk_panic with generated programs
// (src/runtime/teko_rt.h), so this one source links into BOTH the compiler lib (via CMake) and
// the host-cc-built native binary (driver.c::run_cc compiles it alongside teko_rt.c).
#include "assert.h"

// THE SEED'S DUAL ROLE, and why WEAK alone could not carry it.
//
// run_cc links this seed into EVERY native binary so a program that CALLS teko::assert but does not
// DEFINE it resolves the symbols. But the teko compiler's OWN corpus DOES define them (assert.tks ->
// the generated teko.c), so that build must not see a second definition. Weak linkage used to serve
// both roles: the corpus's strong definitions win when present, the seed fills in when absent.
//
// WEAK IS BROKEN ON PE/COFF. GCC lowers a weak DEFINITION to the real symbol renamed
// `.weak.<name>.` plus an *undefined* weak external `<name>`, and GNU ld for PE does not resolve
// another object's strong reference against it. Reproduced with x86_64-w64-mingw32-gcc,
// order-independent:
//     undefined reference to `teko__assert__is_true'
// So on Windows this seed was effectively DEAD: any program calling teko::assert without defining it
// could not LINK. `regressor.tkr`'s `assert_native` scenario is exactly such a program, and the
// 0.3.1 regression fold is what first ran it on Windows at all.
//
// THE FIX — the ROLE is chosen by the BUILD, not guessed by the linker. `TK_ASSERT_SEED_STRONG` is
// defined by `project.tks::build_cc_argv` exactly when the program being built does NOT declare
// `teko::assert` itself (`program_declares_assert_seed`), i.e. exactly when this seed is the ONLY
// definition. Then the definitions are STRONG and resolve on ELF, Mach-O and PE/COFF alike.
//
// WEAK REMAINS THE DEFAULT, and that is deliberate: a PREVIOUSLY RELEASED seed compiles this tree
// without passing the new flag, and it links teko.c + assert.c together. Strong-by-default would
// make every existing seed fail to bootstrap gen1 with a duplicate symbol. Defaulting to weak keeps
// the flagless link line byte-identical to today's — which is also why the compiler's own build (the
// no-flag case) keeps its exact link line, and the FIXPOINT with it.
#ifndef TK_ASSERT_SEED_STRONG
#define TK_ASSERT_WEAK __attribute__((weak))
#else
#define TK_ASSERT_WEAK
#endif

TK_ASSERT_WEAK void teko__assert__is_true(bool c)  { if (!c) tk_panic("assertion failed: is_true"); }
TK_ASSERT_WEAK void teko__assert__is_false(bool c) { if ( c) tk_panic("assertion failed: is_false"); }

TK_ASSERT_WEAK void teko__assert__str_contains(tk_str hay, tk_str needle) {
    // Plain byte-substring scan over the spans; no allocation. Empty needle ⊆ any hay.
    if (needle.len == 0) return;
    if (needle.len <= hay.len) {
        for (size_t i = 0; i + needle.len <= hay.len; i += 1) {
            size_t j = 0;
            while (j < needle.len && hay.ptr[i + j] == needle.ptr[j]) j += 1;
            if (j == needle.len) return;   // found
        }
    }
    tk_panic("assertion failed: str_contains");
}
