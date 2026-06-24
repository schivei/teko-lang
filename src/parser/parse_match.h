// src/parser/parse_match.h   (namespace 'teko::parser')
//
// `match` parsing, the C23 mirror of parser/parse_match.tks. match IS an expression
// (B.15); parse_atom (parse_expr) dispatches here, so parse_match is extern. parse_arms
// stays file-local in parse_match.c.
#ifndef TK_PARSER_PARSE_MATCH_H
#define TK_PARSER_PARSE_MATCH_H

#include "result.h"   // tk_parsed_result
#include "../lexer/token.h"

#include <stddef.h>

tk_parsed_result parse_match(const tk_token *t, size_t n, size_t pos);

#endif // TK_PARSER_PARSE_MATCH_H
