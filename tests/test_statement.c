#include "unity.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/parser_statements.h"

void test_parser_recovery_on_syntax_error(void) {
    const char* broken_src = "for mut i: i32; i < 10; i++) { let ; }";
    Lexer lex;
    lexer_init(&lex, broken_src);
    Parser parser;
    parser_init(&parser, &lex);
    const auto node = parse_statement(&parser);
    if (node) {
        free_statement_ast_node(node);
        TEST_FAIL_MESSAGE("node are not null");
    }

    TEST_ASSERT_NULL(node);
}
