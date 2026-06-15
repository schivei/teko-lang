#ifndef FRONTEND_INTEROP_H
#define FRONTEND_INTEROP_H

#include "codegen_li.h"

// Phase 11 (Browser FFI frontend FE-C): compile the Browser-FFI interop subset of a
// real Teko source string into an IL BytecodeBuffer. Drives the real lexer and reuses
// the real `parse_extern_declaration` (so `extern fn … from "ns" as "name"` is now
// consumed, not discarded): each extern becomes an import in the buffer's import table
// and a top-level call `name(arg, …)` to one lowers to OP_SCONST/OP_ICONST + OP_SETARG
// + OP_CALL_IMPORT. A trailing OP_HALT closes main. String/int literal args are
// supported (the interop surface); richer expressions are out of this subset.
//
// Returns 0 on success. The caller owns `buffer` (create/free via codegen_li_*).
int teko_compile_interop(const char* source, BytecodeBuffer* buffer);

#endif // FRONTEND_INTEROP_H
