// src/checker/resolve.c
#include "resolve.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>     // snprintf — named error messages

// Build a checker error "<msg>: <name>" — names the offending symbol (M.3 — clarify, never vague).
tk_error tk_error_named(const char *msg, tk_str name) {
    size_t len = strlen(msg) + name.len + 4;
    char *buf = tk_alloc(len); if (!buf) abort();
    snprintf(buf, len, "%s: %.*s", msg, (int)name.len, (const char *)name.ptr);
    return tk_error_make(buf);
}

// (C1.8) the surface spelling of a prim kind — the same names tk_builtin_type accepts (scope.c),
// so a rendered mismatch reads in the user's own vocabulary ("i32", "f64", "bool", …).
static const char *prim_name(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:   return "u8";   case TK_PRIM_U16:  return "u16";
        case TK_PRIM_U32:  return "u32";  case TK_PRIM_U64:  return "u64";
        case TK_PRIM_U128: return "u128";
        case TK_PRIM_I8:   return "i8";   case TK_PRIM_I16:  return "i16";
        case TK_PRIM_I32:  return "i32";  case TK_PRIM_I64:  return "i64";
        case TK_PRIM_I128: return "i128";
        case TK_PRIM_F16:  return "f16";  case TK_PRIM_F32:  return "f32";
        case TK_PRIM_F64:  return "f64";
        case TK_PRIM_BOOL: return "bool";
    }
    return "<prim>";
}

// (C1.8) a fresh whole-compile-lifetime copy of a NUL-terminated string (tk_alloc; no free in the
// bootstrap — M.5 arena-style). Used to hand the static prim/marker spellings back as owned strings
// so callers (the renderer's recursive composers) treat every result uniformly.
static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *out = tk_alloc(n + 1); if (!out) abort();
    memcpy(out, s, n + 1);
    return out;
}

// (C1.8) render a semantic type to a human string for diagnostics. Recursion builds slice/optional/
// variant bottom-up; each result is a fresh tk_alloc'd, whole-compile-lifetime string. See resolve.h.
const char *tk_type_render(tk_type t) {
    switch (t.tag) {
        case TK_TYPE_PRIM:  return dup_cstr(prim_name(t.as.prim));
        case TK_TYPE_BYTE:  return dup_cstr("byte");
        case TK_TYPE_STR:   return dup_cstr("str");
        case TK_TYPE_ERROR: return dup_cstr("error");
        case TK_TYPE_VOID:  return dup_cstr("void");
        case TK_TYPE_NAMED: {
            // a nominal user type — its declared name verbatim.
            size_t n = t.as.named.name.len;
            char *out = tk_alloc(n + 1); if (!out) abort();
            if (n != 0) memcpy(out, t.as.named.name.ptr, n);
            out[n] = '\0';
            return out;
        }
        case TK_TYPE_SLICE: {
            // []<elem>; a SENTINEL slice (element == NULL, the untyped `empty()`) renders "[]_".
            const char *el = t.as.slice.element ? tk_type_render(*t.as.slice.element) : "_";
            size_t cap = strlen(el) + 3;   // "[]" + el + NUL
            char *out = tk_alloc(cap); if (!out) abort();
            snprintf(out, cap, "[]%s", el);
            return out;
        }
        case TK_TYPE_OPTIONAL: {
            // <inner>?; a SENTINEL optional (inner == NULL, a bare `null`) renders "_?".
            const char *in = t.as.optional.inner ? tk_type_render(*t.as.optional.inner) : "_";
            size_t cap = strlen(in) + 2;   // in + "?" + NUL
            char *out = tk_alloc(cap); if (!out) abort();
            snprintf(out, cap, "%s?", in);
            return out;
        }
        case TK_TYPE_VARIANT: {
            // "A | B | …" — join each member with " | ". Two-pass: size, then fill.
            if (t.as.variant.len == 0) return dup_cstr("<empty variant>");
            const char *sep = " | ";
            size_t seplen = strlen(sep);
            size_t cap = 1;   // NUL
            const char **parts = tk_alloc(t.as.variant.len * sizeof *parts); if (!parts) abort();
            for (size_t i = 0; i < t.as.variant.len; i += 1) {
                parts[i] = tk_type_render(t.as.variant.members[i]);
                cap += strlen(parts[i]);
                if (i + 1 < t.as.variant.len) cap += seplen;
            }
            char *out = tk_alloc(cap); if (!out) abort();
            out[0] = '\0';
            for (size_t i = 0; i < t.as.variant.len; i += 1) {
                strcat(out, parts[i]);
                if (i + 1 < t.as.variant.len) strcat(out, sep);
            }
            tk_free0(parts);
            return out;
        }
        case TK_TYPE_FUNC: {
            // a function type — uncommon in user-facing mismatch messages, but render it honestly
            // as "(p0, p1, …) -> ret" rather than a placeholder (M.3 — show what we know).
            const char *ret = t.as.func.ret ? tk_type_render(*t.as.func.ret) : "void";
            const char **ps = NULL;
            size_t cap = strlen("() -> ") + strlen(ret) + 1;
            if (t.as.func.nparams > 0) {
                ps = tk_alloc(t.as.func.nparams * sizeof *ps); if (!ps) abort();
                for (size_t i = 0; i < t.as.func.nparams; i += 1) {
                    ps[i] = tk_type_render(t.as.func.params[i]);
                    cap += strlen(ps[i]);
                    if (i + 1 < t.as.func.nparams) cap += 2;   // ", "
                }
            }
            char *out = tk_alloc(cap); if (!out) abort();
            out[0] = '\0';
            strcat(out, "(");
            for (size_t i = 0; i < t.as.func.nparams; i += 1) {
                strcat(out, ps[i]);
                if (i + 1 < t.as.func.nparams) strcat(out, ", ");
            }
            strcat(out, ") -> "); strcat(out, ret);
            if (ps) tk_free0(ps);
            return out;
        }
    }
    return dup_cstr("<type>");
}

static bool name_eq(tk_str a, tk_str b) {
    return a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0;
}
// box a Type onto the heap — the compiler-managed indirection, made concrete.
static tk_type *box(tk_type t) {
    tk_type *p = tk_alloc(sizeof *p);
    if (!p) abort();
    *p = t;
    return p;
}

tk_decl_result tk_type_table_find(tk_type_table table, tk_str name) {
    for (size_t i = 0; i < table.len; i += 1) {
        if (name_eq(table.ptr[i].name, name)) {
            return (tk_decl_result){ .ok = true, .as.value = table.ptr[i].decl };
        }
    }
    return (tk_decl_result){ .ok = false, .as.error = tk_error_make("not a user type") };
}

// non-static: shared with match.c (the typed pattern checker resolves case/struct names — C7).
tk_type_result resolve_named(tk_path path, tk_type_table table) {
    if (path.len == 0)   // M.1: an empty path is an internal invariant break — an honest error, never a crash
        return (tk_type_result){ .ok = false, .as.error = tk_error_make("internal: empty type path (a void/missing type used where a value type is required)") };
    tk_str name = path.segments[path.len - 1].name;       // seed: last segment
    tk_type_result bt = tk_builtin_type(name);            // u8…u64, byte, str, error
    if (bt.ok) return bt;
    tk_decl_result ut = tk_type_table_find(table, name);  // a user type
    if (ut.ok) {
        // A TRANSPARENT alias `type Name = <type-expr>` resolves THROUGH to the aliased type
        // (self-host parity): `TypeTable = []TypeReg` → a SLICE, `Foo = Circle` → its NAMED
        // struct, etc. struct/enum/variant decls stay NOMINAL (a bare NAMED of `name`).
        // A trivial self-alias chain (`type A = B; type B = A`) is broken by a depth bound:
        // tk_resolve_type → resolve_named re-enters here, so a runaway chain is finite-bounded.
        if (ut.as.value.body.tag == TK_BODY_ALIAS) {
            static int alias_depth = 0;
            if (alias_depth > 64)
                return (tk_type_result){ .ok = false, .as.error = tk_error_make("type alias resolves cyclically (self-referential alias chain)") };
            alias_depth += 1;
            tk_type_result r = tk_resolve_type(ut.as.value.body.as.alias_body.alias, table);
            alias_depth -= 1;
            return r;
        }
        tk_type t = { .tag = TK_TYPE_NAMED, .as.named.name = name };
        return (tk_type_result){ .ok = true, .as.value = t };
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_named("unknown type", name) };
}

// A NAMED type referring to a `variant` decl → its expanded TK_TYPE_VARIANT. A variant decl's
// body IS a union type-expr (A | B | …), so tk_resolve_type on it yields the VARIANT with its
// members; the members resolve to NAMED (resolve_named keeps user types nominal), so this
// terminates and does not recurse into nested named variants. Anything else returns unchanged.
tk_type tk_expand_variant(tk_type t, tk_type_table table) {
    if (t.tag != TK_TYPE_NAMED) return t;
    tk_decl_result d = tk_type_table_find(table, t.as.named.name);
    if (!d.ok || d.as.value.body.tag != TK_BODY_VARIANT) return t;
    tk_type_result ex = tk_resolve_type(d.as.value.body.as.variant_body.type_expr, table);
    return ex.ok ? ex.as.value : t;
}

tk_type_result tk_resolve_type(tk_type_expr te, tk_type_table table) {
    switch (te.tag) {
        case TK_TEXPR_NAMED:
            return resolve_named(te.as.named.path, table);
        case TK_TEXPR_SLICE: {
            tk_type_result el = tk_resolve_type(*te.as.slice.element, table);
            if (!el.ok) return el;
            tk_type t = { .tag = TK_TYPE_SLICE, .as.slice.element = box(el.as.value) };
            return (tk_type_result){ .ok = true, .as.value = t };
        }
        case TK_TEXPR_UNION: {
            tk_type *members = NULL; size_t n = 0;
            for (size_t i = 0; i < te.as.uni.len; i += 1) {
                tk_type_result m = tk_resolve_type(te.as.uni.members[i], table);
                if (!m.ok) { tk_free0(members); return m; }
                // M.1/rule 2 & 3: a variant member must be a COMPLETE type — never
                // `void` (not a value) and never nullable (`T?` — disjoint domain).
                if (m.as.value.tag == TK_TYPE_VOID) {
                    tk_free0(members);
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a variant member may not be `void`") };
                }
                if (m.as.value.tag == TK_TYPE_OPTIONAL) {
                    tk_free0(members);
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a variant member may not be nullable (`T?`) — use `T | …` and mark the whole type `?`") };
                }
                members = tk_realloc0(members, (n + 1) * sizeof *members);
                if (!members) abort();
                members[n] = m.as.value; n += 1;
            }
            tk_type t = { .tag = TK_TYPE_VARIANT, .as.variant = { members, n } };
            return (tk_type_result){ .ok = true, .as.value = t };
        }
        case TK_TEXPR_OPTIONAL: {
            // OptionalType `T?` → TK_TYPE_OPTIONAL{ inner } (REBOOT_PLAN §202). The
            // variant-member-not-nullable rule (above, UNION case) still rejects an
            // optional as a variant member; here we just build the nullable type.
            tk_type_result in = tk_resolve_type(*te.as.optional.inner, table);
            if (!in.ok) return in;
            // an optional of optional collapses (`T??` == `T?`), and `void?` is illegal
            // (void is not a value — M.3); both guarded for honesty.
            if (in.as.value.tag == TK_TYPE_VOID)
                return (tk_type_result){ .ok = false, .as.error = tk_error_make("`void` cannot be made optional (`void?`) — void is not a value (M.3)") };
            if (in.as.value.tag == TK_TYPE_OPTIONAL)
                return in;   // `T??` collapses to `T?`
            tk_type t = { .tag = TK_TYPE_OPTIONAL, .as.optional.inner = box(in.as.value) };
            return (tk_type_result){ .ok = true, .as.value = t };
        }
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("bad type expr") };
}
