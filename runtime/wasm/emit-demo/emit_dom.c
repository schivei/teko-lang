// Phase 11 (Browser FFI) MVP-2 — executable proof that the Teko WASM backend can
// drive the DOM through auto-generated JS glue. Drives the real backend to emit a
// module that imports the minimal dom.* vocabulary and, in $main:
//
//   el  = dom.createElement("span")
//         dom.setText(el, "hello from teko")
//   out = dom.getElementById("out")
//         dom.appendChild(out, el)
//
// It also writes the matching <mod>.glue.mjs (string marshalling + handle table).
// The host harness (run-dom.mjs / browser/dom-run.mjs) instantiates the module
// with that glue, calls main(), and asserts #out > span textContent.
//
// Build:
//   cc -I src runtime/wasm/emit-demo/emit_dom.c build/libteko_core.a -o emit_dom
//   ./emit_dom runtime/wasm/samples/emitted_dom.wat runtime/wasm/samples/emitted_dom.glue.mjs
//
// Models the eventual lowering of `@dom`/`extern fn … from "dom" as "…"` calls.
#include "codegen/codegen_metal.h"
#include <stdio.h>
#include <string.h>

static void le32(unsigned char* b, int* n, int32_t v) {
    b[(*n)++] = v & 0xFF; b[(*n)++] = (v >> 8) & 0xFF;
    b[(*n)++] = (v >> 16) & 0xFF; b[(*n)++] = (v >> 24) & 0xFF;
}

int main(int argc, char** argv) {
    const char* out_wat  = (argc > 1) ? argv[1] : "emitted_dom.wat";
    const char* out_glue = (argc > 2) ? argv[2] : "emitted_dom.glue.mjs";

    TekoTarget target;
    memset(&target, 0, sizeof target);
    target.arch = ARCH_WASM32; target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);
    MetalContext* ctx = teko_metal_create(out_wat, target);
    if (!ctx) { fprintf(stderr, "emit_dom: cannot open %s\n", out_wat); return 2; }

    // String pool — indices 0,1,2; the glue reads each as (ptr,len).
    static const char* strings[] = { "span", "out", "hello from teko" };
    enum { S_SPAN = 0, S_OUT = 1, S_TEXT = 2 };
    teko_metal_set_strings(ctx, strings, 3);

    // Minimal DOM vocabulary. Each string arg is one i32 (ptr) + one i32 (len),
    // so createElement/getElementById are 2-param, setText is 3-param.
    static const TekoWasmImport imports[] = {
        { "dom", "createElement",  2, 1 }, // (ptr,len) -> handle    : import #0
        { "dom", "setText",        3, 0 }, // (handle,ptr,len)       : import #1
        { "dom", "getElementById", 2, 1 }, // (ptr,len) -> handle    : import #2
        { "dom", "appendChild",    2, 0 }, // (parent,child)         : import #3
    };
    enum { I_CREATE = 0, I_SETTEXT = 1, I_GETBYID = 2, I_APPEND = 3 };
    teko_metal_set_imports(ctx, imports, 4);

    const int len_span = (int)strlen(strings[S_SPAN]); // 4
    const int len_out  = (int)strlen(strings[S_OUT]);  // 3
    const int len_text = (int)strlen(strings[S_TEXT]); // 15

    unsigned char b[128]; int n = 0;

    // el = createElement("span");  args = ($a0=ptr, $w0=len)
    b[n++] = OP_SCONST;      le32(b, &n, S_SPAN);     // $w0 = &"span"
    b[n++] = OP_SETARG;      le32(b, &n, 0);          // $a0 = ptr
    b[n++] = OP_ICONST;      le32(b, &n, len_span);   // $w0 = len
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_CREATE);   // $w0 = handle(el)
    b[n++] = OP_STORE;                                 // $w1 = el (stash)

    // setText(el, "hello from teko");  args = ($a0=el, $a1=ptr, $w0=len)
    b[n++] = OP_LOAD;                                  // $w0 = el
    b[n++] = OP_SETARG;      le32(b, &n, 0);          // $a0 = el
    b[n++] = OP_SCONST;      le32(b, &n, S_TEXT);     // $w0 = &"hello from teko"
    b[n++] = OP_SETARG;      le32(b, &n, 1);          // $a1 = ptr
    b[n++] = OP_ICONST;      le32(b, &n, len_text);   // $w0 = len
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_SETTEXT);  // setText(el, ptr, len)

    // out = getElementById("out");  args = ($a0=ptr, $w0=len)
    b[n++] = OP_SCONST;      le32(b, &n, S_OUT);      // $w0 = &"out"
    b[n++] = OP_SETARG;      le32(b, &n, 0);          // $a0 = ptr
    b[n++] = OP_ICONST;      le32(b, &n, len_out);    // $w0 = len
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_GETBYID);  // $w0 = handle(out)

    // appendChild(out, el);  args = ($a0=out, $w0=el)
    b[n++] = OP_SETARG;      le32(b, &n, 0);          // $a0 = out (currently in $w0)
    b[n++] = OP_LOAD;                                  // $w0 = el ($w1 still holds el)
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_APPEND);   // appendChild(out, el)

    b[n++] = OP_HALT;

    teko_metal_emit_program(ctx, b, (uint32_t)n);

    // Generate the JS glue from the still-live import table BEFORE closing (close
    // frees ctx). The glue is a separate file; close only finalizes the .wat.
    int glue_rc = teko_metal_emit_dom_glue(ctx, out_glue);
    teko_metal_close(ctx);
    if (glue_rc != 0) {
        fprintf(stderr, "emit_dom: cannot write glue %s\n", out_glue);
        return 2;
    }
    printf("emit_dom: wrote %d IL bytes -> %s (+ glue %s)\n", n, out_wat, out_glue);
    return 0;
}
