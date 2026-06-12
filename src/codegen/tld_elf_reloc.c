#include "tld_elf_reloc.h"
#include <stdio.h>

void tld_elf64_perform_relocations(uint8_t* code_buffer, uint32_t code_size,
                                   const TekoElfLabelMap* labels, uint32_t label_count,
                                   const TekoElfRelocEntry* relocs, uint32_t reloc_count,
                                   uint64_t base_entry_vaddr) {
    if (!code_buffer || code_size == 0) return;

    // Varre todas as entradas que o compilador marcou como necessitadas de remendo
    for (uint32_t i = 0; i < reloc_count; i++) {
        TekoElfRelocEntry reloc = relocs[i];
        uint32_t target_offset = 0;
        bool label_found = false;

        // 1. Localiza o endereço físico da label alvo no mapa de símbolos
        for (uint32_t j = 0; j < label_count; j++) {
            if (labels[j].label_id == reloc.target_label_id) {
                target_offset = labels[j].bytecode_offset;
                label_found = true;
                break;
            }
        }

        if (!label_found) {
            fprintf(stderr, "[TLD Linker Error]: Simbolo indefinido. Label_%d nao encontrada.\n", reloc.target_label_id);
            continue;
        }

        // 2. Aplica as regras matemáticas de remendo binário
        if (reloc.is_relative) {
            // Regra do JMP Relativo (Padrão x86_64 / ARM64):
            // Distancia = Destino - (Posicao_Atual + 4 bytes do tamanho do operando)
            int32_t relative_offset = (int32_t)target_offset - ((int32_t)reloc.patch_offset + 4);

            // Grava os 4 bytes do remendo em formato Little Endian diretamente no buffer
            code_buffer[reloc.patch_offset + 0] = (relative_offset >> 0)  & 0xFF;
            code_buffer[reloc.patch_offset + 1] = (relative_offset >> 8)  & 0xFF;
            code_buffer[reloc.patch_offset + 2] = (relative_offset >> 16) & 0xFF;
            code_buffer[reloc.patch_offset + 3] = (relative_offset >> 24) & 0xFF;
        }
        else {
            // Regra de Carga de Memória Absoluta (Padrão para Strings/.rodata):
            // Endereço Virtual Final = Base Virtual da RAM + Posição no Bytecode
            uint64_t absolute_vaddr = base_entry_vaddr + target_offset;

            // Remenda a instrução injetando o ponteiro virtual de 64 bits de forma atômica
            code_buffer[reloc.patch_offset + 0] = (absolute_vaddr >> 0)  & 0xFF;
            code_buffer[reloc.patch_offset + 1] = (absolute_vaddr >> 8)  & 0xFF;
            code_buffer[reloc.patch_offset + 2] = (absolute_vaddr >> 16) & 0xFF;
            code_buffer[reloc.patch_offset + 3] = (absolute_vaddr >> 24) & 0xFF;
            // Caso a instrução gaste 8 bytes, preenche o topo limpo
            if (reloc.patch_offset + 7 < code_size) {
                code_buffer[reloc.patch_offset + 4] = (absolute_vaddr >> 32) & 0xFF;
                code_buffer[reloc.patch_offset + 5] = (absolute_vaddr >> 40) & 0xFF;
                code_buffer[reloc.patch_offset + 6] = (absolute_vaddr >> 48) & 0xFF;
                code_buffer[reloc.patch_offset + 7] = (absolute_vaddr >> 56) & 0xFF;
            }
        }
    }
}
