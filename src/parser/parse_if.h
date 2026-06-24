// src/parser/parse_if.h   (namespace 'teko::parser')
//
// `if`/`else` parsing, the C23 mirror of parser/parse_if.tks. if/else IS an expression
// (B.20); parse_atom (parse_expr) dispatches here, so parse_if is extern.
#ifndef TK_PARSER_PARSE_IF_H
#define TK_PARSER_PARSE_IF_H

#include "result.h"   // tk_parsed_result
#include "../lexer/token.h"

#include <stddef.h>

tk_parsed_result parse_if(const tk_token *t, size_t n, size_t pos);

#endif // TK_PARSER_PARSE_IF_H
