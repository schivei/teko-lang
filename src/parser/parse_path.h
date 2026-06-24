// src/parser/parse_path.h   (namespace 'teko::parser')
//
// Path parsing, the C23 mirror of parser/parse_path.tks. `parse_path` is shared across
// the parser (types, expressions, patterns, uses all read a path), so it is extern.
#ifndef TK_PARSER_PARSE_PATH_H
#define TK_PARSER_PARSE_PATH_H

#include "result.h"   // tk_parsed_path_result
#include "../lexer/token.h"

#include <stddef.h>

tk_parsed_path_result parse_path(const tk_token *t, size_t n, size_t pos);

#endif // TK_PARSER_PARSE_PATH_H
