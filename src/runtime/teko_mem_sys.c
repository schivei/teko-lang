#include "teko_mem_sys.h"

// ---------------------------------------------------------------------------
// RAMIFICAÇÃO 1: ECOSSISTEMA MICROSOFT WINDOWS (Win32 VirtualAlloc API)
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

void* teko_sys_allocate_pages(size_t size) {
    if (size == 0) return NULL;
    // MEM_COMMIT | MEM_RESERVE aloca fisicamente páginas limpas (zeradas) na RAM virtual
    // PAGE_READWRITE define a proteção básica exigida pelas nossas Arenas de dados
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

bool teko_sys_free_pages(void* address, size_t size) {
    if (!address) return false;
    // MEM_RELEASE devolve as páginas de volta ao gerenciador do Windows instantaneamente
    return VirtualFree(address, 0, MEM_RELEASE) != 0;
}

// ---------------------------------------------------------------------------
// RAMIFICAÇÃO 2: ECOSSISTEMA UNIX/POSIX (Linux, macOS Darwin, FreeBSD via mmap)
// ---------------------------------------------------------------------------
#else
#include <sys/mman.h>

// Fallback caso MAP_ANONYMOUS não esteja mapeado nominalmente em alguma variante BSD antiga
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

void* teko_sys_allocate_pages(size_t size) {
    if (size == 0) return NULL;

    // PROT_READ | PROT_WRITE concede acesso de leitura e escrita às páginas
    // MAP_PRIVATE | MAP_ANONYMOUS aloca memória virtual isolada e limpa fora do sistema de arquivos
    void* addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (addr == MAP_FAILED) {
        return NULL;
    }
    return addr;
}

bool teko_sys_free_pages(void* address, size_t size) {
    if (!address || size == 0) return false;
    // munmap descarta o bloco e recicla o espaço na tabela de páginas do Kernel em O(1)
    return munmap(address, size) == 0;
}
#endif
