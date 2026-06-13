#include "unity.h"
#include "../src/lexer.h"
#include "../src/parser_ffi.h"
#include <string.h>

// Covers parse_extern_declaration across its three shapes (struct / fn / block),
// plus parse_complex_type and the from/as suffixes. Module was previously untested.
void test_ffi_extern_struct_function_and_block_parsing(void) {
    // 1. extern struct with two typed fields, bound to a dynamic library
    {
        const char* src = "extern struct ExternalStructure { name: str; code: u8; } from \"my.dylib\";";
        Lexer lex; lexer_init(&lex, src);
        Parser parser; parser_init(&parser, &lex);
        FFIASTNode* node = parse_extern_declaration(&parser);
        TEST_ASSERT_NOT_NULL(node);
        TEST_ASSERT_EQUAL_INT(NODE_FFI_STRUCT, node->type);
        TEST_ASSERT_EQUAL_STRING("ExternalStructure", node->data.ffi_struct.struct_name);
        TEST_ASSERT_EQUAL_INT(2, node->data.ffi_struct.field_count);
        TEST_ASSERT_EQUAL_STRING("name", node->data.ffi_struct.fields[0].field_name);
        TEST_ASSERT_EQUAL_STRING("str", node->data.ffi_struct.fields[0].field_type);
        TEST_ASSERT_EQUAL_STRING("code", node->data.ffi_struct.fields[1].field_name);
        TEST_ASSERT_EQUAL_STRING("u8", node->data.ffi_struct.fields[1].field_type);
        TEST_ASSERT_NOT_NULL(node->from_lib);
        TEST_ASSERT_NOT_NULL(strstr(node->from_lib, "my.dylib"));
        free_ffi_ast_node(node);
    }

    // 2. extern fn with a return type and an 'as' alias
    {
        const char* src = "extern fn get_my() : i32 from \"my.dylib\" as \"GetMy\";";
        Lexer lex; lexer_init(&lex, src);
        Parser parser; parser_init(&parser, &lex);
        FFIASTNode* node = parse_extern_declaration(&parser);
        TEST_ASSERT_NOT_NULL(node);
        TEST_ASSERT_EQUAL_INT(NODE_FFI_FUNCTION, node->type);
        TEST_ASSERT_EQUAL_STRING("get_my", node->data.ffi_function.fn_name);
        TEST_ASSERT_EQUAL_STRING("i32", node->data.ffi_function.return_type);
        TEST_ASSERT_EQUAL_INT(0, node->data.ffi_function.param_count);
        TEST_ASSERT_NOT_NULL(node->data.ffi_function.alias);
        TEST_ASSERT_NOT_NULL(strstr(node->data.ffi_function.alias, "GetMy"));
        free_ffi_ast_node(node);
    }

    // 3. extern block with two functions; first takes a nested pointer-generic parameter
    {
        const char* src = "extern { fn hello(p: ptr<ExternalStructure>) : ptr<void>; fn make() : ptr<ExternalStructure>; } from \"another.so\";";
        Lexer lex; lexer_init(&lex, src);
        Parser parser; parser_init(&parser, &lex);
        FFIASTNode* node = parse_extern_declaration(&parser);
        TEST_ASSERT_NOT_NULL(node);
        TEST_ASSERT_EQUAL_INT(NODE_FFI_BLOCK, node->type);
        TEST_ASSERT_EQUAL_INT(2, node->data.ffi_block.function_count);
        FFIASTNode* fn0 = node->data.ffi_block.functions[0];
        TEST_ASSERT_EQUAL_STRING("hello", fn0->data.ffi_function.fn_name);
        TEST_ASSERT_EQUAL_INT(1, fn0->data.ffi_function.param_count);
        TEST_ASSERT_EQUAL_STRING("p", fn0->data.ffi_function.params[0].param_name);
        TEST_ASSERT_NOT_NULL(strstr(fn0->data.ffi_function.params[0].param_type, "ptr<ExternalStructure>"));
        TEST_ASSERT_NOT_NULL(node->from_lib);
        TEST_ASSERT_NOT_NULL(strstr(node->from_lib, "another.so"));
        free_ffi_ast_node(node);
    }
}
