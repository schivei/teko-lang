#include "unity.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/parser_async_control.h"

void test_async_control_flow_and_raised_catch(void) {
    const char* src = "return exs switch { _ => await calculate_code<u8>(p); } raised (err) { return 3; };";
    Lexer lex;
    lexer_init(&lex, src);
    Parser parser;
    parser_init(&parser, &lex);
    AsyncControlASTNode* node = parse_async_control_statement(&parser);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT(NODE_RAISED_CATCH, node->type);
    free_async_control_ast_node(node);
}