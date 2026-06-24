// src/parser/parser.c — the parser FUNCTION BODIES, the C23 mirror of the canonical
// Teko parser (src/parser/*.tks). B0c part 2 (path-to-first-binary).
//
// This file completes the parser: the 12 list-append helpers + the 2 box helpers
// (ast.h prototypes), the token-cursor predicates (result.h prototypes — cursor.tks /
// optokens.tks), and every parse_* function body (the precedence ladder, patterns,
// arms, statements, if/match, declarations, and the file-level entries).
//
// The transcription is faithful to the Teko algorithm: same productions, same
// precedence/associativity, same error messages, same `next`-cursor threading.
// Recursive children are heap-boxed (tk_box_expr / tk_box_type) — the compiler-managed
// indirection of the Teko side. Field/tag names are reconciled against the AST headers
// (ast.h / result.h) and the canonical consumers (src/checker/*, src/emit/*), which
// are the code that must compile.

#include "parser.h"
#include "../lexer/lexer.h"   // tk_lit_int / tk_lit_byte (the literal decoders — reuse, do NOT redefine)

#include <string.h>   // memcpy — for the box helpers

// ===================================================================
// List-append helpers (ast.h prototypes). `(T** ptr, size_t* n, T x)` grow-append,
// mirroring teko::list::push (CONSUMES + RETURNS; allocation failure PANICS — M.5).
// Here the (ptr, n) pair is grown in place; the doubling-with-floor-8 capacity policy
// matches TK_LIST in core.h. A separate `cap` is not tracked: each push reallocs to the
// exact `n + 1`, which is simpler and the seed never frees these arenas (B0).
// ===================================================================
#define TK_PUSH_BODY(T)                                          \
    do {                                                         \
        size_t m = *n;                                           \
        T *np = realloc(*xs, (m + 1) * sizeof(T));               \
        if (np == NULL) { abort(); }                            \
        np[m] = item;                                            \
        *xs = np;                                                \
        *n  = m + 1;                                             \
    } while (0)

void tk_exprs_push (tk_expr **xs,      size_t *n, tk_expr      item) { TK_PUSH_BODY(tk_expr); }
void tk_stmts_push (tk_statement **xs, size_t *n, tk_statement item) { TK_PUSH_BODY(tk_statement); }
void tk_pats_push  (tk_pattern **xs,   size_t *n, tk_pattern   item) { TK_PUSH_BODY(tk_pattern); }
void tk_arms_push  (tk_arm **xs,       size_t *n, tk_arm       item) { TK_PUSH_BODY(tk_arm); }
void tk_params_push(tk_param **xs,     size_t *n, tk_param     item) { TK_PUSH_BODY(tk_param); }
void tk_fields_push(tk_field **xs,     size_t *n, tk_field     item) { TK_PUSH_BODY(tk_field); }
void tk_segs_push  (tk_segment **xs,   size_t *n, tk_segment   item) { TK_PUSH_BODY(tk_segment); }
void tk_strs_push  (tk_str **xs,       size_t *n, tk_str       item) { TK_PUSH_BODY(tk_str); }
void tk_types_push (tk_type_expr **xs, size_t *n, tk_type_expr item) { TK_PUSH_BODY(tk_type_expr); }
void tk_terms_push (tk_cmp_term **xs,  size_t *n, tk_cmp_term  item) { TK_PUSH_BODY(tk_cmp_term); }
void tk_decls_push (tk_decl **xs,      size_t *n, tk_decl      item) { TK_PUSH_BODY(tk_decl); }
void tk_uses_push  (tk_use_decl **xs,  size_t *n, tk_use_decl  item) { TK_PUSH_BODY(tk_use_decl); }

#undef TK_PUSH_BODY

// box helpers — heap a node so a recursive parent can point at it (the compiler-managed
// indirection of the Teko side). malloc + copy; allocation failure PANICS (M.5).
tk_expr *tk_box_expr(tk_expr e) {
    tk_expr *p = malloc(sizeof(tk_expr));
    if (p == NULL) { abort(); }
    memcpy(p, &e, sizeof(tk_expr));
    return p;
}
tk_type_expr *tk_box_type(tk_type_expr t) {
    tk_type_expr *p = malloc(sizeof(tk_type_expr));
    if (p == NULL) { abort(); }
    memcpy(p, &t, sizeof(tk_type_expr));
    return p;
}

// ===================================================================
// Token-cursor predicates (cursor.tks / optokens.tks). The C cursor carries the slice
// explicitly as (t, n, pos): a tk_token pointer, its length, and the current index.
// ===================================================================
bool tk_has_token(const tk_token *t, size_t n, size_t pos) {
    (void)t;
    return pos < n;
}

bool tk_is_kind_at(const tk_token *t, size_t n, size_t pos, tk_token_kind k) {
    if (!tk_has_token(t, n, pos)) { return false; }
    return t[pos].kind == k;
}

bool tk_is_sep(const tk_token *t, size_t n, size_t pos) {
    return tk_is_kind_at(t, n, pos, TK_TOKEN_SEMICOLON) || tk_is_kind_at(t, n, pos, TK_TOKEN_NEWLINE);
}

size_t tk_skip_seps(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos;
    for (;;) {
        if (!tk_is_sep(t, n, p)) { break; }
        p += 1;
    }
    return p;
}

bool tk_is_unary(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_MINUS || k == TK_TOKEN_TILDE || k == TK_TOKEN_BANG;
}

bool tk_is_shift(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_SHL || k == TK_TOKEN_SHR;
}

bool tk_is_multiplicative(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_STAR || k == TK_TOKEN_SLASH || k == TK_TOKEN_PERCENT || k == TK_TOKEN_AMP;
}

bool tk_is_additive(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_PLUS || k == TK_TOKEN_MINUS || k == TK_TOKEN_PIPE || k == TK_TOKEN_CARET;
}

bool tk_is_comparison(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_LT || k == TK_TOKEN_GT || k == TK_TOKEN_LE ||
           k == TK_TOKEN_GE || k == TK_TOKEN_EQEQ || k == TK_TOKEN_NE;
}

bool tk_is_andand(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    return t[pos].kind == TK_TOKEN_ANDAND;
}

bool tk_is_oror(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    return t[pos].kind == TK_TOKEN_OROR;
}

bool tk_is_assign_op(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_ASSIGN ||
           k == TK_TOKEN_PLUSEQ  || k == TK_TOKEN_MINUSEQ ||
           k == TK_TOKEN_STAREQ  || k == TK_TOKEN_SLASHEQ ||
           k == TK_TOKEN_PERCENTEQ ||
           k == TK_TOKEN_AMPEQ   || k == TK_TOKEN_PIPEEQ  ||
           k == TK_TOKEN_CARETEQ ||
           k == TK_TOKEN_SHLEQ   || k == TK_TOKEN_SHREQ;
}

// ===================================================================
// Forward declarations (mutual recursion across the grammar).
// ===================================================================
static tk_parsed_path_result   parse_path(const tk_token *t, size_t n, size_t pos);
static tk_parsed_type_result   parse_type_primary(const tk_token *t, size_t n, size_t pos);

static tk_parsed_result        parse_if(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result        parse_match(const tk_token *t, size_t n, size_t pos);
static tk_parsed_args_result   parse_call_args(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result        parse_atom(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result        parse_postfix(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result        parse_unary(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result        parse_cast(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result        parse_shift(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result        parse_multiplicative(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result        parse_additive(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result        parse_comparison(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result        parse_and(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result        parse_or(const tk_token *t, size_t n, size_t pos);

static tk_parsed_names_result  parse_field_names(const tk_token *t, size_t n, size_t pos);
static tk_parsed_stmt_result   parse_binding(const tk_token *t, size_t n, size_t pos);
static tk_parsed_stmt_result   parse_assign(const tk_token *t, size_t n, size_t pos);
static tk_parsed_target_result parse_bind_target(const tk_token *t, size_t n, size_t pos);

static tk_type_expr            no_type(void);

// ===================================================================
// Paths (parse_path.tks).
// ===================================================================
static tk_parsed_path_result parse_path(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT)) {
        return (tk_parsed_path_result){ .ok = false, .as.error = tk_error_make("expected a name") };
    }
    tk_segment *segs = NULL; size_t ns = 0;
    tk_segs_push(&segs, &ns, (tk_segment){ .name = t[pos].text });
    size_t p = pos + 1;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_COLONCOLON)) {
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_IDENT)) {
            return (tk_parsed_path_result){ .ok = false, .as.error = tk_error_make("expected a name after '::'") };
        }
        tk_segs_push(&segs, &ns, (tk_segment){ .name = t[p + 1].text });
        p += 2;
    }
    return (tk_parsed_path_result){ .ok = true,
        .as.value = { .node = { .segments = segs, .len = ns }, .next = p } };
}

// ===================================================================
// Type expressions (type.tks / parse_type.tks).
// ===================================================================
static tk_parsed_type_result parse_named(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_path_result pp = parse_path(t, n, pos);
    if (!pp.ok) { return (tk_parsed_type_result){ .ok = false, .as.error = pp.as.error }; }
    tk_type_expr ty = { .tag = TK_TEXPR_NAMED, .as.named = { .path = pp.as.value.node } };
    return (tk_parsed_type_result){ .ok = true,
        .as.value = { .node = ty, .next = pp.as.value.next } };
}

static tk_parsed_type_result parse_slice(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_RBRACKET)) {
        return (tk_parsed_type_result){ .ok = false,
            .as.error = tk_error_make("expected ']' to close '[' in a slice type '[]T'") };
    }
    tk_parsed_type_result e = parse_type_primary(t, n, pos + 2);
    if (!e.ok) { return e; }
    tk_type_expr *elem = tk_box_type(e.as.value.node);   // compiler-managed indirection
    tk_type_expr ty = { .tag = TK_TEXPR_SLICE, .as.slice = { .element = elem } };
    return (tk_parsed_type_result){ .ok = true,
        .as.value = { .node = ty, .next = e.as.value.next } };
}

static tk_parsed_type_result parse_type_primary(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LBRACKET)) { return parse_slice(t, n, pos); }
    return parse_named(t, n, pos);
}

tk_parsed_type_result tk_parse_type(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_type_result first = parse_type_primary(t, n, pos);
    if (!first.ok) { return first; }
    tk_type_expr *members = NULL; size_t nm = 0;
    tk_types_push(&members, &nm, first.as.value.node);
    size_t p = first.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_PIPE)) {
        tk_parsed_type_result m = parse_type_primary(t, n, p + 1);
        if (!m.ok) { return m; }
        tk_types_push(&members, &nm, m.as.value.node);
        p = m.as.value.next;
    }
    if (nm == 1) {
        return (tk_parsed_type_result){ .ok = true,
            .as.value = { .node = members[0], .next = p } };
    }
    tk_type_expr ty = { .tag = TK_TEXPR_UNION, .as.uni = { .members = members, .len = nm } };
    return (tk_parsed_type_result){ .ok = true, .as.value = { .node = ty, .next = p } };
}

// ===================================================================
// Expressions (parse_expr.tks) — the full precedence ladder.
// ===================================================================
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

tk_parsed_result tk_parse_expr(const tk_token *t, size_t n, size_t pos) {
    return parse_or(t, n, pos);   // descends the whole ladder
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

// ===================================================================
// Patterns (pattern.tks / parse_pattern.tks).
// ===================================================================
static tk_parsed_pattern_result parse_pattern_primary(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) {
        return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("expected a pattern") };
    }
    tk_token_kind k = t[pos].kind;
    if (k == TK_TOKEN_UNDERSCORE) {
        tk_pattern p = { .tag = TK_PAT_WILDCARD };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_NUMBER) {
        tk_expr v = { .tag = TK_EXPR_NUMBER, .as.number = { .value = tk_lit_int(t[pos].text) } };
        tk_pattern p = { .tag = TK_PAT_LITERAL, .as.literal = { .value = v } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_STR) {
        tk_expr v = { .tag = TK_EXPR_STR, .as.str = { .text = t[pos].text } };
        tk_pattern p = { .tag = TK_PAT_LITERAL, .as.literal = { .value = v } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_BYTE) {
        tk_expr v = { .tag = TK_EXPR_BYTE, .as.byte = { .value = tk_lit_byte(t[pos].text) } };
        tk_pattern p = { .tag = TK_PAT_LITERAL, .as.literal = { .value = v } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_IDENT) {
        tk_parsed_path_result pp = parse_path(t, n, pos);
        if (!pp.ok) { return (tk_parsed_pattern_result){ .ok = false, .as.error = pp.as.error }; }
        size_t after = pp.as.value.next;
        if (tk_is_kind_at(t, n, after, TK_TOKEN_AS)) {
            if (!tk_is_kind_at(t, n, after + 1, TK_TOKEN_IDENT)) {
                return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("expected a name after `as` in a pattern") };
            }
            tk_pattern p = { .tag = TK_PAT_BIND, .as.bind = { .type_name = pp.as.value.node, .has_binding = true, .binding = t[after + 1].text } };
            return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = after + 2 } };
        }
        if (tk_is_kind_at(t, n, after, TK_TOKEN_LBRACE)) {
            tk_parsed_names_result fns = parse_field_names(t, n, after);
            if (!fns.ok) { return (tk_parsed_pattern_result){ .ok = false, .as.error = fns.as.error }; }
            tk_pattern fp = { .tag = TK_PAT_FIELD, .as.field = { .type_name = pp.as.value.node, .fields = fns.as.value.names, .n_fields = fns.as.value.n_names } };
            return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = fp, .next = fns.as.value.next } };
        }
        tk_pattern p = { .tag = TK_PAT_BIND, .as.bind = { .type_name = pp.as.value.node, .has_binding = false, .binding = (tk_str){0} } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = after } };
    }
    return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("expected a pattern") };
}

static tk_parsed_pattern_result parse_pattern_range(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_pattern_result lo = parse_pattern_primary(t, n, pos);
    if (!lo.ok) { return lo; }
    if (!tk_is_kind_at(t, n, lo.as.value.next, TK_TOKEN_DOTDOTEQ)) { return lo; }
    if (lo.as.value.node.tag != TK_PAT_LITERAL) {
        return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("a range bound must be a literal") };
    }
    tk_expr lo_e = lo.as.value.node.as.literal.value;
    tk_parsed_pattern_result hi = parse_pattern_primary(t, n, lo.as.value.next + 1);
    if (!hi.ok) { return hi; }
    if (hi.as.value.node.tag != TK_PAT_LITERAL) {
        return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("a range bound must be a literal") };
    }
    tk_pattern p = { .tag = TK_PAT_RANGE, .as.range = { .lo = lo_e, .hi = hi.as.value.node.as.literal.value } };
    return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = hi.as.value.next } };
}

tk_parsed_pattern_result tk_parse_pattern(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_pattern_result first = parse_pattern_range(t, n, pos);
    if (!first.ok) { return first; }
    tk_pattern *opts = NULL; size_t no = 0;
    tk_pats_push(&opts, &no, first.as.value.node);
    size_t p = first.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_PIPE)) {
        tk_parsed_pattern_result o = parse_pattern_range(t, n, p + 1);
        if (!o.ok) { return o; }
        tk_pats_push(&opts, &no, o.as.value.node);
        p = o.as.value.next;
    }
    if (no == 1) {
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = first.as.value.node, .next = p } };
    }
    tk_pattern alt = { .tag = TK_PAT_ALT, .as.alt = { .options = opts, .n_options = no } };
    return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = alt, .next = p } };
}

// `{ f; g }` — a field-name list (parse_pattern.tks). `pos` is at `{`.
static tk_parsed_names_result parse_field_names(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);   // consume `{`, skip leading separators
    tk_str *names = NULL; size_t nn = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) {
        return (tk_parsed_names_result){ .ok = true, .as.value = { .names = names, .n_names = 0, .next = p + 1 } };
    }
    for (;;) {
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT)) {
            return (tk_parsed_names_result){ .ok = false, .as.error = tk_error_make("expected a field name in `Type { … }`") };
        }
        tk_strs_push(&names, &nn, t[p].text);
        p += 1;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_names_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a field name") };
        }
        p = tk_skip_seps(t, n, p);
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }   // trailing separator
    }
    return (tk_parsed_names_result){ .ok = true, .as.value = { .names = names, .n_names = nn, .next = p + 1 } };   // consume `}`
}

// ===================================================================
// Arms (parse_arm.tks).
// ===================================================================
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

// ===================================================================
// Statements (parse_stmt.tks).
// ===================================================================
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

static tk_type_expr no_type(void) {
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

// ===================================================================
// if / match (parse_if.tks / parse_match.tks).
// ===================================================================
static tk_parsed_result parse_if(const tk_token *t, size_t n, size_t pos) {
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

static tk_parsed_arms_result parse_arms(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);   // consume `{`, skip leading separators
    tk_arm *arms = NULL; size_t na = 0;
    for (;;) {
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        tk_parsed_arm_result a = tk_parse_arm(t, n, p);
        if (!a.ok) { return (tk_parsed_arms_result){ .ok = false, .as.error = a.as.error }; }
        tk_arms_push(&arms, &na, a.as.value.node);
        p = a.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_arms_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a match arm") };
        }
        p = tk_skip_seps(t, n, p);
    }
    if (na == 0) {
        return (tk_parsed_arms_result){ .ok = false, .as.error = tk_error_make("a `match` needs at least one arm") };
    }
    return (tk_parsed_arms_result){ .ok = true, .as.value = { .arms = arms, .n_arms = na, .next = p + 1 } };
}

static tk_parsed_result parse_match(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result subj = tk_parse_expr(t, n, pos + 1);
    if (!subj.ok) { return subj; }
    if (!tk_is_kind_at(t, n, subj.as.value.next, TK_TOKEN_LBRACE)) {
        return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected '{' after the `match` subject") };
    }
    tk_parsed_arms_result arms = parse_arms(t, n, subj.as.value.next);
    if (!arms.ok) { return (tk_parsed_result){ .ok = false, .as.error = arms.as.error }; }
    tk_expr e = { .tag = TK_EXPR_MATCH, .as.match_expr = { .subject = tk_box_expr(subj.as.value.node),
        .arms = arms.as.value.arms, .narms = arms.as.value.n_arms } };
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = arms.as.value.next } };
}

// ===================================================================
// Declarations (parse_decl.tks).
// ===================================================================
static tk_parsed_params_result parse_params(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos + 1;                                  // consume `(`
    tk_param *params = NULL; size_t np = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN)) {
        return (tk_parsed_params_result){ .ok = true, .as.value = { .params = params, .n_params = 0, .next = p + 1 } };
    }
    for (;;) {
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT)) {
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_error_make("expected a parameter name") };
        }
        tk_str name = t[p].text;
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_COLON)) {
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_error_make("expected ':' after a parameter name") };
        }
        tk_parsed_type_result ty = tk_parse_type(t, n, p + 2);
        if (!ty.ok) { return (tk_parsed_params_result){ .ok = false, .as.error = ty.as.error }; }
        tk_params_push(&params, &np, (tk_param){ .name = name, .type_ann = ty.as.value.node });
        p = ty.as.value.next;
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) { break; }
        p += 1;                                          // consume `,`
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN)) {
        return (tk_parsed_params_result){ .ok = false, .as.error = tk_error_make("expected ')' to close the parameter list") };
    }
    return (tk_parsed_params_result){ .ok = true, .as.value = { .params = params, .n_params = np, .next = p + 1 } };
}

tk_parsed_decl_result tk_parse_function(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos;
    bool has_doc = false; tk_str doc = (tk_str){0};
    if (tk_is_kind_at(t, n, p, TK_TOKEN_DOC)) { has_doc = true; doc = t[p].text; p += 1; }
    bool is_exp = false;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_EXP)) { is_exp = true; p += 1; }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_FN)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected `fn`") };
    }
    p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected a function name") };
    }
    tk_str name = t[p].text; p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_LPAREN)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected '(' for the parameter list") };
    }
    tk_parsed_params_result ps = parse_params(t, n, p);
    if (!ps.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = ps.as.error }; }
    p = ps.as.value.next;
    bool has_return = false; tk_type_expr ret = no_type();
    if (tk_is_kind_at(t, n, p, TK_TOKEN_ARROW)) {
        tk_parsed_type_result r = tk_parse_type(t, n, p + 1);
        if (!r.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = r.as.error }; }
        has_return = true; ret = r.as.value.node; p = r.as.value.next;
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_LBRACE)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected '{' for the function body") };
    }
    tk_parsed_block_result blk = tk_parse_block(t, n, p);
    if (!blk.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = blk.as.error }; }
    tk_function f = { .name = name, .params = ps.as.value.params, .nparams = ps.as.value.n_params,
        .has_return = has_return, .return_type = ret,
        .body = blk.as.value.statements, .nbody = blk.as.value.n,
        .is_exp = is_exp, .has_doc = has_doc, .doc = doc };
    tk_decl d = { .tag = TK_DECL_FUNCTION, .as.function = f };
    return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = d, .next = blk.as.value.next } };
}

static tk_parsed_fields_result parse_fields(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);   // consume `{`, skip leading separators
    tk_field *fields = NULL; size_t nf = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) {
        return (tk_parsed_fields_result){ .ok = true, .as.value = { .fields = fields, .n_fields = 0, .next = p + 1 } };
    }
    for (;;) {
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT)) {
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_error_make("expected a field name") };
        }
        tk_str name = t[p].text;
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_COLON)) {
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_error_make("expected ':' after a field name") };
        }
        tk_parsed_type_result ty = tk_parse_type(t, n, p + 2);
        if (!ty.ok) { return (tk_parsed_fields_result){ .ok = false, .as.error = ty.as.error }; }
        tk_fields_push(&fields, &nf, (tk_field){ .name = name, .type_ann = ty.as.value.node });
        p = ty.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a field") };
        }
        p = tk_skip_seps(t, n, p);
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }   // trailing separator
    }
    return (tk_parsed_fields_result){ .ok = true, .as.value = { .fields = fields, .n_fields = nf, .next = p + 1 } };
}

static tk_parsed_body_result parse_type_body(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_STRUCT)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE)) {
            return (tk_parsed_body_result){ .ok = false, .as.error = tk_error_make("expected '{' after `struct`") };
        }
        tk_parsed_fields_result fs = parse_fields(t, n, pos + 1);
        if (!fs.ok) { return (tk_parsed_body_result){ .ok = false, .as.error = fs.as.error }; }
        tk_type_body b = { .tag = TK_BODY_STRUCT, .as.struct_body = { .fields = fs.as.value.fields, .n_fields = fs.as.value.n_fields } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = fs.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_ENUM)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE)) {
            return (tk_parsed_body_result){ .ok = false, .as.error = tk_error_make("expected '{' after `enum`") };
        }
        tk_parsed_names_result ms = parse_field_names(t, n, pos + 1);
        if (!ms.ok) { return (tk_parsed_body_result){ .ok = false, .as.error = ms.as.error }; }
        tk_type_body b = { .tag = TK_BODY_ENUM, .as.enum_body = { .members = ms.as.value.names, .n_members = ms.as.value.n_names } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = ms.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_VARIANT)) {
        tk_parsed_type_result ty = tk_parse_type(t, n, pos + 1);
        if (!ty.ok) { return (tk_parsed_body_result){ .ok = false, .as.error = ty.as.error }; }
        tk_type_body b = { .tag = TK_BODY_VARIANT, .as.variant_body = { .type_expr = ty.as.value.node } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = ty.as.value.next } };
    }
    return (tk_parsed_body_result){ .ok = false, .as.error = tk_error_make("expected `struct`, `enum`, or `variant`") };
}

tk_parsed_decl_result tk_parse_type_decl(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos;
    bool has_doc = false; tk_str doc = (tk_str){0};
    if (tk_is_kind_at(t, n, p, TK_TOKEN_DOC)) { has_doc = true; doc = t[p].text; p += 1; }
    bool is_exp = false;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_EXP)) { is_exp = true; p += 1; }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_TYPE)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected `type`") };
    }
    p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected a type name") };
    }
    tk_str name = t[p].text; p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_ASSIGN)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected '=' in a type declaration") };
    }
    tk_parsed_body_result body = parse_type_body(t, n, p + 1);
    if (!body.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = body.as.error }; }
    tk_type_decl td = { .name = name, .body = body.as.value.node, .is_exp = is_exp, .has_doc = has_doc, .doc = doc };
    tk_decl d = { .tag = TK_DECL_TYPE, .as.type_decl = td };
    return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = d, .next = body.as.value.next } };
}

static tk_parsed_decl_result parse_decl(const tk_token *t, size_t n, size_t pos) {
    size_t k = pos;
    if (tk_is_kind_at(t, n, k, TK_TOKEN_DOC)) { k += 1; }
    if (tk_is_kind_at(t, n, k, TK_TOKEN_EXP)) { k += 1; }
    if (tk_is_kind_at(t, n, k, TK_TOKEN_FN))   { return tk_parse_function(t, n, pos); }
    if (tk_is_kind_at(t, n, k, TK_TOKEN_TYPE)) { return tk_parse_type_decl(t, n, pos); }
    return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected a declaration (`fn`/`type`, optionally `exp`/doc); loose statements belong in main.tks") };
}

// ===================================================================
// File model (parse_file.tks): is_decl_start, use, use-header, main file, module.
// ===================================================================
static bool is_decl_start(const tk_token *t, size_t n, size_t pos) {
    return tk_is_kind_at(t, n, pos, TK_TOKEN_FN) || tk_is_kind_at(t, n, pos, TK_TOKEN_TYPE);
}

static tk_parsed_use_result parse_use(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_path_result pp = parse_path(t, n, pos + 1);
    if (!pp.ok) { return (tk_parsed_use_result){ .ok = false, .as.error = pp.as.error }; }
    bool has_alias = false; tk_str alias = (tk_str){0}; size_t p = pp.as.value.next;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_AS)) {
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_IDENT)) {
            return (tk_parsed_use_result){ .ok = false, .as.error = tk_error_make("expected a name after `as` in a `use`") };
        }
        has_alias = true; alias = t[p + 1].text; p += 2;
    }
    tk_use_decl u = { .path = pp.as.value.node, .has_alias = has_alias, .alias = alias };
    return (tk_parsed_use_result){ .ok = true, .as.value = { .node = u, .next = p } };
}

static tk_parsed_uses_result parse_use_header(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos);
    tk_use_decl *uses = NULL; size_t nu = 0;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_USE)) {
        tk_parsed_use_result u = parse_use(t, n, p);
        if (!u.ok) { return (tk_parsed_uses_result){ .ok = false, .as.error = u.as.error }; }
        tk_uses_push(&uses, &nu, u.as.value.node);
        p = u.as.value.next;
        if (!tk_has_token(t, n, p)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_uses_result){ .ok = false, .as.error = tk_error_make("expected ';' or a newline after a `use`") };
        }
        p = tk_skip_seps(t, n, p);
    }
    return (tk_parsed_uses_result){ .ok = true, .as.value = { .uses = uses, .n_uses = nu, .next = p } };
}

tk_parsed_main_file_result tk_parse_main_file(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_uses_result hdr = parse_use_header(t, n, pos);
    if (!hdr.ok) { return (tk_parsed_main_file_result){ .ok = false, .as.error = hdr.as.error }; }
    size_t p = tk_skip_seps(t, n, hdr.as.value.next);
    tk_statement *body = NULL; size_t nb = 0;
    for (;;) {
        if (!tk_has_token(t, n, p)) { break; }
        if (is_decl_start(t, n, p)) {
            return (tk_parsed_main_file_result){ .ok = false, .as.error = tk_error_make("main.tks is a virtual main: it may not declare types or functions (only `use` + statements)") };
        }
        tk_parsed_stmt_result s = tk_parse_statement(t, n, p);
        if (!s.ok) { return (tk_parsed_main_file_result){ .ok = false, .as.error = s.as.error }; }
        tk_stmts_push(&body, &nb, s.as.value.node);
        p = s.as.value.next;
        if (!tk_has_token(t, n, p)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_main_file_result){ .ok = false, .as.error = tk_error_make("expected ';' or a newline after a statement") };
        }
        p = tk_skip_seps(t, n, p);
    }
    tk_main_file mf = { .uses = hdr.as.value.uses, .n_uses = hdr.as.value.n_uses, .body = body, .n_body = nb };
    return (tk_parsed_main_file_result){ .ok = true, .as.value = { .node = mf, .next = p } };
}

tk_parsed_module_result tk_parse_module(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_uses_result hdr = parse_use_header(t, n, pos);
    if (!hdr.ok) { return (tk_parsed_module_result){ .ok = false, .as.error = hdr.as.error }; }
    size_t p = tk_skip_seps(t, n, hdr.as.value.next);
    tk_decl *decls = NULL; size_t nd = 0;
    for (;;) {
        if (!tk_has_token(t, n, p)) { break; }
        tk_parsed_decl_result d = parse_decl(t, n, p);
        if (!d.ok) { return (tk_parsed_module_result){ .ok = false, .as.error = d.as.error }; }
        tk_decls_push(&decls, &nd, d.as.value.node);
        p = d.as.value.next;
        if (!tk_has_token(t, n, p)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_module_result){ .ok = false, .as.error = tk_error_make("expected ';' or a newline after a declaration") };
        }
        p = tk_skip_seps(t, n, p);
    }
    tk_module m = { .uses = hdr.as.value.uses, .n_uses = hdr.as.value.n_uses, .decls = decls, .n_decls = nd };
    return (tk_parsed_module_result){ .ok = true, .as.value = { .node = m, .next = p } };
}
