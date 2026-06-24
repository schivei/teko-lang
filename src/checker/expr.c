// src/checker/expr.c   (namespace 'teko::checker')
// The expression-typing half of the typed pass (the evolved check_expr family):
// leaves, operators (B.22), call, cast (C2), field access (C3), and the if/match
// VALUE forms (B.20 / B.15). Pairs expr.tks. The statement/function/item/program
// typers live in typer.c — the two TUs see each other via expr.h + typer_internal.h.
#include "expr.h"
#include "typer_internal.h"   // tk_type_block (statement side, for if/match bodies)
#include <string.h>           // memcmp (string-span compares)

// shared from match.c (E5b-2), promoted to non-static for reuse:
tk_env_result tk_check_pattern(tk_pattern p, tk_type subject, tk_env env, tk_type_table table);
// (tk_exhaustive is declared in typer_internal.h)

// ---- shared small helpers (one definition here; expr.h declares them for typer.c) ----
tk_texpr      *tk_box(tk_texpr t) { tk_texpr *p = malloc(sizeof *p); if (!p) abort(); *p = t; return p; }
tk_type        tk_prim_t(tk_prim_kind k) { return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = k }; }
tk_type        tk_unit_t(void)           { return (tk_type){ .tag = TK_TYPE_UNIT }; }
bool           tk_is_bool(tk_type t)     { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }
tk_texpr_result tk_xok(tk_texpr t)     { return (tk_texpr_result){ .ok = true,  .as.value = t }; }
tk_texpr_result tk_xerr(const char *m) { return (tk_texpr_result){ .ok = false, .as.error = tk_error_make(m) }; }
tk_texpr_result tk_xferr(tk_error e)   { return (tk_texpr_result){ .ok = false, .as.error = e }; }

// short aliases for local use (terse, matches the original site spellings).
// NOTE: no `prim` alias — it would clobber the `.as.prim` union member; call tk_prim_t directly.
#define box   tk_box
#define xok   tk_xok
#define xerr  tk_xerr
#define xferr tk_xferr

// ---- file-local predicates (B.22 core — see expr.tks) ----
static bool is_bool(tk_type t)      { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }
static bool is_integer(tk_type t)   { return t.tag == TK_TYPE_PRIM && t.as.prim != TK_PRIM_BOOL; }
static bool is_comparable(tk_type a, tk_type b) { if (is_integer(a) && is_integer(b)) return true; return tk_type_eq(&a, &b); }
static bool op_is_shift(tk_token_kind op) { return op == TK_TOKEN_SHL || op == TK_TOKEN_SHR; }
static bool op_is_arith_bitwise(tk_token_kind op) {
    return op == TK_TOKEN_PLUS || op == TK_TOKEN_MINUS || op == TK_TOKEN_STAR ||
           op == TK_TOKEN_SLASH || op == TK_TOKEN_PERCENT ||
           op == TK_TOKEN_AMP || op == TK_TOKEN_PIPE || op == TK_TOKEN_CARET;
}

// growable lists for the typed children (teko::list realized — file-local stamps).
TK_LIST(tk_tcmp_term,  tk_tcmp_list)
TK_LIST(tk_texpr,      tk_texpr_list)
TK_LIST(tk_tarm,       tk_tarm_list)

// ---- leaves ----
static tk_texpr_result type_var(tk_var v, tk_env env) {
    tk_type_result t = tk_env_lookup(env, v.name); if (!t.ok) return xferr(t.as.error);
    return xok((tk_texpr){ .tag = TK_TEXPR_VAR, .type = t.as.value, .as.var = { v.name } });
}

// ---- operators (same B.22 regimes as check_binary/unary/compare) ----
static tk_texpr_result type_binary(tk_binary b, tk_env env, tk_type_table table) {
    tk_texpr_result l = tk_typer_expr(*b.left,  env, table); if (!l.ok) return l;
    tk_texpr_result r = tk_typer_expr(*b.right, env, table); if (!r.ok) return r;
    tk_type lt = l.as.value.type, rt = r.as.value.type;
    if (op_is_shift(b.op)) {
        if (!is_integer(lt) || !is_integer(rt)) return xerr("shift needs integer operands");
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    if (op_is_arith_bitwise(b.op)) {
        if (!is_integer(lt)) return xerr("arithmetic/bitwise needs an integer");
        if (!tk_type_eq(&lt, &rt)) return xerr("operands must be the same type (no promotion — B.22)");
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    return xerr("not a binary operator");
}

static tk_texpr_result type_unary(tk_unary u, tk_env env, tk_type_table table) {
    tk_texpr_result o = tk_typer_expr(*u.operand, env, table); if (!o.ok) return o;
    tk_type ot = o.as.value.type;
    if (u.op == TK_TOKEN_MINUS || u.op == TK_TOKEN_TILDE) {
        if (!is_integer(ot)) return xerr("unary -/~ needs an integer");
        return xok((tk_texpr){ .tag = TK_TEXPR_UNARY, .type = ot, .as.unary = { u.op, box(o.as.value) } });
    }
    if (u.op == TK_TOKEN_BANG) {
        if (!is_bool(ot)) return xerr("! needs a bool");
        return xok((tk_texpr){ .tag = TK_TEXPR_UNARY, .type = ot, .as.unary = { u.op, box(o.as.value) } });
    }
    return xerr("not a unary operator");
}

static tk_texpr_result type_compare(tk_compare c, tk_env env, tk_type_table table) {
    tk_texpr_result f = tk_typer_expr(*c.first, env, table); if (!f.ok) return f;
    tk_type prev = f.as.value.type;
    tk_tcmp_list terms = tk_tcmp_list_empty();
    for (size_t i = 0; i < c.nrest; i += 1) {
        tk_texpr_result cur = tk_typer_expr(*c.rest[i].operand, env, table); if (!cur.ok) return cur;
        if (!is_comparable(prev, cur.as.value.type)) return xerr("operands are not comparable");
        terms = tk_tcmp_list_push(terms, (tk_tcmp_term){ c.rest[i].op, box(cur.as.value) });
        prev = cur.as.value.type;
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_COMPARE, .type = tk_prim_t(TK_PRIM_BOOL),
                           .as.compare = { box(f.as.value), terms.ptr, terms.len } });
}

static tk_texpr_result type_call(tk_call c, tk_env env, tk_type_table table) {
    tk_str name = c.callee.segments[c.callee.len - 1].name;
    tk_type_result ftr = tk_env_lookup(env, name);   // user functions resolve first;
    if (!ftr.ok) ftr = tk_builtin_fn(name);          // injected, non-shadowable stdlib is the fallback
    if (!ftr.ok) return xferr(ftr.as.error);
    tk_type ft = ftr.as.value;
    if (ft.tag != TK_TYPE_FUNC) return xerr("not a function");
    if (c.nargs != ft.as.func.nparams) return xerr("wrong number of arguments");
    tk_texpr_list args = tk_texpr_list_empty();
    for (size_t i = 0; i < c.nargs; i += 1) {
        tk_texpr_result a = tk_typer_expr(c.args[i], env, table); if (!a.ok) return a;
        if (!tk_type_eq(&a.as.value.type, &ft.as.func.params[i])) return xerr("argument type mismatch");
        args = tk_texpr_list_push(args, a.as.value);
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_CALL, .type = *ft.as.func.ret,
                           .as.call = { c.callee, args.ptr, args.len } });
}

// ---- cast `to` (C2): a DEFINED conversion is allowed; loss is caught, never silent (M.1) ----
// Two-pronged: a constant out-of-range operand → compile error here; a runtime value's loss →
// codegen guard (debug-panic / release-defined, like overflow), deferred (M.4). Bool/non-numeric
// undefined. (Redefines the early compile-forbid rule — see the Redefinitions Index.)
static bool value_fits(int64_t v, tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:   return v >= 0 && v <= 255;
        case TK_PRIM_U16:  return v >= 0 && v <= 65535;
        case TK_PRIM_U32:  return v >= 0 && v <= 4294967295;
        case TK_PRIM_U64:  return v >= 0;                         // i64 max < u64 max
        case TK_PRIM_I8:   return v >= -128 && v <= 127;
        case TK_PRIM_I16:  return v >= -32768 && v <= 32767;
        case TK_PRIM_I32:  return v >= -2147483648 && v <= 2147483647;
        case TK_PRIM_I64:  return true;                          // v is already an i64
        case TK_PRIM_BOOL: return false;                         // guarded by the caller
    }
    return false;
}
// C6: an annotated binding whose value type ≠ T. A numeric literal adopts T if it fits (leaf stays
// i64 — Side D); a non-literal mismatch or out-of-range literal is rejected. NULL = ok.
// Used by the typed type_binding in typer.c (reuses value_fits).
const char *annotated_literal_reason(tk_expr value, tk_type ann) {
    if (value.tag != TK_EXPR_NUMBER || ann.tag != TK_TYPE_PRIM) return "value type does not match annotation";
    if (!value_fits(value.as.number.value, ann.as.prim)) return "literal out of range for the annotated type (M.1 — fail early)";
    return NULL;
}
// is `from -> to` a DEFINED conversion? NULL = yes; else the M.3 barrier message. Any integer ->
// any integer is defined (B — the loss is runtime/codegen's). Only Bool / non-numeric are rejected.
// byte casts AS u8 (B.36 "byte = u8 newtype"): the effective prim kind for range/cast rules.
// false for bool / non-numeric (no cast kind); true with *out set otherwise. M.5 — shared by
// cast_reason + the constant-range check in type_cast.
static bool cast_kind(tk_type t, tk_prim_kind *out) {
    if (t.tag == TK_TYPE_PRIM) {
        if (t.as.prim == TK_PRIM_BOOL) return false;
        *out = t.as.prim;
        return true;
    }
    if (t.tag == TK_TYPE_BYTE) { *out = TK_PRIM_U8; return true; }
    return false;
}
static const char *cast_reason(tk_type from, tk_type to) {
    if (tk_type_eq(&from, &to)) return NULL;                     // same type — a no-op
    if ((from.tag == TK_TYPE_PRIM && from.as.prim == TK_PRIM_BOOL)
        || (to.tag == TK_TYPE_PRIM && to.as.prim == TK_PRIM_BOOL))
        return "bool casts are not defined in the seed";        // bool on either end — distinct message (C2)
    tk_prim_kind kf, kt;
    if (!cast_kind(from, &kf)) return "cast not defined for this type in the seed (Named/Str/Slice/… — pending)";
    if (!cast_kind(to,   &kt)) return "cast not defined: a primitive to a non-primitive type";
    return NULL;                                                // any integer/byte -> any integer/byte (B; byte AS u8)
}
bool tk_cast_ok(tk_type from, tk_type to) { return cast_reason(from, to) == NULL; }

static tk_texpr_result type_cast(tk_cast c, tk_env env, tk_type_table table) {
    tk_texpr_result inner = tk_typer_expr(*c.expr, env, table); if (!inner.ok) return inner;
    tk_type_result tgt = tk_resolve_type(c.target, table);     if (!tgt.ok) return xferr(tgt.as.error);
    const char *why = cast_reason(inner.as.value.type, tgt.as.value);
    if (why != NULL) return xerr(why);
    // fail early (M.1): a constant literal already out of the target's range is a compile error.
    // The target's effective kind comes from cast_kind, so `… to byte` checks the U8 range (0..255).
    tk_prim_kind ck;
    if (c.expr->tag == TK_EXPR_NUMBER && cast_kind(tgt.as.value, &ck)
        && !value_fits(c.expr->as.number.value, ck))
        return xerr("constant out of range for the cast target (M.1 — fail early)");
    return xok((tk_texpr){ .tag = TK_TEXPR_CAST, .type = tgt.as.value, .as.cast = { box(inner.as.value) } });
}

// ---- field access `x.field` (C3): read a struct field; `.type` is the field's resolved type ----
static bool tk_str_eq(tk_str a, tk_str b) { return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0); }

// non-static: shared with match.c (the FieldPattern case forward-declares it — C7a).
tk_type_result field_type(tk_struct_body sb, tk_str field, tk_type_table table) {
    for (size_t i = 0; i < sb.n_fields; i += 1)
        if (tk_str_eq(sb.fields[i].name, field)) return tk_resolve_type(sb.fields[i].type_ann, table);
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("no such field") };
}

static tk_texpr_result type_field_access(tk_field_access fa, tk_env env, tk_type_table table) {
    tk_texpr_result recv = tk_typer_expr(*fa.receiver, env, table); if (!recv.ok) return recv;
    if (recv.as.value.type.tag != TK_TYPE_NAMED) return xerr("field access requires a struct receiver");
    tk_decl_result decl = tk_type_table_find(table, recv.as.value.type.as.named.name);
    if (!decl.ok) return xerr("unknown type for field access");
    if (decl.as.value.body.tag != TK_BODY_STRUCT) return xerr("type is not a struct (no fields)");
    tk_type_result ft = field_type(decl.as.value.body.as.struct_body, fa.field, table);
    if (!ft.ok) return xerr("no such field");
    return xok((tk_texpr){ .tag = TK_TEXPR_FIELD_ACCESS, .type = ft.as.value,
                           .as.field_access = { box(recv.as.value), fa.field } });
}

// forward decls for the if/match VALUE forms (mutual recursion: expr ↔ block).
static tk_texpr_result type_if(tk_if_expr f, tk_env env, tk_type_table table);
static tk_texpr_result type_match(tk_match_expr m, tk_env env, tk_type_table table);

// ---- the expression dispatch (the evolved check_expr) ----
tk_texpr_result tk_typer_expr(tk_expr e, tk_env env, tk_type_table table) {
    switch (e.tag) {
        case TK_EXPR_NUMBER: return xok((tk_texpr){ .tag = TK_TEXPR_NUMBER, .type = tk_prim_t(TK_PRIM_I64), .as.number = { e.as.number.value } });
        case TK_EXPR_STR:    return xok((tk_texpr){ .tag = TK_TEXPR_STR,  .type = (tk_type){ .tag = TK_TYPE_STR },  .as.str  = { e.as.str.text } });
        case TK_EXPR_BYTE:   return xok((tk_texpr){ .tag = TK_TEXPR_BYTE, .type = (tk_type){ .tag = TK_TYPE_BYTE }, .as.byte = { e.as.byte.value } });
        case TK_EXPR_VAR:    return type_var(e.as.var, env);
        case TK_EXPR_BINARY: return type_binary(e.as.binary, env, table);
        case TK_EXPR_UNARY:  return type_unary(e.as.unary, env, table);
        case TK_EXPR_COMPARE:return type_compare(e.as.compare, env, table);
        case TK_EXPR_CALL:   return type_call(e.as.call, env, table);
        case TK_EXPR_IF:     return type_if(e.as.if_expr, env, table);
        case TK_EXPR_MATCH:  return type_match(e.as.match_expr, env, table);
        case TK_EXPR_FIELD_ACCESS: return type_field_access(e.as.field_access, env, table);
        case TK_EXPR_CAST:         return type_cast(e.as.cast, env, table);
        case TK_EXPR_METHOD_CALL:  return xerr("method typing is deferred (B.29 / M.4)");
        case TK_EXPR_PATH:         return xerr("path-expr typing pending (Enum::Member)");
    }
    return xerr("unknown expression");
}

// ---- the value-type a typed block yields (shared with typer.c's if/match stmt forms) ----
tk_type tk_tblock_type(tk_tstatement *stmts, size_t n) {
    if (n == 0) return tk_unit_t();
    if (stmts[n - 1].tag == TK_TSTMT_EXPR) return stmts[n - 1].as.expr_stmt.expr.type;
    return tk_unit_t();
}

// ---- if as a VALUE ----
static tk_texpr_result type_if(tk_if_expr f, tk_env env, tk_type_table table) {
    tk_texpr_result c = tk_typer_expr(*f.cond, env, table); if (!c.ok) return c;
    if (!is_bool(c.as.value.type)) return xerr("an `if` condition must be a bool");
    if (!f.has_else)               return xerr("an `if` used as a value needs an `else`");
    tk_typed_block_result tb = tk_type_block(f.then_blk, f.nthen, env, table); if (!tb.ok) return xferr(tb.as.error);
    tk_typed_block_result eb = tk_type_block(f.else_blk, f.nelse, env, table); if (!eb.ok) return xferr(eb.as.error);
    tk_type tt = tk_tblock_type(tb.as.value.stmts, tb.as.value.n);
    tk_type et = tk_tblock_type(eb.as.value.stmts, eb.as.value.n);
    if (!tk_type_eq(&tt, &et)) return xerr("the `if` branches have different types");
    return xok((tk_texpr){ .tag = TK_TEXPR_IF, .type = tt, .as.if_expr = {
        box(c.as.value), tb.as.value.stmts, tb.as.value.n, true, eb.as.value.stmts, eb.as.value.n } });
}

// ---- one typed arm (shared with typer.c's match STATEMENT form) ----
tk_tarm_result tk_type_arm(tk_arm a, tk_type subject, tk_env env, tk_type_table table) {
    tk_env_result e2 = tk_check_pattern(a.pattern, subject, env, table);
    if (!e2.ok) return (tk_tarm_result){ .ok = false, .as.error = e2.as.error };
    tk_texpr_result body = tk_typer_expr(a.body, e2.as.value, table);
    if (!body.ok) return (tk_tarm_result){ .ok = false, .as.error = body.as.error };
    tk_texpr *bodyp = box(body.as.value);
    tk_texpr *guard = bodyp;                         // gated by has_when (placeholder reuses body)
    if (a.has_when) {
        tk_texpr_result g = tk_typer_expr(a.guard, e2.as.value, table);
        if (!g.ok) return (tk_tarm_result){ .ok = false, .as.error = g.as.error };
        if (!is_bool(g.as.value.type)) return (tk_tarm_result){ .ok = false, .as.error = tk_error_make("a `when` guard must be a bool") };
        guard = box(g.as.value);
    }
    return (tk_tarm_result){ .ok = true, .as.value = { .pattern = a.pattern, .has_when = a.has_when, .guard = guard, .body = bodyp } };
}

// ---- match as a VALUE ----
static tk_texpr_result type_match(tk_match_expr m, tk_env env, tk_type_table table) {
    tk_texpr_result s = tk_typer_expr(*m.subject, env, table); if (!s.ok) return s;
    if (m.narms == 0) return xerr("a `match` needs at least one arm");
    tk_tarm_result a0 = tk_type_arm(m.arms[0], s.as.value.type, env, table); if (!a0.ok) return xferr(a0.as.error);
    tk_type first = a0.as.value.body->type;
    tk_tarm_list arms = tk_tarm_list_empty();
    arms = tk_tarm_list_push(arms, a0.as.value);
    for (size_t i = 1; i < m.narms; i += 1) {
        tk_tarm_result ai = tk_type_arm(m.arms[i], s.as.value.type, env, table); if (!ai.ok) return xferr(ai.as.error);
        if (!tk_type_eq(&ai.as.value.body->type, &first)) return xerr("the `match` arms have different types");
        arms = tk_tarm_list_push(arms, ai.as.value);
    }
    if (!tk_exhaustive(m.arms, m.narms, s.as.value.type)) return xerr("non-exhaustive `match` (cover all cases or add `_`)");
    return xok((tk_texpr){ .tag = TK_TEXPR_MATCH, .type = first, .as.match_expr = { box(s.as.value), arms.ptr, arms.len } });
}
