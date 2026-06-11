#include "unity.h"
#include "../src/lexer.h"
#include "../src/lexer_string.h"

void test_string_raw_interpolation_and_arity(void) {
    const char* src1 = "$`{{{{{123}}}}\\}\\n`b3";
    Lexer lex;
    lexer_init(&lex, src1);
    ExtendedStringToken ext_token = lex_advanced_string(&lex);
    TEST_ASSERT_EQUAL_INT(TOKEN_STRING_RAW_INTERP, ext_token.type);
    TEST_ASSERT_EQUAL_INT(3, ext_token.bracket_arity);
    TEST_ASSERT_TRUE(ext_token.is_raw);
    free_extended_string_token(&ext_token);
}

const char* get_string_token_type_name(int type) {
    switch (type) {
        case TOKEN_STRING_LIT:          return "STRING_LITERAL";
        case TOKEN_STRING_INTERPOLATED: return "STRING_INTERPOLATED";
        case TOKEN_STRING_RAW_LIT:      return "STRING_RAW_LITERAL";
        case TOKEN_STRING_RAW_INTERP:   return "STRING_RAW_INTERPOLATED";
        default:                        return "UNKNOWN_STRING_TOKEN";
    }
}