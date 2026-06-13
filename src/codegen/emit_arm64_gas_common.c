#include "emit_arm64_gas_common.h"
#include <stdio.h>

// Shared GAS-syntax arm64 emitter for Linux, FreeBSD and macOS. The three targets
// emit identical instructions apart from the banner, symbol decoration, string
// label and its relocation syntax (ELF :lo12: vs Mach-O @PAGE/@PAGEOFF), the
// halt/exit sequence, the local-label tag, and a couple of comment lines.
void emit_arm64_gas_common(MetalContext* ctx, OpCode op, int32_t arg, const Arm64GasEmitParams* p) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: %s\n", p->banner);
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global %smain\n", p->sym_prefix);
            fprintf(ctx->file, ".align 4\n\n");
            fprintf(ctx->file, "%smain:\n", p->sym_prefix);
            fprintf(ctx->file, "    stp x29, x30, [sp, #-32]!\n");
            fprintf(ctx->file, "    str x19, [sp, #16]\n");
            fprintf(ctx->file, "    mov x29, sp\n\n");
            break;

        case OP_HALT:
            fprintf(ctx->file, "%s", p->halt_comment);
            if (p->halt_svc_num >= 0) {
                fprintf(ctx->file, "    mov w0, #0\n");
                fprintf(ctx->file, "    mov x8, #%d\n", p->halt_svc_num);
                fprintf(ctx->file, "    svc #0\n");
            }
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    mov w0, #%d\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    adrp x0, %s%d%s\n", p->str_label, arg, p->adrp_suffix);
            fprintf(ctx->file, "    add x0, x0, %s%s%d%s\n", p->add_reloc, p->str_label, arg, p->add_suffix);
            break;

        case OP_STORE:
            fprintf(ctx->file, "    mov w1, w0\n");
            break;

        case OP_LOAD:
            fprintf(ctx->file, "    mov w0, w1\n");
            break;

        case OP_ADD:
            fprintf(ctx->file, "    add w0, w0, w1\n");
            break;

        case OP_SUB:
            fprintf(ctx->file, "    sub w0, w0, w1\n");
            break;

        case OP_MUL:
            fprintf(ctx->file, "    mul w0, w0, w1\n");
            break;

        case OP_DIV:
            ctx->label_count++;
            fprintf(ctx->file, "    cbz w1, .L_%s_div_zero_%d\n", p->tag, ctx->label_count);
            fprintf(ctx->file, "    sdiv w0, w0, w1\n");
            fprintf(ctx->file, "    b .L_%s_div_ok_%d\n", p->tag, ctx->label_count);
            fprintf(ctx->file, ".L_%s_div_zero_%d:\n", p->tag, ctx->label_count);
            fprintf(ctx->file, "    mov w0, #-1\n");
            fprintf(ctx->file, ".L_%s_div_ok_%d:\n", p->tag, ctx->label_count);
            break;

        case OP_ARENA_PUSH:
            fprintf(ctx->file, "    sub sp, sp, #1024\n");
            fprintf(ctx->file, "    mov x19, sp\n");
            break;

        case OP_ARENA_POP:
            fprintf(ctx->file, "    add sp, sp, #1024\n");
            break;

        case OP_SPAWN_ASYNC:
            if (p->spawn_comment) fprintf(ctx->file, "%s", p->spawn_comment);
            fprintf(ctx->file, "    mov x0, sp\n");
            fprintf(ctx->file, "    bl %spthread_create\n", p->sym_prefix);
            break;

        case OP_CHAN_INIT:
            if (p->chan_comment) fprintf(ctx->file, "%s", p->chan_comment);
            fprintf(ctx->file, "    mov x0, x19\n");
            fprintf(ctx->file, "    add x19, x19, #32\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    mov w0, #1\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        case OP_JMP:
            fprintf(ctx->file, "    b .L_%s_label_%d\n", p->tag, arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    cbz w0, .L_%s_label_%d\n", p->tag, arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    ldr x19, [sp, #16]\n");
            fprintf(ctx->file, "    ldp x29, x30, [sp], #32\n");
            fprintf(ctx->file, "    ret\n");
            break;

        default:
            // DCE RESURRECTION
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_%s_label_%d:\n", p->tag, (int)op);
            }
            break;
    }
}
