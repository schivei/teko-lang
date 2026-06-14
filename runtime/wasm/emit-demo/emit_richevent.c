// Phase 11 (Browser FFI) MVP-4 — rich event payload. Builds on MVP-3 callbacks +
// the MVP-4 allocator: the listener marshals the event TARGET'S VALUE (a JS string)
// into wasm memory and calls back with (ptr,len), not just a handle. Emits:
//
//   $main:           dom.on_value(dom.getElementById("inp"), "input", /*fn*/ 0)
//   routine 0 (cb):  echo(ptr,len): dom.setText(dom.getElementById("echo"), ptr, len)
//
// The glue's dom.on_value reads e.target.value, teko_alloc's it, copies the bytes,
// and calls teko_invoke2(0, ptr, len). The host harness types into #inp and asserts
// #echo mirrors it — proving JS event data crosses into Teko via the real allocator.
//
// Build:
//   cc -I src runtime/wasm/emit-demo/emit_richevent.c build/libteko_core.a -o emit_richevent
//   ./emit_richevent runtime/wasm/samples/emitted_richevent.wat \
//                     runtime/wasm/samples/emitted_richevent.glue.mjs
#include "codegen/codegen_metal.h"
#include <stdio.h>
#include <string.h>

static void le32(unsigned char* b, int* n, int32_t v) {
    b[(*n)++] = v & 0xFF; b[(*n)++] = (v >> 8) & 0xFF;
    b[(*n)++] = (v >> 16) & 0xFF; b[(*n)++] = (v >> 24) & 0xFF;
}

int main(int argc, char** argv) {
    const char* out_wat  = (argc > 1) ? argv[1] : "emitted_richevent.wat";
    const char* out_glue = (argc > 2) ? argv[2] : "emitted_richevent.glue.mjs";

    TekoTarget target;
    memset(&target, 0, sizeof target);
    target.arch = ARCH_WASM32; target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);
    MetalContext* ctx = teko_metal_create(out_wat, target);
    if (!ctx) { fprintf(stderr, "emit_richevent: cannot open %s\n", out_wat); return 2; }

    static const char* strings[] = { "inp", "echo", "input" };
    enum { S_INP = 0, S_ECHO = 1, S_INPUT = 2 };
    teko_metal_set_strings(ctx, strings, 3);

    static const TekoWasmImport imports[] = {
        { "dom", "getElementById", 2, 1 }, // (ptr,len) -> handle       : #0
        { "dom", "setText",        3, 0 }, // (handle,ptr,len)          : #1
        { "dom", "on_value",       4, 0 }, // (handle,ptr,len,fn_index) : #2
    };
    enum { I_GETBYID = 0, I_SETTEXT = 1, I_ONVALUE = 2 };
    teko_metal_set_imports(ctx, imports, 3);

    const int len_inp   = (int)strlen(strings[S_INP]);   // 3
    const int len_echo  = (int)strlen(strings[S_ECHO]);  // 4
    const int len_input = (int)strlen(strings[S_INPUT]); // 5
    const int CB_FN = 0;

    unsigned char b[192]; int n = 0;

    // --- $main: on_value(getElementById("inp"), "input", CB_FN) ---
    b[n++] = OP_SCONST;      le32(b, &n, S_INP);     // $w0 = &"inp"
    b[n++] = OP_SETARG;      le32(b, &n, 0);         // $a0 = ptr
    b[n++] = OP_ICONST;      le32(b, &n, len_inp);   // $w0 = len
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_GETBYID); // $w0 = #inp handle

    b[n++] = OP_SETARG;      le32(b, &n, 0);         // $a0 = #inp handle
    b[n++] = OP_SCONST;      le32(b, &n, S_INPUT);   // $w0 = &"input"
    b[n++] = OP_SETARG;      le32(b, &n, 1);         // $a1 = ptr
    b[n++] = OP_ICONST;      le32(b, &n, len_input); // $w0 = len
    b[n++] = OP_SETARG;      le32(b, &n, 2);         // $a2 = len
    b[n++] = OP_ICONST;      le32(b, &n, CB_FN);     // $w0 = fn index
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_ONVALUE); // on_value(#inp, "input", 0)
    b[n++] = OP_HALT;

    // --- routine 0: echo(ptr=$w0, len=$w1) -> setText(#echo, ptr, len) ---
    b[n++] = OP_FUNC_BEGIN;  le32(b, &n, CB_FN);
    b[n++] = OP_SETARG;      le32(b, &n, 1);         // $a1 = value ptr
    b[n++] = OP_SCONST;      le32(b, &n, S_ECHO);    // $w0 = &"echo"
    b[n++] = OP_SETARG;      le32(b, &n, 0);         // $a0 = echo-name ptr
    b[n++] = OP_ICONST;      le32(b, &n, len_echo);  // $w0 = echo-name len
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_GETBYID); // $w0 = #echo handle
    b[n++] = OP_SETARG;      le32(b, &n, 0);         // $a0 = #echo handle
    b[n++] = OP_LOAD;                                 // $w0 = $w1 = value len
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_SETTEXT); // setText(#echo, value ptr, value len)
    b[n++] = OP_FUNC_END;

    teko_metal_emit_program(ctx, b, (uint32_t)n);

    int grc = teko_metal_emit_dom_glue(ctx, out_glue);
    teko_metal_close(ctx);
    if (grc != 0) { fprintf(stderr, "emit_richevent: cannot write glue %s\n", out_glue); return 2; }
    printf("emit_richevent: wrote %d IL bytes -> %s (+ glue %s)\n", n, out_wat, out_glue);
    return 0;
}
