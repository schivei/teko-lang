// src/assert/assert.h   (namespace 'teko::assert')
//
// teko::assert — the C SEED of assert.tks: the injected, non-shadowable testing
// assertions under the reserved `teko::` root. The generic CALL lowering emits
// `teko__assert__<name>(...)` for a call to `teko::assert::<name>`, so these symbols
// realize the seed in C (no codegen change). The typer injects the signatures
// (src/checker/scope.c::tk_builtin_fn).
//
// FAIL LOUD (M.1): a false assertion is a PANIC (tk_panic), not a soft return. The seed
// subset is NON-generic; equals/is_error/is_ok need generics — DEFERRED (M.3).
//
// Shares tk_str / tk_panic with GENERATED programs via src/runtime/teko_rt.h, so the same
// source links into both the compiler lib and the host-cc-built native binary.
#ifndef TK_ASSERT_H
#define TK_ASSERT_H

#include "../runtime/teko_rt.h"   // tk_str, tk_byte, tk_panic

void teko__assert__is_true(bool c);        // panic unless c
void teko__assert__is_false(bool c);       // panic unless !c
void teko__assert__str_contains(tk_str hay, tk_str needle);  // panic unless needle ⊆ hay

#endif // TK_ASSERT_H
