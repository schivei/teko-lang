// src/parser/parse_pattern.h   (namespace 'teko::parser')
//
// Pattern parsing, the C23 mirror of parser/parse_pattern.tks. tk_parse_pattern is the
// public entry; parse_field_names (a `{ f; g }` name list) is shared — bindings
// (parse_stmt) and enum/destructure bodies (parse_decl) reuse it. parse_pattern_primary
// / parse_pattern_range stay file-local in parse_pattern.c.
#ifndef TK_PARSER_PARSE_PATTERN_H
#define TK_PARSER_PARSE_PATTERN_H

#include "result.h"   // tk_parsed_pattern_result, tk_parsed_names_result
#include "../lexer/token.h"

#include <stddef.h>

tk_parsed_pattern_result tk_parse_pattern  (const tk_token *t, size_t n, size_t pos);
tk_parsed_names_result   parse_field_names (const tk_token *t, size_t n, size_t pos);  // `{ f; g }` name list

#endif // TK_PARSER_PARSE_PATTERN_H
