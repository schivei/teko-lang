// src/checker/typer.c   (namespace 'teko::checker')
// The statement / function / item / program half of the typed pass (the evolved
// check_* producers — C1). The expression-typing half (tk_typer_expr + the
// per-expr-kind helpers, predicates, cast/range machinery, field access, and the
// if/match VALUE forms) lives in expr.c; the two TUs see each other via expr.h
// (tk_typer_expr, shared helpers, annotated_literal_reason) and typer_internal.h
// (tk_type_block ↔ tk_type_arm / tk_tblock_type / tk_exhaustive). Pairs typer.tks.
#include "typer.h"
#include "expr.h"            // tk_typer_expr, shared helpers (tk_box/tk_xok/…), annotated_literal_reason
#include "typer_internal.h"  // tk_type_block (this TU defines it), tk_type_arm, tk_tblock_type, tk_exhaustive
#include "collect.h"         // tk_collect, tk_collected_result

// short aliases for local use (terse, matches the original site spellings).
#define box   tk_box
#define unit_t tk_unit_t
#define is_bool tk_is_bool
#define xok   tk_xok
#define xerr  tk_xerr
#define xferr tk_xferr

// growable lists for the typed children (teko::list realized — file-local stamps).
TK_LIST(tk_tarm,       tk_tarm_list)
TK_LIST(tk_tstatement, tk_tstmt_list)
TK_LIST(tk_titem,      tk_titem_list)

// forward decls (the if/match STATEMENT forms; mutual recursion with type_block).
static tk_texpr_result type_if_stmt(tk_if_expr f, tk_env env, tk_type_table table);
static tk_texpr_result type_match_stmt(tk_match_expr m, tk_env env, tk_type_table table);

// ---- a typed block: thread the env, collect typed statements (shared via typer_internal.h) ----
tk_typed_block_result tk_type_block(tk_statement *stmts, size_t n, tk_env env, tk_type_table table) {
    tk_env cur = env;
    tk_tstmt_list out = tk_tstmt_list_empty();
    for (size_t i = 0; i < n; i += 1) {
        tk_typed_stmt_result ts = tk_type_statement(stmts[i], cur, table);
        if (!ts.ok) return (tk_typed_block_result){ .ok = false, .as.error = ts.as.error };
        out = tk_tstmt_list_push(out, ts.as.value.node);
        cur = ts.as.value.env;
    }
    return (tk_typed_block_result){ .ok = true, .as.value = { .stmts = out.ptr, .n = out.len, .env = cur } };
}

// ---- if as a STATEMENT (value discarded → Unit) ----
static tk_texpr_result type_if_stmt(tk_if_expr f, tk_env env, tk_type_table table) {
    tk_texpr_result c = tk_typer_expr(*f.cond, env, table); if (!c.ok) return c;
    if (!is_bool(c.as.value.type)) return xerr("an `if` condition must be a bool");
    tk_typed_block_result tb = tk_type_block(f.then_blk, f.nthen, env, table); if (!tb.ok) return xferr(tb.as.error);
    tk_tstatement *eb_stmts = tb.as.value.stmts; size_t eb_n = 0;   // gated by has_else
    if (f.has_else) {
        tk_typed_block_result eb = tk_type_block(f.else_blk, f.nelse, env, table); if (!eb.ok) return xferr(eb.as.error);
        eb_stmts = eb.as.value.stmts; eb_n = eb.as.value.n;
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_IF, .type = unit_t(), .as.if_expr = {
        box(c.as.value), tb.as.value.stmts, tb.as.value.n, f.has_else, eb_stmts, eb_n } });
}

// ---- match as a STATEMENT (value discarded → Unit) ----
static tk_texpr_result type_match_stmt(tk_match_expr m, tk_env env, tk_type_table table) {
    tk_texpr_result s = tk_typer_expr(*m.subject, env, table); if (!s.ok) return s;
    tk_tarm_list arms = tk_tarm_list_empty();
    for (size_t i = 0; i < m.narms; i += 1) {
        tk_tarm_result ai = tk_type_arm(m.arms[i], s.as.value.type, env, table); if (!ai.ok) return xferr(ai.as.error);
        arms = tk_tarm_list_push(arms, ai.as.value);
    }
    if (!tk_exhaustive(m.arms, m.narms, s.as.value.type)) return xerr("non-exhaustive `match` (cover all cases or add `_`)");
    return xok((tk_texpr){ .tag = TK_TEXPR_MATCH, .type = unit_t(), .as.match_expr = { box(s.as.value), arms.ptr, arms.len } });
}

// ---- statements ----
static tk_typed_stmt_result sok(tk_tstatement node, tk_env env) { return (tk_typed_stmt_result){ .ok = true, .as.value = { node, env } }; }
static tk_typed_stmt_result sfail(tk_error e)   { return (tk_typed_stmt_result){ .ok = false, .as.error = e }; }
static tk_typed_stmt_result smsg(const char *m) { return sfail(tk_error_make(m)); }

static tk_typed_stmt_result type_binding(tk_binding b, tk_env env, tk_type_table table) {
    tk_texpr_result v = tk_typer_expr(b.value, env, table); if (!v.ok) return sfail(v.as.error);
    tk_type bound = v.as.value.type;
    if (b.has_type) {
        tk_type_result a = tk_resolve_type(b.type_ann, table); if (!a.ok) return sfail(a.as.error);
        if (!tk_type_eq(&v.as.value.type, &a.as.value)) {       // C6: a fitting literal adopts T (leaf stays i64)
            const char *why = annotated_literal_reason(b.value, a.as.value);
            if (why != NULL) return smsg(why);
        }
        bound = a.as.value;
    }
    tk_tstatement node = { .tag = TK_TSTMT_BINDING, .as.binding = { b.kind, b.target, bound, v.as.value } };
    if (b.target.tag == TK_BIND_SIMPLE)
        return sok(node, tk_env_define(env, b.target.as.simple.name, bound, tk_bind_is_mut(b.kind)));
    return sok(node, env);   // [destructuring binding: refinement]
}

static tk_typed_stmt_result type_assign(tk_assign a, tk_env env, tk_type_table table) {
    tk_binding_result tb = tk_env_lookup_binding(env, a.name); if (!tb.ok) return sfail(tb.as.error);
    if (!tb.as.value.is_mut) return smsg("cannot assign to immutable binding — declare it `mut` (B.21)");
    tk_texpr_result v = tk_typer_expr(a.value, env, table); if (!v.ok) return sfail(v.as.error);
    if (!tk_type_eq(&tb.as.value.type, &v.as.value.type)) return smsg("assigned value does not match the target type");
    tk_tstatement node = { .tag = TK_TSTMT_ASSIGN, .as.assign = { a.name, a.op, v.as.value } };
    return sok(node, env);   // mut rule enforced (B.21)
}

static tk_typed_stmt_result type_return(tk_return r, tk_env env, tk_type_table table) {
    tk_texpr_result v = tk_typer_expr(r.value, env, table); if (!v.ok) return sfail(v.as.error);
    tk_tstatement node = { .tag = TK_TSTMT_RETURN, .as.ret = { r.has_value, v.as.value } };
    return sok(node, env);   // value gated by has_value (B.20); return-type match enforced by tk_type_function's check_returns (C5)
}

static tk_typed_stmt_result type_loop(tk_loop_stmt l, tk_env env, tk_type_table table) {
    tk_typed_block_result tb = tk_type_block(l.body, l.nbody, env, table); if (!tb.ok) return sfail(tb.as.error);
    tk_tstatement node = { .tag = TK_TSTMT_LOOP, .as.loop_stmt = { tb.as.value.stmts, tb.as.value.n } };
    return sok(node, env);   // body bindings stay block-local
}

static tk_typed_stmt_result type_exprstmt(tk_expr_stmt es, tk_env env, tk_type_table table) {
    tk_texpr_result te;
    if (es.expr.tag == TK_EXPR_IF)         te = type_if_stmt(es.expr.as.if_expr, env, table);
    else if (es.expr.tag == TK_EXPR_MATCH) te = type_match_stmt(es.expr.as.match_expr, env, table);
    else                                   te = tk_typer_expr(es.expr, env, table);
    if (!te.ok) return sfail(te.as.error);
    tk_tstatement node = { .tag = TK_TSTMT_EXPR, .as.expr_stmt = { te.as.value } };
    return sok(node, env);
}

tk_typed_stmt_result tk_type_statement(tk_statement s, tk_env env, tk_type_table table) {
    switch (s.tag) {
        case TK_STMT_BINDING:  return type_binding(s.as.binding, env, table);
        case TK_STMT_ASSIGN:   return type_assign(s.as.assign, env, table);
        case TK_STMT_RETURN:   return type_return(s.as.ret, env, table);
        case TK_STMT_LOOP:     return type_loop(s.as.loop_stmt, env, table);
        case TK_STMT_BREAK:    return sok((tk_tstatement){ .tag = TK_TSTMT_BREAK }, env);
        case TK_STMT_CONTINUE: return sok((tk_tstatement){ .tag = TK_TSTMT_CONTINUE }, env);
        case TK_STMT_EXPR:     return type_exprstmt(s.as.expr_stmt, env, table);
    }
    return smsg("unknown statement");
}

// ---- items + program ----
static tk_type function_return(tk_function f, tk_type_table table) {
    if (!f.has_return) return unit_t();
    tk_type_result r = tk_resolve_type(f.return_type, table);
    return r.ok ? r.as.value : unit_t();   // collect validated signatures; a bad annotation surfaces there
}

// ---- C5: return / final-expr vs the declared return type (see the Teko twin; NULL = ok) ----
static bool assignable_to(tk_type from, tk_type to) {   // B.14 — variant member inclusion
    if (tk_type_eq(&from, &to)) return true;
    if (to.tag == TK_TYPE_VARIANT)
        for (size_t i = 0; i < to.as.variant.len; i += 1)
            if (tk_type_eq(&from, &to.as.variant.members[i])) return true;
    return false;
}
static const char *check_returns(const tk_tstatement *stmts, size_t n, tk_type ret);   // fwd (mutual)
static const char *check_returns_inexpr(const tk_texpr *e, tk_type ret) {
    if (e->tag == TK_TEXPR_IF) {
        const char *t = check_returns(e->as.if_expr.then_blk, e->as.if_expr.nthen, ret); if (t) return t;
        return check_returns(e->as.if_expr.else_blk, e->as.if_expr.nelse, ret);
    }
    return NULL;   // match-arm returns await the divergence item
}
static const char *check_return_stmt(const tk_tstatement *s, tk_type ret) {
    switch (s->tag) {
        case TK_TSTMT_RETURN:
            if (s->as.ret.has_value)
                return assignable_to(s->as.ret.value.type, ret) ? NULL
                     : "return value does not match the function's declared return type";
            return assignable_to(unit_t(), ret) ? NULL
                 : "bare `return` in a function that declares a non-Unit return type";
        case TK_TSTMT_LOOP: return check_returns(s->as.loop_stmt.body, s->as.loop_stmt.nbody, ret);
        case TK_TSTMT_EXPR: return check_returns_inexpr(&s->as.expr_stmt.expr, ret);
        default:            return NULL;
    }
}
static const char *check_returns(const tk_tstatement *stmts, size_t n, tk_type ret) {
    for (size_t i = 0; i < n; i += 1) { const char *e = check_return_stmt(&stmts[i], ret); if (e) return e; }
    return NULL;
}
static const char *check_trailing_value(const tk_tstatement *stmts, size_t n, tk_type ret) {
    if (n == 0) return NULL;
    const tk_tstatement *last = &stmts[n - 1];
    if (last->tag != TK_TSTMT_EXPR) return NULL;   // trailing loop/if/match → no claim (guard)
    return assignable_to(last->as.expr_stmt.expr.type, ret) ? NULL
         : "the function's final expression does not match its declared return type";
}

tk_tfunction_result tk_type_function(tk_function f, tk_env env, tk_type_table table) {
    tk_env local = env;
    for (size_t i = 0; i < f.nparams; i += 1) {           // params immutable (B.21)
        tk_type_result pt = tk_resolve_type(f.params[i].type_ann, table);
        if (!pt.ok) return (tk_tfunction_result){ .ok = false, .as.error = pt.as.error };
        local = tk_env_define(local, f.params[i].name, pt.as.value, false);
    }
    tk_type ret = function_return(f, table);
    tk_typed_block_result tb = tk_type_block(f.body, f.nbody, local, table);
    if (!tb.ok) return (tk_tfunction_result){ .ok = false, .as.error = tb.as.error };
    { const char *why = check_returns(tb.as.value.stmts, tb.as.value.n, ret);        // C5: each `return e` matches
      if (why) return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make(why) }; }
    { const char *why = check_trailing_value(tb.as.value.stmts, tb.as.value.n, ret); // C5: trailing value (when present)
      if (why) return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make(why) }; }
    tk_tfunction tf = { .name = f.name, .params = f.params, .nparams = f.nparams,
                        .return_type = ret, .body = tb.as.value.stmts, .nbody = tb.as.value.n,
                        .is_exp = f.is_exp, .has_doc = f.has_doc, .doc = f.doc };
    return (tk_tfunction_result){ .ok = true, .as.value = tf };
}

tk_titem_result tk_type_item(tk_item item, tk_env env, tk_type_table table) {
    switch (item.tag) {
        case TK_ITEM_FUNCTION: {
            tk_tfunction_result tf = tk_type_function(item.as.function, env, table);
            if (!tf.ok) return (tk_titem_result){ .ok = false, .as.error = tf.as.error };
            return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_FUNCTION, .as.function = tf.as.value } };
        }
        case TK_ITEM_TYPE_DECL: return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_TYPE_DECL, .as.type_decl = item.as.type_decl } };
        case TK_ITEM_USE:       return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_USE, .as.use_decl = item.as.use_decl } };
        case TK_ITEM_STATEMENT: {
            tk_typed_stmt_result ts = tk_type_statement(item.as.statement, env, table);
            if (!ts.ok) return (tk_titem_result){ .ok = false, .as.error = ts.as.error };
            return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_STATEMENT, .as.statement = ts.as.value.node } };
        }
    }
    return (tk_titem_result){ .ok = false, .as.error = tk_error_make("unknown item") };
}

tk_tprogram_result tk_type_program(tk_program program) {
    tk_collected_result c = tk_collect(program);
    if (!c.ok) return (tk_tprogram_result){ .ok = false, .as.error = c.as.error };
    tk_titem_list items = tk_titem_list_empty();
    // Thread the env across LOOSE top-level statements (the virtual-main): a `let a`
    // must enter scope for the statements that follow it (mirrors type_block's env
    // threading). Non-statement items (functions/types/uses) are typed against the
    // collected env and do not advance it.
    tk_env cur = c.as.value.env;
    for (size_t i = 0; i < program.len; i += 1) {
        if (program.items[i].tag == TK_ITEM_STATEMENT) {
            tk_typed_stmt_result ts = tk_type_statement(program.items[i].as.statement, cur, c.as.value.types);
            if (!ts.ok) return (tk_tprogram_result){ .ok = false, .as.error = ts.as.error };
            cur = ts.as.value.env;   // advance scope for subsequent statements
            items = tk_titem_list_push(items, (tk_titem){ .tag = TK_TITEM_STATEMENT, .as.statement = ts.as.value.node });
            continue;
        }
        tk_titem_result ti = tk_type_item(program.items[i], c.as.value.env, c.as.value.types);
        if (!ti.ok) return (tk_tprogram_result){ .ok = false, .as.error = ti.as.error };
        items = tk_titem_list_push(items, ti.as.value);
    }
    return (tk_tprogram_result){ .ok = true, .as.value = { .items = items.ptr, .nitems = items.len } };
}
