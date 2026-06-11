#ifndef VM_ARENA_H
#define VM_ARENA_H

#include <stdint.h>
#include <stddef.h>

#define TEKO_ARENA_PAGE_SIZE 4096 // Tamanho base de 4KB por página da Arena

// Estrutura de uma página/bloco contíguo da Arena
typedef struct TekoArenaPage {
    unsigned char* memory;
    size_t capacity;
    size_t offset;
    struct TekoArenaPage* next; // Encadeamento para o caso de estouro de capacidade
} TekoArenaPage;

// Estrutura do gerenciador da Arena (Atende ao ctx: arena da linguagem)
typedef struct {
    TekoArenaPage* head;
    TekoArenaPage* current;
} TekoArena;

// Assinaturas públicas do Alocador por Região
TekoArena* teko_arena_create(void);
void* teko_arena_alloc(TekoArena* arena, size_t size);
void teko_arena_reset(TekoArena* arena);
void teko_arena_destroy(TekoArena* arena);

#endif // VM_ARENA_H