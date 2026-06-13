// Phase 10.3 — executable proof of mid-function suspension. Drives the real Teko
// WASM backend to emit a program whose green thread SUSPENDS in the middle of its
// body and later RESUMES, and writes the WAT to disk. CI assembles it with
// wat2wasm and runs it under Node + wasmtime, asserting main() == 30.
//
// Build:
//   cc -I src runtime/wasm/emit-demo/emit_suspend.c build/libteko_core.a -o emit_suspend
//   ./emit_suspend runtime/wasm/samples/emitted_suspend.wat
//
// IL program (one channel C; two green threads):
//   main:      CHAN_INIT, ICONST 0, SPAWN(consumer), ICONST 1, SPAWN(producer),
//              CHAN_GET, HALT
//   consumer:  FUNC_BEGIN(0), ICONST 0, STORE,        ;; running sum = 0
//              CHAN_GET, ADD, STORE,                  ;; recv v1; sum += v1
//              CHAN_GET, ADD,                         ;; recv v2; sum += v2
//              CHAN_PUT, FUNC_END                     ;; put the sum back
//   producer:  FUNC_BEGIN(1), ICONST 10, CHAN_PUT, ICONST 20, CHAN_PUT, FUNC_END
//
// main spawns the consumer FIRST. The scheduler runs it; the channel is empty, so
// it suspends at the first receive (spilling sum=0 to its frame) and returns to
// the scheduler. The producer then runs and fills the channel. The consumer is
// re-dispatched, RESUMES at the first receive with sum reloaded, drains 10 then
// 20, puts 30 back, and completes. main receives 30.
#include "codegen/codegen_metal.h"
#include <stdio.h>
#include <string.h>

static void le32(unsigned char* b, int* n, int32_t v) {
    b[(*n)++] = v & 0xFF;
    b[(*n)++] = (v >> 8) & 0xFF;
    b[(*n)++] = (v >> 16) & 0xFF;
    b[(*n)++] = (v >> 24) & 0xFF;
}

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "emitted_suspend.wat";

    TekoTarget target;
    memset(&target, 0, sizeof target);
    target.arch = ARCH_WASM32;
    target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(out, target);
    if (!ctx) {
        fprintf(stderr, "emit_suspend: cannot open %s\n", out);
        return 2;
    }

    unsigned char b[128];
    int n = 0;
    // --- main ---
    b[n++] = OP_CHAN_INIT;
    b[n++] = OP_ICONST; le32(b, &n, 0); b[n++] = OP_SPAWN_ASYNC; // consumer (table slot 0)
    b[n++] = OP_ICONST; le32(b, &n, 1); b[n++] = OP_SPAWN_ASYNC; // producer (table slot 1)
    b[n++] = OP_CHAN_GET;                                        // root blocking receive -> sum
    b[n++] = OP_HALT;
    // --- consumer (fn 0): sum two suspending receives, put the sum back ---
    b[n++] = OP_FUNC_BEGIN; le32(b, &n, 0);
    b[n++] = OP_ICONST; le32(b, &n, 0); b[n++] = OP_STORE;       // $w1 = 0 (running sum)
    b[n++] = OP_CHAN_GET; b[n++] = OP_ADD; b[n++] = OP_STORE;    // recv v1; $w0 = v1 + sum; sum = $w0
    b[n++] = OP_CHAN_GET; b[n++] = OP_ADD;                       // recv v2; $w0 = v2 + sum
    b[n++] = OP_CHAN_PUT;                                        // put the total back
    b[n++] = OP_FUNC_END;
    // --- producer (fn 1): fill the channel with 10 then 20 ---
    b[n++] = OP_FUNC_BEGIN; le32(b, &n, 1);
    b[n++] = OP_ICONST; le32(b, &n, 10); b[n++] = OP_CHAN_PUT;
    b[n++] = OP_ICONST; le32(b, &n, 20); b[n++] = OP_CHAN_PUT;
    b[n++] = OP_FUNC_END;

    teko_metal_emit_program(ctx, b, (uint32_t)n);
    teko_metal_close(ctx);
    printf("emit_suspend: wrote %d IL bytes (suspending green thread) -> %s\n", n, out);
    return 0;
}
