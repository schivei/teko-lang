// src/parser/parser.h — the parser FUNCTION prototypes (the public entry points).
//
// B0c part 2 (next lote): complete the parser function bodies — transcribe from
// src/parser/*.tks. This header + parser.c are NOT yet in CMakeLists; parser.c is
// EXPECTED to be incomplete / not-yet-compiling until the body transcription lands.
#ifndef TK_PARSER_PARSER_H
#define TK_PARSER_PARSER_H

#include "ast.h"
#include "result.h"

// public parser entry points (bodies in parser.c — next lote)
tk_parsed_type_result      tk_parse_type(const tk_token *t, size_t n, size_t pos);
tk_parsed_result           tk_parse_expr(const tk_token *t, size_t n, size_t pos);
tk_parsed_pattern_result   tk_parse_pattern(const tk_token *t, size_t n, size_t pos);
tk_parsed_arm_result       tk_parse_arm(const tk_token *t, size_t n, size_t pos);
tk_parsed_stmt_result      tk_parse_statement(const tk_token *t, size_t n, size_t pos);
tk_parsed_block_result     tk_parse_block(const tk_token *t, size_t n, size_t pos);
tk_parsed_decl_result      tk_parse_function(const tk_token *t, size_t n, size_t pos);
tk_parsed_decl_result      tk_parse_type_decl(const tk_token *t, size_t n, size_t pos);
tk_parsed_main_file_result tk_parse_main_file(const tk_token *t, size_t n, size_t pos);
tk_parsed_module_result    tk_parse_module(const tk_token *t, size_t n, size_t pos);

#endif // TK_PARSER_PARSER_H
