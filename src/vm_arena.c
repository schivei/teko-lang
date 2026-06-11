#include "vm_arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Auxiliar para instanciar uma nova página contígua de memória
static TekoArenaPage* create_arena_page(size_t capacity) {
    auto page = (TekoArenaPage*)malloc(sizeof(TekoArenaPage));
    if (!page) return NULL;

    page->capacity = capacity < TEKO_ARENA_PAGE_SIZE ? TEKO_ARENA_PAGE_SIZE : capacity;
    page->offset = 0;
    page->next = NULL;
    page->memory = (unsigned char*)malloc(page->capacity);

    if (!page->memory) {
        free(page);
        return NULL;
    }
    return page;
}

// Inicializa a Arena de contexto
TekoArena* teko_arena_create(void) {
    auto arena = (TekoArena*)malloc(sizeof(TekoArena));
    if (!arena) return NULL;

    arena->head = create_arena_page(TEKO_ARENA_PAGE_SIZE);
    if (!arena->head) {
        free(arena);
        return NULL;
    }
    arena->current = arena->head;
    return arena;
}

// Alocação linear contígua em tempo constante O(1)
void* teko_arena_alloc(TekoArena* arena, size_t size) {
    if (!arena || size == 0) return NULL;

    // Alinhamento de memória para ARM64 (Alinha ponteiros em múltiplos de 8 bytes)
    size_t aligned_size = (size + 7) & ~7;

    // Se o bloco atual não tiver espaço suficiente, cria uma nova página em cascata
    if (arena->current->offset + aligned_size > arena->current->capacity) {
        size_t next_cap = aligned_size > TEKO_ARENA_PAGE_SIZE ? aligned_size : TEKO_ARENA_PAGE_SIZE;
        auto next_page = create_arena_page(next_cap);
        if (!next_page) return NULL;

        arena->current->next = next_page;
        arena->current = next_page;
    }

    // Avança o offset de forma linear e retorna o endereço físico da fatia
    void* alloc_ptr = &arena->current->memory[arena->current->offset];
    arena->current->offset += aligned_size;

    return alloc_ptr;
}

// Reseta a Arena em O(1), tornando toda a memória reutilizável sem desalocar do OS
void teko_arena_reset(TekoArena* arena) {
    if (!arena) return;

    auto page = arena->head;
    while (page != NULL) {
        page->offset = 0; // Apenas zera o offset. Sem loops de free.
        page = page->next;
    }
    arena->current = arena->head; // Aponta de volta para o início
}

// Destruição física completa da Arena ao fim do ciclo de vida da Thread/VM
void teko_arena_destroy(TekoArena* arena) {
    if (!arena) return;

    auto page = arena->head;
    while (page != NULL) {
        auto temp = page;
        page = page->next;
        free(temp->memory);
        free(temp);
    }
    free(arena);
}