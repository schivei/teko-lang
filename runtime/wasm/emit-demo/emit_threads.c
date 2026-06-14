// Phase 10.4 — executable proof of the Layer B (wasm-threads) backend. Drives the
// real Teko WASM backend with the `--target=...-wasm-threads` flag, emitting a
// module that imports a SHARED memory, spawns a green thread through the host
// (`teko_rt.spawn`), and hands a value over an ATOMIC channel. CI runs it under a
// real Worker (node worker_threads + browser Web Worker), asserting main() == 99.
//
// Build:
//   cc -I src runtime/wasm/emit-demo/emit_threads.c build/libteko_core.a -o emit_threads
//   ./emit_threads runtime/wasm/samples/emitted_threads.wat
//
// IL program:
//   main:      CHAN_INIT, ICONST 0, SPAWN_ASYNC, CHAN_GET, HALT
//   routine 0: FUNC_BEGIN(0), ICONST 99, CHAN_PUT, FUNC_END
// The `-threads` target makes CHAN_* lower to i32.atomic.* + memory.atomic.wait32/
// notify, SPAWN to a host teko_rt.spawn, and exports a teko_invoke dispatcher.
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
    const char* out = (argc > 1) ? argv[1] : "emitted_threads.wat";

    TekoTarget target;
    memset(&target, 0, sizeof target);
    target.arch = ARCH_WASM32;
    target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi-threads", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(out, target);
    if (!ctx) {
        fprintf(stderr, "emit_threads: cannot open %s\n", out);
        return 2;
    }

    unsigned char b[128];
    int n = 0;
    b[n++] = OP_CHAN_INIT;
    b[n++] = OP_ICONST; le32(b, &n, 0); b[n++] = OP_SPAWN_ASYNC; // host starts a Worker for routine 0
    b[n++] = OP_CHAN_GET;                                        // atomic wait until the Worker publishes
    b[n++] = OP_HALT;
    b[n++] = OP_FUNC_BEGIN; le32(b, &n, 0);
    b[n++] = OP_ICONST; le32(b, &n, 99); b[n++] = OP_CHAN_PUT;   // atomic store + notify
    b[n++] = OP_FUNC_END;

    teko_metal_emit_program(ctx, b, (uint32_t)n);
    teko_metal_close(ctx);
    printf("emit_threads: wrote %d IL bytes (Layer B shared-memory atomics) -> %s\n", n, out);
    return 0;
}
