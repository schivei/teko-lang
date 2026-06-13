#include "unity.h"
#include "../src/lexer.h"
#include "../src/parser_visibility.h"

void test_global_visibility_modifiers_and_service_restriction(void) {
    const char* src_pub = "pub struct TestStruct { name: str; }";
    Lexer lex;
    lexer_init(&lex, src_pub);
    Parser parser;
    parser_init(&parser, &lex);
    VisibilityASTNode* const vis_node = parse_global_declaration_with_visibility(&parser);
    TEST_ASSERT_NOT_NULL(vis_node);
    TEST_ASSERT_EQUAL_INT(VIS_PROJECT_PUB, vis_node->visibility);
    free_visibility_ast_node(vis_node);

    const char* src_exp_service = "exp service EmailSender : IEmailSender { }";
    lexer_init(&lex, src_exp_service);
    parser_init(&parser, &lex);
    VisibilityASTNode* const invalid_node = parse_global_declaration_with_visibility(&parser);
    if (invalid_node) {
        free_visibility_ast_node(invalid_node);
        TEST_FAIL_MESSAGE("node is not null");
    }
    TEST_ASSERT_NULL(invalid_node);
}