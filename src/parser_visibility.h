#ifndef PARSER_VISIBILITY_H
#define PARSER_VISIBILITY_H

#include "parser.h"

// Níveis de visibilidade e encapsulamento da linguagem
typedef enum {
    VIS_INTERNAL = 0, // Visibilidade padrão (omissão)
    VIS_PROJECT_PUB,  // Modificador 'pub' (visível no projeto)
    VIS_EXPORTED_EXP  // Modificador 'exp' (exportado para fora)
} VisibilityKind;

// Nós específicos de declarações com escopo de visibilidade expandida na AST
typedef enum {
    NODE_VISIBLE_DECL = 1100
} VisibilityASTNodeType;

// Nó envoltório que associa um nível de visibilidade a qualquer declaração global
typedef struct VisibilityASTNode {
    int type;                  // NODE_VISIBLE_DECL
    VisibilityKind visibility; // KIND do modificador
    void* decorated_node;      // Aponta para o nó real (Nó de Struct, Função, Interface, etc.)
    int decorated_node_type;   // Guarda o tipo original do nó decorado para o Codegen
} VisibilityASTNode;

// Assinaturas públicas do Parser de Visibilidade
VisibilityASTNode* parse_global_declaration_with_visibility(Parser* parser);
void free_visibility_ast_node(VisibilityASTNode* node);

#endif // PARSER_VISIBILITY_H