#include "emit_linux_riscv_common.h"
#include <stdio.h>

// Shared RISC-V (Linux) instruction emitter. The 32-bit and 64-bit emitters are
// identical apart from load/store width, frame/offset sizes, the arena frame
// size, the banner bit-width, and the local-label tag — all carried in `p`.
void emit_linux_riscv_common(MetalContext* ctx, OpCode op, int32_t arg, const RiscvEmitParams* p) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: RISC-V %s-bit (Linux)\n", p->bits);
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global main\n\n");
            fprintf(ctx->file, "main:\n");
            fprintf(ctx->file, "    addi sp, sp, -%d\n", p->frame);
            fprintf(ctx->file, "    %s ra, %d(sp)\n", p->store, p->ra_off);
            fprintf(ctx->file, "    %s s1, %d(sp)\n", p->store, p->s1_off);
            fprintf(ctx->file, "    addi s1, sp, %d\n\n", p->frame);
            break;

        case OP_HALT:
            fprintf(ctx->file, "    li a0, 0\n");
            fprintf(ctx->file, "    li a7, 93\n");
            fprintf(ctx->file, "    ecall\n");
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    li a0, %d\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    la a0, .L_linux_str_%d\n", arg);
            break;

        case OP_STORE:
            fprintf(ctx->file, "    mv a1, a0\n");
            break;

        case OP_LOAD:
            fprintf(ctx->file, "    mv a0, a1\n");
            break;

        case OP_ADD:
            fprintf(ctx->file, "    add a0, a0, a1\n");
            break;

        case OP_SUB:
            fprintf(ctx->file, "    sub a0, a0, a1\n");
            break;

        case OP_MUL:
            fprintf(ctx->file, "    mul a0, a0, a1\n");
            break;

        case OP_DIV:
            ctx->label_count++;
            fprintf(ctx->file, "    beqz a1, .L_%s_div_zero_%d\n", p->tag, ctx->label_count);
            fprintf(ctx->file, "    div a0, a0, a1\n");
            fprintf(ctx->file, "    j .L_%s_div_ok_%d\n", p->tag, ctx->label_count);
            fprintf(ctx->file, ".L_%s_div_zero_%d:\n", p->tag, ctx->label_count);
            fprintf(ctx->file, "    li a0, -1\n");
            fprintf(ctx->file, ".L_%s_div_ok_%d:\n", p->tag, ctx->label_count);
            break;

        case OP_ARENA_PUSH:
            fprintf(ctx->file, "    addi sp, sp, -%d\n", p->arena);
            fprintf(ctx->file, "    mv s1, sp\n");
            break;

        case OP_ARENA_POP:
            fprintf(ctx->file, "    addi sp, sp, %d\n", p->arena);
            break;

        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    mv a0, sp\n");
            fprintf(ctx->file, "    call pthread_create\n");
            break;

        case OP_CHAN_INIT:
            fprintf(ctx->file, "    mv a0, s1\n");
            fprintf(ctx->file, "    addi s1, s1, %d\n", p->frame);
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    li a0, 1\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        case OP_JMP:
            fprintf(ctx->file, "    j .L_%s_label_%d\n", p->tag, arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    beqz a0, .L_%s_label_%d\n", p->tag, arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    %s ra, %d(sp)\n", p->load, p->ra_off);
            fprintf(ctx->file, "    %s s1, %d(sp)\n", p->load, p->s1_off);
            fprintf(ctx->file, "    addi sp, sp, %d\n", p->frame);
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
