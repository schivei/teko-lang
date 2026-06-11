#include "unity.h"
#include "vm_core.h"
#include "codegen_li.h"
#include <stdlib.h>

// Testa a execução de uma sequência matemática linear simples de opcodes injetados no interpretador
void test_vm_core_mathematical_execution_loop(void) {
    // Montamos um vetor manual simplificado de bytecode da LI:
    // OP_ICONST (0x01) -> valor 42 -> OP_STORE (0x04) -> registrador 5 -> OP_HALT (0x00)
    unsigned char mock_bytecode[] = {
        0x01, 0x2A, 0x00, 0x00, 0x00, // OP_ICONST 42
        0x04, 0x05, 0x00, 0x00, 0x00, // OP_STORE r5
        0x03, 0x05, 0x00, 0x00, 0x00, // OP_LOAD r5 (move para r0)
        0x00                          // OP_HALT
    };

    // Inicializa o interpretador core com o fluxo injetado
    TekoVM* vm = teko_vm_create(mock_bytecode, sizeof(mock_bytecode), NULL, 0);
    TEST_ASSERT_NOT_NULL(vm);

    // Executa e valida o código de saída contido no registrador r0
    int32_t exit_code = teko_vm_execute(vm);
    TEST_ASSERT_EQUAL_INT32(42, exit_code);
    TEST_ASSERT_EQUAL_INT32(42, vm->registers[5]);

    teko_vm_destroy(vm);
}