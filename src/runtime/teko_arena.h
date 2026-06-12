#ifndef TEKO_ARENA_H
#define TEKO_ARENA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TEKO_ARENA_PAGE_SIZE (128 * 1024) // 128KB por página de Sub-Arena local

// Estrutura de controle de uma Sub-Arena regional isolada
typedef struct {
    uint8_t* memory_raw; // Ponteiro bruto retornado pelas syscalls (mmap/VirtualAlloc)
    size_t   capacity;   // Tamanho total da região (128KB)
    size_t   offset;     // Cursor linear de alocação ativa O(1)
} TekoSubArena;

// Gerenciador de Regiões acoplado ao ciclo de vida das Green Threads
typedef struct {
    TekoSubArena thread_arenas[128]; // Mapeamento indexado por ID de Thread
} TekoRegionManager;

// Assinaturas públicas do alocador de regiões nativo
void tld_region_init(TekoRegionManager* manager);
bool tld_region_register_thread(TekoRegionManager* manager, uint32_t thread_id);
void* tld_region_alloc(TekoRegionManager* manager, uint32_t thread_id, size_t size);
void tld_region_reset_thread(TekoRegionManager* manager, uint32_t thread_id);
void tld_region_destroy(TekoRegionManager* manager, uint32_t thread_count);

#endif // TEKO_ARENA_H
