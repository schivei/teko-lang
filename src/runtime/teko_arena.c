#include "teko_arena.h"
#include "teko_mem_sys.h"
#include <stdio.h>
#include <string.h>

void tld_region_init(TekoRegionManager* manager) {
    if (!manager) return;
    memset(manager, 0, sizeof(TekoRegionManager));
}

bool tld_region_register_thread(TekoRegionManager* manager, uint32_t thread_id) {
    if (!manager || thread_id >= 128) return false;

    TekoSubArena* arena = &manager->thread_arenas[thread_id];

    // Invoca a chamada de sistema pura (mmap / VirtualAlloc) para requisitar 128KB limpos
    arena->memory_raw = (uint8_t*)teko_sys_allocate_pages(TEKO_ARENA_PAGE_SIZE);
    if (!arena->memory_raw) {
        return false;
    }

    arena->capacity = TEKO_ARENA_PAGE_SIZE;
    arena->offset = 0;

    printf("[Teko Arenas]: Sub-Arena O(1) registrada e isolada para a Green Thread %d (128KB RAM Virtual).\n", thread_id);
    return true;
}

void* tld_region_alloc(TekoRegionManager* manager, uint32_t thread_id, size_t size) {
    if (!manager || thread_id >= 128 || size == 0) return NULL;

    TekoSubArena* arena = &manager->thread_arenas[thread_id];
    if (!arena->memory_raw) return NULL;

    // Alinhamento de memória nativo em 8 bytes para maximizar o barramento de leitura de dados da CPU
    size_t aligned_size = (size + 7) & ~7;

    // Proteção de transbordo: Se a Sub-Arena local estourar, o runtime nega a alocação
    if (arena->offset + aligned_size > arena->capacity) {
        fprintf(stderr, "[Teko Runtime Panic]: Out of Memory na Sub-Arena da Thread %d!\n", thread_id);
        return NULL;
    }

    // Alocação atômica em O(1): apenas desloca o cursor e retorna o endereço inicial
    void* alloc_ptr = &arena->memory_raw[arena->offset];
    arena->offset += aligned_size;

    return alloc_ptr;
}

void tld_region_reset_thread(TekoRegionManager* manager, uint32_t thread_id) {
    if (!manager || thread_id >= 128) return;

    TekoSubArena* arena = &manager->thread_arenas[thread_id];

    // LIMPEZA EM TEMPO CONSTANTE O(1): Zera o cursor linear instantaneamente.
    // Toda a memória é marcada para reutilização sem loops de free individuais!
    arena->offset = 0;
    printf("[Teko Arenas]: Ciclo de vida encerrado. Sub-Arena da Thread %d reciclada em 1 ciclo de clock.\n", thread_id);
}

void tld_region_destroy(TekoRegionManager* manager, uint32_t thread_count) {
    if (!manager) return;

    for (uint32_t i = 0; i < thread_count; i++) {
        TekoSubArena* arena = &manager->thread_arenas[i];
        if (arena->memory_raw) {
            // Devolve as páginas brutas de volta para o Kernel de forma higiênica
            teko_sys_free_pages(arena->memory_raw, arena->capacity);
            arena->memory_raw = NULL;
        }
    }
}
