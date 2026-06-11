#ifndef PARSER_STATEMENTS_H
#define PARSER_STATEMENTS_H

#include "parser.h"
#include "parser_types.h"

// Nós para declarações e instruções restantes na AST
typedef enum {
    NODE_ELVIS_EXPR = 950,
    NODE_FUNC_DECL = 1000,
    NODE_VAR_DECL,
    NODE_FOR_LOOP,
    NODE_EXPR_STMT,
    NODE_BLOCK_STMT
} StatementASTNodeType;

// Estrutura para representar parâmetros de funções locais
typedef struct FuncParamNode {
    char* param_name;
    TypeInfo* param_type;
} FuncParamNode;

// Nó unificado de instruções para a AST
typedef struct StatementASTNode {
    int type; // StatementASTNodeType
    union {
        // Dados para declaração de funções: [async] fn nome(...) : tipo { ... }
        struct {
            char* fn_name;
            bool is_async;
            FuncParamNode* params;
            int param_count;
            TypeInfo* return_type;
            struct StatementASTNode** body_statements;
            int body_count;
        } func_decl;

        // Dados para: let/mut nome: tipo = expressão;
        struct {
            char* var_name;
            bool is_mutable;
            TypeInfo* var_type; // Pode ser NULL se houver inferência
            char* initializer_raw;
        } var_decl;

        // Dados para: for (mut i: i32; i < len; i++) { ... }
        struct {
            struct StatementASTNode* init_stmt;
            char* condition_raw;
            char* increment_raw;
            struct StatementASTNode** body_statements;
            int body_count;
        } for_loop;

        // Dados para expressões isoladas (Chamadas de métodos, macros @, atribuições simples)
        struct {
            char* expression_raw;
        } expr_stmt;
    } data;
} StatementASTNode;

// Assinaturas públicas do Parser de Declarações e Instruções
StatementASTNode* parse_function_declaration(Parser* parser, bool is_async);
StatementASTNode* parse_variable_declaration(Parser* parser);
StatementASTNode* parse_for_loop(Parser* parser);
StatementASTNode* parse_statement(Parser* parser);
void free_statement_ast_node(StatementASTNode* node);

#endif // PARSER_STATEMENTS_H