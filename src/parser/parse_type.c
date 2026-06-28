// src/parser/parse_type.c   (namespace 'teko::parser')
//
// Type-expression parsing, the C23 mirror of parser/parse_type.tks.
#include "parse_type.h"
#include "parse_path.h"   // parse_path
#include "cursor.h"       // tk_is_kind_at, tk_skip_seps
#include "ast.h"          // tk_box_type, tk_types_push

// A named type, optionally with generic type-ARGUMENTS `<T1, T2, …>` (S4 type-generics). The `<…>`
// is parsed SPECULATIVELY and backtracks if it is not a closed list — the cast target is a type-
// primary (`x to i64 < y`), so a bare `<` must stay available as the comparison operator. Single-
// level only for now (a compound close `>>`/`>=`/`>>=` backtracks — nested type-args land later).
static tk_parsed_type_result parse_named(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_path_result pp = parse_path(t, n, pos);
    if (!pp.ok) { return (tk_parsed_type_result){ .ok = false, .as.error = pp.as.error }; }
    tk_type_expr plain_ty = { .tag = TK_TEXPR_NAMED, .as.named = { .path = pp.as.value.node } };  // args NULL,0
    tk_parsed_type_result plain = (tk_parsed_type_result){ .ok = true,
        .as.value = { .node = plain_ty, .next = pp.as.value.next } };
    if (!tk_is_kind_at(t, n, pp.as.value.next, TK_TOKEN_LT)) { return plain; }
    tk_type_expr *args = NULL; size_t nargs = 0;
    size_t p = pp.as.value.next + 1;
    for (;;) {
        tk_parsed_type_result a = tk_parse_type(t, n, p);
        if (!a.ok) { return plain; }   // not a type → backtrack
        tk_types_push(&args, &nargs, a.as.value.node);
        p = a.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) { p += 1; continue; }
        if (tk_is_kind_at(t, n, p, TK_TOKEN_GT)) {
            tk_type_expr ty = { .tag = TK_TEXPR_NAMED,
                .as.named = { .path = pp.as.value.node, .args = args, .args_len = nargs } };
            return (tk_parsed_type_result){ .ok = true, .as.value = { .node = ty, .next = p + 1 } };
        }
        return plain;   // no closing `>` (or a compound close) → backtrack
    }
}

static tk_parsed_type_result parse_slice(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_RBRACKET)) {
        return (tk_parsed_type_result){ .ok = false,
            .as.error = tk_err_at(t, n, pos + 1, "expected ']' to close '[' in a slice type '[]T'") };
    }
    tk_parsed_type_result e = parse_type_primary(t, n, pos + 2);
    if (!e.ok) { return e; }
    tk_type_expr *elem = tk_box_type(e.as.value.node);   // compiler-managed indirection
    tk_type_expr ty = { .tag = TK_TEXPR_SLICE, .as.slice = { .element = elem } };
    return (tk_parsed_type_result){ .ok = true,
        .as.value = { .node = ty, .next = e.as.value.next } };
}

tk_parsed_type_result parse_type_primary(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_type_result base =
        tk_is_kind_at(t, n, pos, TK_TOKEN_LBRACKET) ? parse_slice(t, n, pos)
                                                    : parse_named(t, n, pos);
    if (!base.ok) { return base; }
    // postfix `?` → an OPTIONAL type (T?  — REBOOT_PLAN §202; nullability only).
    // Binds tighter than `|`, so `T? | U` is `(T?) | U`; doubled `T??` nests (T?)?.
    size_t p = base.as.value.next;
    tk_type_expr node = base.as.value.node;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_QUESTION)) {
        tk_type_expr *inner = tk_box_type(node);
        node = (tk_type_expr){ .tag = TK_TEXPR_OPTIONAL, .as.optional = { .inner = inner } };
        p += 1;
    }
    return (tk_parsed_type_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

tk_parsed_type_result tk_parse_type(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_type_result first = parse_type_primary(t, n, pos);
    if (!first.ok) { return first; }
    tk_type_expr *members = NULL; size_t nm = 0;
    tk_types_push(&members, &nm, first.as.value.node);
    size_t p = first.as.value.next;
    // A `|` may be led by a newline (MULTI-LINE type union, e.g. a wide `variant` over many
    // lines); separators may also follow the `|`. We only advance `p` once a `|` is found, so a
    // trailing newline that ENDS the type (no continuation `|`) is left intact for the caller.
    for (;;) {
        size_t q = tk_skip_seps(t, n, p);
        if (!tk_is_kind_at(t, n, q, TK_TOKEN_PIPE)) { break; }
        size_t mp = tk_skip_seps(t, n, q + 1);
        tk_parsed_type_result m = parse_type_primary(t, n, mp);
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
