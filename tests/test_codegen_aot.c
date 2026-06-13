#include "unity.h"
#include "../src/codegen_aot.h"
#include "../src/parser_statements.h"
#include "../src/parser_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Covers the AOT C-transpilation backend: header/runtime emission plus a
// variable-declaration statement. Module was previously untested.
void test_codegen_aot_c_transpilation_emits_runtime_and_statements(void) {
    const char* out_path = "teko_aot_codegen_test_output.c";

    AOTContext* ctx = codegen_aot_create(out_path);
    TEST_ASSERT_NOT_NULL(ctx);

    codegen_aot_emit_header(ctx, "DemoProject");

    // Hand-build a `let counter: i32 = 42;` statement node and transpile it.
    char vtype[] = "i32";
    char vname[] = "counter";
    char vinit[] = "42";
    TypeInfo ti = {0};
    ti.base_name = vtype;
    StatementASTNode stmt = {0};
    stmt.type = NODE_VAR_DECL;
    stmt.data.var_decl.var_name = vname;
    stmt.data.var_decl.var_type = &ti;
    stmt.data.var_decl.initializer_raw = vinit;
    codegen_aot_emit_statement(ctx, &stmt);

    codegen_aot_close(ctx);

    // Read the generated C file back and assert the expected transpilation.
    FILE* f = fopen(out_path, "r");
    TEST_ASSERT_NOT_NULL(f);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    remove(out_path);

    TEST_ASSERT_NOT_NULL(strstr(buf, "AOT Backend"));            // generated-by banner
    TEST_ASSERT_NOT_NULL(strstr(buf, "#include <stdio.h>"));     // stable C headers
    TEST_ASSERT_NOT_NULL(strstr(buf, "TekoArena main_arena"));   // embedded O(1) arena runtime
    TEST_ASSERT_NOT_NULL(strstr(buf, "int main("));              // program entry point
    TEST_ASSERT_NOT_NULL(strstr(buf, "DemoProject"));            // project name interpolated
    TEST_ASSERT_NOT_NULL(strstr(buf, "int32_t counter = 42;"));  // i32 var transpiled to int32_t
    TEST_ASSERT_NOT_NULL(strstr(buf, "return 0;"));              // closing
}
