// src/emit/tkb_frame.h
#ifndef TK_EMIT_TKB_FRAME_H
#define TK_EMIT_TKB_FRAME_H
#include "tkb_write.h"
tk_bytes tk_write_u64(tk_bytes b, uint64_t x);
tk_bytes tk_serialize(const tk_texpr *te);
tk_bytes tk_serialize_program(const tk_tprogram *prog);   // (C7.16) whole-program .tkb (version 2)
#endif // TK_EMIT_TKB_FRAME_H
