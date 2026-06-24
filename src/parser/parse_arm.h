// src/parser/parse_arm.h   (namespace 'teko::parser')
//
// Match-arm parsing, the C23 mirror of parser/parse_arm.tks. tk_parse_arm is the public
// entry (parse_arms in parse_match calls it); parse_guard stays file-local.
#ifndef TK_PARSER_PARSE_ARM_H
#define TK_PARSER_PARSE_ARM_H

#include "result.h"   // tk_parsed_arm_result
#include "../lexer/token.h"

#include <stddef.h>

tk_parsed_arm_result tk_parse_arm(const tk_token *t, size_t n, size_t pos);

#endif // TK_PARSER_PARSE_ARM_H
