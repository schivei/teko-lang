#ifndef EMIT_NATIVE_HOSTED_H
#define EMIT_NATIVE_HOSTED_H

#include "codegen_metal.h"

// Phase 13 — native runner. A real, libc-hosted, *assemble-able* emitter for the host
// architectures (x86_64 SysV + arm64 AAPCS, on ELF/Linux and Mach-O/macOS). Unlike the
// 16 freestanding "metal" emitters (which emit a toy accumulator model pinned by the
// per-arch goldens and target bare metal / no libc), this path emits code the system
// `cc` assembles and links against the teko_rt archive into a runnable binary.
//
// It is selected by MetalContext.hosted (set via teko_metal_set_hosted) and reuses the
// already-threaded string pool (wasm_strings), import table (wasm_imports) and local
// count (wasm_local_count). The freestanding emitters are untouched — goldens stay green.
//
// Accumulator model (mirrors the WASM emitter): $w0 = primary register (x0 / %rax),
// $w1 = scratch callee-saved (x19 / %rbx). Named locals + OP_SETARG staging slots live
// in the stack frame. Supported opcode subset: PROLOG/EPILOG/HALT, ICONST, SCONST,
// STORE/LOAD, STORE_LOCAL/LOAD_LOCAL, SETARG, CALL_IMPORT, CALL_RUNTIME, and the
// integer ALU ops (ADD/SUB/MUL/DIV/MOD/EQ/NE/LT/LE/GT/GE) — enough for the interop +
// crypto language surface, which is straight-line.
void emit_native_hosted(MetalContext* ctx, OpCode op, int32_t arg);

// Resolve an OP_CALL_RUNTIME id (the same dispatch table as the WASM emitter) to the
// teko_rt C symbol it calls, and its arity. Returns NULL for an unknown id.
const char* teko_native_runtime_symbol(int32_t id, int* out_arity);

#endif // EMIT_NATIVE_HOSTED_H
