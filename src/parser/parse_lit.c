// src/parser/parse_lit.c   (namespace 'teko::parser')
//
// Literal decoders, the C23 mirror of parser/parse_lit.tks. RELOCATED from the lexer
// (the bodies were formerly in src/lexer/lexer.c) — these are parser-namespace fns.
#include "parse_lit.h"

// a Number token's text (decimal digits with `_` separators) → i64.
int64_t tk_lit_int(tk_str text) {
    int64_t acc = 0;
    size_t i = 0;
    for (;;) {
        if (i >= text.len) break;
        tk_byte c = text.ptr[i];
        if (c != '_') {
            acc = acc * 10 + (int64_t)c - (int64_t)'0';
        }
        i++;
    }
    return acc;
}

// a Byte token's text is the already-decoded octet (the lexer resolved it).
tk_byte tk_lit_byte(tk_str text) {
    return text.ptr[0];
}
