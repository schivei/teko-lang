#include "vm_concurrency.h"
#include <stdlib.h>
#include <string.h>

// Auxiliar para enfileirar uma Green Thread em uma lista de bloqueados
static void block_thread(GreenThread** list_head, GreenThread* thread) {
    if (!thread) return;
    thread->state = THREAD_BLOCKED;
    thread->next = NULL;

    if (!*list_head) {
        *list_head = thread;
    } else {
        auto curr = *list_head;
        while (curr->next) curr = curr->next;
        curr->next = thread;
    }
}

// Auxiliar para desbloquear a primeira Green Thread de uma lista e devolvê-la ao Scheduler
static void unblock_thread(GreenThread** list_head, VMScheduler* sched) {
    if (!*list_head || !sched) return;

    auto thread = *list_head;
    *list_head = thread->next;

    thread->state = THREAD_READY;
    thread->next = NULL;

    // Recoloca a corrotina liberada na cauda da fila de execução global
    if (!sched->queue_head) {
        sched->queue_head = thread;
        sched->queue_tail = thread;
    } else {
        sched->queue_tail->next = thread;
        sched->queue_tail = thread;
    }
}

TekoVMChannel* teko_channel_create(uint32_t capacity) {
    auto chan = (TekoVMChannel*)malloc(sizeof(TekoVMChannel));
    if (!chan) return NULL;

    chan->capacity = capacity == 0 ? 1 : capacity; // Garante tamanho mínimo de 1 para bounded básico
    chan->buffer = (int32_t*)calloc(chan->capacity, sizeof(int32_t));
    chan->head = 0;
    chan->tail = 0;
    chan->count = 0;
    chan->blocked_readers = NULL;
    chan->blocked_writers = NULL;
    return chan;
}

// Operação Assíncrona de Escrita no Canal (chan.put)
bool teko_channel_put(TekoVMChannel* chan, int32_t value, VMScheduler* sched, TekoVM* vm) {
    if (!chan || !sched || !vm) return false;

    // Se o canal estiver cheio, bloqueia a Green Thread atual imediatamente
    if (chan->count >= chan->capacity) {
        auto active = sched->current_thread;
        if (active) {
            block_thread(&chan->blocked_writers, active);
            vm_scheduler_yield(sched, vm); // Suspende a execução da VM corrente
        }
        return false;
    }

    chan->buffer[chan->tail] = value;
    chan->tail = (chan->tail + 1) % chan->capacity;
    chan->count++;

    // Se havia alguma Green Thread parada esperando para ler dados, libera ela!
    if (chan->blocked_readers) {
        unblock_thread(&chan->blocked_readers, sched);
    }
    return true;
}

// Operação de Leitura do Canal
bool teko_channel_get(TekoVMChannel* chan, int32_t* out_value, VMScheduler* sched, TekoVM* vm) {
    if (!chan || !out_value || !sched || !vm) return false;

    // Se o canal estiver completamente vazio, bota a Green Thread na geladeira
    if (chan->count == 0) {
        auto active = sched->current_thread;
        if (active) {
            block_thread(&chan->blocked_readers, active);
            vm_scheduler_yield(sched, vm);
        }
        return false;
    }

    *out_value = chan->buffer[chan->head];
    chan->head = (chan->head + 1) % chan->capacity;
    chan->count--;

    // Se havia alguma Green Thread bloqueada esperando espaço para escrever, acorda ela!
    if (chan->blocked_writers) {
        unblock_thread(&chan->blocked_writers, sched);
    }
    return true;
}

// Operação mtx.lock()
void teko_mutex_lock(TekoVMMutex* mutex, VMScheduler* sched, TekoVM* vm) {
    if (!mutex || !sched || !vm) return;

    auto active = sched->current_thread;
    if (mutex->is_locked) {
        if (active && mutex->owner != active) {
            block_thread(&mutex->blocked_threads, active);
            vm_scheduler_yield(sched, vm);
        }
    } else {
        mutex->is_locked = true;
        mutex->owner = active;
    }
}

// Operação mtx.unlock()
void teko_mutex_unlock(TekoVMMutex* mutex, VMScheduler* sched, TekoVM* vm) {
    if (!mutex || !mutex->is_locked || !sched) return;

    mutex->is_locked = false;
    mutex->owner = NULL;

    if (mutex->blocked_threads) {
        unblock_thread(&mutex->blocked_threads, sched);
    }
}

// Operação wg.add(X)
void teko_waiter_add(TekoVMWaiter* waiter, int32_t delta) {
    if (waiter) waiter->counter += delta;
}

// Operação wg.done()
void teko_waiter_done(TekoVMWaiter* waiter) {
    if (waiter) waiter->counter--;
}

// Operação wg.wait()
void teko_waiter_wait(TekoVMWaiter* waiter, VMScheduler* sched, TekoVM* vm) {
    if (!waiter || !sched || !vm) return;

    // Se o contador for maior que zero, a Green Thread atual fica travada até as outras darem .done()
    if (waiter->counter > 0) {
        auto active = sched->current_thread;
        if (active) {
            block_thread(&waiter->blocked_waiters, active);
            vm_scheduler_yield(sched, vm);
        }
    }
}