#include "unity.h"
#include "../../src/runtime/teko_mem_sys.h"
#include <string.h>

void test_teko_runtime_sys_allocation_and_page_recycling(void) {
    size_t page_size = 4096; // Aloca exatamente uma página padrão de 4KB

    // 1. Executa a requisição de baixo nível ao Kernel
    uint8_t* raw_memory = (uint8_t*)teko_sys_allocate_pages(page_size);
    TEST_ASSERT_NOT_NULL(raw_memory);

    // 2. Teste de estresse físico: Escreve no início e no final do bloco alocado
    // Se o Kernel não tiver mapeado a página corretamente, isso causará um Segmentation Fault imediato
    memset(raw_memory, 0xA5, page_size);
    TEST_ASSERT_EQUAL_HEX8(0xA5, raw_memory[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, raw_memory[page_size - 1]);

    // 3. Executa a devolução atômica da página para o Sistema Operacional
    bool free_ok = teko_sys_free_pages(raw_memory, page_size);
    TEST_ASSERT_TRUE(free_ok);
}
