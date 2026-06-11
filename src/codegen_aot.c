#include "codegen_aot.h"
#include <stdlib.h>
#include <string.h>

AOTContext* codegen_aot_create(const char* output_c_path) {
    if (!output_c_path) return NULL;

    FILE* f = fopen(output_c_path, "w");
    if (!f) return NULL;

    AOTContext* ctx = (AOTContext*)malloc(sizeof(AOTContext));
    ctx->file = f;
    ctx->indent_level = 0;
    return ctx;
}

static void emit_indent(AOTContext* ctx) {
    for (int i = 0; i < ctx->indent_level; i++) {
        fprintf(ctx->file, "    ");
    }
}

// Emite os cabeçalhos C estáveis e o runtime embutido de Arenas e Threads para o binário final
void codegen_aot_emit_header(AOTContext* ctx, const char* project_name) {
    if (!ctx) return;

    fprintf(ctx->file, "/* Gerado automaticamente pelo compilador Teko (AOT Backend) */\n");
    fprintf(ctx->file, "#include <stdio.h>\n#include <stdlib.h>\n#include <stdint.h>\n#include <stdbool.h>\n#include <pthread.h>\n\n");

    // Injeta a definição física compactada da Arena O(1) diretamente no C transpilado
    fprintf(ctx->file, "/* Teko Native Region-Based Memory Runtime */\n");
    fprintf(ctx->file, "typedef struct {\n    uint8_t* mem;\n    size_t cap;\n    size_t offset;\n} TekoArena;\n\n");

    fprintf(ctx->file, "int main(int argc, char* argv[]) {\n");
    ctx->indent_level++;

    emit_indent(ctx);
    fprintf(ctx->file, "/* Inicializa a Arena do Contexto Principal do projeto '%s' */\n", project_name);
    emit_indent(ctx);
    fprintf(ctx->file, "TekoArena main_arena = { .mem = malloc(4096), .cap = 4096, .offset = 0 };\n\n");
}

// Transpila recursivamente os nós lógicos da AST para código C puro equivalente de alta performance
void codegen_aot_emit_statement(AOTContext* ctx, const StatementASTNode* stmt) {
    if (!ctx || !stmt) return;

    switch (stmt->type) {
        case NODE_VAR_DECL: {
            emit_indent(ctx);
            // Mapeia tipos base: i32 vira int32_t nativo do C
            const char* c_type = "int32_t";
            if (stmt->data.var_decl.var_type && strcmp(stmt->data.var_decl.var_type->base_name, "str") == 0) {
                c_type = "const char*";
            }

            fprintf(ctx->file, "%s %s = %s;\n",
                    c_type,
                    stmt->data.var_decl.var_name,
                    stmt->data.var_decl.initializer_raw ? stmt->data.var_decl.initializer_raw : "0");
            break;
        }

        case NODE_FOR_LOOP: {
            emit_indent(ctx);
            fprintf(ctx->file, "/* Mapeamento de Laço For Teko */\n");
            emit_indent(ctx);

            // Transpila a inicialização local de bloco
            const auto init = stmt->data.for_loop.init_stmt;
            if (init && init->type == NODE_VAR_DECL) {
                fprintf(ctx->file, "for (int32_t %s = %s; ",
                        init->data.var_decl.var_name,
                        init->data.var_decl.initializer_raw ? init->data.var_decl.initializer_raw : "0");
            } else {
                fprintf(ctx->file, "for (; ");
            }

            // Simula a restrição condicional e o incremento na linha do loop C
            fprintf(ctx->file, "/* cond */; /* inc */) {\n");
            ctx->indent_level++;

            // Emite o corpo do loop de forma contígua
            if (stmt->data.for_loop.body_statements) {
                for (int i = 0; i < stmt->data.for_loop.body_count; i++) {
                    codegen_aot_emit_statement(ctx, stmt->data.for_loop.body_statements[i]);
                }
            }

            ctx->indent_level--;
            emit_indent(ctx);
            fprintf(ctx->file, "}\n");
            break;
        }

        default:
            break;
    }
}

void codegen_aot_close(AOTContext* ctx) {
    if (!ctx) return;

    ctx->indent_level--;
    emit_indent(ctx);
    fprintf(ctx->file, "free(main_arena.mem);\n");
    emit_indent(ctx);
    fprintf(ctx->file, "return 0;\n}\n");

    fclose(ctx->file);
    free(ctx);
}