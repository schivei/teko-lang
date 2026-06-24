// parser/result.h
typedef struct { tk_str *names; size_t n_names; size_t next; } tk_parsed_names;
TK_RESULT(tk_parsed_names, tk_parsed_names_result);

// forward decl so parse_pattern_primary (above) can call it.
static tk_parsed_names_result parse_field_names(const tk_token *t, size_t n, size_t pos);

static tk_parsed_names_result parse_field_names(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);   // consume `{`, skip leading separators
    tk_str *names = NULL; size_t nn = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE))
        return (tk_parsed_names_result){ .ok = true, .as.value = { .names = names, .n_names = 0, .next = p + 1 } };
    for (;;) {
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT))
            return (tk_parsed_names_result){ .ok = false, .as.error = tk_error_make("expected a field name in `Type { … }`") };
        tk_strs_push(&names, &nn, t[p].text); p += 1;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_names_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a field name") };
        p = tk_skip_seps(t, n, p);
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
    }
    return (tk_parsed_names_result){ .ok = true, .as.value = { .names = names, .n_names = nn, .next = p + 1 } };
}

// parser/result.h — new results.
typedef struct { tk_use_decl node; size_t next; }                 tk_parsed_use;
typedef struct { tk_use_decl *uses; size_t n_uses; size_t next; } tk_parsed_uses;
typedef struct { tk_main_file node; size_t next; }                tk_parsed_main_file;
TK_RESULT(tk_parsed_use,       tk_parsed_use_result);
TK_RESULT(tk_parsed_uses,      tk_parsed_uses_result);
TK_RESULT(tk_parsed_main_file, tk_parsed_main_file_result);

static bool is_decl_start(const tk_token *t, size_t n, size_t pos) {
    return tk_is_kind_at(t, n, pos, TK_TOKEN_FN) || tk_is_kind_at(t, n, pos, TK_TOKEN_TYPE);
}

static tk_parsed_use_result parse_use(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_path_result pp = parse_path(t, n, pos + 1);
    if (!pp.ok) return (tk_parsed_use_result){ .ok = false, .as.error = pp.as.error };
    bool has_alias = false; tk_str alias = (tk_str){0}; size_t p = pp.as.value.next;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_AS)) {
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_IDENT))
            return (tk_parsed_use_result){ .ok = false, .as.error = tk_error_make("expected a name after `as` in a `use`") };
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
        if (!u.ok) return (tk_parsed_uses_result){ .ok = false, .as.error = u.as.error };
        tk_uses_push(&uses, &nu, u.as.value.node); p = u.as.value.next;
        if (!tk_has_token(t, n, p)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_uses_result){ .ok = false, .as.error = tk_error_make("expected ';' or a newline after a `use`") };
        p = tk_skip_seps(t, n, p);
    }
    return (tk_parsed_uses_result){ .ok = true, .as.value = { .uses = uses, .n_uses = nu, .next = p } };
}

tk_parsed_main_file_result tk_parse_main_file(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_uses_result hdr = parse_use_header(t, n, pos);
    if (!hdr.ok) return (tk_parsed_main_file_result){ .ok = false, .as.error = hdr.as.error };
    size_t p = tk_skip_seps(t, n, hdr.as.value.next);
    tk_statement *body = NULL; size_t nb = 0;
    for (;;) {
        if (!tk_has_token(t, n, p)) break;
        if (is_decl_start(t, n, p))
            return (tk_parsed_main_file_result){ .ok = false, .as.error = tk_error_make("main.tks is a virtual main: it may not declare types or functions (only `use` + statements)") };
        tk_parsed_stmt_result s = tk_parse_statement(t, n, p);
        if (!s.ok) return (tk_parsed_main_file_result){ .ok = false, .as.error = s.as.error };
        tk_stmts_push(&body, &nb, s.as.value.node); p = s.as.value.next;
        if (!tk_has_token(t, n, p)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_main_file_result){ .ok = false, .as.error = tk_error_make("expected ';' or a newline after a statement") };
        p = tk_skip_seps(t, n, p);
    }
    tk_main_file mf = { .uses = hdr.as.value.uses, .n_uses = hdr.as.value.n_uses, .body = body, .n_body = nb };
    return (tk_parsed_main_file_result){ .ok = true, .as.value = { .node = mf, .next = p } };
}

// parser/result.h — module result.
typedef struct { tk_module node; size_t next; } tk_parsed_module;
TK_RESULT(tk_parsed_module, tk_parsed_module_result);

static tk_parsed_decl_result parse_decl(const tk_token *t, size_t n, size_t pos) {
    size_t k = pos;
    if (tk_is_kind_at(t, n, k, TK_TOKEN_DOC)) k += 1;
    if (tk_is_kind_at(t, n, k, TK_TOKEN_EXP)) k += 1;
    if (tk_is_kind_at(t, n, k, TK_TOKEN_FN))   return tk_parse_function(t, n, pos);
    if (tk_is_kind_at(t, n, k, TK_TOKEN_TYPE)) return tk_parse_type_decl(t, n, pos);
    return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected a declaration (`fn`/`type`, optionally `exp`/doc); loose statements belong in main.tks") };
}

tk_parsed_module_result tk_parse_module(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_uses_result hdr = parse_use_header(t, n, pos);
    if (!hdr.ok) return (tk_parsed_module_result){ .ok = false, .as.error = hdr.as.error };
    size_t p = tk_skip_seps(t, n, hdr.as.value.next);
    tk_decl *decls = NULL; size_t nd = 0;
    for (;;) {
        if (!tk_has_token(t, n, p)) break;
        tk_parsed_decl_result d = parse_decl(t, n, p);
        if (!d.ok) return (tk_parsed_module_result){ .ok = false, .as.error = d.as.error };
        tk_decls_push(&decls, &nd, d.as.value.node); p = d.as.value.next;
        if (!tk_has_token(t, n, p)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_module_result){ .ok = false, .as.error = tk_error_make("expected ';' or a newline after a declaration") };
        p = tk_skip_seps(t, n, p);
    }
    tk_module m = { .uses = hdr.as.value.uses, .n_uses = hdr.as.value.n_uses, .decls = decls, .n_decls = nd };
    return (tk_parsed_module_result){ .ok = true, .as.value = { .node = m, .next = p } };
}

// src/build/tkp_rule.{h,c}
typedef enum { TK_ARTIFACT_EXECUTABLE, TK_ARTIFACT_LIBRARY } tk_artifact;
TK_RESULT(tk_artifact, tk_artifact_result);   // tk_artifact | error

tk_artifact_result tk_check_main_file_rule(tk_artifact artifact, bool has_main) {
    if (artifact == TK_ARTIFACT_EXECUTABLE && !has_main)
        return (tk_artifact_result){ .ok = false, .as.error = tk_error_make("an executable project requires a main.tks") };
    if (artifact == TK_ARTIFACT_LIBRARY && has_main)
        return (tk_artifact_result){ .ok = false, .as.error = tk_error_make("a library project may not have a main.tks") };
    return (tk_artifact_result){ .ok = true, .as.value = artifact };
}
