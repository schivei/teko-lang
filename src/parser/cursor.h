// src/parser/cursor.h   (namespace 'teko::parser')
//
// Token-cursor predicate DECLARATIONS, the C23 mirror of parser/cursor.tks. The C
// cursor carries the token slice explicitly as (t, n, pos): a tk_token pointer, its
// length, and the current index.
#ifndef TK_PARSER_CURSOR_H
#define TK_PARSER_CURSOR_H

#include "../lexer/token.h"   // tk_token, tk_token_kind

#include <stdbool.h>
#include <stddef.h>           // size_t

bool   tk_has_token (const tk_token *t, size_t n, size_t pos);                  // is there a token at pos?
bool   tk_is_kind_at(const tk_token *t, size_t n, size_t pos, tk_token_kind k); // has_token + kind compare
bool   tk_is_sep    (const tk_token *t, size_t n, size_t pos);                  // `;` or a newline (B.17)
size_t tk_skip_seps (const tk_token *t, size_t n, size_t pos);                  // skip a run of separators

#endif // TK_PARSER_CURSOR_H
