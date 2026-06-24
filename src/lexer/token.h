// token.h — tk_token_kind enum: append last (do not shift the operator ordinals).
TK_TOKEN_TO    // `to` — the cast operator (x to T); preserves→ok / loses→error (E7)
TK_TOKEN_DOT   // `.`  — postfix field/method access (P2/F2)

// lexer.c — tk_keyword_kind(text): add the `to` row, beside `as`.
if (tk_str_eq(text, "to")) return TK_TOKEN_TO;

// lexer.c — tk_read_symbol: a lone `.` (after the `..=`/`..` checks) → TK_TOKEN_DOT.
case '.': return tk_sym(src, pos, 1, TK_TOKEN_DOT);
