#ifndef PARSER_CONCURRENT_H
#define PARSER_CONCURRENT_H

#include "parser.h"

// Nós específicos para concorrência na AST
typedef enum {
    NODE_ASYNC_FN = 200,   // Início do bloco assíncrono
    NODE_AWAIT_EXPR,       // Expressão await
    NODE_DEFER_BLOCK,      // Bloco defer comum
    NODE_ASYNC_DEFER_BLOCK,// Bloco async defer
    NODE_WHEN_MODIFIER,    // Comando condicional pós-fixado 'when'
    NODE_CHAN_INIT,        // Inicialização de canal: chan<T> ou chan<T>(cap)
    NODE_CONCURRENT_OBJECT // waiter ou mutex
} ConcurrentASTNodeType;

typedef enum {
    OBJ_WAITER,
    OBJ_MUTEX
} ConcurrentObjectType;

// Estrutura unificada de nós concorrentes para a AST
typedef struct ConcurrentASTNode {
    int type; // ConcurrentASTNodeType
    union {
        // Expressão await: await <expr>
        struct {
            struct ConcurrentASTNode* expression;
        } await_expr;

        // Modificador condicional pós-fixado: <cmd> when <condition>
        struct {
            struct ConcurrentASTNode* command;
            char* condition_lexeme;
        } when_modifier;

        // Inicialização de canal: chan<type>(capacity)
        struct {
            char* channel_type;
            int capacity; // 0 se for unbounded (sem capacidade definida)
        } chan_init;

        // Primitivos de sincronização: waiter ou mutex
        struct {
            ConcurrentObjectType obj_type;
        } sync_obj;

        // Blocos defer / async defer
        struct {
            struct ConcurrentASTNode** statements;
            int statement_count;
        } defer_block;
    } data;
} ConcurrentASTNode;

// Assinaturas das funções do Parser de Concorrência
ConcurrentASTNode* parse_await_expression(Parser* parser);
ConcurrentASTNode* parse_defer_statement(Parser* parser, bool is_async);
ConcurrentASTNode* parse_concurrency_assignment(Parser* parser);
void free_concurrent_ast_node(ConcurrentASTNode* node);

#endif // PARSER_CONCURRENT_H