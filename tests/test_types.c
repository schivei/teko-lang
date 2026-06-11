#include "unity.h"
#include "../src/lexer.h"
#include "../src/parser_types.h"

void test_virtual_namespace_expansion_at_sign(void) {
    const char* src_macro = "@marshall.from_ptr";
    Lexer lex;
    lexer_init(&lex, src_macro);
    Parser parser;
    parser_init(&parser, &lex);

    TypeInfo* info = parse_complete_type_info(&parser);

    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_STRING("teko::marshall.from_ptr", info->base_name);

    free_type_info(info);
}

void test_reserved_namespace_protection(void) {
    const char* src_illegal = "use teko::marshall;";
    Lexer lex;
    lexer_init(&lex, src_illegal);
    Parser parser;
    parser_init(&parser, &lex);

    parser.is_stdlib_compilation = false;
    const auto illegal_node = parse_use_statement(&parser);
    if (illegal_node) {
        free_ast_node(illegal_node);
        TEST_FAIL_MESSAGE("node is not null");
    }
    TEST_ASSERT_NULL(illegal_node);

    lexer_init(&lex, src_illegal);
    parser_init(&parser, &lex);
    parser.is_stdlib_compilation = true;

    const auto legal_node = parse_use_statement(&parser);
    if (!legal_node) {
        TEST_FAIL_MESSAGE("node is null");
    }
    TEST_ASSERT_NOT_NULL(legal_node);
    TEST_ASSERT_EQUAL_STRING("teko::marshall", legal_node->data.use_stmt.path);
    free_ast_node(legal_node);
}
