#ifndef TLD_ELF_RELOC_H
#define TLD_ELF_RELOC_H

#include <stdint.h>
#include <stdbool.h>

// Estrutura para registrar onde as labels físicas se posicionam no bytecode
typedef struct {
    uint32_t label_id;
    uint32_t bytecode_offset; // Posição exata do byte da label no array
} TekoElfLabelMap;

// Estrutura para registrar instruções que precisam de remendo de endereço
typedef struct {
    uint32_t patch_offset;     // Onde na memória precisamos injetar o endereço final
    uint32_t target_label_id;  // ID da label que esta instrução está buscando
    bool is_relative;          // True para JMP (relativo), False para String (absoluto)
} TekoElfRelocEntry;

// Motor de Relocação do Linker ELF64
void tld_elf64_perform_relocations(uint8_t* code_buffer, uint32_t code_size,
                                   const TekoElfLabelMap* labels, uint32_t label_count,
                                   const TekoElfRelocEntry* relocs, uint32_t reloc_count,
                                   uint64_t base_entry_vaddr);

#endif // TLD_ELF_RELOC_H
