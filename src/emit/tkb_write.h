// src/emit/tkb_write.h
#ifndef TK_EMIT_TKB_WRITE_H
#define TK_EMIT_TKB_WRITE_H
#include "tkb_buf.h"
#include "../checker/tast.h"
tk_bytes tk_write_type(tk_bytes b, tk_strtable t, tk_type ty);
tk_bytes tk_write_texpr(tk_bytes b, tk_strtable t, const tk_texpr *te);
#endif // TK_EMIT_TKB_WRITE_H
