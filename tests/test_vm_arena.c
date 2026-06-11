#include "unity.h"
#include "vm_arena.h"
#include <stdint.h>

// Valida a integridade do alocador linear e o reaproveitamento de memória em tempo constante
void test_vm_arena_linear_allocation_and_reset(void) {
    TekoArena* arena = teko_arena_create();
    TEST_ASSERT_NOT_NULL(arena);

    // 1. Executa múltiplas alocações pequenas consecutivas
    int* ptr1 = (int*)teko_arena_alloc(arena, sizeof(int));
    int* ptr2 = (int*)teko_arena_alloc(arena, sizeof(int));

    TEST_ASSERT_NOT_NULL(ptr1);
    TEST_ASSERT_NOT_NULL(ptr2);

    // 2. Valida o alinhamento de ponteiros em 8 bytes (obrigatório para ARM64)
    uintptr_t addr1 = (uintptr_t)ptr1;
    uintptr_t addr2 = (uintptr_t)ptr2;
    TEST_ASSERT_EQUAL_INT(0, addr1 % 8);
    TEST_ASSERT_EQUAL_INT(0, addr2 % 8);
    TEST_ASSERT_TRUE(addr2 > addr1); // Deve estar sequencial na memória física

    // 3. Testa o estouro controlado de página forçando a alocação de um bloco gigante
    void* big_ptr = teko_arena_alloc(arena, 5000);
    TEST_ASSERT_NOT_NULL(big_ptr);
    TEST_ASSERT_NOT_NULL(arena->head->next); // Garante que criou a segunda página em cascata

    // 4. Executa o reset em O(1) e valida que a memória foi disponibilizada novamente
    teko_arena_reset(arena);
    TEST_ASSERT_EQUAL_INT(0, arena->head->offset);

    teko_arena_destroy(arena);
}