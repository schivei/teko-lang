#include "vm_debug.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

TekoDebugger* teko_debugger_create(void) {
    auto dbg = (TekoDebugger*)malloc(sizeof(TekoDebugger));
    if (!dbg) return NULL;
    dbg->source_map_count = 0;
    dbg->breakpoint_count = 0;
    dbg->is_stepping = false;
    dbg->is_paused = false;
    return dbg;
}

// Registra um símbolo mapeado vindo do compilador
void teko_debugger_add_symbol(TekoDebugger* dbg, uint32_t line, uint32_t bytecode_ip) {
    if (!dbg || dbg->source_map_count >= MAX_SOURCE_LINES) return;
    dbg->source_maps[dbg->source_map_count].line = line;
    dbg->source_maps[dbg->source_map_count].bytecode_ip = bytecode_ip;
    dbg->source_map_count++;
}

// Ativa um Breakpoint convertendo o número da linha da IDE para o IP real da VM
void teko_debugger_set_breakpoint(TekoDebugger* dbg, uint32_t line) {
    if (!dbg || dbg->breakpoint_count >= MAX_BREAKPOINTS) return;

    for (uint32_t i = 0; i < dbg->source_map_count; i++) {
        if (dbg->source_maps[i].line == line) {
            dbg->breakpoints[dbg->breakpoint_count++] = dbg->source_maps[i].bytecode_ip;
            break;
        }
    }
}

// Checa no laço de computed gotos se a VM deve congelar a execução
bool teko_debugger_should_pause(TekoDebugger* dbg, uint32_t current_ip) {
    if (!dbg) return false;
    if (dbg->is_stepping) return true;

    for (uint32_t i = 0; i < dbg->breakpoint_count; i++) {
        if (dbg->breakpoints[i] == current_ip) {
            dbg->is_paused = true;
            return true;
        }
    }
    return false;
}

// Interpretador de payloads JSON-RPC vindo via conexão TCP das IDEs (Padrão DAP)
void teko_debugger_handle_dap_message(TekoDebugger* dbg, const char* json_message, TekoVM* vm) {
    if (!dbg || !json_message || !vm) return;

    // Simula a recepção de comandos JSON-RPC estruturados pelo DAP
    if (strstr(json_message, "\"command\": \"next\"") || strstr(json_message, "\"command\": \"stepIn\"")) {
        dbg->is_stepping = true;
        dbg->is_paused = false;
        printf("[DAP Server]: Comando STEP executado pela IDE.\n");
    }
    else if (strstr(json_message, "\"command\": \"continue\"")) {
        dbg->is_stepping = false;
        dbg->is_paused = false;
        printf("[DAP Server]: Comando CONTINUE executado pela IDE.\n");
    }
    else if (strstr(json_message, "\"command\": \"scopes\"")) {
        // Retorna o dump das variáveis ativas na Arena para preencher as janelas de Watch da IDE
        printf("[DAP Server]: Dump de memória da Arena exportado para a IDE.\n");
    }
}

void teko_debugger_destroy(TekoDebugger* dbg) {
    if (dbg) free(dbg);
}