#include "unity.h"
#include "../src/lexer.h"
#include "../src/parser_generics.h"
#include <string.h>

// Covers parse_generic_parameters_decl and parse_generic_constraints_where.
// Module was previously untested.
void test_generics_type_parameters_and_where_constraints(void) {
    // 1. Type parameter list: <T, U> yields two named parameters
    {
        const char* src = "<T, U>";
        Lexer lex; lexer_init(&lex, src);
        Parser parser; parser_init(&parser, &lex);
        GenericFunctionSignature sig = {0};
        parse_generic_parameters_decl(&parser, &sig);
        TEST_ASSERT_EQUAL_INT(2, sig.type_parameter_count);
        TEST_ASSERT_EQUAL_STRING("T", sig.type_parameters[0]);
        TEST_ASSERT_EQUAL_STRING("U", sig.type_parameters[1]);
        free_generic_function_signature_members(&sig);
    }

    // 2. A 'where' clause captures the constraint bound (e.g. 'struct')
    {
        const char* src = "where T : struct {";
        Lexer lex; lexer_init(&lex, src);
        Parser parser; parser_init(&parser, &lex);
        GenericFunctionSignature sig = {0};
        parse_generic_constraints_where(&parser, &sig);
        TEST_ASSERT_EQUAL_INT(1, sig.constraint_count);
        TEST_ASSERT_NOT_NULL(sig.constraints[0].constraint_bound);
        TEST_ASSERT_EQUAL_STRING("struct", sig.constraints[0].constraint_bound);
        free_generic_function_signature_members(&sig);
    }

    // 3. No type-parameter list present: count stays zero, no allocation leak path
    {
        const char* src = "identifier_not_lt";
        Lexer lex; lexer_init(&lex, src);
        Parser parser; parser_init(&parser, &lex);
        GenericFunctionSignature sig = {0};
        parse_generic_parameters_decl(&parser, &sig);
        TEST_ASSERT_EQUAL_INT(0, sig.type_parameter_count);
        free_generic_function_signature_members(&sig);
    }
}
