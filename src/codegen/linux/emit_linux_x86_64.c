#include "../codegen_metal.h"
#include <stdio.h>

void emit_linux_x86_64(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        // ====================================================================
        // 1. DIRETIVAS DE INICIALIZAÇÃO, SÍMBOLOS E PRÓLOGO (ELF ABI LINUX)
        // ====================================================================
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: Intel/AMD (64-bit Linux)\n");
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global main\n\n");
            fprintf(ctx->file, "main:\n");

            // Prólogo System V AMD64 ABI: Preserva o Frame Pointer e o registrador da Arena (%r12)
            fprintf(ctx->file, "    pushq %%rbp\n");
            fprintf(ctx->file, "    movq %%rsp, %%rbp\n");
            fprintf(ctx->file, "    pushq %%r12\n\n"); // %r12 é o registrador callee-saved da nossa Arena Ativa
            break;

        // ====================================================================
        // 2. OPCODES DE LITERAIS, REGISTRADORES E CONVERSÃO
        // ====================================================================
        case OP_HALT:
            fprintf(ctx->file, "    ;; [Linux x86_64 Halt]: sys_exit nativo via interrupcao\n");
            fprintf(ctx->file, "    movq $60, %%rax\n"); // Syscall exit no Linux x86_64 é 60
            fprintf(ctx->file, "    movq $0, %%rdi\n");  // Código de saída 0
            fprintf(ctx->file, "    syscall\n");
            break;

        case OP_ICONST:
            // Carrega um valor imediato diretamente no acumulador soberano %eax
            fprintf(ctx->file, "    movl $%d, %%eax\n", arg);
            break;

        case OP_SCONST:
            // Endereçamento RIP-relative nativo e obrigatório para ELF PIE no Linux
            fprintf(ctx->file, "    leaq .L_linux_str_%d(%%rip), %%rax\n", arg);
            break;

        case OP_STORE:
            // Transfere o acumulador principal %eax para o registrador base %ebx
            fprintf(ctx->file, "    movl %%eax, %%ebx\n");
            break;

        case OP_LOAD:
            // Restaura o valor guardado de %ebx de volta para %eax
            fprintf(ctx->file, "    movl %%ebx, %%eax\n");
            break;

        // ====================================================================
        // 3. MOTOR ARITMÉTICO CORE E PROTEÇÃO CONTRA DIVISÃO POR ZERO
        // ====================================================================
        case OP_ADD:
            fprintf(ctx->file, "    addl %%ebx, %%eax\n");
            break;

        case OP_SUB:
            fprintf(ctx->file, "    subl %%ebx, %%eax\n");
            break;

        case OP_MUL:
            fprintf(ctx->file, "    imull %%ebx, %%eax\n");
            break;

        case OP_DIV:
            ctx->label_count++;
            // BARREIRA DE HARDWARE: Compara o divisor %ebx com zero
            fprintf(ctx->file, "    cmpl $0, %%ebx\n");
            fprintf(ctx->file, "    je .L_linux_div_zero_%d\n", ctx->label_count);

            // Preparação x86_64: Estende o sinal de %eax para o par %edx:%eax antes da divisao
            fprintf(ctx->file, "    cltd\n");
            fprintf(ctx->file, "    idivl %%ebx\n");
            fprintf(ctx->file, "    jmp .L_linux_div_ok_%d\n", ctx->label_count);

            fprintf(ctx->file, ".L_linux_div_zero_%d:\n", ctx->label_count);
            fprintf(ctx->file, "    movl $-1, %%eax\n"); // Evita o crash enviando o fallback
            fprintf(ctx->file, ".L_linux_div_ok_%d:\n", ctx->label_count);
            break;

        // ====================================================================
        // 4. GERENCIAMENTO DE MEMÓRIA POR REGIÃO (ARENA NATIVA O(1))
        // ====================================================================
        case OP_ARENA_PUSH:
            // Aloca 1KB contíguo decrementando o Stack Pointer (%rsp)
            fprintf(ctx->file, "    subq $1024, %%rsp\n");
            // %r12 vira o cursor estático contíguo da Arena
            fprintf(ctx->file, "    movq %%rsp, %%r12\n");
            break;

        case OP_ARENA_POP:
            // Desalocação atômica em tempo O(1) restaurando a pilha
            fprintf(ctx->file, "    addq $1024, %%rsp\n");
            break;

        // ====================================================================
        // 5. PARALELISMO E CANAIS (INTRINSICS INTEGRADAS ÀS THREADS DO LINUX)
        // ====================================================================
        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    ;; --- [Linux AOT Coroutine Spawn via pthread_create] ---\n");
            fprintf(ctx->file, "    movq %%rsp, %%rdi\n");
            fprintf(ctx->file, "    call pthread_create\n");
            break;

        case OP_CHAN_INIT:
            fprintf(ctx->file, "    ;; --- [Linux AOT Arena-Based Channel Allocation] ---\n");
            fprintf(ctx->file, "    movq %%r12, %%rax\n");
            fprintf(ctx->file, "    addq $32, %%r12\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    movl $1, %%eax\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        // ====================================================================
        // 6. CONTROLE DE FLUXO E RETORNO DE SUB-ROTINAS
        // ====================================================================
        case OP_JMP:
            fprintf(ctx->file, "    jmp .L_linux_label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    cmpl $0, %%eax\n");
            fprintf(ctx->file, "    je .L_linux_label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            // Epílogo: Restaura a Arena original %r12 e desmonta o frame de pilha
            fprintf(ctx->file, "    popq %%r12\n");
            fprintf(ctx->file, "    popq %%rbp\n");
            fprintf(ctx->file, "    ret\n");
            break;

        default:
            // RESSURREIÇÃO DCE LINUX X86_64
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_linux_label_%d:\n", (int)op);
            }
            break;
    }
}
