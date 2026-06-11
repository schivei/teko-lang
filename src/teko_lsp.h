#ifndef TEKO_LSP_H
#define TEKO_LSP_H

#include <stdbool.h>
#include "project_manager.h"

// Tipos de requisições padronizadas pelo protocolo LSP
typedef enum {
    LSP_REQ_INITIALIZE,      // Mapeia o aperto de mão inicial da IDE
    LSP_REQ_DID_OPEN,        // Notifica que o usuário abriu um arquivo .tks
    LSP_REQ_DID_CHANGE,      // Notifica alteração de texto em tempo real (para linter)
    LSP_REQ_FORMATTING,      // Disparado no atalho de formatar código (fmt)
    LSP_REQ_COMPLETION,      // Disparado para autocompletar métodos (ex: @marshall.)
    LSP_REQ_UNKNOWN
} LSPRequestKind;

// Estrutura do Servidor LSP (tekols)
typedef struct {
    bool is_initialized;
    char* current_workspace_root;
    TekoProjectConfig* active_project;
} TekoLanguageServer;

// Assinaturas públicas do Servidor de Linguagem
TekoLanguageServer* teko_lsp_create(void);
LSPRequestKind teko_lsp_parse_request(const char* json_rpc_payload);
void teko_lsp_process_formatting(TekoLanguageServer* server, const char* file_path);
void teko_lsp_send_diagnostics(TekoLanguageServer* server, const char* file_path, const char* error_msg, int line);
void teko_lsp_destroy(TekoLanguageServer* server);

#endif // TEKO_LSP_H