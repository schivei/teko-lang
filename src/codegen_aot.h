#ifndef CODEGEN_AOT_H
#define CODEGEN_AOT_H

#include "parser_statements.h"
#include <stdio.h>

// Estrutura que gerencia o buffer de escrita do arquivo C transpilado
typedef struct {
    FILE* file;
    int indent_level;
} AOTContext;

// Assinaturas públicas do Backend AOT
AOTContext* codegen_aot_create(const char* output_c_path);
void codegen_aot_emit_header(AOTContext* ctx, const char* project_name);
void codegen_aot_emit_statement(AOTContext* ctx, const StatementASTNode* stmt);
void codegen_aot_close(AOTContext* ctx);

#endif // CODEGEN_AOT_H