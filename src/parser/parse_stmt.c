// src/parser/parse_stmt.c   (namespace 'teko::parser')
//
// Statement + block parsing, the C23 mirror of parser/parse_stmt.tks.
#include "parse_stmt.h"
#include "parse_expr.h"      // tk_parse_expr
#include "parse_type.h"      // tk_parse_type
#include "parse_pattern.h"   // parse_field_names (destructure target)
#include "cursor.h"          // tk_has_token, tk_is_kind_at, tk_is_sep, tk_skip_seps
#include "optokens.h"        // tk_is_assign_op
#include "ast.h"             // tk_stmts_push

// file-local forward declarations (defined below; tk_parse_statement dispatches to them)
static tk_parsed_stmt_result parse_binding(const tk_token *t, size_t n, size_t pos);
static tk_parsed_stmt_result parse_assign (const tk_token *t, size_t n, size_t pos);

tk_parsed_stmt_result tk_parse_statement(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) {
        return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_error_make("expected a statement") };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_RETURN)) {
        size_t after = pos + 1;
        if (!tk_has_token(t, n, after) || tk_is_sep(t, n, after) || tk_is_kind_at(t, n, after, TK_TOKEN_RBRACE)) {
            tk_expr zero = { .tag = TK_EXPR_NUMBER, .as.number = { .value = 0 } };
            tk_statement s = { .tag = TK_STMT_RETURN, .as.ret = { .has_value = false, .value = zero } };
            return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = after } };
        }
        tk_parsed_result v = tk_parse_expr(t, n, after);
        if (!v.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error }; }
        tk_statement s = { .tag = TK_STMT_RETURN, .as.ret = { .has_value = true, .value = v.as.value.node } };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LOOP)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE)) {
            return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_error_make("expected '{' after `loop`") };
        }
        tk_parsed_block_result blk = tk_parse_block(t, n, pos + 1);
        if (!blk.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = blk.as.error }; }
        tk_statement s = { .tag = TK_STMT_LOOP, .as.loop_stmt = { .body = blk.as.value.statements, .nbody = blk.as.value.n } };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = blk.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_BREAK)) {
        tk_statement s = { .tag = TK_STMT_BREAK };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = pos + 1 } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_CONTINUE)) {
        tk_statement s = { .tag = TK_STMT_CONTINUE };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = pos + 1 } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LET) || tk_is_kind_at(t, n, pos, TK_TOKEN_MUT) || tk_is_kind_at(t, n, pos, TK_TOKEN_CONST)) {
        return parse_binding(t, n, pos);
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT) && tk_is_assign_op(t, n, pos + 1)) {
        return parse_assign(t, n, pos);
    }
    tk_parsed_result e = tk_parse_expr(t, n, pos);
    if (!e.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = e.as.error }; }
    tk_statement s = { .tag = TK_STMT_EXPR, .as.expr_stmt = { .expr = e.as.value.node } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = e.as.value.next } };
}

tk_parsed_block_result tk_parse_block(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);   // consume `{`, skip leading separators
    tk_statement *stmts = NULL; size_t ns = 0;
    for (;;) {
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        tk_parsed_stmt_result s = tk_parse_statement(t, n, p);
        if (!s.ok) { return (tk_parsed_block_result){ .ok = false, .as.error = s.as.error }; }
        tk_stmts_push(&stmts, &ns, s.as.value.node);
        p = s.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_block_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a statement") };
        }
        p = tk_skip_seps(t, n, p);
    }
    return (tk_parsed_block_result){ .ok = true, .as.value = { .statements = stmts, .n = ns, .next = p + 1 } };   // consume `}`
}

tk_type_expr no_type(void) {
    return (tk_type_expr){ .tag = TK_TEXPR_NAMED, .as.named = { .path = { .segments = NULL, .len = 0 } } };
}

static tk_annotation_result parse_annotation(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_COLON)) {
        return (tk_annotation_result){ .ok = true, .as.value = { .has_type = false, .type_ann = no_type(), .next = pos } };
    }
    tk_parsed_type_result ty = tk_parse_type(t, n, pos + 1);
    if (!ty.ok) { return (tk_annotation_result){ .ok = false, .as.error = ty.as.error }; }
    return (tk_annotation_result){ .ok = true, .as.value = { .has_type = true, .type_ann = ty.as.value.node, .next = ty.as.value.next } };
}

static tk_parsed_target_result parse_bind_target(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LBRACE)) {
        tk_parsed_names_result names = parse_field_names(t, n, pos);
        if (!names.ok) { return (tk_parsed_target_result){ .ok = false, .as.error = names.as.error }; }
        tk_bind_target tgt = { .tag = TK_BIND_DESTRUCTURE, .as.destructure = { .names = names.as.value.names, .nnames = names.as.value.n_names } };
        return (tk_parsed_target_result){ .ok = true, .as.value = { .node = tgt, .next = names.as.value.next } };
    }
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT)) {
        return (tk_parsed_target_result){ .ok = false, .as.error = tk_error_make("expected a name or `{ … }` after `let`/`mut`/`const`") };
    }
    tk_bind_target tgt = { .tag = TK_BIND_SIMPLE, .as.simple = { .name = t[pos].text } };
    return (tk_parsed_target_result){ .ok = true, .as.value = { .node = tgt, .next = pos + 1 } };
}

static tk_parsed_stmt_result parse_binding(const tk_token *t, size_t n, size_t pos) {
    tk_bind_kind kind = TK_BIND_LET;
    if (t[pos].kind == TK_TOKEN_MUT)   { kind = TK_BIND_MUT; }
    if (t[pos].kind == TK_TOKEN_CONST) { kind = TK_BIND_CONST; }
    tk_parsed_target_result tgt = parse_bind_target(t, n, pos + 1);
    if (!tgt.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = tgt.as.error }; }
    tk_annotation_result ann = parse_annotation(t, n, tgt.as.value.next);
    if (!ann.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = ann.as.error }; }
    if (!tk_is_kind_at(t, n, ann.as.value.next, TK_TOKEN_ASSIGN)) {
        return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_error_make("expected '=' in a binding") };
    }
    tk_parsed_result v = tk_parse_expr(t, n, ann.as.value.next + 1);
    if (!v.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error }; }
    tk_statement s = { .tag = TK_STMT_BINDING, .as.binding = { .kind = kind, .target = tgt.as.value.node,
        .has_type = ann.as.value.has_type, .type_ann = ann.as.value.type_ann, .value = v.as.value.node } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
}

static tk_parsed_stmt_result parse_assign(const tk_token *t, size_t n, size_t pos) {
    tk_str name = t[pos].text;
    tk_token_kind op = t[pos + 1].kind;
    tk_parsed_result v = tk_parse_expr(t, n, pos + 2);
    if (!v.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error }; }
    tk_statement s = { .tag = TK_STMT_ASSIGN, .as.assign = { .name = name, .op = op, .value = v.as.value.node } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
}
