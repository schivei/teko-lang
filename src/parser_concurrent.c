#include "parser_concurrent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void conc_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Analisa expressões 'await ...'
ConcurrentASTNode* parse_await_expression(Parser* parser) {
    conc_advance(parser); // Consome 'await'

    ConcurrentASTNode* node = (ConcurrentASTNode*)malloc(sizeof(ConcurrentASTNode));
    node->type = NODE_AWAIT_EXPR;
    node->data.await_expr.expression = NULL;

    // Em uma implementação completa, chamaria a função genérica de parse de expressão.
    // Aqui capturamos o identificador da chamada assíncrona para compor a AST.
    if (parser->current_token.type == TOKEN_IDENTIFIER || parser->current_token.type == TOKEN_MACRO_IDENT) {
        ConcurrentASTNode* expr_child = (ConcurrentASTNode*)malloc(sizeof(ConcurrentASTNode));
        // Simplificado como identificador de folha para este escopo
        expr_child->type = NODE_CONCURRENT_OBJECT;
        node->data.await_expr.expression = expr_child;
        conc_advance(parser);
    }

    return node;
}

// Analisa os blocos 'defer' e 'async defer'
ConcurrentASTNode* parse_defer_statement(Parser* parser, bool is_async) {
    conc_advance(parser); // Consome 'defer'

    ConcurrentASTNode* node = (ConcurrentASTNode*)malloc(sizeof(ConcurrentASTNode));
    node->type = is_async ? NODE_ASYNC_DEFER_BLOCK : NODE_DEFER_BLOCK;
    node->data.defer_block.statements = NULL;
    node->data.defer_block.statement_count = 0;

    if (parser->current_token.type == TOKEN_LBRACE) {
        conc_advance(parser); // Consome '{'

        int cap = 4;
        node->data.defer_block.statements = (ConcurrentASTNode**)malloc(sizeof(ConcurrentASTNode*) * cap);

        // Varre o escopo interno do bloco defer
        while (parser->current_token.type != TOKEN_RBRACE && parser->current_token.type != TOKEN_EOF) {
            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                // Avanço simples para simular o consumo de comandos internos no exemplo
                conc_advance(parser);
            } else {
                conc_advance(parser);
            }
        }

        if (parser->current_token.type == TOKEN_RBRACE) {
            conc_advance(parser); // Consome '}'
        }
    }

    return node;
}

// Analisa atribuições rápidas do operador ':=' (canais, waiter, mutex)
ConcurrentASTNode* parse_concurrency_assignment(Parser* parser) {
    ConcurrentASTNode* node = (ConcurrentASTNode*)malloc(sizeof(ConcurrentASTNode));
    node->type = TOKEN_UNKNOWN;

    // Caso 1: Inicialização de canais -> ch := chan<i32> ou chan<i32>(1)
    if (strcmp(parser->current_token.lexeme, "chan") == 0) {
        node->type = NODE_CHAN_INIT;
        node->data.chan_init.channel_type = NULL;
        node->data.chan_init.capacity = 0; // Unbounded por padrão

        conc_advance(parser); // Consome 'chan'

        if (parser->current_token.type == TOKEN_LT) {
            conc_advance(parser); // Consome '<'
            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                node->data.chan_init.channel_type = strdup(parser->current_token.lexeme);
                conc_advance(parser);
            }
            if (parser->current_token.type == TOKEN_GT) {
                conc_advance(parser); // Consome '>'
            }
        }

        // Verifica se o canal é bounded (ex: chan<i32>(1))
        if (parser->current_token.type == TOKEN_LPAREN) {
            conc_advance(parser); // Consome '('
            if (parser->current_token.type == TOKEN_LIT_INT) {
                node->data.chan_init.capacity = atoi(parser->current_token.lexeme);
                conc_advance(parser);
            }
            if (parser->current_token.type == TOKEN_RPAREN) {
                conc_advance(parser); // Consome ')'
            }
        }
    }
    // Caso 2: Semáforos e Grupos de Espera -> wg := waiter
    else if (strcmp(parser->current_token.lexeme, "waiter") == 0) {
        node->type = NODE_CONCURRENT_OBJECT;
        node->data.sync_obj.obj_type = OBJ_WAITER;
        conc_advance(parser);
    }
    // Caso 3: Exclusão Mútua -> mtx := mutex
    else if (strcmp(parser->current_token.lexeme, "mutex") == 0) {
        node->type = NODE_CONCURRENT_OBJECT;
        node->data.sync_obj.obj_type = OBJ_MUTEX;
        conc_advance(parser);
    }

    return node;
}

// Limpeza segura de memória evitando ponteiros nulos nas falhas de parsing
void free_concurrent_ast_node(ConcurrentASTNode* node) {
    if (!node) return;

    if (node->type == NODE_AWAIT_EXPR) {
        if (node->data.await_expr.expression) {
            free_concurrent_ast_node(node->data.await_expr.expression);
        }
    } else if (node->type == NODE_WHEN_MODIFIER) {
        if (node->data.when_modifier.command) {
            free_concurrent_ast_node(node->data.when_modifier.command);
        }
        if (node->data.when_modifier.condition_lexeme) {
            free(node->data.when_modifier.condition_lexeme);
        }
    } else if (node->type == NODE_CHAN_INIT) {
        if (node->data.chan_init.channel_type) {
            free(node->data.chan_init.channel_type);
        }
    } else if (node->type == NODE_DEFER_BLOCK || node->type == NODE_ASYNC_DEFER_BLOCK) {
        if (node->data.defer_block.statements) {
            for (int i = 0; i < node->data.defer_block.statement_count; i++) {
                if (node->data.defer_block.statements[i]) {
                    free_concurrent_ast_node(node->data.defer_block.statements[i]);
                }
            }
            free(node->data.defer_block.statements);
        }
    }

    free(node);
}