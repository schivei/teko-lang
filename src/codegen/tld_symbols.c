#include "tld_symbols.h"
#include <string.h>
#include <stdio.h>

void tld_symbols_init(TekoSymbolTable* table) {
    if (!table) return;
    table->symbol_count = 0;
    table->patch_count = 0;
}

bool tld_symbols_define(TekoSymbolTable* table, const char* name, TekoSymbolType type, uint32_t local_offset) {
    if (!table || !name || table->symbol_count >= MAX_SYMBOL_COUNT) return false;

    // Verifica duplicidade de símbolos
    for (uint32_t i = 0; i < table->symbol_count; i++) {
        if (strcmp(table->symbols[i].name, name) == 0) {
            return false; // Símbolo já definido (Conflito de Linkagem)
        }
    }

    TekoLinkerSymbol* sym = &table->symbols[table->symbol_count++];
    strncpy(sym->name, name, MAX_SYMBOL_NAME - 1);
    sym->type = type;
    sym->local_offset = local_offset;
    sym->virtual_address = 0;
    sym->is_defined = true;
    return true;
}

void tld_symbols_reference(TekoSymbolTable* table, const char* target_name, uint32_t patch_offset) {
    if (!table || !target_name || table->patch_count >= MAX_SYMBOL_COUNT) return;

    TekoDependencyPatch* patch = &table->patches[table->patch_count++];
    strncpy(patch->target_name, target_name, MAX_SYMBOL_NAME - 1);
    patch->patch_offset = patch_offset;
    patch->call_site_vaddr = 0;
}

bool tld_symbols_resolve_and_inject(TekoSymbolTable* table, uint8_t* code_buffer, uint64_t base_vaddr) {
    if (!table || !code_buffer) return false;

    // Passo 1: Passagem de Geometria - Calcula o VMA absoluto de todos os símbolos definidos
    for (uint32_t i = 0; i < table->symbol_count; i++) {
        table->symbols[i].virtual_address = base_vaddr + table->symbols[i].local_offset;
    }

    // Passo 2: Passagem de Remendo - Injeta as dependências resolvendo os endereços
    for (uint32_t i = 0; i < table->patch_count; i++) {
        TekoDependencyPatch* patch = &table->patches[i];
        TekoLinkerSymbol* target_sym = NULL;

        // Procura o destino da dependência na tabela
        for (uint32_t j = 0; j < table->symbol_count; j++) {
            if (strcmp(table->symbols[j].name, patch->target_name) == 0) {
                target_sym = &table->symbols[j];
                break;
            }
        }

        if (!target_sym) {
            fprintf(stderr, "[TLD Linker Error]: Falha catastrófica de injeção! A dependência '%s' não foi encontrada.\n", patch->target_name);
            return false; // Símbolo não resolvido aborta o build de release
        }

        // Calcula o endereço absoluto do local da chamada na RAM
        patch->call_site_vaddr = base_vaddr + patch->patch_offset;

        // Regra de Injeção de Desvio Relativo Padrão de 32 bits (Call/JMP x86_64 e ARM64):
        // Offset = Endereço_Destino - (Endereço_da_Chamada + 4 bytes do operando)
        int32_t relative_offset = (int32_t)target_sym->virtual_address - ((int32_t)patch->call_site_vaddr + 4);

        // Injeta os bytes da dependência resolvida diretamente no código em formato Little Endian
        code_buffer[patch->patch_offset + 0] = (relative_offset >> 0)  & 0xFF;
        code_buffer[patch->patch_offset + 1] = (relative_offset >> 8)  & 0xFF;
        code_buffer[patch->patch_offset + 2] = (relative_offset >> 16) & 0xFF;
        code_buffer[patch->patch_offset + 3] = (relative_offset >> 24) & 0xFF;

        printf("[TLD Linker]: Injeção de dependência estática bem-sucedida! '%s' amarrada no offset 0x%X\n", patch->target_name, patch->patch_offset);
    }

    return true;
}
