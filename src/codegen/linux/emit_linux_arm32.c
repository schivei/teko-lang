#include "../codegen_metal.h"
#include <stdio.h>

void emit_linux_arm32(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        // ====================================================================
        // 1. DIRETIVAS DE INICIALIZAÇÃO, SÍMBOLOS E PRÓLOGO (AAPCS ELF ABI)
        // ====================================================================
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: ARM32 (32-bit Linux ARMv7)\n");
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global main\n");
            fprintf(ctx->file, ".align 2\n\n");
            fprintf(ctx->file, "main:\n");

            // Prólogo: Reserva espaço e preserva o Frame Pointer (fp/r11), Link Register (lr/r14)
            // e o nosso cursor de Arena Callee-Saved (r4)
            fprintf(ctx->file, "    push {r4, fp, lr}\n");
            fprintf(ctx->file, "    add fp, sp, #8\n\n");
            break;

        // ====================================================================
        // 2. OPCODES DE LITERAIS, REGISTRADORES E CONVERSÃO
        // ====================================================================
        case OP_HALT:
            fprintf(ctx->file, "    ;; [Linux ARM32 Halt]: sys_exit nativo via interrupcao svc\n");
            fprintf(ctx->file, "    mov r0, #0\n");    // Código de saída 0 em r0
            fprintf(ctx->file, "    mov r7, #1\n");    // Número da syscall exit no Linux ARM32 é 1
            fprintf(ctx->file, "    svc 0\n");         // Supervisor Call (Syscall trap)
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    mov r0, #%d\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    ldr r0, =.L_linux_str_%d\n", arg);
            break;

        case OP_STORE:
            fprintf(ctx->file, "    mov r1, r0\n");
            break;

        case OP_LOAD:
            fprintf(ctx->file, "    mov r0, r1\n");
            break;

        // ====================================================================
        // 3. MOTOR ARITMÉTICO CORE RISC DE 32 BITS
        // ====================================================================
        case OP_ADD:
            fprintf(ctx->file, "    add r0, r0, r1\n");
            break;

        case OP_SUB:
            fprintf(ctx->file, "    sub r0, r0, r1\n");
            break;

        case OP_MUL:
            fprintf(ctx->file, "    mul r0, r0, r1\n");
            break;

        case OP_DIV:
            ctx->label_count++;
            fprintf(ctx->file, "    cmp r1, #0\n");
            fprintf(ctx->file, "    beq .L_linux_arm32_div_zero_%d\n", ctx->label_count);
            fprintf(ctx->file, "    sdiv r0, r0, r1\n");
            fprintf(ctx->file, "    b .L_linux_arm32_div_ok_%d\n", ctx->label_count);
            fprintf(ctx->file, ".L_linux_arm32_div_zero_%d:\n", ctx->label_count);
            fprintf(ctx->file, "    mov r0, #-1\n");
            fprintf(ctx->file, ".L_linux_arm32_div_ok_%d:\n", ctx->label_count);
            break;

        // ====================================================================
        // 4. GERENCIAMENTO DE MEMÓRIA POR REGIÃO (ARENA NATIVA O(1))
        // ====================================================================
        case OP_ARENA_PUSH:
            fprintf(ctx->file, "    sub sp, sp, #512\n");
            fprintf(ctx->file, "    mov r4, sp\n");
            break;

        case OP_ARENA_POP:
            fprintf(ctx->file, "    add sp, sp, #512\n");
            break;

        // ====================================================================
        // 5. PARALELISMO E CANAIS (INTRINSICS THREADS DO LINUX)
        // ====================================================================
        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    ;; --- [Linux ARM32 Spawn via pthread_create] ---\n");
            fprintf(ctx->file, "    mov r0, sp\n");
            fprintf(ctx->file, "    bl pthread_create\n");
            break;

        case OP_CHAN_INIT:
            fprintf(ctx->file, "    mov r0, r4\n");
            fprintf(ctx->file, "    add r4, r4, #16\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    mov r0, #1\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        // ====================================================================
        // 6. CONTROLE DE FLUXO, DESVIOS E EPÍLOGO
        // ====================================================================
        case OP_JMP:
            fprintf(ctx->file, "    b .L_linux_arm32_label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    cmp r0, #0\n");
            fprintf(ctx->file, "    beq .L_linux_arm32_label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    pop {r4, fp, lr}\n");
            fprintf(ctx->file, "    bx lr\n");
            break;

        default:
            // RESSURREIÇÃO DCE LINUX ARM32
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_linux_arm32_label_%d:\n", (int)op);
            }
            break;
    }
}
