#ifndef TEKO_THREAD_H
#define TEKO_THREAD_H

#include <stdint.h>
#include <stdbool.h>

#define TEKO_STACK_SIZE (64 * 1024) // 64KB de pilha isolada por Green Thread
#define MAX_THREADS     128

typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_ZOMBIE
} TekoThreadState;

// Estrutura física para preservação do contexto da CPU (Agnóstica por abstração)
typedef struct {
    void*    esp;      // Ponteiro de Pilha (Stack Pointer)
    void*    ebp;      // Frame Pointer
    void*    eip;      // Ponteiro de Instrução (Program Counter)
    uint64_t regs[8];  // Registradores gerais callee-saved (r12-r15, rbx, etc.)
} TekoCpuContext;

// Estrutura do Bloco de Controle de Thread (TCB)
typedef struct {
    uint32_t        id;
    TekoThreadState state;
    uint8_t*        stack_memory;  // Ponteiro da pilha alocada
    TekoCpuContext  context;       // Contexto salvo da CPU
} TekoGreenThread;

// O Escalonador Central M:N Cooperativo
typedef struct {
    TekoGreenThread threads[MAX_THREADS];
    uint32_t        thread_count;
    int32_t         current_running_id; // ID da thread ativa na CPU
} TekoScheduler;

// Assinaturas públicas do barramento concorrente nativo
void tld_scheduler_init(TekoScheduler* sched);
int32_t tld_thread_spawn(TekoScheduler* sched, void (*fn)(void));
void tld_thread_yield(TekoScheduler* sched);
void tld_scheduler_run(TekoScheduler* sched);

#endif // TEKO_THREAD_H
