#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_teko_aot_dead_code_elimination_purgue(void) {
    const char* asm_path = "output_dce_smoke_test.s";

    TekoTarget target;
    target.arch = ARCH_X86_64;
    target.os = OS_LINUX;

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // Injeta a sequência:
    // OP_ICONST 500 (0x01) -> OP_RETURN (0x22) -> OP_ADD (0x05 - INALCANÇÁVEL MORTA) -> Label 100 (0x64 - RESSUSCITA) -> OP_SUB (0x06)
    unsigned char mock_dead_bytes[] = {
        0x01, 0xF4, 0x01, 0x00, 0x00, // OP_ICONST 500
        0x22,                         // OP_RETURN (Ativa a barreira de código morto)
        0x05,                         // OP_ADD (DEVE SER ELIMINADA TOTALMENTE)
        0x64,                         // Label 100 (Desativa a barreira de código morto)
        0x06                          // OP_SUB (DEVE VOLTAR A SER ALCANÇÁVEL E EMITIDA)
    };

    teko_metal_emit_program(ctx, mock_dead_bytes, sizeof(mock_dead_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);

    char* buffer = (char*)malloc(4096);
    memset(buffer, 0, 4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    // Asserções comprovando o sucesso mecânico da nossa otimização DCE
    TEST_ASSERT_NOT_NULL(strstr(buffer, "movl $500, %eax"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "ret"));

    // O Mnemônico "addl" da instrução morta NÃO pode existir no arquivo final!
    TEST_ASSERT_NULL(strstr(buffer, "addl"));

    // O fluxo pós-label reativou a compilação, o mnemônico "subl" e a label DEVEM existir!
    TEST_ASSERT_NOT_NULL(strstr(buffer, ".L_linux_label_100:"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "subl"));

    free(buffer);
    remove(asm_path);
}
