#ifndef TLD_SYMBOLS_H
#define TLD_SYMBOLS_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_SYMBOL_NAME 128
#define MAX_SYMBOL_COUNT 512

// Tipo de símbolo mapeado no silício
typedef enum {
    SYM_FUNC,      // Função ou Método público
    SYM_SERVICE,   // Serviço ou Injeção de Dependência nativa
    SYM_RODATA     // String ou Constante imutável (.rodata)
} TekoSymbolType;

// Estrutura física de um Símbolo na Tabela do Linker
typedef struct {
    char name[MAX_SYMBOL_NAME];
    TekoSymbolType type;
    uint64_t virtual_address; // Endereço virtual calculado na RAM (VMA)
    uint32_t local_offset;     // Offset relativo dentro do seu próprio bloco de bytes
    bool is_defined;          // True se o corpo físico foi fornecido, False se for dependência externa
} TekoLinkerSymbol;

// Estrutura de uma Referência Pendente (Onde a dependência foi invocada e precisa de injeção)
typedef struct {
    char target_name[MAX_SYMBOL_NAME]; // Nome do símbolo que ela busca
    uint32_t patch_offset;              // Posição exata no código onde os bytes do salto devem ser injetados
    uint64_t call_site_vaddr;          // Endereço virtual de onde a chamada está ocorrendo
} TekoDependencyPatch;

// Estrutura unificada da Tabela do Linker
typedef struct {
    TekoLinkerSymbol symbols[MAX_SYMBOL_COUNT];
    uint32_t symbol_count;
    TekoDependencyPatch patches[MAX_SYMBOL_COUNT];
    uint32_t patch_count;
} TekoSymbolTable;

// Assinaturas públicas do Gerenciador de Dependências do Linker
void tld_symbols_init(TekoSymbolTable* table);
bool tld_symbols_define(TekoSymbolTable* table, const char* name, TekoSymbolType type, uint32_t local_offset);
void tld_symbols_reference(TekoSymbolTable* table, const char* target_name, uint32_t patch_offset);
bool tld_symbols_resolve_and_inject(TekoSymbolTable* table, uint8_t* code_buffer, uint64_t base_vaddr);

#endif // TLD_SYMBOLS_H
