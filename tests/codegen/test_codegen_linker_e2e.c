#include "unity.h"
#include "codegen/tld_elf.h"
#include "codegen/tld_elf_arch.h"
#include "codegen/tld_symbols.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_teko_linker_e2e_extern_service_injection_and_elf_generation(void) {
    const char* output_binary = "output_teko_e2e_exec";
    TekoSymbolTable symbol_table;
    tld_symbols_init(&symbol_table);

    // Buffer unificado para simular a fusão contígua das seções de código na memória do Linker
    uint8_t final_code_segment[512];
    memset(final_code_segment, 0x90, sizeof(final_code_segment)); // Inicia com NOPs nativos
    uint32_t current_offset = 0;

    // ====================================================================
    // PASSO 1: COMPILAÇÃO DO FRAGMENTO 1 (corpo do serviço Flows.notify)
    // ====================================================================
    uint32_t service_start_offset = current_offset;

    // Registra e define o símbolo da dependência na tabela global do Linker
    bool def_ok = tld_symbols_define(&symbol_table, "@flows.notify", SYM_SERVICE, service_start_offset);
    TEST_ASSERT_TRUE(def_ok);

    // Emite o corpo binário da sub-rotina para a CPU Intel x86_64: ICONST 42 -> RETURN (Mapeado via OP_STORE de 1 byte)
    current_offset += tld_arch_encode_instruction(ARCH_X86_64, OP_ICONST, 42, &final_code_segment[current_offset]);
    current_offset += tld_arch_encode_instruction(ARCH_X86_64, OP_STORE, 0, &final_code_segment[current_offset]);

    // ====================================================================
    // PASSO 2: COMPILAÇÃO DO FRAGMENTO 2 (main executável chamando o extern)
    // ====================================================================
    uint32_t main_start_offset = current_offset;
    bool main_def_ok = tld_symbols_define(&symbol_table, "main", SYM_FUNC, main_start_offset);
    TEST_ASSERT_TRUE(main_def_ok);

    // O main realiza uma chamada 'call' de hardware. Reservamos 1 byte para o opcode call (0xE8)
    final_code_segment[current_offset++] = 0xE8;

    // O ponto exato onde os 4 bytes do offset relativo da dependência devem ser injetados
    uint32_t patch_target_offset = current_offset;

    // Registra a referência pendente vinculando o 'call site' ao nome textual do extern
    tld_symbols_reference(&symbol_table, "@flows.notify", patch_target_offset);
    current_offset += 4; // Avança o espaço reservado para o operando do endereço de 32 bits

    // Termina o main emitindo o HALT nativo de encerramento da CPU
    current_offset += tld_arch_encode_instruction(ARCH_X86_64, OP_HALT, 0, &final_code_segment[current_offset]);

    // ====================================================================
    // PASSO 3: RESOLUÇÃO DE LINK-TIME E INJEÇÃO COORDENADA DE DEPENDÊNCIAS
    // ====================================================================
    uint64_t linux_base_vaddr = 0x400000; // VMA canônico do Kernel Linux de 64 bits

    // Invoca o resolvedor de símbolos para remendar os bytes nulos do 'call' na memória
    bool resolution_ok = tld_symbols_resolve_and_inject(&symbol_table, final_code_segment, linux_base_vaddr);
    TEST_ASSERT_TRUE(resolution_ok);

    // VALIDAÇÃO MATEMÁTICA ANTES DA ESCRITA EM DISCO:
    // Pelo algoritmo do Linker: Offset = Destino (0) - (Patch_Offset (6) + 4) = 0 - 10 = -10 bytes!
    // -10 em complemento de dois de 32 bits é expresso em hexadecimal como 0xFFFFFFF6
    TEST_ASSERT_EQUAL_HEX8(0xF4, final_code_segment[patch_target_offset + 0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, final_code_segment[patch_target_offset + 1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, final_code_segment[patch_target_offset + 2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, final_code_segment[patch_target_offset + 3]);

    // ====================================================================
    // PASSO 4: ESCRITA DO EXECUTÁVEL FINAL INDEPENDENTE (ELF64 EM DISCO)
    // ====================================================================
    bool write_ok = tld_elf64_write_executable(output_binary, final_code_segment, current_offset, ELF_ARCH_X86_64);
    TEST_ASSERT_TRUE(write_ok);

    // Auditoria final abrindo o executável real gerado para checar integridade de bytes
    FILE* executable = fopen(output_binary, "rb");
    TEST_ASSERT_NOT_NULL(executable);

    // Captura o tamanho exato em disco do executável binário gerado
    fseek(executable, 0, SEEK_END);
    long binary_size = ftell(executable);
    fseek(executable, 0, SEEK_SET);

    // Aloca um buffer de bytes puros (uint8_t) para impedir o truncamento por '\0'
    uint8_t* full_binary_buffer = (uint8_t*)malloc(binary_size);
    TEST_ASSERT_NOT_NULL(full_binary_buffer);
    size_t read_bytes = fread(full_binary_buffer, 1, binary_size, executable);
    TEST_ASSERT_EQUAL_size_t(binary_size, read_bytes);
    fclose(executable);

    // 1. Certifica a existência da assinatura molecular binária do formato ELF (\x7fELF)
    TEST_ASSERT_EQUAL_HEX8(0x7F, full_binary_buffer[0]);
    TEST_ASSERT_EQUAL_HEX8('E',  full_binary_buffer[1]);
    TEST_ASSERT_EQUAL_HEX8('L',  full_binary_buffer[2]);
    TEST_ASSERT_EQUAL_HEX8('F',  full_binary_buffer[3]);

    // 2. AUDITORIA DA SEÇÃO .RODATA: Varredura brute-force de bytes na memória para achar as strings
    bool found_hello = false;
    bool found_cqrs = false;

    // Varre os bytes buscando as sequências contíguas das assinaturas de texto em qualquer offset do binário
    for (long idx = 0; idx < binary_size - 10; idx++) {
        if (memcmp(&full_binary_buffer[idx], "Hello Teko", 10) == 0) {
            found_hello = true;
        }
        if (memcmp(&full_binary_buffer[idx], "CQRS_Service_Active", 19) == 0) {
            found_cqrs = true;
        }
    }
    TEST_ASSERT_EQUAL_HEX8(9, full_binary_buffer[7]);

    // Asserções do Unity chancelando que os dados estáticos foram embutidos com perfeição física
    TEST_ASSERT_TRUE(found_hello);
    TEST_ASSERT_TRUE(found_cqrs);

    // Liberação higiênica e fechamento do ciclo de fumaça
    free(full_binary_buffer);
    remove(output_binary);
}
