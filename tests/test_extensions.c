#include "unity.h"
#include "../src/lexer.h"
#include "../src/parser_extensions.h"
#include "../src/parser_types.h"
#include <string.h>

// Covers parse_type_extension: the receiver binding, a normal extension method,
// and an inline operator overload. Module was previously untested.
void test_extensions_methods_and_operator_overload_parsing(void) {
    const char* src =
        "extend(self: str) { "
        "append_line(line: str) : str { return self; } "
        "operator +(other: str) : str => self; "
        "}";
    Lexer lex; lexer_init(&lex, src);
    Parser parser; parser_init(&parser, &lex);

    ExtensionASTNode* node = parse_type_extension(&parser);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT(NODE_TYPE_EXTENSION, node->type);
    TEST_ASSERT_EQUAL_STRING("self", node->self_param_name);
    TEST_ASSERT_NOT_NULL(node->self_type);

    // Two members: one method, one operator overload
    TEST_ASSERT_EQUAL_INT(2, node->member_count);

    TEST_ASSERT_EQUAL_INT(NODE_EXTENSION_METHOD, node->members[0].type);
    TEST_ASSERT_EQUAL_STRING("append_line", node->members[0].name);

    TEST_ASSERT_EQUAL_INT(NODE_EXTENSION_OPERATOR, node->members[1].type);
    TEST_ASSERT_EQUAL_STRING("+", node->members[1].name);
    TEST_ASSERT_TRUE(node->members[1].is_inline);

    free_extension_ast_node(node);
}
