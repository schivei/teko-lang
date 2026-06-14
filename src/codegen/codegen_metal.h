#ifndef CODEGEN_METAL_H
#define CODEGEN_METAL_H

#include <stdio.h>
#include <stdint.h>
#include "../teko_target.h"
#include "../codegen_li.h"

// Context of the contiguous bare-metal emitter
typedef struct {
    FILE* file;
    TekoTarget target;
    uint32_t label_count;
    // Phase 10.2b/10.3 WASM multi-function state (unused by the native emitters):
    //  wasm_open       - 0 nothing open, 1 $main open, 2 a $routine_N open
    //  routine_count   - number of green-thread functions emitted (table size)
    //  routine_ids     - their ids, for the (elem ...) table-init at module close
    //  routine_yields  - yield points (suspending CHAN_GETs) in the current
    //                    routine, counted by the orchestrator before FUNC_BEGIN;
    //                    sizes the state-machine block nest + br_table (10.3)
    //  yield_idx       - which yield point we are emitting within the routine
    int wasm_open;
    int wasm_routine_count;
    int wasm_routine_ids[64];
    int wasm_routine_yields;
    int wasm_yield_idx;
    // Phase 11 (Browser FFI): the IL string constant pool, threaded in so the WASM
    // emitter can lay it out as a real (data ...) segment and resolve OP_SCONST to a
    // true byte offset (instead of a placeholder). NULL/0 when unset — the emitter
    // then falls back to its legacy hardcoded data. Unused by the native emitters.
    const char** wasm_strings;
    int wasm_string_count;
} MetalContext;

// Phase 11: hand the WASM emitter the IL string pool before teko_metal_emit_program
// so it can emit the (data ...) segment and correct OP_SCONST offsets. `strings`
// must outlive the emit call; pass count 0 (or do not call) to keep legacy behavior.
void teko_metal_set_strings(MetalContext* ctx, const char** strings, int count);

// 1. APPLE ECOSYSTEM (Darwin Kernel)
void emit_darwin_arm64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_darwin_x86_64(MetalContext* ctx, OpCode op, int32_t arg);

// 2. LINUX ECOSYSTEM (ELF ABI)
void emit_linux_x86_64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_x86(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_arm64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_arm32(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_riscv64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_riscv32(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_mips(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_ppc64(MetalContext* ctx, OpCode op, int32_t arg);

// 3. WINDOWS ECOSYSTEM (PE/COFF Vector)
void emit_win_x86_64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_win_x86(MetalContext* ctx, OpCode op, int32_t arg);
void emit_win_arm64(MetalContext* ctx, OpCode op, int32_t arg);

// 4. BSD UNIX ECOSYSTEM (Native FreeBSD)
void emit_freebsd_x86_64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_freebsd_arm64(MetalContext* ctx, OpCode op, int32_t arg);

// 5. EMBEDDED AND VIRTUAL ENVIRONMENTS (Bare-Metal)
void emit_wasm_pure(MetalContext* ctx, OpCode op, int32_t arg);

// Lifecycle signatures of the Central Orchestrator
MetalContext* teko_metal_create(const char* output_asm_path, TekoTarget target);
void teko_metal_emit_program(MetalContext* ctx, const unsigned char* bytecode, uint32_t size);
void teko_metal_close(MetalContext* ctx);

#endif // CODEGEN_METAL_H
