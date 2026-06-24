// src/parser/parse_arm.c   (namespace 'teko::parser')
//
// Match-arm parsing, the C23 mirror of parser/parse_arm.tks.
#include "parse_arm.h"
#include "parse_pattern.h"   // tk_parse_pattern
#include "parse_expr.h"      // tk_parse_expr
#include "cursor.h"          // tk_is_kind_at

static tk_guard_result parse_guard(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_WHEN)) {
        tk_expr zero = { .tag = TK_EXPR_NUMBER, .as.number = { .value = 0 } };   // placeholder, gated by has_when
        return (tk_guard_result){ .ok = true, .as.value = { .has_when = false, .guard = zero, .next = pos } };
    }
    tk_parsed_result g = tk_parse_expr(t, n, pos + 1);
    if (!g.ok) { return (tk_guard_result){ .ok = false, .as.error = g.as.error }; }
    return (tk_guard_result){ .ok = true, .as.value = { .has_when = true, .guard = g.as.value.node, .next = g.as.value.next } };
}

tk_parsed_arm_result tk_parse_arm(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_pattern_result pat = tk_parse_pattern(t, n, pos);
    if (!pat.ok) { return (tk_parsed_arm_result){ .ok = false, .as.error = pat.as.error }; }
    tk_guard_result g = parse_guard(t, n, pat.as.value.next);
    if (!g.ok) { return (tk_parsed_arm_result){ .ok = false, .as.error = g.as.error }; }
    if (!tk_is_kind_at(t, n, g.as.value.next, TK_TOKEN_FATARROW)) {
        return (tk_parsed_arm_result){ .ok = false, .as.error = tk_error_make("expected '=>' after a match pattern") };
    }
    tk_parsed_result body = tk_parse_expr(t, n, g.as.value.next + 1);
    if (!body.ok) { return (tk_parsed_arm_result){ .ok = false, .as.error = body.as.error }; }
    tk_arm arm = { .pattern = pat.as.value.node, .has_when = g.as.value.has_when, .guard = g.as.value.guard, .body = body.as.value.node };
    return (tk_parsed_arm_result){ .ok = true, .as.value = { .node = arm, .next = body.as.value.next } };
}
