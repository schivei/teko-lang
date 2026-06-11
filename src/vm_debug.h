#ifndef VM_DEBUG_H
#define VM_DEBUG_H

#include <stdint.h>
#include <stdbool.h>
#include "vm_core.h"

#define MAX_BREAKPOINTS 64
#define MAX_SOURCE_LINES 1024

// Mapeia uma linha do código-fonte original para o endereço IP do Bytecode (.tkb)
typedef struct {
    uint32_t line;
    uint32_t bytecode_ip;
} DebugSourceMap;

// Estrutura do Subsistema de Debugger (DAP-Compatible Core)
typedef struct {
    DebugSourceMap source_maps[MAX_SOURCE_LINES];
    uint32_t source_map_count;

    uint32_t breakpoints[MAX_BREAKPOINTS];
    uint32_t breakpoint_count;

    bool is_stepping;         // Se ativo, avança instrução por instrução (Step Into)
    bool is_paused;           // Estado de congelamento da execução
} TekoDebugger;

// Assinaturas públicas do Debugger
TekoDebugger* teko_debugger_create(void);
void teko_debugger_add_symbol(TekoDebugger* dbg, uint32_t line, uint32_t bytecode_ip);
void teko_debugger_set_breakpoint(TekoDebugger* dbg, uint32_t line);
bool teko_debugger_should_pause(TekoDebugger* dbg, uint32_t current_ip);
void teko_debugger_handle_dap_message(TekoDebugger* dbg, const char* json_message, TekoVM* vm);
void teko_debugger_destroy(TekoDebugger* dbg);

#endif // VM_DEBUG_H