// src/text/text.h — teko::text: bootstrap text types + operations (A.16 / B.36).
#ifndef TK_TEXT_H
#define TK_TEXT_H

#include "../core.h"   // tk_error, TK_RESULT (DAG: text → core)
#include <stddef.h>    // size_t
#include <stdint.h>    // uint8_t

// byte — one octet. Teko's `byte` is a newtype over u8 (distinct identity, same
// rep); C typedefs are transparent, so that identity lives in the Teko checker.
typedef uint8_t tk_byte;

// str — a VIEW into validated UTF-8 bytes (zero-copy, B.35). Validity is set once
// at construction and trusted downstream, never re-checked (B.36).
typedef struct {
    const tk_byte *ptr;   // the bytes (valid UTF-8)
    size_t         len;   // length in BYTES (not codepoints)
} tk_str;

// `str | error` — the result of validating raw bytes as UTF-8.
TK_RESULT(tk_str, tk_str_result);

// str_from_utf8 — the validated constructor (the ONLY door from bytes to a str;
// UTF-8 FORCED — B.36). Zero-copy on success (views the same bytes).
tk_str_result tk_str_from_utf8(const tk_byte *bytes, size_t len);

// slice [start, end) — a zero-copy VIEW sharing s's bytes (no re-validation; the
// caller cuts on codepoint boundaries). The reference's `slice(source, a, b)`.
static inline tk_str tk_str_slice(tk_str s, size_t start, size_t end) {
    return (tk_str){ .ptr = s.ptr + start, .len = end - start };
}

#endif // TK_TEXT_H
