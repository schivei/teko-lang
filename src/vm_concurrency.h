#ifndef VM_CONCURRENCY_H
#define VM_CONCURRENCY_H

#include <stdint.h>
#include <stdbool.h>
#include "vm_scheduler.h"

// Estrutura física de um Canal (suporta Bounded e Unbounded circular)
typedef struct {
    int32_t* buffer;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    GreenThread* blocked_readers; // Fila de Green Threads esperando dados
    GreenThread* blocked_writers; // Fila de Green Threads esperando espaço
} TekoVMChannel;

// Estrutura física do Mutex (Exclusão Mútua)
typedef struct {
    bool is_locked;
    GreenThread* owner;
    GreenThread* blocked_threads; // Fila de espera pelo Lock
} TekoVMMutex;

// Estrutura física do Waiter (Grupo de Espera)
typedef struct {
    int32_t counter;
    GreenThread* blocked_waiters; // Green Threads paradas no .wait()
} TekoVMWaiter;

// Assinaturas públicas do Runtime de Concorrência
TekoVMChannel* teko_channel_create(uint32_t capacity);
bool teko_channel_put(TekoVMChannel* chan, int32_t value, VMScheduler* sched, TekoVM* vm);
bool teko_channel_get(TekoVMChannel* chan, int32_t* out_value, VMScheduler* sched, TekoVM* vm);

void teko_mutex_lock(TekoVMMutex* mutex, VMScheduler* sched, TekoVM* vm);
void teko_mutex_unlock(TekoVMMutex* mutex, VMScheduler* sched, TekoVM* vm);

void teko_waiter_add(TekoVMWaiter* waiter, int32_t delta);
void teko_waiter_done(TekoVMWaiter* waiter);
void teko_waiter_wait(TekoVMWaiter* waiter, VMScheduler* sched, TekoVM* vm);

#endif // VM_CONCURRENCY_H