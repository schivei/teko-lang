// src/checker/type.c — nominal type equality (B.13).
#include "type.h"

static bool tk_str_eq(tk_str a, tk_str b) {
    if (a.len != b.len) return false;
    for (size_t i = 0; i < a.len; i += 1) {
        if (a.ptr[i] != b.ptr[i]) return false;
    }
    return true;
}

static bool tk_types_eq(const tk_type *xs, size_t nx, const tk_type *ys, size_t ny) {
    if (nx != ny) return false;
    for (size_t i = 0; i < nx; i += 1) {
        if (!tk_type_eq(&xs[i], &ys[i])) return false;
    }
    return true;
}

bool tk_type_eq(const tk_type *a, const tk_type *b) {
    if (a->tag != b->tag) return false;          // different shapes → not equal
    switch (a->tag) {
        case TK_TYPE_PRIM:  return a->as.prim == b->as.prim;
        case TK_TYPE_BYTE:  return true;
        case TK_TYPE_STR:   return true;
        case TK_TYPE_ERROR: return true;
        case TK_TYPE_UNIT:  return true;
        case TK_TYPE_SLICE: return tk_type_eq(a->as.slice.element, b->as.slice.element);
        case TK_TYPE_NAMED: return tk_str_eq(a->as.named.name, b->as.named.name);   // nominal
        case TK_TYPE_VARIANT:
            return tk_types_eq(a->as.variant.members, a->as.variant.len,
                               b->as.variant.members, b->as.variant.len);
        case TK_TYPE_FUNC:
            return tk_type_eq(a->as.func.ret, b->as.func.ret) &&
                   tk_types_eq(a->as.func.params, a->as.func.nparams,
                               b->as.func.params, b->as.func.nparams);
    }
    return false;
}
