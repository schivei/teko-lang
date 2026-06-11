#include "codegen_metal.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

MetalContext* teko_metal_create(const char* output_asm_path, TekoTarget target) {
    if (!output_asm_path) return NULL;

    FILE* f = fopen(output_asm_path, "w");
    if (!f) return NULL;

    MetalContext* ctx = (MetalContext*)malloc(sizeof(MetalContext));
    ctx->file = f;
    ctx->target = target;
    ctx->label_count = 0;
    return ctx;
}

// O ROTEADOR POLIMÓRFICO CENTRAL: Encaminha os opcodes para a função correta de OS/CPU
static void teko_metal_route_instruction(MetalContext* ctx, OpCode op, int32_t arg) {
    auto os = ctx->target.os;
    auto arch = ctx->target.arch;

    // 1. Ecossistema Apple (macOS Darwin)
    if (os == OS_MACOS_DARWIN) {
        if (arch == ARCH_APPLE_SILICON || arch == ARCH_ARM64) {
            emit_darwin_arm64(ctx, op, arg);
            return;
        }
        if (arch == ARCH_X86_64) {
            emit_darwin_x86_64(ctx, op, arg);
            return;
        }
    }
    // 2. Ecossistema Windows (PE/COFF MSVC)
    else if (os == OS_WINDOWS) {
        if (arch == ARCH_X86_64) { emit_win_x86_64(ctx, op, arg); return; }
        if (arch == ARCH_X86)    { emit_win_x86(ctx, op, arg); return; }
        if (arch == ARCH_ARM64)  { emit_win_arm64(ctx, op, arg); return; }
    }
    // 3. Ecossistema Bare-Metal / Virtualizadores independentes de OS
    else if (os == OS_BARE_METAL || os == OS_WASI) {
        if (arch == ARCH_WASM32 || arch == ARCH_WASM64) { emit_wasm_pure(ctx, op, arg); return; }
    }
    // 4. Ecossistema Linux e BSD Unix (Mapeamento padrão unificado)
    else {
        if (os == OS_LINUX) {
            switch (arch) {
                case ARCH_X86_64: emit_linux_x86_64(ctx, op, arg); return;
                case ARCH_X86:    emit_linux_x86(ctx, op, arg); return;
                case ARCH_ARM64:  emit_linux_arm64(ctx, op, arg); return;
                case ARCH_ARM32:  emit_linux_arm32(ctx, op, arg); return;
                case ARCH_RISCV64:emit_linux_riscv64(ctx, op, arg); return;
                case ARCH_RISCV32:emit_linux_riscv32(ctx, op, arg); return;
                case ARCH_MIPS32:
                case ARCH_MIPS64: emit_linux_mips(ctx, op, arg); return;
                case ARCH_PPC64:  emit_linux_ppc64(ctx, op, arg); return;
                default: break;
            }
        }
        else {
            if (arch == ARCH_X86_64) { emit_freebsd_x86_64(ctx, op, arg); return; }
            if (arch == ARCH_ARM64)  { emit_freebsd_arm64(ctx, op, arg); return; }
        }
    }
}

// Varredura linear com Motores CSE e DCE integrados cooperativamente
static void process_linear_il_bytes(MetalContext* ctx, const unsigned char* bytecode, uint32_t size) {
    uint32_t i = 0;
    bool dead_code_zone = false;

    // ESTADO DE CACHE DO MOTOR CSE
    bool accum_has_value = false;
    int32_t accum_last_value = 0;
    OpCode last_arith_op = (OpCode)0; // Rastreia a última operação matemática realizada

    while (i < size) {
        OpCode op = (OpCode)bytecode[i++];
        int32_t arg = 0;
        bool skip_by_cse = false; // Flag de supressão de instrução redundante

        if (op == OP_ICONST || op == OP_SCONST || op == OP_JMP || op == OP_JMP_IF_FALSE) {
            arg = (bytecode[i + 0] << 0)  |
                  (bytecode[i + 1] << 8)  |
                  (bytecode[i + 2] << 16) |
                  (bytecode[i + 3] << 24);
            i += 4;
        }

        // BARREIRA DE RESSURREIÇÃO DCE / INVALIDAÇÃO DE CACHE CSE (Uma label quebra o fluxo linear)
        if ((int)op >= 100) {
            dead_code_zone = false;
            accum_has_value = false; // Invalida o cache porque não sabemos a origem do salto
            last_arith_op = (OpCode)0;
        }

        // AVALIAÇÃO DO MOTOR CSE (Apenas em áreas alcançáveis)
        if (!dead_code_zone) {
            if (op == OP_ICONST) {
                if (accum_has_value && accum_last_value == arg && last_arith_op == (OpCode)0) {
                    skip_by_cse = true; // Constante redundante consecutiva detectada!
                } else {
                    accum_last_value = arg;
                    accum_has_value = true;
                    last_arith_op = (OpCode)0; // Reset porque reescreveu o acumulador
                }
            }
            else if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV) {
                if (last_arith_op == op) {
                    // Operação matemática idêntica consecutiva sem mutação de registradores detectada!
                    skip_by_cse = true;
                } else {
                    last_arith_op = op;
                    accum_has_value = false; // O resultado mudou, invalida valor constante
                }
            }
            else if (op == OP_STORE || op == OP_LOAD || op == OP_SPAWN_ASYNC || op == OP_CHAN_INIT) {
                // Instruções de barreira ou efeito colateral invalidam estados parciais do CSE
                last_arith_op = (OpCode)0;
            }
        }

        // DESPACHO DOS EVENTOS SE PASSAR PELO FILTRO DUPLO (DCE + CSE)
        if (!dead_code_zone) {
            if (skip_by_cse) {
                printf("[CSE Opt]: Subexpressao redundante purgada da emissao: 0x%02X (arg: %d)\n", op, arg);
            } else {
                if (op == OP_ICONST || op == OP_SCONST || op == OP_JMP || op == OP_JMP_IF_FALSE) {
                    teko_metal_route_instruction(ctx, op, arg);
                }
                else {
                    teko_metal_route_instruction(ctx, op, 0);
                }
            }
        } else {
            printf("[DCE Opt]: Instrucao morta purgada da emissao: 0x%02X\n", op);
        }

        // GATILHO DCE
        if (op == OP_RETURN || op == OP_HALT) {
            dead_code_zone = true;
            accum_has_value = false;
            last_arith_op = (OpCode)0;
        }
    }
}

void teko_metal_emit_program(MetalContext* ctx, const unsigned char* bytecode, uint32_t size) {
    if (!ctx || !bytecode || size == 0) return;

    teko_metal_route_instruction(ctx, OP_PROLOG, 0);
    process_linear_il_bytes(ctx, bytecode, size);
    teko_metal_route_instruction(ctx, OP_EPILOG, 0);
}

void teko_metal_close(MetalContext* ctx) {
    if (!ctx) return;
    if (ctx->file) {
        fclose(ctx->file);
    }
    free(ctx);
}
