// src/emit/tkb_read.h
#ifndef TK_EMIT_TKB_READ_H
#define TK_EMIT_TKB_READ_H
#include "../checker/tast.h"

// a mutable cursor with an `ok` flag — a read past the end sets ok=false.
typedef struct { const tk_byte *data; size_t len; size_t pos; bool ok; } tk_reader;
typedef struct { tk_str *ptr; size_t len; } tk_strs;   // the read string table

uint8_t  tk_read_u8(tk_reader *r);
uint32_t tk_read_u32(tk_reader *r);
uint64_t tk_read_u64(tk_reader *r);
tk_str   tk_read_str(tk_reader *r, tk_strs t);
tk_strs  tk_read_strtable(tk_reader *r);
tk_type  tk_read_type(tk_reader *r, tk_strs t);

#endif // TK_EMIT_TKB_READ_H
