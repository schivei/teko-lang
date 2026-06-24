// src/parser/parse_expr.c   (namespace 'teko::parser')
//
// Expression parsing — the full precedence ladder, the C23 mirror of
// parser/parse_expr.tks. Same productions, precedence, associativity, and error
// messages as the Teko source. Recursive children are heap-boxed (tk_box_expr).
#include "parse_expr.h"
#include "parse_if.h"      // parse_if    (called by parse_atom)
#include "parse_match.h"   // parse_match (called by parse_atom)
#include "parse_path.h"    // parse_path
#include "parse_type.h"    // parse_type_primary (cast target)
#include "parse_lit.h"     // tk_lit_int, tk_lit_byte
#include "cursor.h"        // tk_has_token, tk_is_kind_at
#include "optokens.h"      // tk_is_unary, tk_is_shift, …
#include "ast.h"           // tk_box_expr, tk_exprs_push, tk_terms_push

static tk_parsed_args_result parse_call_args(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos + 1;                                  // consume `(`
    tk_expr *args = NULL; size_t na = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN)) {
        return (tk_parsed_args_result){ .ok = true, .as.value = { .args = args, .n_args = 0, .next = p + 1 } };
    }
    for (;;) {
        tk_parsed_result a = tk_parse_expr(t, n, p);
        if (!a.ok) { return (tk_parsed_args_result){ .ok = false, .as.error = a.as.error }; }
        tk_exprs_push(&args, &na, a.as.value.node);
        p = a.as.value.next;
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) { break; }
        p += 1;                                          // consume `,`
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN)) {
        return (tk_parsed_args_result){ .ok = false, .as.error = tk_error_make("expected ')' to close the argument list") };
    }
    return (tk_parsed_args_result){ .ok = true, .as.value = { .args = args, .n_args = na, .next = p + 1 } };
}

static tk_parsed_result parse_atom(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) {
        return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected an expression") };
    }
    tk_token_kind k = t[pos].kind;
    if (k == TK_TOKEN_NUMBER) {
        tk_expr e = { .tag = TK_EXPR_NUMBER, .as.number = { .value = tk_lit_int(t[pos].text) } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_STR) {
        tk_expr e = { .tag = TK_EXPR_STR, .as.str = { .text = t[pos].text } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_BYTE) {
        tk_expr e = { .tag = TK_EXPR_BYTE, .as.byte = { .value = tk_lit_byte(t[pos].text) } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_LPAREN) {
        tk_parsed_result in = tk_parse_expr(t, n, pos + 1);
        if (!in.ok) { return in; }
        if (!tk_is_kind_at(t, n, in.as.value.next, TK_TOKEN_RPAREN)) {
            return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected ')' to close a parenthesized expression") };
        }
        return (tk_parsed_result){ .ok = true, .as.value = { .node = in.as.value.node, .next = in.as.value.next + 1 } };
    }
    if (k == TK_TOKEN_IF)    { return parse_if(t, n, pos); }
    if (k == TK_TOKEN_MATCH) { return parse_match(t, n, pos); }
    if (k == TK_TOKEN_IDENT) {
        tk_parsed_path_result pp = parse_path(t, n, pos);
        if (!pp.ok) { return (tk_parsed_result){ .ok = false, .as.error = pp.as.error }; }
        if (tk_is_kind_at(t, n, pp.as.value.next, TK_TOKEN_LPAREN)) {
            tk_parsed_args_result ca = parse_call_args(t, n, pp.as.value.next);
            if (!ca.ok) { return (tk_parsed_result){ .ok = false, .as.error = ca.as.error }; }
            tk_expr e = { .tag = TK_EXPR_CALL, .as.call = { .callee = pp.as.value.node,
                .args = ca.as.value.args, .nargs = ca.as.value.n_args } };
            return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = ca.as.value.next } };
        }
        if (pp.as.value.node.len == 1) {
            tk_expr e = { .tag = TK_EXPR_VAR, .as.var = { .name = pp.as.value.node.segments[0].name } };
            return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pp.as.value.next } };
        }
        tk_expr e = { .tag = TK_EXPR_PATH, .as.path = { .path = pp.as.value.node } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pp.as.value.next } };
    }
    return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected an expression") };
}

static tk_parsed_result parse_postfix(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result prim = parse_atom(t, n, pos);
    if (!prim.ok) { return prim; }
    tk_expr node = prim.as.value.node;
    size_t p = prim.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_DOT)) {
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_IDENT)) {
            return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected a field or method name after '.'") };
        }
        tk_str name = t[p + 1].text;
        if (tk_is_kind_at(t, n, p + 2, TK_TOKEN_LPAREN)) {
            tk_parsed_args_result ca = parse_call_args(t, n, p + 2);
            if (!ca.ok) { return (tk_parsed_result){ .ok = false, .as.error = ca.as.error }; }
            tk_expr m = { .tag = TK_EXPR_METHOD_CALL, .as.method_call = { .receiver = tk_box_expr(node),
                .method = name, .args = ca.as.value.args, .nargs = ca.as.value.n_args } };
            node = m; p = ca.as.value.next;
        } else {
            tk_expr f = { .tag = TK_EXPR_FIELD_ACCESS, .as.field_access = { .receiver = tk_box_expr(node), .field = name } };
            node = f; p = p + 2;
        }
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_unary(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_unary(t, n, pos)) {
        tk_token_kind op = t[pos].kind;
        tk_parsed_result o = parse_unary(t, n, pos + 1);
        if (!o.ok) { return o; }
        tk_expr e = { .tag = TK_EXPR_UNARY, .as.unary = { .op = op, .operand = tk_box_expr(o.as.value.node) } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = o.as.value.next } };
    }
    return parse_postfix(t, n, pos);
}

static tk_parsed_result parse_cast(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_unary(t, n, pos);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_TO)) {
        tk_parsed_type_result ty = parse_type_primary(t, n, p + 1);   // target is a type-PRIMARY
        if (!ty.ok) { return (tk_parsed_result){ .ok = false, .as.error = ty.as.error }; }
        tk_expr c = { .tag = TK_EXPR_CAST, .as.cast = { .expr = tk_box_expr(node), .target = ty.as.value.node } };
        node = c; p = ty.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_shift(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_cast(t, n, pos);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_shift(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result rhs = parse_cast(t, n, p + 1);
        if (!rhs.ok) { return rhs; }
        tk_expr b = { .tag = TK_EXPR_BINARY, .as.binary = { .op = op, .left = tk_box_expr(node), .right = tk_box_expr(rhs.as.value.node) } };
        node = b; p = rhs.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_multiplicative(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_shift(t, n, pos);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_multiplicative(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result rhs = parse_shift(t, n, p + 1);
        if (!rhs.ok) { return rhs; }
        tk_expr b = { .tag = TK_EXPR_BINARY, .as.binary = { .op = op, .left = tk_box_expr(node), .right = tk_box_expr(rhs.as.value.node) } };
        node = b; p = rhs.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_additive(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_multiplicative(t, n, pos);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_additive(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result rhs = parse_multiplicative(t, n, p + 1);
        if (!rhs.ok) { return rhs; }
        tk_expr b = { .tag = TK_EXPR_BINARY, .as.binary = { .op = op, .left = tk_box_expr(node), .right = tk_box_expr(rhs.as.value.node) } };
        node = b; p = rhs.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_comparison(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_additive(t, n, pos);
    if (!first.ok) { return first; }
    tk_cmp_term *terms = NULL; size_t nt = 0;
    size_t p = first.as.value.next;
    while (tk_is_comparison(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result rhs = parse_additive(t, n, p + 1);
        if (!rhs.ok) { return rhs; }
        tk_terms_push(&terms, &nt, (tk_cmp_term){ .op = op, .operand = tk_box_expr(rhs.as.value.node) });
        p = rhs.as.value.next;
    }
    if (nt == 0) {
        return (tk_parsed_result){ .ok = true, .as.value = { .node = first.as.value.node, .next = p } };
    }
    tk_expr e = { .tag = TK_EXPR_COMPARE, .as.compare = { .first = tk_box_expr(first.as.value.node), .rest = terms, .nrest = nt } };
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = p } };
}

static tk_parsed_result parse_and(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_comparison(t, n, pos);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_andand(t, n, p)) {
        tk_parsed_result rhs = parse_comparison(t, n, p + 1);
        if (!rhs.ok) { return rhs; }
        tk_expr b = { .tag = TK_EXPR_BINARY, .as.binary = { .op = TK_TOKEN_ANDAND, .left = tk_box_expr(node), .right = tk_box_expr(rhs.as.value.node) } };
        node = b; p = rhs.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_or(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_and(t, n, pos);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_oror(t, n, p)) {
        tk_parsed_result rhs = parse_and(t, n, p + 1);
        if (!rhs.ok) { return rhs; }
        tk_expr b = { .tag = TK_EXPR_BINARY, .as.binary = { .op = TK_TOKEN_OROR, .left = tk_box_expr(node), .right = tk_box_expr(rhs.as.value.node) } };
        node = b; p = rhs.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

tk_parsed_result tk_parse_expr(const tk_token *t, size_t n, size_t pos) {
    return parse_or(t, n, pos);   // descends the whole ladder
}
