#ifndef TEKO_CHANNEL_H
#define TEKO_CHANNEL_H

#include <stdint.h>
#include <stdbool.h>
#include "teko_thread.h"

#define MAX_CHANNEL_BUFFER 16

// Estrutura física de um canal em nível de runtime
typedef struct {
    int32_t  buffer[MAX_CHANNEL_BUFFER]; // Buffer circular para os dados
    uint32_t capacity;                   // Capacidade máxima (0 para canais unbuffered/síncronos)
    uint32_t size;                       // Quantidade atual de elementos
    uint32_t head;                       // Índice de leitura
    uint32_t tail;                       // Índice de escrita

    // Filas de controle para Threads Bloqueadas na engenharia M:N
    int32_t blocked_senders[MAX_THREADS];
    uint32_t blocked_senders_count;
    int32_t blocked_receivers[MAX_THREADS];
    uint32_t blocked_receivers_count;
} TekoChannel;

// Assinaturas públicas do barramento concorrente de canais
void tld_channel_init(TekoChannel* chan, uint32_t capacity);
bool tld_channel_send(TekoChannel* chan, TekoScheduler* sched, int32_t value);
bool tld_channel_receive(TekoChannel* chan, TekoScheduler* sched, int32_t* out_value);

#endif // TEKO_CHANNEL_H
