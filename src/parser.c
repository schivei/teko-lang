#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Avança os tokens do parser
static void parser_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Inicializa o Parser lendo os dois primeiros tokens (Lookahead)
void parser_init(Parser* parser, Lexer* lexer) {
    parser->lexer = lexer;
    // Carrega o lookahead inicial
    parser->current_token = lexer_next_token(parser->lexer);
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Verifica se o token atual é o esperado e avança, ou reporta erro de sintaxe
static bool expect_token(Parser* parser, TokenType type, const char* context) {
    if (parser->current_token.type == type) {
        parser_advance(parser);
        return true;
    }
    fprintf(stderr, "[Erro Sintático] Linha %d: Esperado '%s' em %s, mas encontrou '%s'\n",
            parser->current_token.line, get_token_type_name(type), context, parser->current_token.lexeme);
    return false;
}

// Analisa uma declaração 'use' protegendo o namespace reservado
// Sintaxe suportada:
//   use my::namespace;
//   use dependency::other::app from "dependency";
ASTNode* parse_use_statement(Parser* parser) {
    if (!expect_token(parser, TOKEN_USE, "declaracao use")) return NULL;

    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) return NULL;

    node->type = NODE_USE_STMT;
    node->data.use_stmt.path = NULL;
    node->data.use_stmt.from_source = NULL;

    char path_buffer[512] = {0};

    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        // REGRA DE PROTEÇÃO: Se o identificador inicial for "teko" e não for compilação da stdlib, bloqueia!
        if (strcmp(parser->current_token.lexeme, "teko") == 0 && !parser->is_stdlib_compilation) {
            fprintf(stderr, "[Erro Semântico] Linha %d: O namespace 'teko::' é reservado para o sistema. Use o prefixo '@' para acessar as bibliotecas nativas.\n",
                    parser->current_token.line);
            free(node);
            return NULL;
        }

        strcat(path_buffer, parser->current_token.lexeme);
        parser_advance(parser);
    } else {
        fprintf(stderr, "[Erro Sintático] Linha %d: Esperado identificador após 'use'\n", parser->current_token.line);
        free(node);
        return NULL;
    }

    while (parser->current_token.type == TOKEN_DBL_COLON) {
        strcat(path_buffer, "::");
        parser_advance(parser);

        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            strcat(path_buffer, parser->current_token.lexeme);
            parser_advance(parser);
        } else {
            fprintf(stderr, "[Erro Sintático] Linha %d: Esperado identificador após '::'\n", parser->current_token.line);
            free(node);
            return NULL;
        }
    }

    node->data.use_stmt.path = strdup(path_buffer);

    if (parser->current_token.type == TOKEN_FROM) {
        parser_advance(parser);
        if (parser->current_token.type == TOKEN_LIT_STR) {
            node->data.use_stmt.from_source = strdup(parser->current_token.lexeme);
            parser_advance(parser);
        }
    }

    expect_token(parser, TOKEN_SEMICOLON, "fim da declaracao use");
    return node;
}

// Ponto de entrada do Parser para processar o arquivo de forma recursiva
ASTNode* parser_parse_program(Parser* parser) {
    ASTNode* program_node = (ASTNode*)malloc(sizeof(ASTNode));
    program_node->type = NODE_PROGRAM;
    program_node->data.program.children = NULL;
    program_node->data.program.child_count = 0;

    int capacity = 4;
    program_node->data.program.children = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);

    while (parser->current_token.type != TOKEN_EOF) {
        ASTNode* stmt = NULL;

        switch (parser->current_token.type) {
            case TOKEN_USE:
                stmt = parse_use_statement(parser);
                break;
            default:
                // Ignora temporariamente tokens desconhecidos no nível global para evitar loops no desenvolvimento
                parser_advance(parser);
                break;
        }

        if (stmt != NULL) {
            if (program_node->data.program.child_count >= capacity) {
                capacity *= 2;
                program_node->data.program.children = (ASTNode**)realloc(
                    program_node->data.program.children, sizeof(ASTNode*) * capacity);
            }
            program_node->data.program.children[program_node->data.program.child_count++] = stmt;
        }
    }

    return program_node;
}

// Liberação de memória limpa da árvore recursiva (evita memory leaks)
void free_ast_node(ASTNode* node) {
    if (!node) return;

    if (node->type == NODE_USE_STMT) {
        free(node->data.use_stmt.path);
        if (node->data.use_stmt.from_source) {
            free(node->data.use_stmt.from_source);
        }
    } else if (node->type == NODE_PROGRAM) {
        for (int i = 0; i < node->data.program.child_count; i++) {
            free_ast_node(node->data.program.children[i]);
        }
        free(node->data.program.children);
    }
    free(node);
}