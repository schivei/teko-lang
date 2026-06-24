// src/checker/type.h — the checker's semantic Type model. Mirrors type.tks.
#ifndef TK_CHECK_TYPE_H
#define TK_CHECK_TYPE_H

#include "../core.h"
#include "../text/text.h"   // tk_str
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TK_PRIM_U8, TK_PRIM_U16, TK_PRIM_U32, TK_PRIM_U64,
    TK_PRIM_I8, TK_PRIM_I16, TK_PRIM_I32, TK_PRIM_I64,
    TK_PRIM_BOOL,
} tk_prim_kind;

typedef enum {
    TK_TYPE_PRIM, TK_TYPE_BYTE, TK_TYPE_STR, TK_TYPE_SLICE,
    TK_TYPE_NAMED, TK_TYPE_VARIANT, TK_TYPE_FUNC, TK_TYPE_ERROR, TK_TYPE_UNIT,
} tk_type_tag;

// recursive (the Slice/Variant/Func cases hold tk_type) — the indirection the Teko
// side keeps compiler-managed shows here as a forward declaration + pointers.
typedef struct tk_type tk_type;

struct tk_type {
    tk_type_tag tag;
    union {
        tk_prim_kind prim;                                   // TK_TYPE_PRIM
        struct { tk_type *element; }            slice;       // TK_TYPE_SLICE
        struct { tk_str name; }                 named;       // TK_TYPE_NAMED (nominal)
        struct { tk_type *members; size_t len; } variant;    // TK_TYPE_VARIANT
        struct { tk_type *params; size_t nparams; tk_type *ret; } func;  // TK_TYPE_FUNC
        // BYTE, STR, ERROR carry no payload
    } as;
};

bool tk_type_eq(const tk_type *a, const tk_type *b);

#endif // TK_CHECK_TYPE_H
