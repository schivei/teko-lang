// src/assert/assert.c   (namespace 'teko::assert')
//
// The C SEED impl of assert.tks. Each FAILS LOUD (M.1): on a false assertion ->
// tk_panic("assertion failed: <name>"). Shares tk_str + tk_panic with generated programs
// (src/runtime/teko_rt.h), so this one source links into BOTH the compiler lib (via CMake) and
// the host-cc-built native binary (driver.c::run_cc compiles it alongside teko_rt.c).
#include "assert.h"

void teko__assert__is_true(bool c)  { if (!c) tk_panic("assertion failed: is_true"); }
void teko__assert__is_false(bool c) { if ( c) tk_panic("assertion failed: is_false"); }

void teko__assert__str_contains(tk_str hay, tk_str needle) {
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
