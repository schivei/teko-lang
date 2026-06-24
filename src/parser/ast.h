// parser/ast.h — three new Expr nodes (F2). Recursive fields are pointers.
typedef struct { tk_expr *receiver; tk_str field; }                                tk_field_access; // x.field
typedef struct { tk_expr *receiver; tk_str method; tk_expr *args; size_t n_args; } tk_method_call;  // recv.method(…)
typedef struct { tk_expr *expr; tk_type_expr target; }                             tk_cast;          // x to T

// tk_expr.tag enum — add: TK_EXPR_FIELD_ACCESS, TK_EXPR_METHOD_CALL, TK_EXPR_CAST
// tk_expr.as union — add: tk_field_access field_access; tk_method_call method_call; tk_cast cast;

// --- parser AST (parser/ast.h, type part) ---
typedef struct { tk_str name; } tk_segment;
typedef struct { tk_segment *segments; size_t n; } tk_path;
typedef struct tk_type_expr tk_type_expr;
struct tk_type_expr {
    enum { TK_TYPE_EXPR_NAMED, TK_TYPE_EXPR_SLICE, TK_TYPE_EXPR_UNION } tag;
    union {
        tk_path named;                                      // NamedType
        struct { tk_type_expr *element; } slice;            // SliceType ([]T)
        struct { tk_type_expr *members; size_t n; } onion;  // UnionType (A|B|…)  ('union' is reserved)
    } as;
};

// --- results (parser/result.h) ---
typedef struct { tk_path      node; size_t next; } tk_parsed_path;
typedef struct { tk_type_expr node; size_t next; } tk_parsed_type;
TK_RESULT(tk_parsed_path, tk_parsed_path_result);   // tk_parsed_path | error
TK_RESULT(tk_parsed_type, tk_parsed_type_result);   // tk_parsed_type | error

// forward decls (mutual recursion: primary ↔ slice; type → primary)
static tk_parsed_type_result parse_primary(const tk_token *t, size_t n, size_t pos);

static tk_parsed_path_result parse_path(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT))
        return (tk_parsed_path_result){ .ok = false, .as.error = tk_error_make("expected a name") };
    tk_segment *segs = NULL; size_t ns = 0;
    tk_segs_push(&segs, &ns, (tk_segment){ .name = t[pos].text });
    size_t p = pos + 1;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_COLONCOLON)) {
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_IDENT))
            return (tk_parsed_path_result){ .ok = false, .as.error = tk_error_make("expected a name after '::'") };
        tk_segs_push(&segs, &ns, (tk_segment){ .name = t[p + 1].text });
        p += 2;
    }
    return (tk_parsed_path_result){ .ok = true,
        .as.value = { .node = { .segments = segs, .n = ns }, .next = p } };
}

static tk_parsed_type_result parse_named(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_path_result pp = parse_path(t, n, pos);
    if (!pp.ok) return (tk_parsed_type_result){ .ok = false, .as.error = pp.as.error };
    tk_type_expr ty = { .tag = TK_TYPE_EXPR_NAMED, .as.named = pp.as.value.node };
    return (tk_parsed_type_result){ .ok = true,
        .as.value = { .node = ty, .next = pp.as.value.next } };
}

static tk_parsed_type_result parse_slice(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_RBRACKET))
        return (tk_parsed_type_result){ .ok = false,
            .as.error = tk_error_make("expected ']' to close '[' in a slice type '[]T'") };
    tk_parsed_type_result e = parse_primary(t, n, pos + 2);
    if (!e.ok) return e;
    tk_type_expr *elem = tk_box_type(e.as.value.node);   // compiler-managed indirection
    tk_type_expr ty = { .tag = TK_TYPE_EXPR_SLICE, .as.slice = { .element = elem } };
    return (tk_parsed_type_result){ .ok = true,
        .as.value = { .node = ty, .next = e.as.value.next } };
}

static tk_parsed_type_result parse_primary(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LBRACKET)) return parse_slice(t, n, pos);
    return parse_named(t, n, pos);
}

tk_parsed_type_result tk_parse_type(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_type_result first = parse_primary(t, n, pos);
    if (!first.ok) return first;
    tk_type_expr *members = NULL; size_t nm = 0;
    tk_types_push(&members, &nm, first.as.value.node);
    size_t p = first.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_PIPE)) {
        tk_parsed_type_result m = parse_primary(t, n, p + 1);
        if (!m.ok) return m;
        tk_types_push(&members, &nm, m.as.value.node);
        p = m.as.value.next;
    }
    if (nm == 1) return (tk_parsed_type_result){ .ok = true,
        .as.value = { .node = members[0], .next = p } };
    tk_type_expr ty = { .tag = TK_TYPE_EXPR_UNION, .as.onion = { .members = members, .n = nm } };
    return (tk_parsed_type_result){ .ok = true, .as.value = { .node = ty, .next = p } };
}

// parser/ast.h — one new Expr node: tk_path_expr; tag TK_EXPR_PATH.
typedef struct { tk_path path; } tk_path_expr;   // PrimKind::U32, module statics

// parser/result.h
typedef struct { tk_expr node; size_t next; }                 tk_parsed;
typedef struct { tk_expr *args; size_t n_args; size_t next; } tk_parsed_args;
TK_RESULT(tk_parsed,      tk_parsed_result);
TK_RESULT(tk_parsed_args, tk_parsed_args_result);

// forward decls (atom ↔ expr via paren/args)
tk_parsed_result tk_parse_expr(const tk_token *t, size_t n, size_t pos);

static tk_parsed_args_result parse_call_args(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos + 1;
    tk_expr *args = NULL; size_t na = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN))
        return (tk_parsed_args_result){ .ok = true, .as.value = { .args = args, .n_args = 0, .next = p + 1 } };
    for (;;) {
        tk_parsed_result a = tk_parse_expr(t, n, p);
        if (!a.ok) return (tk_parsed_args_result){ .ok = false, .as.error = a.as.error };
        tk_exprs_push(&args, &na, a.as.value.node); p = a.as.value.next;
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) break;
        p += 1;
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN))
        return (tk_parsed_args_result){ .ok = false, .as.error = tk_error_make("expected ')' to close the argument list") };
    return (tk_parsed_args_result){ .ok = true, .as.value = { .args = args, .n_args = na, .next = p + 1 } };
}

static tk_parsed_result parse_atom(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected an expression") };
    tk_token_kind k = t[pos].kind;
    if (k == TK_TOKEN_NUMBER) { tk_expr e = { .tag = TK_EXPR_NUMBER, .as.number = { .value = tk_lit_int(t[pos].text) } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } }; }
    if (k == TK_TOKEN_STR)    { tk_expr e = { .tag = TK_EXPR_STR,  .as.str  = { .text  = t[pos].text } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } }; }
    if (k == TK_TOKEN_BYTE)   { tk_expr e = { .tag = TK_EXPR_BYTE, .as.byte = { .value = tk_lit_byte(t[pos].text) } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } }; }
    if (k == TK_TOKEN_LPAREN) {
        tk_parsed_result in = tk_parse_expr(t, n, pos + 1);
        if (!in.ok) return in;
        if (!tk_is_kind_at(t, n, in.as.value.next, TK_TOKEN_RPAREN))
            return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected ')' to close a parenthesized expression") };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = in.as.value.node, .next = in.as.value.next + 1 } };
    }
    if (k == TK_TOKEN_IF)    return parse_if(t, n, pos);
    if (k == TK_TOKEN_MATCH) return parse_match(t, n, pos);
    if (k == TK_TOKEN_IDENT) {
        tk_parsed_path_result pp = parse_path(t, n, pos);
        if (!pp.ok) return (tk_parsed_result){ .ok = false, .as.error = pp.as.error };
        size_t after = pp.as.value.next;
        if (tk_is_kind_at(t, n, after, TK_TOKEN_LPAREN)) {
            tk_parsed_args_result ca = parse_call_args(t, n, after);
            if (!ca.ok) return (tk_parsed_result){ .ok = false, .as.error = ca.as.error };
            tk_expr e = { .tag = TK_EXPR_CALL, .as.call = { .callee = pp.as.value.node, .args = ca.as.value.args, .n_args = ca.as.value.n_args } };
            return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = ca.as.value.next } };
        }
        if (pp.as.value.node.n == 1) {
            tk_expr e = { .tag = TK_EXPR_VAR, .as.var = { .name = pp.as.value.node.segments[0].name } };
            return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = after } };
        }
        tk_expr e = { .tag = TK_EXPR_PATH, .as.path = { .path = pp.as.value.node } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = after } };
    }
    return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected an expression") };
}

tk_parsed_result tk_parse_expr(const tk_token *t, size_t n, size_t pos) {
    return parse_or(t, n, pos);   // P2 complete: the whole ladder
}

// forward decl so tk_parse_expr (re-rooted to it) can call it.
static tk_parsed_result parse_postfix(const tk_token *t, size_t n, size_t pos);

static tk_parsed_result parse_postfix(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result prim = parse_atom(t, n, pos);
    if (!prim.ok) return prim;
    tk_expr node = prim.as.value.node; size_t p = prim.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_DOT)) {
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_IDENT))
            return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected a field or method name after '.'") };
        tk_str name = t[p + 1].text;
        if (tk_is_kind_at(t, n, p + 2, TK_TOKEN_LPAREN)) {
            tk_parsed_args_result ca = parse_call_args(t, n, p + 2);
            if (!ca.ok) return (tk_parsed_result){ .ok = false, .as.error = ca.as.error };
            tk_expr m = { .tag = TK_EXPR_METHOD_CALL, .as.method_call = { .receiver = tk_box_expr(node), .method = name, .args = ca.as.value.args, .n_args = ca.as.value.n_args } };
            node = m; p = ca.as.value.next;
        } else {
            tk_expr f = { .tag = TK_EXPR_FIELD_ACCESS, .as.field_access = { .receiver = tk_box_expr(node), .field = name } };
            node = f; p = p + 2;
        }
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

// forward decls so the re-rooted tk_parse_expr can reach parse_cast.
static tk_parsed_result parse_unary(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result parse_cast(const tk_token *t, size_t n, size_t pos);

static tk_parsed_result parse_unary(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_unary(t, n, pos)) {
        tk_token_kind op = t[pos].kind;
        tk_parsed_result o = parse_unary(t, n, pos + 1);
        if (!o.ok) return o;
        tk_expr e = { .tag = TK_EXPR_UNARY, .as.unary = { .op = op, .operand = tk_box_expr(o.as.value.node) } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = o.as.value.next } };
    }
    return parse_postfix(t, n, pos);
}

static tk_parsed_result parse_cast(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_unary(t, n, pos);
    if (!first.ok) return first;
    tk_expr node = first.as.value.node; size_t p = first.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_TO)) {
        tk_parsed_type_result ty = tk_parse_type_primary(t, n, p + 1);   // type-primary (P1)
        if (!ty.ok) return (tk_parsed_result){ .ok = false, .as.error = ty.as.error };
        tk_expr c = { .tag = TK_EXPR_CAST, .as.cast = { .expr = tk_box_expr(node), .target = ty.as.value.node } };
        node = c; p = ty.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_shift(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result parse_multiplicative(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result parse_additive(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result parse_comparison(const tk_token *t, size_t n, size_t pos);

// EXEMPLAR binary level — shift/multiplicative are identical but for predicate + callee.
static tk_parsed_result parse_additive(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_multiplicative(t, n, pos);
    if (!first.ok) return first;
    tk_expr node = first.as.value.node; size_t p = first.as.value.next;
    while (tk_is_additive(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result r = parse_multiplicative(t, n, p + 1);
        if (!r.ok) return r;
        tk_expr b = { .tag = TK_EXPR_BINARY, .as.binary = { .op = op, .left = tk_box_expr(node), .right = tk_box_expr(r.as.value.node) } };
        node = b; p = r.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}
// parse_shift:          first=parse_cast;           while tk_is_shift;          callee parse_cast.
// parse_multiplicative: first=parse_shift;          while tk_is_multiplicative; callee parse_shift.

static tk_parsed_result parse_comparison(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_additive(t, n, pos);
    if (!first.ok) return first;
    tk_cmp_term *terms = NULL; size_t nt = 0; size_t p = first.as.value.next;
    while (tk_is_comparison(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result r = parse_additive(t, n, p + 1);
        if (!r.ok) return r;
        tk_terms_push(&terms, &nt, (tk_cmp_term){ .op = op, .operand = tk_box_expr(r.as.value.node) });
        p = r.as.value.next;
    }
    if (nt == 0) return (tk_parsed_result){ .ok = true, .as.value = { .node = first.as.value.node, .next = p } };
    tk_expr e = { .tag = TK_EXPR_COMPARE, .as.compare = { .first = tk_box_expr(first.as.value.node), .rest = terms, .n_rest = nt } };
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = p } };
}

static tk_parsed_result parse_and(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result parse_or(const tk_token *t, size_t n, size_t pos);

static tk_parsed_result parse_and(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_comparison(t, n, pos);
    if (!first.ok) return first;
    tk_expr node = first.as.value.node; size_t p = first.as.value.next;
    while (tk_is_andand(t, n, p)) {
        tk_parsed_result r = parse_comparison(t, n, p + 1);
        if (!r.ok) return r;
        tk_expr b = { .tag = TK_EXPR_BINARY, .as.binary = { .op = TK_TOKEN_ANDAND, .left = tk_box_expr(node), .right = tk_box_expr(r.as.value.node) } };
        node = b; p = r.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_or(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_and(t, n, pos);
    if (!first.ok) return first;
    tk_expr node = first.as.value.node; size_t p = first.as.value.next;
    while (tk_is_oror(t, n, p)) {
        tk_parsed_result r = parse_and(t, n, p + 1);
        if (!r.ok) return r;
        tk_expr b = { .tag = TK_EXPR_BINARY, .as.binary = { .op = TK_TOKEN_OROR, .left = tk_box_expr(node), .right = tk_box_expr(r.as.value.node) } };
        node = b; p = r.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

// parser/ast.h — pattern nodes (P3a subset).
typedef struct { tk_expr value; } tk_literal_pattern;   // a scalar literal
typedef struct { } tk_wildcard_pattern;                 // _
typedef struct { tk_path type_name; bool has_binding; tk_str binding; } tk_bind_pattern; // Foo / Foo as x  (P3b)
typedef struct { tk_path type_name; tk_str *fields; size_t n_fields; } tk_field_pattern; // Foo { f; g }  (P3b-field)
typedef struct tk_pattern tk_pattern;
typedef struct { tk_expr lo; tk_expr hi; } tk_range_pattern;             // lo ..= hi  (P3c)
typedef struct { tk_pattern *options; size_t n_options; } tk_alt_pattern; // a | b | …  (P3c)
struct tk_pattern {
    enum { TK_PAT_WILDCARD, TK_PAT_LITERAL, TK_PAT_BIND, TK_PAT_FIELD, TK_PAT_RANGE, TK_PAT_ALT } tag;
    union { tk_literal_pattern literal; tk_bind_pattern bind; tk_field_pattern field; tk_range_pattern range; tk_alt_pattern alt; } as;
};
typedef struct { tk_pattern node; size_t next; } tk_parsed_pattern;
TK_RESULT(tk_parsed_pattern, tk_parsed_pattern_result);

tk_parsed_pattern_result tk_parse_pattern(const tk_token *t, size_t n, size_t pos);

static tk_parsed_pattern_result parse_pattern_primary(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("expected a pattern") };
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
    if (k == TK_TOKEN_STR)  { tk_expr v = { .tag = TK_EXPR_STR,  .as.str  = { .text  = t[pos].text } };
        tk_pattern p = { .tag = TK_PAT_LITERAL, .as.literal = { .value = v } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } }; }
    if (k == TK_TOKEN_BYTE) { tk_expr v = { .tag = TK_EXPR_BYTE, .as.byte = { .value = tk_lit_byte(t[pos].text) } };
        tk_pattern p = { .tag = TK_PAT_LITERAL, .as.literal = { .value = v } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } }; }
    if (k == TK_TOKEN_IDENT) {
        tk_parsed_path_result pp = parse_path(t, n, pos);
        if (!pp.ok) return (tk_parsed_pattern_result){ .ok = false, .as.error = pp.as.error };
        size_t after = pp.as.value.next;
        if (tk_is_kind_at(t, n, after, TK_TOKEN_AS)) {
            if (!tk_is_kind_at(t, n, after + 1, TK_TOKEN_IDENT))
                return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("expected a name after `as` in a pattern") };
            tk_pattern p = { .tag = TK_PAT_BIND, .as.bind = { .type_name = pp.as.value.node, .has_binding = true, .binding = t[after + 1].text } };
            return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = after + 2 } };
        }
        if (tk_is_kind_at(t, n, after, TK_TOKEN_LBRACE)) {
            tk_parsed_names_result fns = parse_field_names(t, n, after);
            if (!fns.ok) return (tk_parsed_pattern_result){ .ok = false, .as.error = fns.as.error };
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
    if (!lo.ok) return lo;
    if (!tk_is_kind_at(t, n, lo.as.value.next, TK_TOKEN_DOTDOTEQ)) return lo;
    if (lo.as.value.node.tag != TK_PAT_LITERAL)
        return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("a range bound must be a literal") };
    tk_expr lo_e = lo.as.value.node.as.literal.value;
    tk_parsed_pattern_result hi = parse_pattern_primary(t, n, lo.as.value.next + 1);
    if (!hi.ok) return hi;
    if (hi.as.value.node.tag != TK_PAT_LITERAL)
        return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("a range bound must be a literal") };
    tk_pattern p = { .tag = TK_PAT_RANGE, .as.range = { .lo = lo_e, .hi = hi.as.value.node.as.literal.value } };
    return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = hi.as.value.next } };
}

tk_parsed_pattern_result tk_parse_pattern(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_pattern_result first = parse_pattern_range(t, n, pos);
    if (!first.ok) return first;
    tk_pattern *opts = NULL; size_t no = 0;
    tk_pats_push(&opts, &no, first.as.value.node);
    size_t p = first.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_PIPE)) {
        tk_parsed_pattern_result o = parse_pattern_range(t, n, p + 1);
        if (!o.ok) return o;
        tk_pats_push(&opts, &no, o.as.value.node);
        p = o.as.value.next;
    }
    if (no == 1) return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = first.as.value.node, .next = p } };
    tk_pattern alt = { .tag = TK_PAT_ALT, .as.alt = { .options = opts, .n_options = no } };
    return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = alt, .next = p } };
}

// parser/ast.h — the arm; parser/result.h — the guard + parsed-arm results.
typedef struct { tk_pattern pattern; bool has_when; tk_expr guard; tk_expr body; } tk_arm;
typedef struct { bool has_when; tk_expr guard; size_t next; } tk_guard;
typedef struct { tk_arm node; size_t next; } tk_parsed_arm;
TK_RESULT(tk_guard,      tk_guard_result);
TK_RESULT(tk_parsed_arm, tk_parsed_arm_result);

static tk_guard_result parse_guard(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_WHEN)) {
        tk_expr zero = { .tag = TK_EXPR_NUMBER, .as.number = { .value = 0 } };   // placeholder, gated by has_when
        return (tk_guard_result){ .ok = true, .as.value = { .has_when = false, .guard = zero, .next = pos } };
    }
    tk_parsed_result g = tk_parse_expr(t, n, pos + 1);
    if (!g.ok) return (tk_guard_result){ .ok = false, .as.error = g.as.error };
    return (tk_guard_result){ .ok = true, .as.value = { .has_when = true, .guard = g.as.value.node, .next = g.as.value.next } };
}

tk_parsed_arm_result tk_parse_arm(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_pattern_result pat = tk_parse_pattern(t, n, pos);
    if (!pat.ok) return (tk_parsed_arm_result){ .ok = false, .as.error = pat.as.error };
    tk_guard_result g = parse_guard(t, n, pat.as.value.next);
    if (!g.ok) return (tk_parsed_arm_result){ .ok = false, .as.error = g.as.error };
    if (!tk_is_kind_at(t, n, g.as.value.next, TK_TOKEN_FATARROW))
        return (tk_parsed_arm_result){ .ok = false, .as.error = tk_error_make("expected '=>' after a match pattern") };
    tk_parsed_result body = tk_parse_expr(t, n, g.as.value.next + 1);
    if (!body.ok) return (tk_parsed_arm_result){ .ok = false, .as.error = body.as.error };
    tk_arm arm = { .pattern = pat.as.value.node, .has_when = g.as.value.has_when, .guard = g.as.value.guard, .body = body.as.value.node };
    return (tk_parsed_arm_result){ .ok = true, .as.value = { .node = arm, .next = body.as.value.next } };
}

// parser/ast.h — statement nodes (P4a subset). parser/result.h — the results.
typedef struct { bool has_value; tk_expr value; } tk_return;   // return [expr]
typedef struct { tk_expr expr; } tk_expr_stmt;                 // a bare expression
typedef struct tk_statement tk_statement;
typedef struct { tk_statement *body; size_t n; } tk_loop_stmt; // loop { … }  (P4d)
struct tk_statement {
    enum { TK_STMT_RETURN, TK_STMT_EXPR, TK_STMT_BINDING, TK_STMT_ASSIGN, TK_STMT_LOOP, TK_STMT_BREAK, TK_STMT_CONTINUE } tag;
    union { tk_return ret; tk_expr_stmt expr_stmt; tk_binding binding; tk_assign assign; tk_loop_stmt loop_stmt; } as;
};
typedef struct { tk_statement node; size_t next; } tk_parsed_stmt;
typedef struct { tk_statement *statements; size_t n; size_t next; } tk_parsed_block;
TK_RESULT(tk_parsed_stmt,  tk_parsed_stmt_result);
TK_RESULT(tk_parsed_block, tk_parsed_block_result);

tk_parsed_stmt_result tk_parse_statement(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_error_make("expected a statement") };
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_RETURN)) {
        size_t after = pos + 1;
        if (!tk_has_token(t, n, after) || tk_is_sep(t, n, after) || tk_is_kind_at(t, n, after, TK_TOKEN_RBRACE)) {
            tk_expr zero = { .tag = TK_EXPR_NUMBER, .as.number = { .value = 0 } };
            tk_statement s = { .tag = TK_STMT_RETURN, .as.ret = { .has_value = false, .value = zero } };
            return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = after } };
        }
        tk_parsed_result v = tk_parse_expr(t, n, after);
        if (!v.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error };
        tk_statement s = { .tag = TK_STMT_RETURN, .as.ret = { .has_value = true, .value = v.as.value.node } };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LOOP)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE))
            return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_error_make("expected '{' after `loop`") };
        tk_parsed_block_result blk = tk_parse_block(t, n, pos + 1);
        if (!blk.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = blk.as.error };
        tk_statement s = { .tag = TK_STMT_LOOP, .as.loop_stmt = { .body = blk.as.value.statements, .n = blk.as.value.n } };
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
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LET) || tk_is_kind_at(t, n, pos, TK_TOKEN_MUT) || tk_is_kind_at(t, n, pos, TK_TOKEN_CONST))
        return parse_binding(t, n, pos);
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT) && tk_is_assign_op(t, n, pos + 1))
        return parse_assign(t, n, pos);
    tk_parsed_result e = tk_parse_expr(t, n, pos);
    if (!e.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = e.as.error };
    tk_statement s = { .tag = TK_STMT_EXPR, .as.expr_stmt = { .expr = e.as.value.node } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = e.as.value.next } };
}

tk_parsed_block_result tk_parse_block(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);
    tk_statement *stmts = NULL; size_t ns = 0;
    for (;;) {
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        tk_parsed_stmt_result s = tk_parse_statement(t, n, p);
        if (!s.ok) return (tk_parsed_block_result){ .ok = false, .as.error = s.as.error };
        tk_stmts_push(&stmts, &ns, s.as.value.node); p = s.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_block_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a statement") };
        p = tk_skip_seps(t, n, p);
    }
    return (tk_parsed_block_result){ .ok = true, .as.value = { .statements = stmts, .n = ns, .next = p + 1 } };
}

// parser/ast.h — binding target + binding; parser/result.h — annotation result.
typedef struct { tk_str name; } tk_simple_name;
typedef struct { tk_str *names; size_t n_names; } tk_destructure_target;   // let { x; y } = …  (P4c)
typedef struct {
    enum { TK_TARGET_NAME, TK_TARGET_DESTRUCTURE } tag;
    union { tk_simple_name name; tk_destructure_target destructure; } as;
} tk_bind_target;
typedef enum { TK_BIND_LET, TK_BIND_MUT, TK_BIND_CONST } tk_bind_kind;
typedef struct { tk_bind_kind kind; tk_bind_target target; bool has_type; tk_type_expr type_ann; tk_expr value; } tk_binding;
typedef struct { bool has_type; tk_type_expr type_ann; size_t next; } tk_annotation;
TK_RESULT(tk_annotation, tk_annotation_result);

static tk_parsed_stmt_result parse_binding(const tk_token *t, size_t n, size_t pos);  // forward (parse_statement calls it)

static tk_type_expr no_type(void) {
    return (tk_type_expr){ .tag = TK_TYPE_EXPR_NAMED, .as.named = { .segments = NULL, .n = 0 } };
}

static tk_annotation_result parse_annotation(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_COLON))
        return (tk_annotation_result){ .ok = true, .as.value = { .has_type = false, .type_ann = no_type(), .next = pos } };
    tk_parsed_type_result ty = tk_parse_type(t, n, pos + 1);
    if (!ty.ok) return (tk_annotation_result){ .ok = false, .as.error = ty.as.error };
    return (tk_annotation_result){ .ok = true, .as.value = { .has_type = true, .type_ann = ty.as.value.node, .next = ty.as.value.next } };
}

static tk_parsed_stmt_result parse_binding(const tk_token *t, size_t n, size_t pos) {
    tk_bind_kind kind = TK_BIND_LET;
    if (t[pos].kind == TK_TOKEN_MUT)   kind = TK_BIND_MUT;
    if (t[pos].kind == TK_TOKEN_CONST) kind = TK_BIND_CONST;
    tk_parsed_target_result tgt = parse_bind_target(t, n, pos + 1);
    if (!tgt.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = tgt.as.error };
    tk_annotation_result ann = parse_annotation(t, n, tgt.as.value.next);
    if (!ann.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = ann.as.error };
    if (!tk_is_kind_at(t, n, ann.as.value.next, TK_TOKEN_ASSIGN))
        return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_error_make("expected '=' in a binding") };
    tk_parsed_result v = tk_parse_expr(t, n, ann.as.value.next + 1);
    if (!v.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error };
    tk_statement s = { .tag = TK_STMT_BINDING, .as.binding = { .kind = kind, .target = tgt.as.value.node, .has_type = ann.as.value.has_type, .type_ann = ann.as.value.type_ann, .value = v.as.value.node } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
}

// parser/ast.h — assignment; parser/result.h — parsed-target result.
typedef struct { tk_str name; tk_token_kind op; tk_expr value; } tk_assign;   // name op= value
typedef struct { tk_bind_target node; size_t next; } tk_parsed_target;
TK_RESULT(tk_parsed_target, tk_parsed_target_result);

static tk_parsed_target_result parse_bind_target(const tk_token *t, size_t n, size_t pos);  // forward
static tk_parsed_stmt_result   parse_assign(const tk_token *t, size_t n, size_t pos);        // forward

static tk_parsed_target_result parse_bind_target(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LBRACE)) {
        tk_parsed_names_result names = parse_field_names(t, n, pos);
        if (!names.ok) return (tk_parsed_target_result){ .ok = false, .as.error = names.as.error };
        tk_bind_target tgt = { .tag = TK_TARGET_DESTRUCTURE, .as.destructure = { .names = names.as.value.names, .n_names = names.as.value.n_names } };
        return (tk_parsed_target_result){ .ok = true, .as.value = { .node = tgt, .next = names.as.value.next } };
    }
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT))
        return (tk_parsed_target_result){ .ok = false, .as.error = tk_error_make("expected a name or `{ … }` after `let`/`mut`/`const`") };
    tk_bind_target tgt = { .tag = TK_TARGET_NAME, .as.name = { .name = t[pos].text } };
    return (tk_parsed_target_result){ .ok = true, .as.value = { .node = tgt, .next = pos + 1 } };
}

static tk_parsed_stmt_result parse_assign(const tk_token *t, size_t n, size_t pos) {
    tk_str name = t[pos].text;
    tk_token_kind op = t[pos + 1].kind;
    tk_parsed_result v = tk_parse_expr(t, n, pos + 2);
    if (!v.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error };
    tk_statement s = { .tag = TK_STMT_ASSIGN, .as.assign = { .name = name, .op = op, .value = v.as.value.node } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
}

// parser/ast.h — if/match expr nodes; parser/result.h — the arm-list result.
typedef struct { tk_expr *cond; tk_statement *then_blk; size_t n_then; bool has_else; tk_statement *else_blk; size_t n_else; } tk_if_expr;
typedef struct { tk_expr *subject; tk_arm *arms; size_t n_arms; } tk_match_expr;
typedef struct { tk_arm *arms; size_t n_arms; size_t next; } tk_parsed_arms;
TK_RESULT(tk_parsed_arms, tk_parsed_arms_result);

static tk_parsed_result parse_if(const tk_token *t, size_t n, size_t pos);     // forward
static tk_parsed_result parse_match(const tk_token *t, size_t n, size_t pos);  // forward

static tk_parsed_result parse_if(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result cond = tk_parse_expr(t, n, pos + 1);
    if (!cond.ok) return cond;
    if (!tk_is_kind_at(t, n, cond.as.value.next, TK_TOKEN_LBRACE))
        return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected '{' after the `if` condition") };
    tk_parsed_block_result tb = tk_parse_block(t, n, cond.as.value.next);
    if (!tb.ok) return (tk_parsed_result){ .ok = false, .as.error = tb.as.error };
    size_t p = tb.as.value.next;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_ELSE)) {
        tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node), .then_blk = tb.as.value.statements, .n_then = tb.as.value.n, .has_else = false, .else_blk = NULL, .n_else = 0 } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = p } };
    }
    if (tk_is_kind_at(t, n, p + 1, TK_TOKEN_IF)) {
        tk_parsed_result elif = parse_if(t, n, p + 1);
        if (!elif.ok) return elif;
        tk_statement *eb = NULL; size_t neb = 0;
        tk_statement es = { .tag = TK_STMT_EXPR, .as.expr_stmt = { .expr = elif.as.value.node } };
        tk_stmts_push(&eb, &neb, es);
        tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node), .then_blk = tb.as.value.statements, .n_then = tb.as.value.n, .has_else = true, .else_blk = eb, .n_else = neb } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = elif.as.value.next } };
    }
    if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_LBRACE))
        return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected '{' or `if` after `else`") };
    tk_parsed_block_result ebk = tk_parse_block(t, n, p + 1);
    if (!ebk.ok) return (tk_parsed_result){ .ok = false, .as.error = ebk.as.error };
    tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node), .then_blk = tb.as.value.statements, .n_then = tb.as.value.n, .has_else = true, .else_blk = ebk.as.value.statements, .n_else = ebk.as.value.n } };
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = ebk.as.value.next } };
}

static tk_parsed_arms_result parse_arms(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);
    tk_arm *arms = NULL; size_t na = 0;
    for (;;) {
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        tk_parsed_arm_result a = tk_parse_arm(t, n, p);
        if (!a.ok) return (tk_parsed_arms_result){ .ok = false, .as.error = a.as.error };
        tk_arms_push(&arms, &na, a.as.value.node); p = a.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_arms_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a match arm") };
        p = tk_skip_seps(t, n, p);
    }
    if (na == 0) return (tk_parsed_arms_result){ .ok = false, .as.error = tk_error_make("a `match` needs at least one arm") };
    return (tk_parsed_arms_result){ .ok = true, .as.value = { .arms = arms, .n_arms = na, .next = p + 1 } };
}

static tk_parsed_result parse_match(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result subj = tk_parse_expr(t, n, pos + 1);
    if (!subj.ok) return subj;
    if (!tk_is_kind_at(t, n, subj.as.value.next, TK_TOKEN_LBRACE))
        return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected '{' after the `match` subject") };
    tk_parsed_arms_result arms = parse_arms(t, n, subj.as.value.next);
    if (!arms.ok) return (tk_parsed_result){ .ok = false, .as.error = arms.as.error };
    tk_expr e = { .tag = TK_EXPR_MATCH, .as.match_expr = { .subject = tk_box_expr(subj.as.value.node), .arms = arms.as.value.arms, .n_arms = arms.as.value.n_arms } };
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = arms.as.value.next } };
}

// parser/ast.h — R-main: the file model (retires tk_program/tk_item).
typedef struct {
    enum { TK_DECL_FUNCTION, TK_DECL_TYPE } tag;
    union { tk_function function; tk_type_decl type_decl; } as;
} tk_decl;                                                                                  // a module's declaration
typedef struct { tk_use_decl *uses; size_t n_uses; tk_statement *body; size_t n_body; } tk_main_file;  // use header + virtual main body
typedef struct { tk_use_decl *uses; size_t n_uses; tk_decl *decls; size_t n_decls; } tk_module;        // use header + declarations
typedef struct {
    enum { TK_FILE_MAIN, TK_FILE_MODULE } tag;
    union { tk_main_file main_file; tk_module module; } as;
} tk_file;
typedef struct { tk_decl node; size_t next; } tk_parsed_decl;                               // was tk_parsed_item

// parser/ast.h — params + function; parser/result.h — params + decl results.
typedef struct { tk_str name; tk_type_expr type_ann; } tk_param;   // immutable (B.21)
typedef struct {
    tk_str name; tk_param *params; size_t n_params;
    bool has_return; tk_type_expr return_type;
    tk_statement *body; size_t n_body;
    bool is_exp; bool has_doc; tk_str doc;
} tk_function;
typedef struct { tk_param *params; size_t n_params; size_t next; } tk_parsed_params;
TK_RESULT(tk_parsed_params, tk_parsed_params_result);
TK_RESULT(tk_parsed_decl,   tk_parsed_decl_result);   // tk_parsed_decl from R-main-a

static tk_parsed_params_result parse_params(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos + 1;
    tk_param *params = NULL; size_t np = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN))
        return (tk_parsed_params_result){ .ok = true, .as.value = { .params = params, .n_params = 0, .next = p + 1 } };
    for (;;) {
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT))
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_error_make("expected a parameter name") };
        tk_str name = t[p].text;
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_COLON))
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_error_make("expected ':' after a parameter name") };
        tk_parsed_type_result ty = tk_parse_type(t, n, p + 2);
        if (!ty.ok) return (tk_parsed_params_result){ .ok = false, .as.error = ty.as.error };
        tk_params_push(&params, &np, (tk_param){ .name = name, .type_ann = ty.as.value.node });
        p = ty.as.value.next;
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) break;
        p += 1;
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN))
        return (tk_parsed_params_result){ .ok = false, .as.error = tk_error_make("expected ')' to close the parameter list") };
    return (tk_parsed_params_result){ .ok = true, .as.value = { .params = params, .n_params = np, .next = p + 1 } };
}

tk_parsed_decl_result tk_parse_function(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos;
    bool has_doc = false; tk_str doc = (tk_str){0};
    if (tk_is_kind_at(t, n, p, TK_TOKEN_DOC)) { has_doc = true; doc = t[p].text; p += 1; }
    bool is_exp = false;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_EXP)) { is_exp = true; p += 1; }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_FN))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected `fn`") };
    p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected a function name") };
    tk_str name = t[p].text; p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_LPAREN))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected '(' for the parameter list") };
    tk_parsed_params_result ps = parse_params(t, n, p);
    if (!ps.ok) return (tk_parsed_decl_result){ .ok = false, .as.error = ps.as.error };
    p = ps.as.value.next;
    bool has_return = false; tk_type_expr ret = no_type();
    if (tk_is_kind_at(t, n, p, TK_TOKEN_ARROW)) {
        tk_parsed_type_result r = tk_parse_type(t, n, p + 1);
        if (!r.ok) return (tk_parsed_decl_result){ .ok = false, .as.error = r.as.error };
        has_return = true; ret = r.as.value.node; p = r.as.value.next;
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_LBRACE))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected '{' for the function body") };
    tk_parsed_block_result blk = tk_parse_block(t, n, p);
    if (!blk.ok) return (tk_parsed_decl_result){ .ok = false, .as.error = blk.as.error };
    tk_function f = { .name = name, .params = ps.as.value.params, .n_params = ps.as.value.n_params, .has_return = has_return, .return_type = ret, .body = blk.as.value.statements, .n_body = blk.as.value.n, .is_exp = is_exp, .has_doc = has_doc, .doc = doc };
    tk_decl d = { .tag = TK_DECL_FUNCTION, .as.function = f };
    return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = d, .next = blk.as.value.next } };
}

// parser/ast.h — field + struct body + type body + type decl.
typedef struct { tk_str name; tk_type_expr type_ann; } tk_field;
typedef struct { tk_field *fields; size_t n_fields; } tk_struct_body;
typedef struct { tk_str *members; size_t n_members; } tk_enum_body;     // P5c
typedef struct { tk_type_expr type_expr; } tk_variant_body;             // P5c
typedef struct {
    enum { TK_BODY_STRUCT, TK_BODY_ENUM, TK_BODY_VARIANT } tag;
    union { tk_struct_body struct_body; tk_enum_body enum_body; tk_variant_body variant_body; } as;
} tk_type_body;
typedef struct { tk_str name; tk_type_body body; bool is_exp; bool has_doc; tk_str doc; } tk_type_decl;
typedef struct { tk_field *fields; size_t n_fields; size_t next; } tk_parsed_fields;
typedef struct { tk_type_body node; size_t next; } tk_parsed_body;
TK_RESULT(tk_parsed_fields, tk_parsed_fields_result);
TK_RESULT(tk_parsed_body,   tk_parsed_body_result);

static tk_parsed_fields_result parse_fields(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);
    tk_field *fields = NULL; size_t nf = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE))
        return (tk_parsed_fields_result){ .ok = true, .as.value = { .fields = fields, .n_fields = 0, .next = p + 1 } };
    for (;;) {
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT))
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_error_make("expected a field name") };
        tk_str name = t[p].text;
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_COLON))
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_error_make("expected ':' after a field name") };
        tk_parsed_type_result ty = tk_parse_type(t, n, p + 2);
        if (!ty.ok) return (tk_parsed_fields_result){ .ok = false, .as.error = ty.as.error };
        tk_fields_push(&fields, &nf, (tk_field){ .name = name, .type_ann = ty.as.value.node });
        p = ty.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a field") };
        p = tk_skip_seps(t, n, p);
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
    }
    return (tk_parsed_fields_result){ .ok = true, .as.value = { .fields = fields, .n_fields = nf, .next = p + 1 } };
}

static tk_parsed_body_result parse_type_body(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_STRUCT)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE))
            return (tk_parsed_body_result){ .ok = false, .as.error = tk_error_make("expected '{' after `struct`") };
        tk_parsed_fields_result fs = parse_fields(t, n, pos + 1);
        if (!fs.ok) return (tk_parsed_body_result){ .ok = false, .as.error = fs.as.error };
        tk_type_body b = { .tag = TK_BODY_STRUCT, .as.struct_body = { .fields = fs.as.value.fields, .n_fields = fs.as.value.n_fields } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = fs.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_ENUM)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE))
            return (tk_parsed_body_result){ .ok = false, .as.error = tk_error_make("expected '{' after `enum`") };
        tk_parsed_names_result ms = parse_field_names(t, n, pos + 1);
        if (!ms.ok) return (tk_parsed_body_result){ .ok = false, .as.error = ms.as.error };
        tk_type_body b = { .tag = TK_BODY_ENUM, .as.enum_body = { .members = ms.as.value.names, .n_members = ms.as.value.n_names } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = ms.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_VARIANT)) {
        tk_parsed_type_result ty = tk_parse_type(t, n, pos + 1);
        if (!ty.ok) return (tk_parsed_body_result){ .ok = false, .as.error = ty.as.error };
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
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_TYPE))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected `type`") };
    p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected a type name") };
    tk_str name = t[p].text; p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_ASSIGN))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected '=' in a type declaration") };
    tk_parsed_body_result body = parse_type_body(t, n, p + 1);
    if (!body.ok) return (tk_parsed_decl_result){ .ok = false, .as.error = body.as.error };
    tk_type_decl td = { .name = name, .body = body.as.value.node, .is_exp = is_exp, .has_doc = has_doc, .doc = doc };
    tk_decl d = { .tag = TK_DECL_TYPE, .as.type_decl = td };
    return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = d, .next = body.as.value.next } };
}
