// src/parser/cursor.c   (namespace 'teko::parser')
//
// Token-cursor predicates, the C23 mirror of parser/cursor.tks. The C cursor carries
// the token slice explicitly as (t, n, pos).
//
// NOTE: cursor.tks also declares `kind_at`, `expect`, and `skip_terminators`; the C
// seed never realized those (the parser uses is_kind_at + is_sep/skip_seps instead),
// so they have no C body here — not invented (faithful redistribution only).
#include "cursor.h"

bool tk_has_token(const tk_token *t, size_t n, size_t pos) {
    (void)t;
    return pos < n;
}

bool tk_is_kind_at(const tk_token *t, size_t n, size_t pos, tk_token_kind k) {
    if (!tk_has_token(t, n, pos)) { return false; }
    return t[pos].kind == k;
}

bool tk_is_sep(const tk_token *t, size_t n, size_t pos) {
    return tk_is_kind_at(t, n, pos, TK_TOKEN_SEMICOLON) || tk_is_kind_at(t, n, pos, TK_TOKEN_NEWLINE);
}

size_t tk_skip_seps(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos;
    for (;;) {
        if (!tk_is_sep(t, n, p)) { break; }
        p += 1;
    }
    return p;
}
