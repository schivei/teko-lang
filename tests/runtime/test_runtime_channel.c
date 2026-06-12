#include "unity.h"
#include "../../src/runtime/teko_channel.h"

void test_teko_runtime_channels_blocking_and_signaling(void) {
    TekoScheduler scheduler;
    tld_scheduler_init(&scheduler);

    TekoChannel chan;
    tld_channel_init(&chan, 2); // Inicializa canal com capacidade para 2 elementos

    TEST_ASSERT_EQUAL_UINT32(2, chan.capacity);
    TEST_ASSERT_EQUAL_UINT32(0, chan.size);

    // 1. Escrita normal dentro da capacidade
    bool send1 = tld_channel_send(&chan, &scheduler, 100);
    bool send2 = tld_channel_send(&chan, &scheduler, 200);
    TEST_ASSERT_TRUE(send1);
    TEST_ASSERT_TRUE(send2);
    TEST_ASSERT_EQUAL_UINT32(2, chan.size);

    // 2. Teste de barreira: Tenta escrever no canal cheio (deve falhar e simular bloqueio lógicos)
    bool send3 = tld_channel_send(&chan, &scheduler, 300);
    TEST_ASSERT_FALSE(send3); // Não adiciona, aciona proteção

    // 3. Leitura contígua dos dados limpando o canal
    int32_t val1 = 0;
    int32_t val2 = 0;
    bool recv1 = tld_channel_receive(&chan, &scheduler, &val1);
    bool recv2 = tld_channel_receive(&chan, &scheduler, &val2);

    TEST_ASSERT_TRUE(recv1);
    TEST_ASSERT_TRUE(recv2);
    TEST_ASSERT_EQUAL_INT32(100, val1);
    TEST_ASSERT_EQUAL_INT32(200, val2);
    TEST_ASSERT_EQUAL_UINT32(0, chan.size);

    // 4. Teste de barreira: Tenta ler de um canal vazio (deve falhar e disparar bloqueio)
    int32_t val3 = 0;
    bool recv3 = tld_channel_receive(&chan, &scheduler, &val3);
    TEST_ASSERT_FALSE(recv3);
}
