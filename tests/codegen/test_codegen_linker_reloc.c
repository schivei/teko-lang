#include "unity.h"
#include "codegen/tld_elf_reloc.h"
#include <stdlib.h>
#include <string.h>

void test_teko_linker_elf64_relative_jmp_patch_calculation(void) {
    // Simula um buffer de código de máquina fictício de 20 bytes
    // Onde a posição 5 guarda o operando vazio do JMP que queremos remendar [0x00, 0x00, 0x00, 0x00]
    uint8_t code_buffer[20];
    memset(code_buffer, 0x90, sizeof(code_buffer)); // Preenche com NOPs

    // Configura o mapa de símbolos: Label 42 se posiciona no byte 14
    TekoElfLabelMap labels[1];
    labels[0].label_id = 42;
    labels[0].bytecode_offset = 14;

    // Configura a entrada de relocação: Instrução no byte 5 busca a Label 42 de forma relativa
    TekoElfRelocEntry relocs[1];
    relocs[0].patch_offset = 5;
    relocs[0].target_label_id = 42;
    relocs[0].is_relative = true;

    // Executa o motor de remendos do Linker
    tld_elf64_perform_relocations(code_buffer, sizeof(code_buffer), labels, 1, relocs, 1, 0x400000);

    // DISTANCIA ESPERADA: Destino (14) - (Patch_offset (5) + 4) = 14 - 9 = 5 bytes!
    TEST_ASSERT_EQUAL_HEX8(0x05, code_buffer[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, code_buffer[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00, code_buffer[7]);
    TEST_ASSERT_EQUAL_HEX8(0x00, code_buffer[8]);
}
