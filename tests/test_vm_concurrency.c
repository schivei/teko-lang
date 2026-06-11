#include "unity.h"
#include "vm_concurrency.h"
#include "vm_scheduler.h"
#include "vm_core.h"
#include <stdlib.h>

void test_vm_channel_blocking_and_unblocking(void) {
    auto sched = vm_scheduler_create();
    auto chan = teko_channel_create(1); // Canal de capacidade 1

    unsigned char mock_code[] = {0x00};
    auto vm = teko_vm_create(mock_code, sizeof(mock_code), NULL, 0);

    // Cria as Green Threads produtoras e consumidoras
    auto writer = vm_scheduler_spawn(sched, 0);
    auto reader = vm_scheduler_spawn(sched, 0);

    // Ativa o escritor na VM
    vm_scheduler_run_next(sched, vm);

    // 1. Escrita com sucesso!
    bool p1 = teko_channel_put(chan, 42, sched, vm);
    TEST_ASSERT_TRUE(p1);
    TEST_ASSERT_EQUAL_UINT32(1, chan->count);

    // 2. Segunda escrita consecutiva deve falhar e BLOQUEAR a thread escritora (canal cheio)
    bool p2 = teko_channel_put(chan, 99, sched, vm);
    TEST_ASSERT_FALSE(p2);
    TEST_ASSERT_EQUAL_INT(THREAD_BLOCKED, writer->state);

    // Ativa o leitor na VM para desaspar o barramento
    sched->current_thread = reader;
    reader->state = THREAD_RUNNING;

    int32_t val = 0;
    bool g1 = teko_channel_get(chan, &val, sched, vm);
    TEST_ASSERT_TRUE(g1);
    TEST_ASSERT_EQUAL_INT32(42, val);

    // A leitura deve ter acordado a Green Thread escritora automaticamente!
    TEST_ASSERT_EQUAL_INT(THREAD_READY, writer->state);

    // Desalocações limpas de memória heap
    free(chan->buffer);
    free(chan);
    teko_vm_destroy(vm);
    vm_scheduler_destroy(sched);
}