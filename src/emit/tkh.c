// src/emit/tkh.c
#include "tkh.h"
#include <stdlib.h>

// --- collect (mutates the table via intern; mirrors the writers) ---
// tkb_frame.c's collect_type is file-static, so we re-derive the tiny piece here.
static void th_collect_type(tk_strtable *t, tk_type ty);
static void th_collect_type_list(tk_strtable *t, const tk_type *xs, size_t n) {
    for (size_t i = 0; i < n; i += 1) th_collect_type(t, xs[i]);
}
static void th_collect_type(tk_strtable *t, tk_type ty) {
    switch (ty.tag) {
        case TK_TYPE_NAMED:   tk_st_intern(t, ty.as.named.name); break;
        case TK_TYPE_SLICE:   th_collect_type(t, *ty.as.slice.element); break;
        case TK_TYPE_VARIANT: th_collect_type_list(t, ty.as.variant.members, ty.as.variant.len); break;
        case TK_TYPE_FUNC:    th_collect_type_list(t, ty.as.func.params, ty.as.func.nparams);
                              th_collect_type(t, *ty.as.func.ret); break;
        default: break;
    }
}

static void collect_doc(tk_strtable *t, bool has_doc, tk_str doc) {
    if (has_doc) tk_st_intern(t, doc);
}
static void collect_fnsig(tk_strtable *t, const tk_fnsig *f) {
    tk_st_intern(t, f->name);
    for (size_t i = 0; i < f->nparams; i += 1) { tk_st_intern(t, f->params[i].name); th_collect_type(t, f->params[i].type); }
    th_collect_type(t, f->ret);
    collect_doc(t, f->has_doc, f->doc);
}
static void collect_tyexport(tk_strtable *t, const tk_tyexport *e) {
    tk_st_intern(t, e->name);
    for (size_t i = 0; i < e->nfields; i += 1)  { tk_st_intern(t, e->fields[i].name); th_collect_type(t, e->fields[i].type); }
    for (size_t i = 0; i < e->nmembers; i += 1) tk_st_intern(t, e->members[i]);
    th_collect_type_list(t, e->cases, e->ncases);
    collect_doc(t, e->has_doc, e->doc);
}
static void collect_header(tk_strtable *t, const tk_header *h) {
    for (size_t i = 0; i < h->ntypes; i += 1) collect_tyexport(t, &h->types[i]);
    for (size_t i = 0; i < h->nfns;   i += 1) collect_fnsig(t, &h->fns[i]);
}

// --- writers ---
static tk_bytes write_doc(tk_bytes b, tk_strtable t, bool has_doc, tk_str doc) {
    if (!has_doc) return tk_write_u8(b, 0);
    return tk_write_u32(tk_write_u8(b, 1), tk_st_find(t, doc));
}
static tk_byte shape_byte(tk_ty_shape s) { return (tk_byte)s; }   // Struct=0 Enum=1 Variant=2

static tk_bytes write_type_list(tk_bytes b, tk_strtable t, const tk_type *xs, size_t n) {
    b = tk_write_u32(b, (uint32_t)n);
    for (size_t i = 0; i < n; i += 1) b = tk_write_type(b, t, xs[i]);
    return b;
}
static tk_bytes write_fnsig(tk_bytes b, tk_strtable t, const tk_fnsig *f) {
    b = tk_write_u32(b, tk_st_find(t, f->name));
    b = tk_write_u32(b, (uint32_t)f->nparams);
    for (size_t i = 0; i < f->nparams; i += 1) {
        b = tk_write_u32(b, tk_st_find(t, f->params[i].name));
        b = tk_write_type(b, t, f->params[i].type);
    }
    b = tk_write_type(b, t, f->ret);
    return write_doc(b, t, f->has_doc, f->doc);
}
static tk_bytes write_tyexport(tk_bytes b, tk_strtable t, const tk_tyexport *e) {
    b = tk_write_u32(b, tk_st_find(t, e->name));
    b = tk_write_u8(b, shape_byte(e->shape));
    b = tk_write_u32(b, (uint32_t)e->nfields);
    for (size_t i = 0; i < e->nfields; i += 1) {
        b = tk_write_u32(b, tk_st_find(t, e->fields[i].name));
        b = tk_write_type(b, t, e->fields[i].type);
    }
    b = tk_write_u32(b, (uint32_t)e->nmembers);
    for (size_t i = 0; i < e->nmembers; i += 1) b = tk_write_u32(b, tk_st_find(t, e->members[i]));
    b = write_type_list(b, t, e->cases, e->ncases);
    return write_doc(b, t, e->has_doc, e->doc);
}
static tk_bytes write_header(tk_bytes b, tk_strtable t, const tk_header *h) {
    b = tk_write_u32(b, (uint32_t)h->ntypes);
    for (size_t i = 0; i < h->ntypes; i += 1) b = write_tyexport(b, t, &h->types[i]);
    b = tk_write_u32(b, (uint32_t)h->nfns);
    for (size_t i = 0; i < h->nfns; i += 1) b = write_fnsig(b, t, &h->fns[i]);
    return b;
}

static tk_bytes append_bytes(tk_bytes dst, tk_bytes src) {
    for (size_t i = 0; i < src.len; i += 1) dst = tk_bytes_push(dst, src.ptr[i]);
    return dst;
}

tk_bytes tk_emit_tkh(const tk_header *h) {
    tk_strtable table = tk_st_empty();
    collect_header(&table, h);
    tk_bytes body = write_header(tk_write_strtable((tk_bytes){0}, table), table, h);
    uint64_t hash = tk_fnv1a(body.ptr, body.len);
    tk_bytes out = {0};
    out = tk_write_u8(out, (tk_byte)'T'); out = tk_write_u8(out, (tk_byte)'K');
    out = tk_write_u8(out, (tk_byte)'H'); out = tk_write_u8(out, 0);
    out = tk_write_u32(out, 1);              // version 1
    out = tk_write_u64(out, hash);           // FNV-1a of the body
    return append_bytes(out, body);
}

// --- readers ---
static tk_ty_shape shape_of(uint8_t b) {
    switch (b) { case 0: return TK_TY_STRUCT; case 1: return TK_TY_ENUM; default: return TK_TY_VARIANT; }
}
static void read_doc(tk_reader *r, tk_strs t, bool *has_doc, tk_str *doc) {
    uint8_t p = tk_read_u8(r);
    if (p == 0) { *has_doc = false; *doc = (tk_str){ NULL, 0 }; return; }
    *has_doc = true; *doc = tk_read_str(r, t);
}
static tk_fnsig read_fnsig(tk_reader *r, tk_strs t) {
    tk_fnsig f = {0};
    f.name = tk_read_str(r, t);
    f.nparams = tk_read_u32(r);
    f.params = tk_alloc(f.nparams ? f.nparams * sizeof *f.params : 1); if (!f.params) abort();
    for (size_t i = 0; i < f.nparams; i += 1) { f.params[i].name = tk_read_str(r, t); f.params[i].type = tk_read_type(r, t); }
    f.ret = tk_read_type(r, t);
    read_doc(r, t, &f.has_doc, &f.doc);
    return f;
}
static tk_tyexport read_tyexport(tk_reader *r, tk_strs t) {
    tk_tyexport e = {0};
    e.name  = tk_read_str(r, t);
    e.shape = shape_of(tk_read_u8(r));
    e.nfields = tk_read_u32(r);
    e.fields = tk_alloc(e.nfields ? e.nfields * sizeof *e.fields : 1); if (!e.fields) abort();
    for (size_t i = 0; i < e.nfields; i += 1) { e.fields[i].name = tk_read_str(r, t); e.fields[i].type = tk_read_type(r, t); }
    e.nmembers = tk_read_u32(r);
    e.members = tk_alloc(e.nmembers ? e.nmembers * sizeof *e.members : 1); if (!e.members) abort();
    for (size_t i = 0; i < e.nmembers; i += 1) e.members[i] = tk_read_str(r, t);
    e.ncases = tk_read_u32(r);
    e.cases = tk_alloc(e.ncases ? e.ncases * sizeof *e.cases : 1); if (!e.cases) abort();
    for (size_t i = 0; i < e.ncases; i += 1) e.cases[i] = tk_read_type(r, t);
    read_doc(r, t, &e.has_doc, &e.doc);
    return e;
}
static tk_header read_header(tk_reader *r, tk_strs t) {
    tk_header h = {0};
    h.ntypes = tk_read_u32(r);
    h.types = tk_alloc(h.ntypes ? h.ntypes * sizeof *h.types : 1); if (!h.types) abort();
    for (size_t i = 0; i < h.ntypes; i += 1) h.types[i] = read_tyexport(r, t);
    h.nfns = tk_read_u32(r);
    h.fns = tk_alloc(h.nfns ? h.nfns * sizeof *h.fns : 1); if (!h.fns) abort();
    for (size_t i = 0; i < h.nfns; i += 1) h.fns[i] = read_fnsig(r, t);
    return h;
}

static tk_header_result hdr_err(const char *m) { return (tk_header_result){ .ok = false, .as.error = tk_error_make(m) }; }
static tk_header_result hdr_ok(tk_header h)     { return (tk_header_result){ .ok = true,  .as.value = h }; }

tk_header_result tk_read_tkh(const tk_byte *data, size_t len) {
    tk_reader r = { data, len, 0, true };
    if (tk_read_u8(&r) != 'T' || tk_read_u8(&r) != 'K' || tk_read_u8(&r) != 'H' || tk_read_u8(&r) != 0)
        return hdr_err("not a .tkh (bad magic)");
    if (tk_read_u32(&r) != 1) return hdr_err("unsupported .tkh version");
    uint64_t stored = tk_read_u64(&r);
    if (len < 16) return hdr_err("truncated .tkh header");
    if (tk_fnv1a(data + 16, len - 16) != stored) return hdr_err(".tkh altered or corrupt (hash mismatch)");
    tk_strs table = tk_read_strtable(&r);
    tk_header h = read_header(&r, table);
    if (!r.ok) return hdr_err("truncated/corrupt .tkh");
    return hdr_ok(h);
}
