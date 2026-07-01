// src/lexer/lexer.h — teko::lexer public interface, the C23 mirror of lexer.tks.
//
// The scanner entry mirrors Teko's `tokenize(source: str) -> []Token | error`.
// The token list is a TK_LIST over tk_token; on success the parser consumes
// `result.value.ptr` / `result.value.len` as its `const tk_token *t, size_t n`.
#ifndef TK_LEXER_LEXER_H
#define TK_LEXER_LEXER_H

#include "../core.h"        // TK_LIST, TK_RESULT, tk_error
#include "../text/text.h"   // tk_str, tk_byte
#include "token.h"          // tk_token, tk_token_kind

// the token list — teko::list realized over tk_token (DAG: lexer → core).
TK_LIST(tk_token, tk_tokens);

// `[]Token | error` — the result of tokenize.
TK_RESULT(tk_tokens, tk_tokens_result);

// tokenize — the main scan loop (lexer.tks `tokenize`). Returns the token list or
// the first lex error.
tk_tokens_result tk_tokenize(tk_str source);

// (The literal decoders lit_int / lit_byte moved to the parser namespace —
//  src/parser/parse_lit.h, mirroring parse_lit.tks.)

#endif // TK_LEXER_LEXER_H
