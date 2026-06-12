#include "teko_channel.h"
#include <stdio.h>

void tld_channel_init(TekoChannel* chan, uint32_t capacity) {
    if (!chan) return;
    chan->capacity = capacity > MAX_CHANNEL_BUFFER ? MAX_CHANNEL_BUFFER : capacity;
    chan->size = 0;
    chan->head = 0;
    chan->tail = 0;
    chan->blocked_senders_count = 0;
    chan->blocked_receivers_count = 0;
}

bool tld_channel_send(TekoChannel* chan, TekoScheduler* sched, int32_t value) {
    if (!chan || !sched) return false;

    int32_t current_thread_id = sched->current_running_id;

    // CANAL CHEIO: Bloqueia o emissor de forma cooperativa
    if (chan->size >= chan->capacity) {
        if (current_thread_id != -1 && chan->blocked_senders_count < MAX_THREADS) {
            printf("[Teko Channels]: CANAL CHEIO! Bloqueando Green Thread %d na escrita.\n", current_thread_id);
            chan->blocked_senders[chan->blocked_senders_count++] = current_thread_id;
            sched->threads[current_thread_id].state = THREAD_BLOCKED;

            // Força a CPU a alternar para outra thread pronta
            tld_thread_yield(sched);
        }
        return false;
    }

    // Injeta o dado no buffer circular
    chan->buffer[chan->tail] = value;
    chan->tail = (chan->tail + 1) % MAX_CHANNEL_BUFFER;
    chan->size++;

    // FLUXO DE ACORDO: Se havia algum receptor bloqueado esperando dados, bota ele de volta em READY
    if (chan->blocked_receivers_count > 0) {
        int32_t waiting_thread_id = chan->blocked_receivers[0];
        printf("[Teko Channels]: Dado injetado. Acordando Green Thread %d que estava bloqueada na leitura.\n", waiting_thread_id);

        // Remove do topo da fila de bloqueados deslizando o vetor
        for (uint32_t i = 1; i < chan->blocked_receivers_count; i++) {
            chan->blocked_receivers[i - 1] = chan->blocked_receivers[i];
        }
        chan->blocked_receivers_count--;

        // Thread volta para a esteira ativa do escalonador
        sched->threads[waiting_thread_id].state = THREAD_READY;
    }

    return true;
}

bool tld_channel_receive(TekoChannel* chan, TekoScheduler* sched, int32_t* out_value) {
    if (!chan || !sched || !out_value) return false;

    int32_t current_thread_id = sched->current_running_id;

    // CANAL VAZIO: Bloqueia o receptor de forma cooperativa
    if (chan->size == 0) {
        if (current_thread_id != -1 && chan->blocked_receivers_count < MAX_THREADS) {
            printf("[Teko Channels]: CANAL VAZIO! Bloqueando Green Thread %d na leitura.\n", current_thread_id);
            chan->blocked_receivers[chan->blocked_receivers_count++] = current_thread_id;
            sched->threads[current_thread_id].state = THREAD_BLOCKED;

            tld_thread_yield(sched);
        }
        return false;
    }

    // Extrai o dado do buffer circular
    *out_value = chan->buffer[chan->head];
    chan->head = (chan->head + 1) % MAX_CHANNEL_BUFFER;
    chan->size--;

    // FLUXO DE ACORDO: Se havia algum emissor bloqueado esperando espaço no buffer, acorda ele
    if (chan->blocked_senders_count > 0) {
        int32_t waiting_thread_id = chan->blocked_senders[0];
        printf("[Teko Channels]: Espaco liberado. Acordando Green Thread %d que estava bloqueada na escrita.\n", waiting_thread_id);

        for (uint32_t i = 1; i < chan->blocked_senders_count; i++) {
            chan->blocked_senders[i - 1] = chan->blocked_senders[i];
        }
        chan->blocked_senders_count--;

        sched->threads[waiting_thread_id].state = THREAD_READY;
    }

    return true;
}
