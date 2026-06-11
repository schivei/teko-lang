#include "unity.h"
#include "vm_scheduler.h"
#include "vm_core.h"
#include <stdlib.h>

void test_vm_scheduler_context_switching_and_spawn(void) {
    auto sched = vm_scheduler_create();
    TEST_ASSERT_NOT_NULL(sched);

    // Bytecode fictício para simular duas rotas de IP
    unsigned char mock_code[] = {0x00, 0x00, 0x00, 0x00};
    auto vm = teko_vm_create(mock_code, sizeof(mock_code), NULL, 0);
    TEST_ASSERT_NOT_NULL(vm);

    // Dispara duas Green Threads em IPs diferentes
    auto g1 = vm_scheduler_spawn(sched, 10);
    auto g2 = vm_scheduler_spawn(sched, 20);
    TEST_ASSERT_EQUAL_UINT32(1, g1->id);
    TEST_ASSERT_EQUAL_UINT32(2, g2->id);

    // Roda a primeira thread
    bool run1 = vm_scheduler_run_next(sched, vm);
    TEST_ASSERT_TRUE(run1);
    TEST_ASSERT_EQUAL_UINT32(10, vm->ip);

    // Altera um registrador na VM para testar a persistência
    vm->registers[3] = 999;

    // Executa o yield (Chaveamento voluntário)
    vm_scheduler_yield(sched, vm);
    TEST_ASSERT_EQUAL_INT32(999, g1->registers[3]); // O estado deve ter sido salvo!

    // Destruições
    teko_vm_destroy(vm);
    vm_scheduler_destroy(sched);
}