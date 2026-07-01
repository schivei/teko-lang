// src/parser/optokens.h   (namespace 'teko::parser')
//
// Operator-class token predicates, the C23 mirror of parser/optokens.tks — one
// predicate per precedence level + the assignment-op set.
#ifndef TK_PARSER_OPTOKENS_H
#define TK_PARSER_OPTOKENS_H

#include "../lexer/token.h"   // tk_token, tk_token_kind

#include <stdbool.h>
#include <stddef.h>           // size_t

bool tk_is_unary         (const tk_token *t, size_t n, size_t pos);  // - ~ !            (level 2)
bool tk_is_shift         (const tk_token *t, size_t n, size_t pos);  // << >>            (level 3)
bool tk_is_multiplicative(const tk_token *t, size_t n, size_t pos);  // * / % &          (level 4)
bool tk_is_additive      (const tk_token *t, size_t n, size_t pos);  // + - | ^          (level 5)
bool tk_is_comparison    (const tk_token *t, size_t n, size_t pos);  // < > <= >= == !=  (level 6)
bool tk_is_andand        (const tk_token *t, size_t n, size_t pos);  // &&               (level 7)
bool tk_is_oror          (const tk_token *t, size_t n, size_t pos);  // ||               (level 8)
bool tk_is_assign_op     (const tk_token *t, size_t n, size_t pos);  // = += -= … (B.4 — statement-only)

#endif // TK_PARSER_OPTOKENS_H
