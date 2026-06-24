// src/checker/revalidate.c
#include "revalidate.h"
#include "check.h"   // tk_check_result

bool tk_cast_ok(tk_type from, tk_type to);   // from typer.c — re-derive cast legality (C2)

static tk_check_result cok(void)         { return (tk_check_result){ .ok = true }; }
static tk_check_result cfail(const char *m) { return (tk_check_result){ .ok = false, .error = tk_error_make(m) }; }
static tk_type         prim(tk_prim_kind k) { return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = k }; }
static bool is_bool(tk_type t)    { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }
static bool is_integer(tk_type t) { return t.tag == TK_TYPE_PRIM && t.as.prim != TK_PRIM_BOOL; }
static bool op_is_shift(tk_token_kind op) { return op == TK_TOKEN_SHL || op == TK_TOKEN_SHR; }
static bool op_is_arith_bitwise(tk_token_kind op) {
    return op == TK_TOKEN_PLUS || op == TK_TOKEN_MINUS || op == TK_TOKEN_STAR ||
           op == TK_TOKEN_SLASH || op == TK_TOKEN_PERCENT ||
           op == TK_TOKEN_AMP || op == TK_TOKEN_PIPE || op == TK_TOKEN_CARET;
}

static tk_check_result node_is(tk_type stored, tk_type expected) {
    return tk_type_eq(&stored, &expected) ? cok()
         : cfail("corrupt typed tree: a node's type does not match its derivation");
}

tk_check_result tk_validate_texpr(const tk_texpr *te) {
    switch (te->tag) {
        case TK_TEXPR_NUMBER: return node_is(te->type, prim(TK_PRIM_I64));
        case TK_TEXPR_STR:    return node_is(te->type, (tk_type){ .tag = TK_TYPE_STR });
        case TK_TEXPR_BYTE:   return node_is(te->type, (tk_type){ .tag = TK_TYPE_BYTE });
        case TK_TEXPR_VAR:    return cok();                          // env-dependent → trust
        case TK_TEXPR_CALL: {
            for (size_t i = 0; i < te->as.call.nargs; i += 1) {
                tk_check_result r = tk_validate_texpr(&te->as.call.args[i]);
                if (!r.ok) return r;
            }
            return cok();
        }
        case TK_TEXPR_BINARY: {
            tk_check_result l = tk_validate_texpr(te->as.binary.left);  if (!l.ok) return l;
            tk_check_result r = tk_validate_texpr(te->as.binary.right); if (!r.ok) return r;
            tk_type lt = te->as.binary.left->type, rt = te->as.binary.right->type;
            tk_token_kind op = te->as.binary.op;
            if (op_is_shift(op)) {
                if (!is_integer(lt) || !is_integer(rt)) return cfail("corrupt: shift operands not integer");
                return node_is(te->type, lt);
            }
            if (op_is_arith_bitwise(op)) {
                if (!is_integer(lt)) return cfail("corrupt: arith/bitwise operand not integer");
                if (!tk_type_eq(&lt, &rt)) return cfail("corrupt: binary operands differ (no promotion)");
                return node_is(te->type, lt);
            }
            return cfail("corrupt: unknown binary operator");
        }
        case TK_TEXPR_UNARY: {
            tk_check_result o = tk_validate_texpr(te->as.unary.operand); if (!o.ok) return o;
            tk_type ot = te->as.unary.operand->type;
            if (te->as.unary.op == TK_TOKEN_BANG) {
                if (!is_bool(ot)) return cfail("corrupt: `!` operand not bool");
                return node_is(te->type, prim(TK_PRIM_BOOL));
            }
            if (!is_integer(ot)) return cfail("corrupt: `-`/`~` operand not integer");
            return node_is(te->type, ot);
        }
        case TK_TEXPR_COMPARE: {
            tk_check_result f = tk_validate_texpr(te->as.compare.first); if (!f.ok) return f;
            for (size_t i = 0; i < te->as.compare.nrest; i += 1) {
                tk_check_result r = tk_validate_texpr(te->as.compare.rest[i].operand);
                if (!r.ok) return r;
            }
            return node_is(te->type, prim(TK_PRIM_BOOL));
        }
        case TK_TEXPR_IF:
        case TK_TEXPR_MATCH:
            return cok();   // [block-bearing: deep revalidation with program-level — E6-2]
        case TK_TEXPR_CAST: {
            tk_check_result e = tk_validate_texpr(te->as.cast.expr); if (!e.ok) return e;
            if (!tk_cast_ok(te->as.cast.expr->type, te->type))      // RE-PROVE the cast (M.3)
                return cfail("corrupt: illegal cast in typed tree");
            return cok();
        }
        case TK_TEXPR_FIELD_ACCESS: {
            tk_check_result r = tk_validate_texpr(te->as.field_access.receiver); if (!r.ok) return r;
            if (te->as.field_access.receiver->type.tag != TK_TYPE_NAMED)   // struct-receiver invariant
                return cfail("corrupt: field access on a non-struct receiver");
            return cok();
        }
    }
    return cfail("corrupt: unknown typed expression");
}
