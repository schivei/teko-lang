#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

// Tipo do nó da AST
typedef enum {
    NODE_PROGRAM,
    NODE_USE_STMT,
    /* Adicione novos nós aqui conforme expandir o parser */
} ASTNodeType;

// Estrutura genérica para nós da AST
typedef struct ASTNode {
    ASTNodeType type;
    union {
        // Dados para o nó 'use' (ex: use my::namespace; ou use dep from "str")
        struct {
            char* path;
            char* from_source; // Preenchido se houver 'from "..."'
        } use_stmt;

        // Bloco de programa contendo múltiplos nós filhos
        struct {
            struct ASTNode** children;
            int child_count;
        } program;
    } data;
} ASTNode;

// Estado do Parser
typedef struct {
    Lexer* lexer;
    Token current_token;
    Token peek_token;
    bool is_stdlib_compilation;
} Parser;

// Funções públicas do Parser
void parser_init(Parser* parser, Lexer* lexer);
ASTNode* parser_parse_program(Parser* parser);
ASTNode* parse_use_statement(Parser* parser);
void free_ast_node(ASTNode* node);

#endif // PARSER_H