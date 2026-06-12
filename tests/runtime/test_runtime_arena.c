#include "unity.h"
#include "../../src/runtime/teko_arena.h"

void test_teko_runtime_arena_thread_isolation_and_alignment(void) {
    TekoRegionManager region_manager;
    tld_region_init(&region_manager);

    // 1. Registra e aloca Sub-Arenas independentes para duas Threads simuladas (ID 1 e ID 2)
    bool reg1 = tld_region_register_thread(&region_manager, 1);
    bool reg2 = tld_region_register_thread(&region_manager, 2);

    TEST_ASSERT_TRUE(reg1);
    TEST_ASSERT_TRUE(reg2);

    // 2. Executa alocações concorrentes locais e checa o isolamento físico de ponteiros
    uint32_t* ptr_t1 = (uint32_t*)tld_region_alloc(&region_manager, 1, sizeof(uint32_t));
    uint64_t* ptr_t2 = (uint64_t*)tld_region_alloc(&region_manager, 2, sizeof(uint64_t));

    TEST_ASSERT_NOT_NULL(ptr_t1);
    TEST_ASSERT_NOT_NULL(ptr_t2);

    // Os ponteiros devem residir em páginas de memórias virtuais totalmente distintas requisitadas ao kernel
    TEST_ASSERT_NOT_EQUAL((void*)ptr_t1, (void*)ptr_t2);

    // Escreve nos blocos para garantir que os endereços são válidos
    *ptr_t1 = 1024;
    *ptr_t2 = 88888888;
    TEST_ASSERT_EQUAL_UINT32(1024, *ptr_t1);
    TEST_ASSERT_EQUAL_UINT64(88888888, *ptr_t2);

    // 3. Testa o alinhamento de 8 bytes nativo do fatiador O(1)
    uint8_t* byte_ptr = (uint8_t*)tld_region_alloc(&region_manager, 1, 1); // Pede apenas 1 byte
    uint32_t* next_ptr = (uint32_t*)tld_region_alloc(&region_manager, 1, sizeof(uint32_t));

    // A distância entre os dois deve ser exatamente 8 bytes devido à máscara de alinhamento da CPU
    ptrdiff_t distance = (uint8_t*)next_ptr - byte_ptr;
    TEST_ASSERT_EQUAL_INT(8, (int)distance);

    // 4. Executa a reciclagem instantânea local e limpa as páginas
    tld_region_reset_thread(&region_manager, 1);
    tld_region_reset_thread(&region_manager, 2);
    tld_region_destroy(&region_manager, 3);
}
