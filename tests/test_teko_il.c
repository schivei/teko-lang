#include "unity.h"
#include "teko_il.h"
#include "codegen_li.h"
#include <stdio.h>
#include <stdlib.h>

// Valida a integridade física do cabeçalho do arquivo .tkb gerado pelo compilador
void test_teko_il_binary_header_integrity(void) {
    BytecodeBuffer* buffer = codegen_li_create_context();
    TEST_ASSERT_NOT_NULL(buffer);

    // Alimenta o contexto com dados fictícios para teste
    codegen_li_add_string_constant(buffer, "TekoMain");

    // Simula a gravação do binário
    const char* test_file = "output_spec_test.tkb";
    teko_il_serialize_binary(buffer, test_file);

    // Abre o arquivo gerado para inspecionar os bytes físicos do cabeçalho
    FILE* file = fopen(test_file, "rb");
    TEST_ASSERT_NOT_NULL(file);

    TekoFileHeader read_header;
    size_t read_bytes = fread(&read_header, sizeof(TekoFileHeader), 1, file);
    fclose(file);

    // Asserções do Unity validando o leiaute de memória do arquivo
    TEST_ASSERT_EQUAL_INT(1, read_bytes);
    TEST_ASSERT_EQUAL_UINT32(TEKO_MAGIC, read_header.magic);
    TEST_ASSERT_EQUAL_UINT16(TEKO_VERSION_MAJOR, read_header.version_major);
    TEST_ASSERT_EQUAL_UINT32(1, read_header.constant_pool_count);

    // Limpeza do ambiente
    remove(test_file);
    codegen_li_free_context(buffer);
}