#include "unity.h"
#include "../src/lexer.h"
#include "../src/parser_messaging.h"

void test_cqrs_handler_with_dependency_injection(void) {
    const char* src = "handler for MyNotifyData { handle(notify) with (IEmailSender sender) : intent<void> => return sender.send_notification(notify.message); }";
    Lexer lex;
    lexer_init(&lex, src);
    Parser parser;
    parser_init(&parser, &lex);
    MessagingASTNode* const node = parse_messaging_handler(&parser);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_STRING("MyNotifyData", node->name);
    free_messaging_ast_node(node);
}
