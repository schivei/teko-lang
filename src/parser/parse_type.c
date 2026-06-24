// src/parser/parse_type.c   (namespace 'teko::parser')
//
// Type-expression parsing, the C23 mirror of parser/parse_type.tks.
#include "parse_type.h"
#include "parse_path.h"   // parse_path
#include "cursor.h"       // tk_is_kind_at
#include "ast.h"          // tk_box_type, tk_types_push

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

tk_parsed_type_result parse_type_primary(const tk_token *t, size_t n, size_t pos) {
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
