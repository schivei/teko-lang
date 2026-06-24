// src/emit/tkh.h — the `.tkh` exported-interface emitter. Mirrors tkh.tks.
#ifndef TK_EMIT_TKH_H
#define TK_EMIT_TKH_H

#include "tkb_frame.h"   // tk_write_u64, the writers, the string table, tk_fnv1a
#include "tkb_read.h"    // tk_reader, tk_strs, tk_read_type, tk_read_strtable

// --- the exported-interface model ---
typedef struct { tk_str name; tk_type type; } tk_sigparam;
typedef struct { tk_str name; tk_type type; } tk_sigfield;

typedef struct {
    tk_str       name;
    tk_sigparam *params;  size_t nparams;
    tk_type      ret;
    bool         has_doc; tk_str doc;
} tk_fnsig;

typedef enum { TK_TY_STRUCT, TK_TY_ENUM, TK_TY_VARIANT } tk_ty_shape;

typedef struct {
    tk_str       name;
    tk_ty_shape  shape;
    tk_sigfield *fields;  size_t nfields;     // Struct
    tk_str      *members; size_t nmembers;    // Enum
    tk_type     *cases;   size_t ncases;      // Variant
    bool         has_doc; tk_str doc;
} tk_tyexport;

typedef struct {
    tk_tyexport *types; size_t ntypes;
    tk_fnsig    *fns;   size_t nfns;
} tk_header;

TK_RESULT(tk_header, tk_header_result);

tk_bytes         tk_emit_tkh(const tk_header *h);
tk_header_result tk_read_tkh(const tk_byte *data, size_t len);

#endif // TK_EMIT_TKH_H
