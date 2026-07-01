// src/parser/parser.h   (namespace 'teko::parser')
//
// Umbrella header for the parser's public entry points. The parser was split into one
// C pair per Teko .tks file (cursor, optokens, parse_path, parse_lit, parse_type,
// parse_pattern, parse_expr, parse_if, parse_match, parse_arm, parse_stmt, parse_decl,
// parse_file). This header re-exports the public entries so consumers (the driver) can
// `#include "parser/parser.h"` and reach the whole front end as before.
#ifndef TK_PARSER_PARSER_H
#define TK_PARSER_PARSER_H

#include "ast.h"
#include "result.h"

#include "parse_type.h"    // tk_parse_type
#include "parse_expr.h"    // tk_parse_expr
#include "parse_pattern.h" // tk_parse_pattern
#include "parse_arm.h"     // tk_parse_arm
#include "parse_stmt.h"    // tk_parse_statement, tk_parse_block
#include "parse_decl.h"    // tk_parse_function, tk_parse_type_decl, tk_parse_module
#include "parse_file.h"    // tk_parse_main_file

#endif // TK_PARSER_PARSER_H
