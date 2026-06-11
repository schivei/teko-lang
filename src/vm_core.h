#ifndef VM_CORE_H
#define VM_CORE_H

#include <stdint.h>
#include "codegen_li.h"
#include "vm_arena.h"

#define VM_REGISTERS_COUNT 16
#define VM_STACK_MAX_DEPTH 256

// Estrutura que representa o frame de ativação de uma função na Call Stack
typedef struct {
    uint32_t return_address;     // Endereço de IP para retornar
    int base_register_offset;    // Janela de registradores locais
} VMFrame;

// Estrutura principal do estado de Runtime da VM Teko
typedef struct {
    unsigned char* bytecode;     // Vetor contíguo de opcodes (.tkb)
    uint32_t bytecode_size;      // Tamanho total do segmento de código
    uint32_t ip;                 // Instruction Pointer (Ponteiro de instrução atual)

    int32_t registers[VM_REGISTERS_COUNT]; // Registradores Virtuais Genéricos

    // Tabela de Constantes (String Pool) resolvida em memória
    char** constant_pool;
    uint32_t constant_pool_count;

    // Call Stack para controle de escopo de funções
    VMFrame call_stack[VM_STACK_MAX_DEPTH];
    uint32_t csp;                // Call Stack Pointer

    TekoArena* memory_arena;     // A Arena ativa injetada implicitamente
} TekoVM;

// Assinaturas públicas do Interpretador Core
TekoVM* teko_vm_create(unsigned char* bytecode, uint32_t size, char** string_pool, uint32_t pool_count);
int32_t teko_vm_execute(TekoVM* vm);
void teko_vm_destroy(TekoVM* vm);

#endif // VM_CORE_H