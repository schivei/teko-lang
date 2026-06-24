// src/parser/parse_lit.h   (namespace 'teko::parser')
//
// Literal decoders, the C23 mirror of parser/parse_lit.tks. RELOCATED from the lexer
// (legislator decision): these belong to the parser namespace in Teko, so their C pair
// now lives here. The parser (parse_expr / parse_pattern) includes this header.
#ifndef TK_PARSER_PARSE_LIT_H
#define TK_PARSER_PARSE_LIT_H

#include "../text/text.h"   // tk_str, tk_byte

#include <stdint.h>         // int64_t

// A Number token's text (decimal digits with `_` separators) → i64.
int64_t tk_lit_int(tk_str text);
// A Byte token's text is the already-decoded octet (the lexer resolved it).
tk_byte tk_lit_byte(tk_str text);

#endif // TK_PARSER_PARSE_LIT_H
