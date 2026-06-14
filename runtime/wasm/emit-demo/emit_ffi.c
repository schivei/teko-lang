// Phase 11 (Browser FFI) MVP-1 — executable proof that the Teko WASM backend can
// declare a host import and call it, passing a pooled string. Drives the real
// backend (teko_metal_emit_program) to emit a module that imports `env.log` and
// calls it with the address of a constant-pool string. The host (run-ffi.mjs)
// supplies `env.log(ptr)` and reads the string back from the exported memory.
//
// Build:
//   cc -I src runtime/wasm/emit-demo/emit_ffi.c build/libteko_core.a -o emit_ffi
//   ./emit_ffi runtime/wasm/samples/emitted_ffi.wat
//
// Models the eventual lowering of:
//   extern fn log(msg: ptr) from "env" as "log"
//   log("hello from teko")
#include "codegen/codegen_metal.h"
#include <stdio.h>
#include <string.h>

static void le32(unsigned char* b, int* n, int32_t v) {
    b[(*n)++] = v & 0xFF; b[(*n)++] = (v >> 8) & 0xFF;
    b[(*n)++] = (v >> 16) & 0xFF; b[(*n)++] = (v >> 24) & 0xFF;
}

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "emitted_ffi.wat";
    TekoTarget target;
    memset(&target, 0, sizeof target);
    target.arch = ARCH_WASM32; target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);
    MetalContext* ctx = teko_metal_create(out, target);
    if (!ctx) { fprintf(stderr, "emit_ffi: cannot open %s\n", out); return 2; }

    static const char* strings[] = { "hello from teko" };
    static const TekoWasmImport imports[] = { { "env", "log", 1, 0 } }; // log(i32) -> void
    teko_metal_set_strings(ctx, strings, 1);
    teko_metal_set_imports(ctx, imports, 1);

    unsigned char b[64]; int n = 0;
    b[n++] = OP_SCONST; le32(b, &n, 0);        // $w0 = &pool[0] ("hello from teko")
    b[n++] = OP_CALL_IMPORT; le32(b, &n, 0);   // env.log($w0)
    b[n++] = OP_HALT;                          // main returns $w0 (the ptr)

    teko_metal_emit_program(ctx, b, (uint32_t)n);
    teko_metal_close(ctx);
    printf("emit_ffi: wrote %d IL bytes (import env.log + pooled string) -> %s\n", n, out);
    return 0;
}
