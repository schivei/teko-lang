#include "unity.h"
#include "codegen/codegen_opt.h"
#include <stdlib.h>

void test_teko_aot_escape_analysis_register_allocation(void) {
    // Cenário 1: Bytecode contíguo onde a constante NÃO escapa (Apenas uma soma isolada)
    unsigned char mock_local_code[] = {
        0x01, 0x05, 0x00, 0x00, 0x00,  // OP_ICONST 5
        0x05                           // OP_ADD
    };

    OptInstruction out_ins[16];
    uint32_t out_count = 0;

    teko_optimize_register_allocation(mock_local_code, sizeof(mock_local_code), out_ins, &out_count);

    TEST_ASSERT_EQUAL_UINT32(2, out_count);
    TEST_ASSERT_FALSE(out_ins[0].escapes);
    // Verificação de sucesso: Constante foi otimizada para reter no registrador temporário REG_TEMP0!
    TEST_ASSERT_EQUAL_INT(REG_TEMP0, out_ins[0].assigned_reg);

    // Cenário 2: Bytecode onde a constante ESCAPA (Existe um OP_RETURN subsequente)
    unsigned char mock_escape_code[] = {
        0x01, 0x0A, 0x00, 0x00, 0x00,  // OP_ICONST 10
        0x22                           // OP_RETURN
    };

    teko_optimize_register_allocation(mock_escape_code, sizeof(mock_escape_code), out_ins, &out_count);

    TEST_ASSERT_TRUE(out_ins[0].escapes);
    // Verificação de sucesso: Como o dado escapa, o otimizador reteve no REG_ACCUMULATOR obrigatório
    TEST_ASSERT_EQUAL_INT(REG_ACCUMULATOR, out_ins[0].assigned_reg);
}
