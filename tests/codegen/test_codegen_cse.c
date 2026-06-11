#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_teko_aot_common_subexpression_elimination_filter(void) {
    const char* asm_path = "output_cse_smoke_test.s";

    TekoTarget target;
    target.arch = ARCH_APPLE_SILICON;
    target.os = OS_MACOS_DARWIN;

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // Injeta a sequência com redundâncias:
    // OP_ICONST 42 (0x01) -> OP_ICONST 42 (0x01 - REDUNDANTE EXCLUÍDA) -> OP_ADD (0x05) -> OP_ADD (0x05 - REDUNDANTE EXCLUÍDA)
    unsigned char mock_cse_bytes[] = {
        0x01, 0x2A, 0x00, 0x00, 0x00, // OP_ICONST 42
        0x01, 0x2A, 0x00, 0x00, 0x00, // OP_ICONST 42 (DEVE SER PURGADA PELO CSE)
        0x05,                         // OP_ADD
        0x05                          // OP_ADD (DEVE SER PURGADA PELO CSE)
    };

    teko_metal_emit_program(ctx, mock_cse_bytes, sizeof(mock_cse_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);

    char* buffer = (char*)malloc(4096);
    memset(buffer, 0, 4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    // ANALISA O RESULTADO FÍSICO DO ARQUIVO ASSEMBLY EM DISCO:
    // O mnemônico "mov w0, #42" deve ocorrer EXATAMENTE uma única vez!
    char* first_mov = strstr(buffer, "mov w0, #42");
    TEST_ASSERT_NOT_NULL(first_mov);
    char* second_mov = strstr(first_mov + 11, "mov w0, #42");
    TEST_ASSERT_NULL(second_mov); // Passou! O segundo mov foi removido com sucesso

    // O mnemônico "add w0, w0, w1" deve ocorrer EXATAMENTE uma única vez!
    char* first_add = strstr(buffer, "add w0, w0, w1");
    TEST_ASSERT_NOT_NULL(first_add);
    char* second_add = strstr(first_add + 14, "add w0, w0, w1");
    TEST_ASSERT_NULL(second_add); // Passou! O segundo add foi removido pelo CSE

    free(buffer);
    remove(asm_path);
}
