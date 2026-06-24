// src/vm/vm.c   (namespace 'teko::vm')
//
// D1 — the typed-tree VM / interpreter. A tree-walking evaluator over the CHECKED
// typed tree (tk_tprogram). MIRRORS src/codegen/codegen_c.c semantics node-for-node
// (the differential-correctness anchor): same coverage frontier, same ÷0 / cast guards
// (F3), same print/println/assert builtin recognition, same VIRTUAL-MAIN return rules.
//
// LAWS: M.0 (tagged-union value model — metal, no boxing); M.1 (fail loud — every panic
// routes through the runtime's tk_panic_* so the message MATCHES the native path); M.3
// (honest — any node not yet interpreted ABORTS with a clear "vm: … not yet supported",
// never a wrong-silent value; this frontier matches codegen's).
#include "vm.h"

#include "../lexer/token.h"   // tk_token_kind operator kinds
#include "../parser/ast.h"    // tk_bind_kind, tk_bind_target, tk_path
#include "../text/text.h"     // tk_str, tk_byte

// NOTE: we do NOT #include "teko_rt.h" — it re-typedefs tk_str / tk_byte (it is a
// SELF-CONTAINED header for GENERATED programs, distinct from the compiler's text.h),
// which collides with text.h's identical-shape definitions already pulled in via tast.h.
// Instead we forward-declare exactly the runtime symbols the VM calls, against the
// compiler's tk_str. The runtime's tk_str and text.h's tk_str are bit-identical
// ({const tk_byte*, size_t}) — same ABI — so the link is sound (M.0).
//
// The F3 numeric guards (tk_div_*/tk_mod_*/tk_to_*) are STATIC INLINE in teko_rt.h, so
// they cannot be linked from a prototype; the VM re-derives those checks inline below
// (calling only the shared tk_panic_div0 / tk_panic_cast so the MESSAGES match — M.1).
void tk_print(tk_str s);
void tk_println(tk_str s);
_Noreturn void tk_panic_div0(void);
_Noreturn void tk_panic_cast(void);
void teko__assert__is_true(bool c);
void teko__assert__is_false(bool c);
void teko__assert__str_contains(tk_str hay, tk_str needle);

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>           // malloc/realloc/free, abort
#include <stdio.h>            // fputs, stderr
#include <string.h>           // memcmp

// =========================================================================
// M.3 honest barrier. A node the VM does not yet interpret is NOT silently
// wrong — it aborts loud with a clear message. Distinct prefix ("vm:") from
// codegen's ("codegen:"), but the SAME frontier (see each call site).
// =========================================================================
_Noreturn static void vm_unsupported(const char *msg) {
    fputs("vm: ", stderr);
    fputs(msg, stderr);
    fputs("\n", stderr);
    abort();
}

// =========================================================================
// The VALUE model (M.0 — a tagged union; ints all held in a 64-bit carrier,
// signedness/width tracked alongside so the F3 guards can reproduce codegen's
// panic checks exactly). Bool, str (a {ptr,len} view), list, unit complete it.
// =========================================================================
typedef struct tk_value tk_value;

typedef enum { TK_VAL_INT, TK_VAL_BOOL, TK_VAL_STR, TK_VAL_LIST, TK_VAL_UNIT } tk_value_tag;

// the value list — TK_LIST over tk_value (core.h convention). Declared after tk_value.
typedef struct { tk_value *ptr; size_t len; size_t cap; } tk_value_list;

struct tk_value {
    tk_value_tag tag;
    union {
        // INT: one 64-bit carrier + how to read it. `is_signed` picks int64 vs uint64
        // reading; `width` (8/16/32/64) is the prim's bit-width. Both come from the
        // node's resolved prim — exactly what codegen uses to select tk_div_*/tk_to_*.
        struct { uint64_t bits; bool is_signed; int width; } i;
        bool          b;
        tk_str        s;
        tk_value_list list;
    } as;
};

static tk_value v_unit(void)        { return (tk_value){ .tag = TK_VAL_UNIT }; }
static tk_value v_bool(bool x)      { return (tk_value){ .tag = TK_VAL_BOOL, .as.b = x }; }
static tk_value v_str(tk_str s)     { return (tk_value){ .tag = TK_VAL_STR, .as.s = s }; }
static tk_value v_int(uint64_t bits, bool is_signed, int width) {
    return (tk_value){ .tag = TK_VAL_INT, .as.i = { .bits = bits, .is_signed = is_signed, .width = width } };
}

// Read an INT value's signed view (sign-extended from its width). Used by signed ops
// and by signed-source cast carriers (mirrors codegen's "_s" carrier choice).
static int64_t v_as_i64(tk_value v) {
    return (int64_t)v.as.i.bits;   // bits already hold the value in two's complement
}
static uint64_t v_as_u64(tk_value v) { return v.as.i.bits; }

// =========================================================================
// prim helpers — the VM's copy of codegen_c.c's prim_is_signed / prim_width /
// cast_may_lose (verbatim semantics; this is the mirror). Integer prims only.
// =========================================================================
static bool prim_is_signed(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_I8: case TK_PRIM_I16: case TK_PRIM_I32: case TK_PRIM_I64: return true;
        default: return false;
    }
}
static int prim_width(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:  case TK_PRIM_I8:  return 8;
        case TK_PRIM_U16: case TK_PRIM_I16: return 16;
        case TK_PRIM_U32: case TK_PRIM_I32: return 32;
        case TK_PRIM_U64: case TK_PRIM_I64: return 64;
        case TK_PRIM_BOOL: return 1;
    }
    return 0;
}
static bool prim_is_int(tk_prim_kind k) { return k != TK_PRIM_BOOL; }

// cast src->dst may lose data (needs a runtime guard) — VERBATIM from codegen_c.c.
static bool cast_may_lose(tk_prim_kind src, tk_prim_kind dst) {
    bool ss = prim_is_signed(src), ds = prim_is_signed(dst);
    int sw = prim_width(src), dw = prim_width(dst);
    if (ss == ds)  return sw > dw;
    if (!ss && ds) return sw >= dw;
    /* ss && !ds */ return true;
}

// F3 checked div/mod — re-derives teko_rt.h's tk_div_*/tk_mod_* (which are static inline
// and thus not linkable). Same single-check, same PANIC path (tk_panic_div0 ->
// "teko: panic: division by zero"), so the message MATCHES the native path (M.1). Width
// truncation is applied by norm_int at the call site, mirroring each helper's cast.
static uint64_t checked_div_u(uint64_t a, uint64_t b) { if (b == 0) tk_panic_div0(); return a / b; }
static uint64_t checked_mod_u(uint64_t a, uint64_t b) { if (b == 0) tk_panic_div0(); return a % b; }
static int64_t  checked_div_i(int64_t  a, int64_t  b) { if (b == 0) tk_panic_div0(); return a / b; }
static int64_t  checked_mod_i(int64_t  a, int64_t  b) { if (b == 0) tk_panic_div0(); return a % b; }

// Mask an unsigned result back into its width (codegen relies on C's fixed-width
// wraparound for u8/u16/u32; the VM reproduces it on the 64-bit carrier).
static uint64_t mask_to_width(uint64_t v, int width) {
    if (width >= 64) return v;
    return v & ((((uint64_t)1) << width) - 1);
}
// Sign-extend a width-bit two's-complement value held in a 64-bit carrier, then
// re-truncate to the carrier (so v_as_i64 reads it correctly).
static uint64_t sext_to_width(uint64_t v, int width) {
    if (width >= 64) return v;
    uint64_t m = mask_to_width(v, width);
    uint64_t sign = ((uint64_t)1) << (width - 1);
    if (m & sign) m |= ~((((uint64_t)1) << width) - 1);   // set high bits
    return m;
}

// Normalize a raw 64-bit arithmetic result into a width/signedness-correct INT value
// (matches C's fixed-width integer result for the node's prim).
static tk_value norm_int(uint64_t raw, bool is_signed, int width) {
    uint64_t bits = is_signed ? sext_to_width(raw, width) : mask_to_width(raw, width);
    return v_int(bits, is_signed, width);
}

// Pull the integer prim out of a node's resolved type (the node IS int-typed by the
// checker at every use site below). A non-prim/non-int there is an internal invariant
// break, reported honestly.
static tk_prim_kind expr_int_prim(const tk_texpr *e, const char *ctx) {
    if (e->type.tag != TK_TYPE_PRIM || !prim_is_int(e->type.as.prim)) vm_unsupported(ctx);
    return e->type.as.prim;
}

// =========================================================================
// ENV — a simple chained frame mapping var name -> value. Mirrors the checker's
// lexical scoping: a binding adds to the current frame; assign mutates the nearest
// existing slot; function calls run in a FRESH root frame (no closure capture — M0
// has no captures, matching codegen's flat C functions).
// =========================================================================
typedef struct tk_slot { tk_str name; tk_value val; struct tk_slot *next; } tk_slot;
typedef struct { tk_slot *head; } tk_venv;

static bool name_eq(tk_str a, tk_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}
static tk_slot *env_find(tk_venv *env, tk_str name) {
    for (tk_slot *s = env->head; s != NULL; s = s->next)
        if (name_eq(s->name, name)) return s;
    return NULL;
}
static void env_define(tk_venv *env, tk_str name, tk_value val) {
    tk_slot *s = malloc(sizeof *s);
    if (s == NULL) abort();
    s->name = name; s->val = val; s->next = env->head;
    env->head = s;
}
static void env_free(tk_venv *env) {
    tk_slot *s = env->head;
    while (s != NULL) { tk_slot *n = s->next; free(s); s = n; }
    env->head = NULL;
}

// =========================================================================
// CONTROL FLOW — exec results carry the non-local exits a block can produce.
// Mirrors codegen's C control structure: return / break / continue. A `return`
// value rides `ret`. (No exceptions; panics exit the process via tk_panic_*.)
// =========================================================================
typedef enum { TK_FLOW_NORMAL, TK_FLOW_RETURN, TK_FLOW_BREAK, TK_FLOW_CONTINUE } tk_flow_kind;
typedef struct { tk_flow_kind kind; bool has_ret; tk_value ret; } tk_flow;

// The whole program (so a call expr can find a top-level function by name).
static tk_tprogram g_prog;

// forward decls
static tk_value tk_vm_eval_expr(const tk_texpr *e, tk_venv *env);
static tk_flow  tk_vm_exec_block(const tk_tstatement *body, size_t n, tk_venv *env);

// =========================================================================
// BUILTIN call recognition — VERBATIM mirror of codegen_c.c's CALL lowering:
// `print`/`println` and `teko::assert::*` are non-shadowable builtins, recognized
// by the path's LAST segment when the path is single-segment OR rooted at `teko`.
// =========================================================================
static bool seg_is(tk_str s, const char *lit) {
    size_t n = 0; while (lit[n]) n += 1;
    return s.len == n && (n == 0 || memcmp(s.ptr, lit, n) == 0);
}

// Try the builtins; returns true and sets *out if `p` named one (and runs it).
static bool try_builtin_call(tk_path p, const tk_texpr *args, size_t nargs,
                             tk_venv *env, tk_value *out) {
    if (p.len < 1) return false;
    tk_str last = p.segments[p.len - 1].name;
    bool addressable = (p.len == 1) || seg_is(p.segments[0].name, "teko");
    if (!addressable) return false;

    // print / println — write a str arg to stdout via the runtime (SAME bytes/newline
    // as the native path: tk_print writes exactly len bytes; tk_println adds one '\n').
    if (seg_is(last, "print") || seg_is(last, "println")) {
        if (nargs != 1) vm_unsupported("print/println expects exactly one argument");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_STR) vm_unsupported("print/println on a non-str value not yet supported");
        if (seg_is(last, "print")) tk_print(a.as.s); else tk_println(a.as.s);
        *out = v_unit();
        return true;
    }

    // teko::assert::* — the injected testing assertions. Recognized as a `teko`-rooted
    // path whose tail is `assert::<name>` (mirrors codegen emitting teko__assert__<name>;
    // the runtime symbols PANIC with the canonical "assertion failed: <name>" — M.1).
    bool assert_ns = (p.len >= 2) && seg_is(p.segments[0].name, "teko")
                                  && seg_is(p.segments[p.len - 2].name, "assert");
    if (assert_ns) {
        if (seg_is(last, "is_true") || seg_is(last, "is_false")) {
            if (nargs != 1) vm_unsupported("teko::assert::is_true/is_false expects one argument");
            tk_value a = tk_vm_eval_expr(&args[0], env);
            if (a.tag != TK_VAL_BOOL) vm_unsupported("teko::assert::is_true/is_false on a non-bool not yet supported");
            if (seg_is(last, "is_true")) teko__assert__is_true(a.as.b);
            else                          teko__assert__is_false(a.as.b);
            *out = v_unit();
            return true;
        }
        if (seg_is(last, "str_contains")) {
            if (nargs != 2) vm_unsupported("teko::assert::str_contains expects two arguments");
            tk_value hay = tk_vm_eval_expr(&args[0], env);
            tk_value ndl = tk_vm_eval_expr(&args[1], env);
            if (hay.tag != TK_VAL_STR || ndl.tag != TK_VAL_STR)
                vm_unsupported("teko::assert::str_contains on non-str args not yet supported");
            teko__assert__str_contains(hay.as.s, ndl.as.s);
            *out = v_unit();
            return true;
        }
        // other assert::* (equals/is_ok/...) need generics — DEFERRED (matches the seed).
        vm_unsupported("vm: this teko::assert builtin not yet supported (needs generics)");
    }
    return false;
}

// Find a top-level user function by (single-segment) name. M0 calls are single-segment
// identifiers joined by "__" in codegen; here we match the joined path against a function
// name. Single-segment is the common case; multi-segment user calls are honest-deferred.
static const tk_tfunction *find_function(tk_path p) {
    if (p.len != 1) return NULL;   // multi-segment user calls deferred (matches M0)
    tk_str name = p.segments[0].name;
    for (size_t i = 0; i < g_prog.nitems; i += 1) {
        if (g_prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        if (name_eq(g_prog.items[i].as.function.name, name))
            return &g_prog.items[i].as.function;
    }
    return NULL;
}

// =========================================================================
// EXPRESSIONS -> tk_value. The node coverage MIRRORS codegen_c.c's emit_expr:
//   Number, Var, Str, Byte, Binary, Unary, Compare, Cast, Call, FieldAccess.
//   IfExpr / MatchExpr as a VALUE = honest-unsupported (same frontier as codegen).
// =========================================================================
static tk_value eval_binary(const tk_texpr *e, tk_venv *env) {
    tk_token_kind op = e->as.binary.op;

    // Logical && / || — bool-typed, short-circuit (matches C's && / ||).
    if (op == TK_TOKEN_ANDAND || op == TK_TOKEN_OROR) {
        tk_value l = tk_vm_eval_expr(e->as.binary.left, env);
        if (l.tag != TK_VAL_BOOL) vm_unsupported("logical operator on a non-bool not yet supported");
        if (op == TK_TOKEN_ANDAND && !l.as.b) return v_bool(false);
        if (op == TK_TOKEN_OROR  &&  l.as.b) return v_bool(true);
        tk_value r = tk_vm_eval_expr(e->as.binary.right, env);
        if (r.tag != TK_VAL_BOOL) vm_unsupported("logical operator on a non-bool not yet supported");
        return v_bool(r.as.b);
    }

    // Integer binary ops. The result prim (and thus width/signedness) is the node's
    // type — EXACTLY what codegen's prim_div_tag / fixed-width C arithmetic use.
    tk_prim_kind rp = expr_int_prim(e, "binary arithmetic on a non-integer type not yet supported");
    bool is_signed = prim_is_signed(rp);
    int  width     = prim_width(rp);

    tk_value lv = tk_vm_eval_expr(e->as.binary.left, env);
    tk_value rv = tk_vm_eval_expr(e->as.binary.right, env);
    if (lv.tag != TK_VAL_INT || rv.tag != TK_VAL_INT)
        vm_unsupported("binary arithmetic on a non-integer value not yet supported");

    // F3 (M.1): `/` and `%` route through the SAME runtime guards codegen emits, so a
    // zero divisor PANICS with the identical "division by zero" message. Width/signedness
    // select the helper, mirroring prim_div_tag.
    if (op == TK_TOKEN_SLASH || op == TK_TOKEN_PERCENT) {
        bool isdiv = (op == TK_TOKEN_SLASH);
        // Operands are already in-range for their width; the quotient/remainder of
        // in-range values is identical at any carrier width — so a 64-bit checked op +
        // norm_int width-truncation reproduces tk_div_<width>/tk_mod_<width> exactly.
        uint64_t res;
        if (is_signed) {
            int64_t a = v_as_i64(lv), b = v_as_i64(rv);
            res = (uint64_t)(isdiv ? checked_div_i(a, b) : checked_mod_i(a, b));
        } else {
            uint64_t a = v_as_u64(lv), b = v_as_u64(rv);
            res = isdiv ? checked_div_u(a, b) : checked_mod_u(a, b);
        }
        return norm_int(res, is_signed, width);
    }

    // +,-,*,&,|,^,<<,>> — plain fixed-width arithmetic (overflow guarding DEFERRED to
    // build profiles, exactly as codegen leaves +,-,* as plain C — out of scope here).
    uint64_t a = v_as_u64(lv), b = v_as_u64(rv), raw;
    switch (op) {
        case TK_TOKEN_PLUS:  raw = a + b; break;
        case TK_TOKEN_MINUS: raw = a - b; break;
        case TK_TOKEN_STAR:  raw = a * b; break;
        case TK_TOKEN_AMP:   raw = a & b; break;
        case TK_TOKEN_PIPE:  raw = a | b; break;
        case TK_TOKEN_CARET: raw = a ^ b; break;
        case TK_TOKEN_SHL:   raw = a << (b & 63); break;
        case TK_TOKEN_SHR:
            if (is_signed) raw = (uint64_t)(v_as_i64(lv) >> (b & 63));
            else           raw = a >> (b & 63);
            break;
        default: vm_unsupported("binary operator not yet supported");
    }
    return norm_int(raw, is_signed, width);
}

static tk_value eval_unary(const tk_texpr *e, tk_venv *env) {
    tk_token_kind op = e->as.unary.op;
    tk_value x = tk_vm_eval_expr(e->as.unary.operand, env);
    switch (op) {
        case TK_TOKEN_BANG:
            if (x.tag != TK_VAL_BOOL) vm_unsupported("logical not on a non-bool not yet supported");
            return v_bool(!x.as.b);
        case TK_TOKEN_MINUS: {
            tk_prim_kind rp = expr_int_prim(e, "unary minus on a non-integer type not yet supported");
            if (x.tag != TK_VAL_INT) vm_unsupported("unary minus on a non-integer value not yet supported");
            return norm_int((uint64_t)(- (int64_t)x.as.i.bits), prim_is_signed(rp), prim_width(rp));
        }
        case TK_TOKEN_TILDE: {
            tk_prim_kind rp = expr_int_prim(e, "bitwise not on a non-integer type not yet supported");
            if (x.tag != TK_VAL_INT) vm_unsupported("bitwise not on a non-integer value not yet supported");
            return norm_int(~x.as.i.bits, prim_is_signed(rp), prim_width(rp));
        }
        default: vm_unsupported("unary operator not yet supported");
    }
}

// One adjacent comparison a <op> b — codegen lowers chains to && of these.
static bool cmp_pair(tk_value l, tk_token_kind op, tk_value r) {
    if (l.tag == TK_VAL_INT && r.tag == TK_VAL_INT) {
        bool sgn = l.as.i.is_signed;   // operands share signedness (checker-typed)
        if (sgn) {
            int64_t a = v_as_i64(l), b = v_as_i64(r);
            switch (op) {
                case TK_TOKEN_EQEQ: return a == b; case TK_TOKEN_NE: return a != b;
                case TK_TOKEN_LT:   return a <  b; case TK_TOKEN_LE: return a <= b;
                case TK_TOKEN_GT:   return a >  b; case TK_TOKEN_GE: return a >= b;
                default: vm_unsupported("comparison operator not yet supported");
            }
        } else {
            uint64_t a = v_as_u64(l), b = v_as_u64(r);
            switch (op) {
                case TK_TOKEN_EQEQ: return a == b; case TK_TOKEN_NE: return a != b;
                case TK_TOKEN_LT:   return a <  b; case TK_TOKEN_LE: return a <= b;
                case TK_TOKEN_GT:   return a >  b; case TK_TOKEN_GE: return a >= b;
                default: vm_unsupported("comparison operator not yet supported");
            }
        }
    }
    if (l.tag == TK_VAL_BOOL && r.tag == TK_VAL_BOOL) {
        switch (op) {
            case TK_TOKEN_EQEQ: return l.as.b == r.as.b;
            case TK_TOKEN_NE:   return l.as.b != r.as.b;
            default: vm_unsupported("ordered comparison on bool not yet supported");
        }
    }
    vm_unsupported("comparison on these value kinds not yet supported");
}

static tk_value eval_compare(const tk_texpr *e, tk_venv *env) {
    size_t nrest = e->as.compare.nrest;
    if (nrest == 0) return tk_vm_eval_expr(e->as.compare.first, env);   // degenerate (matches codegen)
    // chained a<b<c -> AND over adjacent pairs. Mirror codegen, but only re-evaluate
    // each operand once per its appearances; codegen emits each twice — pure exprs, so
    // value-equivalent. (Operands here are checker-pure leaf/var/number in M0.)
    const tk_texpr *prev = e->as.compare.first;
    tk_value prevv = tk_vm_eval_expr(prev, env);
    bool acc = true;
    for (size_t i = 0; i < nrest; i += 1) {
        tk_tcmp_term term = e->as.compare.rest[i];
        tk_value rv = tk_vm_eval_expr(term.operand, env);
        acc = acc && cmp_pair(prevv, term.op, rv);
        prevv = rv;
    }
    return v_bool(acc);
}

static tk_value eval_cast(const tk_texpr *e, tk_venv *env) {
    const tk_texpr *inner = e->as.cast.expr;
    tk_value iv = tk_vm_eval_expr(inner, env);

    // Same guard predicate as codegen: both sides int prims AND cast_may_lose ->
    // route the value through the runtime tk_to_* check (PANIC "impossible conversion"
    // when out of range). The carrier is picked by SOURCE signedness, exactly as codegen
    // chooses the "_s" / "_u" suffix.
    bool both_int = e->type.tag == TK_TYPE_PRIM && inner->type.tag == TK_TYPE_PRIM
                 && prim_is_int(e->type.as.prim) && prim_is_int(inner->type.as.prim);
    if (!both_int) {
        // Non-int casts: codegen only supports int targets here; anything else is the
        // honest frontier. (Bool<->int and str casts are not in M0.)
        vm_unsupported("cast to/from a non-integer type not yet supported");
    }
    tk_prim_kind dst = e->type.as.prim, src = inner->type.as.prim;
    if (iv.tag != TK_VAL_INT) vm_unsupported("cast of a non-integer value not yet supported");

    bool dsigned = prim_is_signed(dst);
    int  dwidth  = prim_width(dst);

    if (cast_may_lose(src, dst)) {
        // Re-derive teko_rt.h's tk_to_*_s/_u range checks (static inline -> not linkable).
        // The carrier matches codegen's: signed source rides an int64 carrier, unsigned a
        // uint64 carrier. Out of the DESTINATION range -> tk_panic_cast ("impossible
        // conversion"), identical to the native path (M.1).
        // Destination representable range as a closed interval, on the wider signed axis.
        int64_t lo, hi;            // dst range (hi for u64 handled specially below)
        if (dsigned) {
            // i8/i16/i32/i64
            if (dwidth >= 64) { lo = INT64_MIN; hi = INT64_MAX; }
            else { lo = -(((int64_t)1) << (dwidth - 1)); hi = (((int64_t)1) << (dwidth - 1)) - 1; }
        } else {
            lo = 0; hi = 0;        // hi unused for unsigned dst (checked via u-bound)
        }
        if (prim_is_signed(src)) {
            int64_t v = v_as_i64(iv);
            if (dsigned) {
                if (v < lo || v > hi) tk_panic_cast();
                return norm_int((uint64_t)v, true, dwidth);
            }
            // signed source -> unsigned dst (tk_to_u*_s): negatives never fit; upper bound
            // for u8/u16/u32 is 2^w-1; u64 source-signed only needs the negative check.
            if (v < 0) tk_panic_cast();
            if (dwidth < 64 && (uint64_t)v > ((((uint64_t)1) << dwidth) - 1)) tk_panic_cast();
            return norm_int((uint64_t)v, false, dwidth);
        } else {
            uint64_t v = v_as_u64(iv);
            if (dsigned) {
                // unsigned source -> signed dst (tk_to_i*_u): upper bound is the signed max.
                uint64_t smax = (dwidth >= 64) ? (uint64_t)INT64_MAX
                                               : ((((uint64_t)1) << (dwidth - 1)) - 1);
                if (v > smax) tk_panic_cast();
                return norm_int(v, true, dwidth);
            }
            // unsigned source -> unsigned dst (tk_to_u*_u): upper bound 2^w-1 (u64 never narrows).
            if (dwidth < 64 && v > ((((uint64_t)1) << dwidth) - 1)) tk_panic_cast();
            return norm_int(v, false, dwidth);
        }
    }
    // Widening / same-type / lossless: a plain reinterpret to the target width
    // (codegen emits the bare C cast). Re-normalize into the destination prim.
    return norm_int(iv.as.i.bits, dsigned, dwidth);
}

static tk_value eval_call(const tk_texpr *e, tk_venv *env) {
    tk_path p = e->as.call.callee;
    const tk_texpr *args = e->as.call.args;
    size_t nargs = e->as.call.nargs;

    tk_value out;
    if (try_builtin_call(p, args, nargs, env, &out)) return out;

    const tk_tfunction *fn = find_function(p);
    if (fn == NULL) vm_unsupported("call to an unknown function not yet supported");
    // M0/codegen: functions take NO params (codegen fails on params). Honest frontier.
    if (fn->nparams != 0 || nargs != 0)
        vm_unsupported("function parameters not yet supported");

    // A fresh root frame — no closure capture (M0 functions are flat, like codegen's C).
    tk_venv fenv = { .head = NULL };
    tk_flow fl = tk_vm_exec_block(fn->body, fn->nbody, &fenv);
    env_free(&fenv);
    if (fl.kind == TK_FLOW_RETURN && fl.has_ret) return fl.ret;
    return v_unit();   // a Unit-returning fn (or fell off the end)
}

static tk_value tk_vm_eval_expr(const tk_texpr *e, tk_venv *env) {
    switch (e->tag) {
        case TK_TEXPR_NUMBER: {
            // The literal's prim comes from the node's type; bool literals don't reach here.
            if (e->type.tag != TK_TYPE_PRIM)
                vm_unsupported("number literal with a non-primitive type not yet supported");
            tk_prim_kind k = e->type.as.prim;
            if (k == TK_PRIM_BOOL) return v_bool(e->as.number.value != 0);
            return norm_int((uint64_t)e->as.number.value, prim_is_signed(k), prim_width(k));
        }
        case TK_TEXPR_VAR: {
            tk_slot *s = env_find(env, e->as.var.name);
            if (s == NULL) vm_unsupported("reference to an unbound variable (internal: checker should reject)");
            return s->val;
        }
        case TK_TEXPR_STR:  return v_str(e->as.str.text);
        case TK_TEXPR_BYTE: return v_int((uint64_t)e->as.byte.value, false, 8);   // byte == u8 rep
        case TK_TEXPR_BINARY:       return eval_binary(e, env);
        case TK_TEXPR_UNARY:        return eval_unary(e, env);
        case TK_TEXPR_COMPARE:      return eval_compare(e, env);
        case TK_TEXPR_CAST:         return eval_cast(e, env);
        case TK_TEXPR_CALL:         return eval_call(e, env);
        case TK_TEXPR_FIELD_ACCESS:
            // codegen lowers (recv.field), but named/struct types aren't in M0 — honest frontier.
            vm_unsupported("field access not yet supported (named types not in M0)");
        case TK_TEXPR_IF:    vm_unsupported("if-expression (as a value) not yet supported");   // matches codegen
        case TK_TEXPR_MATCH: vm_unsupported("match-expression (as a value) not yet supported"); // matches codegen
    }
    vm_unsupported("unknown expression not yet supported");
}

// =========================================================================
// STATEMENTS / BLOCKS. Coverage MIRRORS codegen_c.c's emit_stmt:
//   Binding, Assign, Return, ExprStmt, Loop, Break, Continue.
// =========================================================================
static tk_flow flow_normal(void) { return (tk_flow){ .kind = TK_FLOW_NORMAL }; }

static tk_flow exec_stmt(const tk_tstatement *s, tk_venv *env) {
    switch (s->tag) {
        case TK_TSTMT_BINDING: {
            tk_bind_target tgt = s->as.binding.target;
            if (tgt.tag != TK_BIND_SIMPLE)
                vm_unsupported("destructuring binding not yet supported");   // matches codegen
            tk_value v = tk_vm_eval_expr(&s->as.binding.value, env);
            env_define(env, tgt.as.simple.name, v);
            return flow_normal();
        }
        case TK_TSTMT_ASSIGN: {
            tk_slot *slot = env_find(env, s->as.assign.name);
            if (slot == NULL) vm_unsupported("assignment to an unbound variable (internal: checker should reject)");
            tk_token_kind op = s->as.assign.op;
            tk_value rhs = tk_vm_eval_expr(&s->as.assign.value, env);
            if (op == TK_TOKEN_ASSIGN) { slot->val = rhs; return flow_normal(); }
            // compound assign x op= v: only meaningful on ints in M0. Reproduce the op on
            // the slot's int value at its own width/signedness.
            if (slot->val.tag != TK_VAL_INT || rhs.tag != TK_VAL_INT)
                vm_unsupported("compound assignment on a non-integer value not yet supported");
            bool sgn = slot->val.as.i.is_signed; int w = slot->val.as.i.width;
            uint64_t a = slot->val.as.i.bits, b = rhs.as.i.bits, raw;
            switch (op) {
                case TK_TOKEN_PLUSEQ:  raw = a + b; break;
                case TK_TOKEN_MINUSEQ: raw = a - b; break;
                case TK_TOKEN_STAREQ:  raw = a * b; break;
                case TK_TOKEN_AMPEQ:   raw = a & b; break;
                case TK_TOKEN_PIPEEQ:  raw = a | b; break;
                case TK_TOKEN_CARETEQ: raw = a ^ b; break;
                case TK_TOKEN_SHLEQ:   raw = a << (b & 63); break;
                case TK_TOKEN_SHREQ:   raw = sgn ? (uint64_t)((int64_t)a >> (b & 63)) : (a >> (b & 63)); break;
                case TK_TOKEN_SLASHEQ:
                case TK_TOKEN_PERCENTEQ: {
                    // checked: route through the guard so ÷0 PANICS like the native path.
                    bool isdiv = (op == TK_TOKEN_SLASHEQ);
                    if (sgn) raw = (uint64_t)(isdiv ? checked_div_i((int64_t)a,(int64_t)b)
                                                    : checked_mod_i((int64_t)a,(int64_t)b));
                    else     raw = isdiv ? checked_div_u(a,b) : checked_mod_u(a,b);
                    break;
                }
                default: vm_unsupported("assignment operator not yet supported");
            }
            slot->val = norm_int(raw, sgn, w);
            return flow_normal();
        }
        case TK_TSTMT_RETURN: {
            tk_flow fl = { .kind = TK_FLOW_RETURN, .has_ret = s->as.ret.has_value };
            if (s->as.ret.has_value) fl.ret = tk_vm_eval_expr(&s->as.ret.value, env);
            return fl;
        }
        case TK_TSTMT_EXPR:
            (void)tk_vm_eval_expr(&s->as.expr_stmt.expr, env);
            return flow_normal();
        case TK_TSTMT_LOOP: {
            // while(1){…}; break/continue steer it (matches codegen's loop lowering).
            for (;;) {
                tk_flow fl = tk_vm_exec_block(s->as.loop_stmt.body, s->as.loop_stmt.nbody, env);
                if (fl.kind == TK_FLOW_RETURN) return fl;
                if (fl.kind == TK_FLOW_BREAK)  break;
                // NORMAL or CONTINUE: loop again.
            }
            return flow_normal();
        }
        case TK_TSTMT_BREAK:    return (tk_flow){ .kind = TK_FLOW_BREAK };
        case TK_TSTMT_CONTINUE: return (tk_flow){ .kind = TK_FLOW_CONTINUE };
    }
    vm_unsupported("unknown statement not yet supported");
}

static tk_flow tk_vm_exec_block(const tk_tstatement *body, size_t n, tk_venv *env) {
    for (size_t i = 0; i < n; i += 1) {
        tk_flow fl = exec_stmt(&body[i], env);
        if (fl.kind != TK_FLOW_NORMAL) return fl;   // non-local exit short-circuits
    }
    return flow_normal();
}

// =========================================================================
// PUBLIC ENTRY — run the VIRTUAL-MAIN. MIRRORS codegen's main(): the loose
// top-level statements run in order; a `return n` is an early process exit with
// `(int)n`; falling off the end -> 0. Top-level functions are callable.
// =========================================================================
int tk_vm_run(tk_tprogram prog) {
    g_prog = prog;
    tk_venv env = { .head = NULL };
    int code = 0;
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag != TK_TITEM_STATEMENT) continue;   // functions are callable, not run
        tk_flow fl = exec_stmt(&prog.items[i].as.statement, &env);
        if (fl.kind == TK_FLOW_RETURN) {
            if (fl.has_ret) {
                if (fl.ret.tag == TK_VAL_INT)       code = (int)(int64_t)fl.ret.as.i.bits;
                else if (fl.ret.tag == TK_VAL_BOOL) code = fl.ret.as.b ? 1 : 0;
                else                                code = 0;
            }
            break;   // early exit, exactly like main's `return (int)(n);`
        }
        // BREAK/CONTINUE at top level can't escape a loop — checker rejects; treat as no-op.
    }
    env_free(&env);
    return code;
}
