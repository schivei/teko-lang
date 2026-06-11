#ifndef PARSER_ASYNC_CONTROL_H
#define PARSER_ASYNC_CONTROL_H

#include "parser.h"

// Nós específicos de controle assíncrono e desvio de fluxo na AST
typedef enum {
    NODE_WHEN_EXPR = 900,
    NODE_INLINE_SWITCH,
    NODE_STATEMENT_SWITCH,
    NODE_SWITCH_ARM,
    NODE_RAISED_CATCH
} AsyncControlASTNodeType;

// Estrutura para os braços do inline switch (ex: null => 1;)
typedef struct SwitchArmNode {
    char* pattern_lexeme;       // Ex: "null", "_"
    char* condition_raw;        // Conteúdo pós 'when' se houver (ex: "exs.name == \"World\"")
    struct AsyncControlASTNode* value_expr; // Expressão resultante (pode conter await)
} SwitchArmNode;

// Estrutura para os blocos de caso do switch padrão (ex: null => { ... })
typedef struct SwitchCaseNode {
    char* condition_pattern;      // Ex: "null", "_", ou valores literais
    char* case_body_raw;          // Instruções dentro do caso
} SwitchCaseNode;

// Nó principal de controle assíncrono para a AST
typedef struct AsyncControlASTNode {
    int type; // AsyncControlASTNodeType
    union {
        // Dados para modificador pós-fixado: <cmd> when <cond>
        struct {
            char* command_raw;
            char* condition_raw;
        } when_expr;

        // Dados para inline switch: expression switch { ... }
        struct {
            char* target_expression_raw;
            SwitchArmNode* arms;
            int arm_count;
        } inline_switch;

        // Dados para interceptador de erros: ... raised (err) { ... }
        struct {
            struct AsyncControlASTNode* protected_block;
            char* error_variable_name; // Ex: "err"
            char* catch_body_raw;
        } raised_catch;

        // Dados para switch comum/padrão
        struct {
            char* control_expression_raw; // Expressão avaliada, ex: "(exs)"
            SwitchCaseNode* cases;
            int case_count;
        } statement_switch;
    } data;
} AsyncControlASTNode;

// Assinaturas públicas do Parser de Controle Assíncrono
AsyncControlASTNode* parse_async_control_statement(Parser* parser);
void free_async_control_ast_node(AsyncControlASTNode* node);

#endif // PARSER_ASYNC_CONTROL_H