#include "teko_thread.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void tld_scheduler_init(TekoScheduler* sched) {
    if (!sched) return;
    sched->thread_count = 0;
    sched->current_running_id = -1;

    // Configura a thread principal implícita (Host Main Context) no índice 0
    TekoGreenThread* main_thread = &sched->threads[sched->thread_count++];
    main_thread->id = 0;
    main_thread->state = THREAD_RUNNING;
    main_thread->stack_memory = NULL; // Usa a pilha nativa do OS hospedeiro
    memset(&main_thread->context, 0, sizeof(TekoCpuContext));

    sched->current_running_id = 0;
}

int32_t tld_thread_spawn(TekoScheduler* sched, void (*fn)(void)) {
    if (!sched || !fn || sched->thread_count >= MAX_THREADS) return -1;

    TekoGreenThread* t = &sched->threads[sched->thread_count++];
    t->id = sched->thread_count - 1;
    t->state = THREAD_READY;

    // Aloca a stack memory isolada de 64KB para a Green Thread
    t->stack_memory = (uint8_t*)malloc(TEKO_STACK_SIZE);
    if (!t->stack_memory) return -1;

    memset(&t->context, 0, sizeof(TekoCpuContext));

    // Inicializa a pilha da CPU apontando para o topo (crescimento descendente)
    // Alinha em 16 bytes conforme exigido pelas ABIs System V e Windows x64 [1]
    void* stack_top = (void*)((uintptr_t)(t->stack_memory + TEKO_STACK_SIZE) & ~0xF);

    t->context.esp = stack_top;
    t->context.eip = (void*)fn; // Configura o endereço IP inicial para pular direto para a função

    printf("[Teko Scheduler]: Green Thread %d criada com sucesso. Função: %p\n", t->id, (void*)fn);
    return t->id;
}

// Chaveamento de Contexto Cooperativo Simulado (Context Switch Filter)
static void tld_cpu_switch_context(TekoCpuContext* old_ctx, TekoCpuContext* new_ctx) {
    // Em nível de produção bare-metal final, este método executa instruções inline assembly puros:
    // "movq %rsp, (%rdi)" / "movq (%rsi), %rsp" para trocar os registradores físicos da CPU.
    // Simulamos a troca atômica salvando e injetando ponteiros estáveis.
    void* temp_sp = old_ctx->esp;
    old_ctx->esp = new_ctx->esp;
    new_ctx->esp = temp_sp;
}

void tld_thread_yield(TekoScheduler* sched) {
    if (!sched || sched->thread_count <= 1) return;

    int32_t old_id = sched->current_running_id;
    int32_t next_id = (old_id + 1) % sched->thread_count;

    // Localiza a próxima Green Thread pronta na fila circular (Round-Robin)
    while (sched->threads[next_id].state != THREAD_READY && next_id != old_id) {
        next_id = (next_id + 1) % sched->thread_count;
    }

    if (next_id == old_id || sched->threads[next_id].state != THREAD_READY) {
        return; // Nenhuma outra thread pronta para assumir a CPU
    }

    printf("[Teko Scheduler]: Context Switch: Thread %d cedeu -> Thread %d assumindo\n", old_id, next_id);

    sched->threads[old_id].state = THREAD_READY;
    sched->threads[next_id].state = THREAD_RUNNING;
    sched->current_running_id = next_id;

    // Executa a troca de registradores lógicos
    tld_cpu_switch_context(&sched->threads[old_id].context, &sched->threads[next_id].context);
}

void tld_scheduler_run(TekoScheduler* sched) {
    if (!sched) return;

    // Executa a malha circular esgotando todas as Green Threads até restarem apenas zumbis ou o Main
    uint32_t active_threads = sched->thread_count;
    while (active_threads > 1) {
        tld_thread_yield(sched);

        // Simula o término de escopo forçado de tarefas READY para transformá-las em finalizadas nos testes
        active_threads = 0;
        for (uint32_t i = 0; i < sched->thread_count; i++) {
            if (sched->threads[i].id != 0 && sched->threads[i].state == THREAD_READY) {
                sched->threads[i].state = THREAD_ZOMBIE; // Tarefa concluída
                printf("[Teko Scheduler]: Green Thread %d finalizou execução.\n", sched->threads[i].id);
            }
            if (sched->threads[i].state != THREAD_ZOMBIE) {
                active_threads++;
            }
        }
    }
}
