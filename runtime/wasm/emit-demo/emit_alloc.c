// Phase 11 (Browser FFI) MVP-4 — executable proof of JS->Teko strings + the
// ergonomic facade + the real allocator working together. Drives the real backend
// to emit a module whose callback routine (table slot 0) is:
//
//   showMessage(ptr, len): dom.setText(dom.getElementById("out"), ptr, len)
//
// plus an auto-generated facade `emitted_alloc.mjs` exposing
// `mod.showMessage("...")` — which marshals the JS string into wasm memory via
// teko_alloc and dispatches the routine via teko_invoke2. The host harness proves
// `mod.showMessage("hello from JS via alloc")` puts that text into #out, i.e. the
// bytes survived JS -> teko_alloc'd wasm memory -> Teko -> back to the DOM.
//
// Build:
//   cc -I src runtime/wasm/emit-demo/emit_alloc.c build/libteko_core.a -o emit_alloc
//   ./emit_alloc runtime/wasm/samples/emitted_alloc.wat \
//                runtime/wasm/samples/emitted_alloc.glue.mjs \
//                runtime/wasm/samples/emitted_alloc.mjs
#include "codegen/codegen_metal.h"
#include <stdio.h>
#include <string.h>

static void le32(unsigned char* b, int* n, int32_t v) {
    b[(*n)++] = v & 0xFF; b[(*n)++] = (v >> 8) & 0xFF;
    b[(*n)++] = (v >> 16) & 0xFF; b[(*n)++] = (v >> 24) & 0xFF;
}

int main(int argc, char** argv) {
    const char* out_wat    = (argc > 1) ? argv[1] : "emitted_alloc.wat";
    const char* out_glue   = (argc > 2) ? argv[2] : "emitted_alloc.glue.mjs";
    const char* out_facade = (argc > 3) ? argv[3] : "emitted_alloc.mjs";

    TekoTarget target;
    memset(&target, 0, sizeof target);
    target.arch = ARCH_WASM32; target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);
    MetalContext* ctx = teko_metal_create(out_wat, target);
    if (!ctx) { fprintf(stderr, "emit_alloc: cannot open %s\n", out_wat); return 2; }

    static const char* strings[] = { "out" };
    enum { S_OUT = 0 };
    teko_metal_set_strings(ctx, strings, 1);

    static const TekoWasmImport imports[] = {
        { "dom", "getElementById", 2, 1 }, // (ptr,len) -> handle : #0
        { "dom", "setText",        3, 0 }, // (handle,ptr,len)    : #1
    };
    enum { I_GETBYID = 0, I_SETTEXT = 1 };
    teko_metal_set_imports(ctx, imports, 2);

    const int len_out = (int)strlen(strings[S_OUT]); // 3
    const int SHOW_FN = 0;                           // table slot of $routine_0

    unsigned char b[160]; int n = 0;

    // --- $main: nothing to do; the dev calls mod.showMessage() via the facade. ---
    b[n++] = OP_HALT;

    // --- routine 0: showMessage(ptr=$w0, len=$w1) via teko_invoke2(0, ptr, len) ---
    b[n++] = OP_FUNC_BEGIN;  le32(b, &n, SHOW_FN);
    b[n++] = OP_SETARG;      le32(b, &n, 1);          // $a1 = msg ptr
    b[n++] = OP_SCONST;      le32(b, &n, S_OUT);      // $w0 = &"out"
    b[n++] = OP_SETARG;      le32(b, &n, 0);          // $a0 = out-name ptr
    b[n++] = OP_ICONST;      le32(b, &n, len_out);    // $w0 = out-name len
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_GETBYID);  // $w0 = #out handle
    b[n++] = OP_SETARG;      le32(b, &n, 0);          // $a0 = #out handle
    b[n++] = OP_LOAD;                                  // $w0 = $w1 = msg len
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_SETTEXT);  // setText(#out, msg ptr, msg len)
    b[n++] = OP_FUNC_END;

    teko_metal_emit_program(ctx, b, (uint32_t)n);

    int grc = teko_metal_emit_dom_glue(ctx, out_glue);
    static const TekoWasmFacadeEntry facade[] = { { "showMessage", SHOW_FN } };
    int frc = teko_metal_emit_facade(ctx, out_facade, "./emitted_alloc.glue.mjs", facade, 1);
    teko_metal_close(ctx);
    if (grc != 0 || frc != 0) { fprintf(stderr, "emit_alloc: cannot write glue/facade\n"); return 2; }
    printf("emit_alloc: wrote %d IL bytes -> %s (+ glue %s + facade %s)\n", n, out_wat, out_glue, out_facade);
    return 0;
}
