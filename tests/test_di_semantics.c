#include "unity.h"
#include "parser_di.h"
#include <string.h>
#include <stdlib.h>

void test_di_lifetime_arena_assignment(void) {
    HandlerDependency dep;
    dep.dep_type = strdup("IEmailSender");
    dep.dep_name = strdup("sender");

    dep.lifetime = LIFETIME_TRANSIENT;

    TEST_ASSERT_EQUAL_INT(LIFETIME_TRANSIENT, dep.lifetime);
    TEST_ASSERT_EQUAL_STRING("IEmailSender", dep.dep_type);
    TEST_ASSERT_EQUAL_STRING("sender", dep.dep_name);

    free(dep.dep_type);
    free(dep.dep_name);
}