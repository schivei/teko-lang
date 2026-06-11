#ifndef CODEGEN_OPT_H
#define CODEGEN_OPT_H

#include <stdbool.h>
#include <stdint.h>
#include "../codegen_li.h"

// Enumeração de Registradores Otimizados Atribuídos
typedef enum {
    REG_ACCUMULATOR, // w0 / %eax (Padrão de retorno/interrupção)
    REG_BASE,        // w1 / %ebx (Padrão de operando secundário)
    REG_TEMP0,       // w2 / %ecx (Retenção local otimizada O(1))
    REG_TEMP1,       // w3 / %edx (Retenção local otimizada O(1))
    REG_ARENA        // x19 / %r12 (Escapou para a memória)
} TekoPhysReg;

// Estrutura que rastreia o ciclo de vida de uma instrução na LI
typedef struct {
    OpCode op;
    int32_t arg;
    bool escapes;
    TekoPhysReg assigned_reg;
} OptInstruction;

// Assinaturas públicas do Motor de Otimização AOT
void teko_optimize_register_allocation(const unsigned char* bytecode, uint32_t size, OptInstruction* out_optimized, uint32_t* out_count);

#endif // CODEGEN_OPT_H
