#include "../core/teko_core.h"

#include <stdio.h>
#include <string.h>

static TekoSource source_from_text(const char *path,
                                   const char *type_name,
                                   TekoSourceKind kind,
                                   const char *text) {
    TekoSource source;
    source.path = teko_string_from_cstr(path);
    source.type_name = teko_string_from_cstr(type_name);
    source.kind = kind;
    source.text = teko_string_from_cstr(text);
    return source;
}

static int output_contains(const TekoCompileResult *result, const char *needle) {
    size_t i;
    for (i = 0; i < result->output_count; i++) {
        if (result->outputs[i].text.data &&
            strstr(result->outputs[i].text.data, needle)) {
            return 1;
        }
    }
    return 0;
}

static int diagnostics_contain(const TekoCompileResult *result, const char *needle) {
    size_t i;
    for (i = 0; i < result->diagnostic_count; i++) {
        if (result->diagnostics[i].message.data &&
            strstr(result->diagnostics[i].message.data, needle)) {
            return 1;
        }
    }
    return 0;
}

static int expect_success(const char *name,
                          const TekoSource *sources,
                          size_t source_count,
                          const char *expected_output) {
    TekoCompileResult result;
    int ok;
    teko_compile_sources(sources, source_count, 0, &result);
    ok = result.success && output_contains(&result, expected_output);
    if (!ok) {
        fprintf(stderr, "FAIL %s: expected successful compile containing '%s'\n", name, expected_output);
        if (result.diagnostic_count) {
            size_t i;
            for (i = 0; i < result.diagnostic_count; i++) {
                fprintf(stderr, "  diagnostic: %.*s\n",
                        (int)result.diagnostics[i].message.length,
                        result.diagnostics[i].message.data);
            }
        }
    }
    teko_free_compile_result(0, &result);
    return ok;
}

static int expect_failure(const char *name,
                          const char *text,
                          const char *expected_diagnostic,
                          unsigned expected_line,
                          unsigned expected_column) {
    TekoSource source = source_from_text("test.teko", "test", TEKO_SOURCE_TEKO, text);
    TekoCompileResult result;
    int ok;
    teko_compile_sources(&source, 1, 0, &result);
    ok = !result.success && diagnostics_contain(&result, expected_diagnostic);
    if (ok && expected_line && expected_column) {
        size_t i;
        int found_location = 0;
        for (i = 0; i < result.diagnostic_count; i++) {
            if (result.diagnostics[i].message.data &&
                strstr(result.diagnostics[i].message.data, expected_diagnostic) &&
                result.diagnostics[i].line == expected_line &&
                result.diagnostics[i].column == expected_column) {
                found_location = 1;
            }
        }
        ok = found_location;
    }
    if (!ok) {
        fprintf(stderr, "FAIL %s: expected diagnostic containing '%s' at %u:%u\n",
                name, expected_diagnostic, expected_line, expected_column);
        fprintf(stderr, "  success=%d diagnostics=%lu\n", result.success, (unsigned long)result.diagnostic_count);
        {
            size_t i;
            for (i = 0; i < result.diagnostic_count; i++) {
                fprintf(stderr, "  diagnostic: %.*s\n",
                        (int)result.diagnostics[i].message.length,
                        result.diagnostics[i].message.data);
            }
        }
    }
    teko_free_compile_result(0, &result);
    return ok;
}

static int test_core0_success(void) {
    TekoSource sources[3];
    sources[0] = source_from_text("Point.struct.teko", "Point", TEKO_SOURCE_STRUCT, "X : int\nY : int\n");
    sources[1] = source_from_text("TokenKind.enum.teko", "TokenKind", TEKO_SOURCE_ENUM, "Eof\nIdentifier\n");
    sources[2] = source_from_text("main.teko", "main", TEKO_SOURCE_TEKO,
                                  "fn add(a: int, b: int) -> int {\n"
                                  "    return a + b\n"
                                  "}\n"
                                  "\n"
                                  "fn main() -> int {\n"
                                  "    let value: int = add(20, 22)\n"
                                  "    if value == 42 {\n"
                                  "        return 0\n"
                                  "    } else {\n"
                                  "        return 1\n"
                                  "    }\n"
                                  "}\n");
    return expect_success("core0_success", sources, 3, "int32_t add");
}

static int test_unknown_identifier(void) {
    return expect_failure("unknown_identifier",
                          "fn main() -> int {\n"
                          "    return missing\n"
                          "}\n",
                          "unknown identifier",
                          2,
                          12);
}

static int test_unknown_type(void) {
    return expect_failure("unknown_type",
                          "fn main() -> MissingType {\n"
                          "    return 0\n"
                          "}\n",
                          "unknown type",
                          1,
                          14);
}

static int test_call_arity(void) {
    return expect_failure("call_arity",
                          "fn add(a: int, b: int) -> int {\n"
                          "    return a + b\n"
                          "}\n"
                          "fn main() -> int {\n"
                          "    return add(1)\n"
                          "}\n",
                          "function call arity mismatch",
                          5,
                          12);
}

static int test_void_return_value(void) {
    return expect_failure("void_return_value",
                          "fn main() -> void {\n"
                          "    return 1\n"
                          "}\n",
                          "void function cannot return a value",
                          2,
                          5);
}

static int test_local_requires_type(void) {
    return expect_failure("local_requires_type",
                          "fn main() -> int {\n"
                          "    let value = 1\n"
                          "    return value\n"
                          "}\n",
                          "Core 0 locals require an explicit type",
                          2,
                          5);
}

static int test_return_type_mismatch(void) {
    return expect_failure("return_type_mismatch",
                          "fn main() -> int {\n"
                          "    return \"nope\"\n"
                          "}\n",
                          "return value type mismatch",
                          2,
                          5);
}

static int test_local_initializer_type_mismatch(void) {
    return expect_failure("local_initializer_type_mismatch",
                          "fn main() -> int {\n"
                          "    let value: int = \"x\"\n"
                          "    return value\n"
                          "}\n",
                          "local initializer type mismatch",
                          2,
                          5);
}

static int test_argument_type_mismatch(void) {
    return expect_failure("argument_type_mismatch",
                          "fn add(a: int, b: int) -> int {\n"
                          "    return a + b\n"
                          "}\n"
                          "fn main() -> int {\n"
                          "    return add(\"x\", 2)\n"
                          "}\n",
                          "function argument type mismatch",
                          5,
                          12);
}

static int test_if_condition_must_be_bool(void) {
    return expect_failure("if_condition_must_be_bool",
                          "fn main() -> int {\n"
                          "    if 1 {\n"
                          "        return 0\n"
                          "    }\n"
                          "    return 1\n"
                          "}\n",
                          "if condition must be bool",
                          2,
                          5);
}

static int test_arithmetic_type_mismatch(void) {
    return expect_failure("arithmetic_type_mismatch",
                          "fn main() -> int {\n"
                          "    return 1 + \"x\"\n"
                          "}\n",
                          "binary arithmetic operands must have the same numeric type",
                          2,
                          14);
}

static int test_unsupported_source_kind(void) {
    TekoSource source = source_from_text("Sample.class.teko",
                                         "Sample",
                                         teko_source_kind_from_path(teko_string_from_cstr("Sample.class.teko")),
                                         "type is transient;\n");
    TekoCompileResult result;
    int ok;
    teko_compile_sources(&source, 1, 0, &result);
    ok = !result.success && diagnostics_contain(&result, "unsupported Teko source kind");
    if (!ok) {
        fprintf(stderr, "FAIL unsupported_source_kind: expected unsupported Teko source kind\n");
    }
    teko_free_compile_result(0, &result);
    return ok;
}

int main(void) {
    int ok = 1;
    ok = test_core0_success() && ok;
    ok = test_unknown_identifier() && ok;
    ok = test_unknown_type() && ok;
    ok = test_call_arity() && ok;
    ok = test_void_return_value() && ok;
    ok = test_local_requires_type() && ok;
    ok = test_return_type_mismatch() && ok;
    ok = test_local_initializer_type_mismatch() && ok;
    ok = test_argument_type_mismatch() && ok;
    ok = test_if_condition_must_be_bool() && ok;
    ok = test_arithmetic_type_mismatch() && ok;
    ok = test_unsupported_source_kind() && ok;
    if (ok) {
        printf("core tests passed\n");
        return 0;
    }
    return 1;
}
