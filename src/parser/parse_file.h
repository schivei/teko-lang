// src/parser/parse_file.h   (namespace 'teko::parser')
//
// File-level parsing, the C23 mirror of parser/parse_file.tks: the `use` header and the
// main-file entry. tk_parse_main_file is the public entry; parse_use_header is shared
// with parse_decl (a module also opens with a `use` header). is_decl_start / parse_use
// stay file-local in parse_file.c.
#ifndef TK_PARSER_PARSE_FILE_H
#define TK_PARSER_PARSE_FILE_H

#include "result.h"   // tk_parsed_main_file_result, tk_parsed_uses_result
#include "../lexer/token.h"

#include <stddef.h>

tk_parsed_main_file_result tk_parse_main_file(const tk_token *t, size_t n, size_t pos);
tk_parsed_uses_result      parse_use_header  (const tk_token *t, size_t n, size_t pos);  // a `use` header (shared with module)

#endif // TK_PARSER_PARSE_FILE_H
