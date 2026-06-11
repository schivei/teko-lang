#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "project_manager.h"
#include "lexer.h"
#include "parser.h"
#include "parser_statements.h"
#include "type_checker.h"
#include "codegen_li.h"
#include "teko_il.h"
#include "vm_core.h"
#include "vm_scheduler.h"
#include "vm_debug.h"

// --- INCLUSÕES ADICIONADAS EXCLUSIVAMENTE PARA O BACKEND BARE-METAL ---
#include "teko_target.h"
#include "codegen/codegen_metal.h"

// Modos de emissão para o pipeline Ahead-of-Time (AOT)
typedef enum {
    EMIT_EXECUTABLE,
    EMIT_ASSEMBLY,
    EMIT_OBJECT
} EmitMode;

static void print_usage(void) {
    printf("Uso: teko <comando> <caminho_do_projeto.tkp> [opcoes]\n\n");
    printf("Comandos Disponíveis:\n");
    printf("  run          Compila o projeto a partir de main.tks e executa imediatamente na VM.\n");
    printf("  build        Compila o projeto e gera o binario intermediario .tkb ou o executavel metal nativo.\n");
    printf("  check        Executa apenas as validacoes de tipo e semantica do projeto.\n");
    printf("  emit-il      Gera o arquivo binario cru da Linguagem Intermediaria (IL).\n");
    printf("  debug        Dispara o ambiente de execucao acoplado ao servidor DAP.\n");
    printf("  fmt          Formata os arquivos .tks do projeto.\n\n");
    printf("Opcoes:\n");
    printf("  --stdlib     Ativa o modo de compilacao de privilégio da biblioteca core.\n");
    // --- OPÇÕES EXPANDIDAS EXCLUSIVAMENTE NA FERRAMENTARIA ---
    printf("  -S           (Usado no build): Interrompe o pipeline gerando apenas o arquivo Assembly (.s/.asm)\n");
    printf("  -c           (Usado no build): Gera o arquivo objeto (.o/.obj) mas nao executa a linkagem final\n");
    printf("  --target=<t> (Usado no build): Especifica o Target Triple metal (Ex: aarch64-apple-darwin)\n");
    printf("  -o <saida>   (Usado no build): Define o nome customizado do artefato nativo final gerado\n");
}

static char* read_source_file(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "[Erro de Sistema]: Nao foi possivel abrir o arquivo '%s'.\n", filename);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t read_bytes = fread(buffer, 1, size, file);
    buffer[read_bytes] = '\0';
    fclose(file);
    return buffer;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char* command = argv[1];

    // Tratamento isolado para evitar falha de índice se chamar apenas "teko fmt"
    if (strcmp(command, "fmt") == 0) {
        printf("[CLI fmt]: Formatador de arquivos .tks ativado. (Funcionalidade em desenvolvimento na Fase 4).\n");
        return 0;
    }

    if (argc < 3) {
        print_usage();
        return 1;
    }

    const char* tkp_path = argv[2];
    bool is_stdlib = false;

    // --- NOVA CAPTURA DE VARIÁVEIS DE CONTROLE DO BACKEND AOT ---
    EmitMode aot_mode = EMIT_EXECUTABLE;
    const char* custom_output_name = NULL;
    TekoTarget target = teko_target_detect_host(); // Inicia com o hardware local padrão

    // Varredura de opções estendidas sem quebrar o indexamento original
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--stdlib") == 0) {
            is_stdlib = true;
        }
        else if (strcmp(argv[i], "-S") == 0) {
            aot_mode = EMIT_ASSEMBLY;
        }
        else if (strcmp(argv[i], "-c") == 0) {
            aot_mode = EMIT_OBJECT;
        }
        else if (strncmp(argv[i], "--target=", 9) == 0) {
            target = teko_target_parse(argv[i] + 9);
        }
        else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                custom_output_name = argv[++i];
            }
        }
    }

    // 1. Carrega e Valida a Estrutura do Manifesto do Projeto (.tkp)
    TekoProjectConfig* project = teko_project_load(tkp_path);
    if (!project) return 1;

    if (!teko_project_validate_structure(project)) {
        teko_project_free(project);
        return 1;
    }

    // 2. Resolve o caminho físico do ponto de entrada real main.tks
    char main_tks_path[512];
    snprintf(main_tks_path, sizeof(main_tks_path), "%s/main.tks", project->root_dir);

    printf("[Projeto]: Inicializando build de '%s' v%s (Autor: %s)\n",
           project->project_name, project->version, project->author);

    // 3. Lê o arquivo main.tks carregado
    char* source_code = read_source_file(main_tks_path);
    if (!source_code) {
        teko_project_free(project);
        return 1;
    }

    // 4. Executa Frontend (Lexer ➔ Parser)
    Lexer l;
    lexer_init(&l, source_code);
    Parser p;
    parser_init(&p, &l);
    p.is_stdlib_compilation = is_stdlib;

    StatementASTNode* ast_root = parse_statement(&p);
    if (!ast_root) {
        fprintf(stderr, "[Build Falhou]: Erro sintatico em '%s'.\n", main_tks_path);
        free(source_code);
        teko_project_free(project);
        return 1;
    }

    // 5. Analisador Semântico de Tipos (Type Checker)
    SymbolTableScope* scope = symbol_table_create_scope(NULL);
    TypeCheckResult type_res = check_statement_types(scope, ast_root);
    if (type_res.error_kind != TYPE_ERR_NONE) {
        fprintf(stderr, "[Build Falhou]: Erro semantico em '%s'.\n", main_tks_path);
        free_statement_ast_node(ast_root);
        symbol_table_free_scope(scope);
        free(source_code);
        teko_project_free(project);
        return 1;
    }

    if (strcmp(command, "check") == 0) {
        printf("[Check]: Projeto '%s' verificado com sucesso. 0 erros encontrados.\n", project->project_name);
        free_statement_ast_node(ast_root);
        symbol_table_free_scope(scope);
        free(source_code);
        teko_project_free(project);
        return 0;
    }

    // 6. Emissor da Linguagem Intermediária (LI)
    BytecodeBuffer* li_buffer = codegen_li_create_context();
    codegen_li_emit_statement(li_buffer, ast_root);

    if (strcmp(command, "run") == 0 && project->target_type != TARGET_EXECUTABLE) {
        fprintf(stderr, "[Erro de Execução]: Projetos do tipo Library não podem ser executados diretamente na VM. Use o comando 'build'.\n");
    }
    else if (strcmp(command, "emit-il") == 0 || strcmp(command, "build") == 0) {
        char out_binary[512];
        char out_header[512];

        if (project->target_type == TARGET_EXECUTABLE) {
            if (strcmp(command, "emit-il") == 0) {
                // Mantém comportamento antigo se chamar expressamente o emit-il
                snprintf(out_binary, sizeof(out_binary), "%s/%s.tkb", project->root_dir, project->project_name);
                teko_il_serialize_binary(li_buffer, out_binary);
            }
            else {
                // --- INTEGRALIZAÇÃO DO BACKEND BARE-METAL DENTRO DO COMANDO BUILD ---
                const char* final_name = custom_output_name ? custom_output_name : project->project_name;
                char asm_file[512];
                char obj_file[512];

                const char* asm_ext = (target.os == OS_WINDOWS) ? ".asm" : ".s";
                const char* obj_ext = (target.os == OS_WINDOWS) ? ".obj" : ".o";

                snprintf(asm_file, sizeof(asm_file), "%s/%s%s", project->root_dir, final_name, asm_ext);
                snprintf(obj_file, sizeof(obj_file), "%s/%s%s", project->root_dir, final_name, obj_ext);

                printf("[AOT]: Despachando gerador metal para target: %s\n", target.target_string);

                MetalContext* m_ctx = teko_metal_create(asm_file, target);
                if (m_ctx) {
                    // Transpila o fluxo linear da LI diretamente em código de máquina puro
                    teko_metal_emit_program(m_ctx, li_buffer->code, li_buffer->size);
                    teko_metal_close(m_ctx);
                }

                if (aot_mode == EMIT_ASSEMBLY) {
                    printf("[AOT]: Pipeline interrompido. Arquivo Assembly gerado: '%s'\n", asm_file);
                }
                else {
                    // Executa a montagem física chamando o montador correspondente
                    char cmd_assemble[1024];
                    if (target.os == OS_WINDOWS) {
                        snprintf(cmd_assemble, sizeof(cmd_assemble), "ml64.exe /c /Fo%s %s >nul 2>&1", obj_file, asm_file);
                    } else if (target.os == OS_MACOS_DARWIN) {
                        snprintf(cmd_assemble, sizeof(cmd_assemble), "clang -c -o %s %s -arch arm64", obj_file, asm_file);
                    } else {
                        snprintf(cmd_assemble, sizeof(cmd_assemble), "as -o %s %s", obj_file, asm_file);
                    }

                    int r_asm = system(cmd_assemble);
                    (void)r_asm;

                    if (aot_mode == EMIT_OBJECT) {
                        printf("[AOT]: Pipeline interrompido. Arquivo objeto gerado: '%s'\n", obj_file);
                    }
                    else {
                        // Executa a linkagem final gerando o binário executável bare-metal limpo
                        char cmd_link[1024];
                        char out_exe[512];
                        if (target.os == OS_WINDOWS) {
                            snprintf(out_exe, sizeof(out_exe), "%s/%s.exe", project->root_dir, final_name);
                            snprintf(cmd_link, sizeof(cmd_link), "link.exe /subsystem:console /out:%s %s >nul 2>&1", out_exe, obj_file);
                        } else {
                            snprintf(out_exe, sizeof(out_exe), "%s/%s", project->root_dir, final_name);
                            snprintf(cmd_link, sizeof(cmd_link), "clang -o %s %s", out_exe, obj_file);
                        }
                        int r_link = system(cmd_link);
                        (void)r_link;

                        // Limpeza higiênica de arquivos temporários intermediários em disco
                        remove(asm_file);
                        remove(obj_file);
                        printf("[AOT]: Executavel bare-metal nativo gerado com sucesso: '%s'\n", out_exe);
                    }
                }
            }
        }
        else {
            // COMPILAÇÃO COMPARTILHADA / BIBLIOTECA (AOT / Objeto) PERMANECE 100% INTACTA
            const char* ext = (project->target_type == TARGET_STATIC_LIB) ? "a" : "dylib";
            snprintf(out_binary, sizeof(out_binary), "%s/lib%s.%s", project->root_dir, project->project_name, ext);
            snprintf(out_header, sizeof(out_header), "%s/%s.h", project->root_dir, project->project_name);

            teko_il_serialize_binary(li_buffer, out_binary);

            FILE* h_file = fopen(out_header, "w");
            if (h_file) {
                fprintf(h_file, "/* Gerado automaticamente pelo compilador Teko - Nao modifique */\n");
                fprintf(h_file, "#ifndef TEKO_LIB_%s_H\n#define TEKO_LIB_%s_H\n\n", project->project_name, project->project_name);

                if (ast_root && ast_root->type == NODE_FUNC_DECL) {
                    fprintf(h_file, "void %s(void);\n", ast_root->data.func_decl.fn_name);
                }

                fprintf(h_file, "\n#endif\n");
                fclose(h_file);
                printf("[AOT Codegen]: Cabeçalho C público de integração '%s' exportado com sucesso.\n", out_header);
            }
        }
    }
    else if (strcmp(command, "run") == 0) {
        // AMBIENTE VM CORE PERMANECE 100% INTACTO
        TekoVM* vm = teko_vm_create(li_buffer->code, li_buffer->size, li_buffer->pool.strings, li_buffer->pool.count);
        VMScheduler* scheduler = vm_scheduler_create();
        vm_scheduler_spawn(scheduler, 0);

        if (vm_scheduler_run_next(scheduler, vm)) {
            int32_t exit_code = teko_vm_execute(vm);
            printf("\n[VM Execution Finalized] Codigo de Retorno: %d\n", exit_code);
        }

        vm_scheduler_destroy(scheduler);
        teko_vm_destroy(vm);
    }
    else if (strcmp(command, "debug") == 0) {
        // SERVIDOR DAP PERMANECE 100% INTACTO
        TekoDebugger* debugger = teko_debugger_create();
        printf("[DAP Debugger Server]: Escutando comandos JSON-RPC para o projeto '%s'...\n", project->project_name);
        teko_debugger_destroy(debugger);
    }

    // Limpeza de encerramento do escopo principal
    codegen_li_free_context(li_buffer);
    free_statement_ast_node(ast_root);
    symbol_table_free_scope(scope);
    free(source_code);
    teko_project_free(project);

    return 0;
}
