#ifndef VM_SCHEDULER_H
#define VM_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "vm_core.h"

// Estados possíveis de uma Green Thread no runtime da VM
typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_DEAD
} ThreadState;

// Estrutura que representa uma Green Thread isolada (M)
typedef struct GreenThread {
    uint32_t id;
    ThreadState state;
    uint32_t ip;                            // Instruction Pointer próprio
    int32_t registers[VM_REGISTERS_COUNT];  // Registradores virtuais salvos
    TekoArena* thread_arena;                // Frame Arena local da corrotina
    struct GreenThread* next;               // Encadeamento para fila
} GreenThread;

// Estrutura do Agendador M:N (Scheduler)
typedef struct {
    GreenThread* queue_head;
    GreenThread* queue_tail;
    GreenThread* current_thread;
    uint32_t next_thread_id;
} VMScheduler;

// Assinaturas públicas do Scheduler
VMScheduler* vm_scheduler_create(void);
GreenThread* vm_scheduler_spawn(VMScheduler* sched, uint32_t entry_ip);
void vm_scheduler_yield(VMScheduler* sched, TekoVM* main_vm);
bool vm_scheduler_run_next(VMScheduler* sched, TekoVM* main_vm);
void vm_scheduler_destroy(VMScheduler* sched);

#endif // VM_SCHEDULER_H