#include "unity.h"
#include "../src/parser_statements.h"
#include "../src/parser_types.h"
#include "../src/semantic_concurrent.h"
#include <string.h>
#include <stdlib.h>

void test_concurrency_and_channel_semantics(void) {
    StatementASTNode illegal_var;
    illegal_var.type = NODE_VAR_DECL;
    illegal_var.data.var_decl.is_mutable = false;
    illegal_var.data.var_decl.var_name = strdup("ch");

    TypeInfo type_info;
    type_info.base_name = strdup("chan");
    illegal_var.data.var_decl.var_type = &type_info;

    const ConcurrentValidationResult res_decl = validate_concurrency_variable_creation(&illegal_var);
    TEST_ASSERT_EQUAL_INT(CONC_ERR_INVALID_DECLARATION, res_decl.error_type);
    TEST_ASSERT_NOT_NULL(res_decl.error_message);

    free_concurrent_validation_result(res_decl);
    free(illegal_var.data.var_decl.var_name);
    free(type_info.base_name);

    const ConcurrentValidationResult res_method_bad = validate_channel_method_access("invalidMethod");
    TEST_ASSERT_EQUAL_INT(CONC_ERR_ILLEGAL_METHOD, res_method_bad.error_type);
    free_concurrent_validation_result(res_method_bad);

    const ConcurrentValidationResult res_method_good = validate_channel_method_access("put");
    TEST_ASSERT_EQUAL_INT(CONC_ERR_NONE, res_method_good.error_type);
    free_concurrent_validation_result(res_method_good);
}

void test_concurrency_methods_validation(void) {
    ConcurrentValidationResult res_lock = validate_channel_method_access("lock");
    TEST_ASSERT_EQUAL_INT(CONC_ERR_NONE, res_lock.error_type);
    free_concurrent_validation_result(res_lock);

    ConcurrentValidationResult res_wait = validate_channel_method_access("wait");
    TEST_ASSERT_EQUAL_INT(CONC_ERR_NONE, res_wait.error_type);
    free_concurrent_validation_result(res_wait);

    ConcurrentValidationResult res_bad = validate_channel_method_access("clearAll");
    TEST_ASSERT_EQUAL_INT(CONC_ERR_ILLEGAL_METHOD, res_bad.error_type);
    free_concurrent_validation_result(res_bad);
}