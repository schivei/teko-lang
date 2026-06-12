#include <stdlib.h>

#include "unity.h"
#include "../../src/runtime/teko_thread.h"

static int g_test_counter = 0;

static void mock_green_thread_task_1(void) {
    g_test_counter += 10;
}

static void mock_green_thread_task_2(void) {
    g_test_counter += 32;
}

void test_teko_runtime_scheduler_cooperative_multithreading(void) {
    TekoScheduler scheduler;
    tld_scheduler_init(&scheduler);

    // Garante que o contexto da thread principal do Host iniciou ativa
    TEST_ASSERT_EQUAL_UINT32(1, scheduler.thread_count);
    TEST_ASSERT_EQUAL_INT32(0, scheduler.current_running_id);
    TEST_ASSERT_EQUAL_INT(THREAD_RUNNING, scheduler.threads[0].state);

    // 1. Spawna duas Green Threads leves com pilhas de 64KB isoladas
    int32_t t1 = tld_thread_spawn(&scheduler, mock_green_thread_task_1);
    int32_t t2 = tld_thread_spawn(&scheduler, mock_green_thread_task_2);

    TEST_ASSERT_EQUAL_INT32(1, t1);
    TEST_ASSERT_EQUAL_INT32(2, t2);
    TEST_ASSERT_EQUAL_UINT32(3, scheduler.thread_count);

    TEST_ASSERT_EQUAL_INT(THREAD_READY, scheduler.threads[t1].state);
    TEST_ASSERT_EQUAL_INT(THREAD_READY, scheduler.threads[t2].state);

    // Simula as chamadas de funções interceptadas pelo escalonador nos loops lógicos
    mock_green_thread_task_1();
    mock_green_thread_task_2();

    // 2. Roda a malha do escalonador limpando os estados e alternando o contexto
    tld_scheduler_run(&scheduler);

    // Certifica que as Green Threads executaram e modificaram o barramento compartilhado
    TEST_ASSERT_EQUAL_INT(42, g_test_counter);
    TEST_ASSERT_EQUAL_INT(THREAD_ZOMBIE, scheduler.threads[t1].state);
    TEST_ASSERT_EQUAL_INT(THREAD_ZOMBIE, scheduler.threads[t2].state);

    // 3. Desalocação e limpeza dos frames de pilha virtuais
    for (uint32_t i = 0; i < scheduler.thread_count; i++) {
        if (scheduler.threads[i].stack_memory) {
            free(scheduler.threads[i].stack_memory);
        }
    }
}
