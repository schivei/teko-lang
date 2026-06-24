// src/parser/parse_if.c   (namespace 'teko::parser')
//
// `if`/`else` parsing, the C23 mirror of parser/parse_if.tks.
#include "parse_if.h"
#include "parse_expr.h"   // tk_parse_expr
#include "parse_stmt.h"   // tk_parse_block
#include "cursor.h"       // tk_is_kind_at
#include "ast.h"          // tk_box_expr, tk_stmts_push

tk_parsed_result parse_if(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result cond = tk_parse_expr(t, n, pos + 1);
    if (!cond.ok) { return cond; }
    if (!tk_is_kind_at(t, n, cond.as.value.next, TK_TOKEN_LBRACE)) {
        return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected '{' after the `if` condition") };
    }
    tk_parsed_block_result tb = tk_parse_block(t, n, cond.as.value.next);
    if (!tb.ok) { return (tk_parsed_result){ .ok = false, .as.error = tb.as.error }; }
    size_t p = tb.as.value.next;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_ELSE)) {
        tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node),
            .then_blk = tb.as.value.statements, .nthen = tb.as.value.n,
            .has_else = false, .else_blk = NULL, .nelse = 0 } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = p } };
    }
    // `else if …` — the else branch is another if-expression, wrapped as one statement.
    if (tk_is_kind_at(t, n, p + 1, TK_TOKEN_IF)) {
        tk_parsed_result elif = parse_if(t, n, p + 1);
        if (!elif.ok) { return elif; }
        tk_statement *eb = NULL; size_t neb = 0;
        tk_statement es = { .tag = TK_STMT_EXPR, .as.expr_stmt = { .expr = elif.as.value.node } };
        tk_stmts_push(&eb, &neb, es);
        tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node),
            .then_blk = tb.as.value.statements, .nthen = tb.as.value.n,
            .has_else = true, .else_blk = eb, .nelse = neb } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = elif.as.value.next } };
    }
    // `else { … }`
    if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_LBRACE)) {
        return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected '{' or `if` after `else`") };
    }
    tk_parsed_block_result ebk = tk_parse_block(t, n, p + 1);
    if (!ebk.ok) { return (tk_parsed_result){ .ok = false, .as.error = ebk.as.error }; }
    tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node),
        .then_blk = tb.as.value.statements, .nthen = tb.as.value.n,
        .has_else = true, .else_blk = ebk.as.value.statements, .nelse = ebk.as.value.n } };
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = ebk.as.value.next } };
}
