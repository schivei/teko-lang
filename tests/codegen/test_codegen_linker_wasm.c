#include "unity.h"
#include "codegen/tld_wasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_teko_linker_wasm_leb128_compression_logic(void) {
    uint8_t buffer[5];

    // Cenário 1: Inteiro pequeno que cabe em 1 byte (menor que 128)
    uint32_t bytes1 = tld_wasm_encode_leb128(42, buffer);
    TEST_ASSERT_EQUAL_UINT32(1, bytes1);
    TEST_ASSERT_EQUAL_HEX8(42, buffer[0]);

    // Cenário 2: Inteiro que exige expansão de tamanho (ex: 300)
    // 300 = 0x012C -> Em LEB128 fica: [0xAC, 0x02]
    uint32_t bytes2 = tld_wasm_encode_leb128(300, buffer);
    TEST_ASSERT_EQUAL_UINT32(2, bytes2);
    TEST_ASSERT_EQUAL_HEX8(0xAC, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buffer[1]);
}

void test_teko_linker_wasm_module_generation_and_magic_check(void) {
    const char* filename = "output_wasm_linker_test.wasm";

    // Injeta um payload de teste simulando a instrução i32.const 100 (0x41 0x64) em opcodes puros WASM
    uint8_t mock_wasm_opcodes[] = { 0x41, 0x64 };

    bool success = tld_wasm_write_module(filename, mock_wasm_opcodes, sizeof(mock_wasm_opcodes));
    TEST_ASSERT_TRUE(success);

    FILE* f = fopen(filename, "rb");
    TEST_ASSERT_NOT_NULL(f);

    uint8_t magic[4];
    size_t read_bytes = fread(magic, 1, 4, f);
    TEST_ASSERT_EQUAL_size_t(4, read_bytes);
    fclose(f);

    // Valida a assinatura mágica do padrão binário global WebAssembly (\x00asm)
    TEST_ASSERT_EQUAL_HEX8(0x00, magic[0]);
    TEST_ASSERT_EQUAL_HEX8(0x61, magic[1]);
    TEST_ASSERT_EQUAL_HEX8(0x73, magic[2]);
    TEST_ASSERT_EQUAL_HEX8(0x6D, magic[3]);

    remove(filename);
}
