#include "../codegen_metal.h"
#include <stdio.h>

void emit_wasm_pure(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        // ====================================================================
        // 1. VIRTUAL MODULE INITIALIZATION AND FUNCTION SCOPE (WAT FORMAT)
        // ====================================================================
        case OP_PROLOG:
            fprintf(ctx->file, "(module\n");
            fprintf(ctx->file, "  ;; --- Target: WebAssembly Text Format (WASM Bare-Metal) ---\n");
            // Host-provided runtime hooks. Channels are now implemented in-module
            // (Phase 10.1, linear-memory ring buffers) so only spawn/await still
            // delegate to the host (cooperative scheduler lands in 10.2).
            fprintf(ctx->file, "  (import \"teko_rt\" \"spawn\" (func $teko_spawn (param i32) (result i32)))\n");
            fprintf(ctx->file, "  (import \"teko_rt\" \"await_intent\" (func $teko_await (param i32) (result i32)))\n");
            fprintf(ctx->file, "  (memory 1)\n");
            fprintf(ctx->file, "  (export \"memory\" (memory 0))\n");
            // O(1) region allocator: a bump pointer into linear memory, based
            // above the .data region (offset 1024) emitted at module close.
            fprintf(ctx->file, "  (global $arena_sp (mut i32) (i32.const 2048))\n");
            fprintf(ctx->file, "  (func $main (result i32)\n");
            fprintf(ctx->file, "    (local $w0 i32) (local $w1 i32)\n");
            break;

        // ====================================================================
        // 2. LITERAL, MEMORY AND CONVERSION OPCODES
        // ====================================================================
        case OP_HALT:
            fprintf(ctx->file, "    ;; [WASM Halt]: Stops execution by pushing the accumulator\n");
            fprintf(ctx->file, "    local.get $w0\n");
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    i32.const %d\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    i32.const %d ;; Offset of Constant Pool in Linear Memory\n", arg * 32);
            break;

        case OP_STORE:
            fprintf(ctx->file, "    local.set $w1\n");
            break;

        case OP_LOAD:
            fprintf(ctx->file, "    local.get $w1\n");
            break;

        // ====================================================================
        // 3. MATHEMATICAL OPERATIONS ON THE VIRTUAL STACK
        // ====================================================================
        case OP_ADD:
            fprintf(ctx->file, "    local.get $w0\n    local.get $w1\n    i32.add\n    local.set $w0\n");
            break;

        case OP_SUB:
            fprintf(ctx->file, "    local.get $w0\n    local.get $w1\n    i32.sub\n    local.set $w0\n");
            break;

        case OP_MUL:
            fprintf(ctx->file, "    local.get $w0\n    local.get $w1\n    i32.mul\n    local.set $w0\n");
            break;

        case OP_DIV:
            fprintf(ctx->file, "    local.get $w1\n");
            fprintf(ctx->file, "    i32.eqz\n");
            fprintf(ctx->file, "    if (result i32)\n");
            fprintf(ctx->file, "      i32.const -1\n");
            fprintf(ctx->file, "    else\n");
            fprintf(ctx->file, "      local.get $w0\n      local.get $w1\n      i32.div_s\n");
            fprintf(ctx->file, "    end\n");
            fprintf(ctx->file, "    local.set $w0\n");
            break;

        // ====================================================================
        // 4. NATIVE ARENA ALLOCATOR (O(1) METHOD)
        // ====================================================================
        case OP_ARENA_PUSH:
            // O(1) bump: advance the arena stack pointer by one 1024-byte frame.
            fprintf(ctx->file, "    ;; --- [WASM Arena Push]: O(1) bump of a 1024-byte frame ---\n");
            fprintf(ctx->file, "    global.get $arena_sp\n");
            fprintf(ctx->file, "    i32.const 1024\n");
            fprintf(ctx->file, "    i32.add\n");
            fprintf(ctx->file, "    global.set $arena_sp\n");
            break;

        case OP_ARENA_POP:
            // O(1) reclaim: rewind the arena stack pointer by one frame.
            fprintf(ctx->file, "    ;; --- [WASM Arena Pop]: O(1) reclaim of the 1024-byte frame ---\n");
            fprintf(ctx->file, "    global.get $arena_sp\n");
            fprintf(ctx->file, "    i32.const 1024\n");
            fprintf(ctx->file, "    i32.sub\n");
            fprintf(ctx->file, "    global.set $arena_sp\n");
            break;

        // ====================================================================
        // 5. VIRTUAL WEB PARALLELISM (WASM THREADS EXTENSION)
        // ====================================================================
        // The following require the WASM threads proposal (shared memory plus
        // memory.atomic.wait/notify) and a host Worker to back them. Standalone
        // WAT cannot spawn threads, so they are routed to host-runtime imports
        // (declared in the prologue) instead of being emitted as silent comments.
        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    ;; [WASM Async Spawn] -> host runtime (threads proposal pending)\n");
            fprintf(ctx->file, "    local.get $w0\n");
            fprintf(ctx->file, "    call $teko_spawn\n");
            fprintf(ctx->file, "    drop\n");
            break;

        case OP_CHAN_INIT:
            // Phase 10.1: bump-allocate a ring-buffer channel in linear memory.
            // Layout (i32 cells): [0]=head [4]=tail [8]=cap, then cap data slots.
            // Channel handle (base pointer) is left in $w0.
            fprintf(ctx->file, "    ;; [WASM Channel Init]: ring buffer in linear memory (cap 8)\n");
            fprintf(ctx->file, "    global.get $arena_sp\n");
            fprintf(ctx->file, "    local.set $w0\n");              // $w0 = channel base P
            fprintf(ctx->file, "    local.get $w0\n    i32.const 0\n    i32.store offset=0\n"); // head = 0
            fprintf(ctx->file, "    local.get $w0\n    i32.const 0\n    i32.store offset=4\n"); // tail = 0
            fprintf(ctx->file, "    local.get $w0\n    i32.const 8\n    i32.store offset=8\n"); // cap = 8
            fprintf(ctx->file, "    global.get $arena_sp\n    i32.const 44\n    i32.add\n    global.set $arena_sp\n"); // reserve 12 + 8*4
            break;

        case OP_CHAN_PUT:
            // Phase 10.1: non-blocking ring-buffer store. Channel handle in $w0,
            // value in $w1. Writes buf[tail] then advances tail (mod cap).
            fprintf(ctx->file, "    ;; [WASM Channel Put]: non-blocking ring-buffer store at tail\n");
            // buf[tail] = value  -> addr = P + tail*4, with the +12 header via store offset
            fprintf(ctx->file, "    local.get $w0\n");
            fprintf(ctx->file, "    local.get $w0\n    i32.load offset=4\n    i32.const 4\n    i32.mul\n    i32.add\n"); // P + tail*4
            fprintf(ctx->file, "    local.get $w1\n");
            fprintf(ctx->file, "    i32.store offset=12\n");        // mem[P + tail*4 + 12] = value
            // tail = (tail + 1) % cap
            fprintf(ctx->file, "    local.get $w0\n");
            fprintf(ctx->file, "    local.get $w0\n    i32.load offset=4\n    i32.const 1\n    i32.add\n");
            fprintf(ctx->file, "    local.get $w0\n    i32.load offset=8\n    i32.rem_u\n");
            fprintf(ctx->file, "    i32.store offset=4\n");          // tail = (tail+1) % cap
            break;

        case OP_AWAIT_INTENT:
            fprintf(ctx->file, "    ;; [WASM Await Intent] -> host runtime (threads proposal pending)\n");
            fprintf(ctx->file, "    local.get $w0\n");
            fprintf(ctx->file, "    call $teko_await\n");
            fprintf(ctx->file, "    drop\n");
            break;

        // ====================================================================
        // 6. CONTROL FLOW AND MODULE CLOSING
        // ====================================================================
        case OP_JMP:
            fprintf(ctx->file, "    br $label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    br_if $label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    local.get $w0\n");
            fprintf(ctx->file, "  )\n");
            fprintf(ctx->file, "  (export \"main\" (func $main))\n");
            fprintf(ctx->file, "  (data (i32.const 1024) \"Hello Teko\\00\")\n");
            fprintf(ctx->file, ")\n");
            break;

        default:
            // DCE RESURRECTION: Injects the structural comment if it is a logical instruction mapped above 100
            if ((int)op >= 100) {
                fprintf(ctx->file, "    ;; Label marker: $label_%d\n", (int)op);
            }
            break;
    }
}
