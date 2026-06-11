#ifndef PARSER_EXTENSIONS_H
#define PARSER_EXTENSIONS_H

#include "parser.h"
#include "parser_types.h"

// Nós específicos para extensões e operadores inline na AST
typedef enum {
    NODE_TYPE_EXTENSION = 700,
    NODE_EXTENSION_METHOD,
    NODE_EXTENSION_OPERATOR
} ExtensionASTNodeType;

// Estrutura para descrever um método ou operador dentro do extend
typedef struct ExtensionMemberNode {
    int type;                // NODE_EXTENSION_METHOD ou NODE_EXTENSION_OPERATOR
    char* name;              // Nome do método ou o símbolo do operador (ex: "+")
    TypeInfo* return_type;   // Tipo de retorno
    char* param_name;        // Nome do parâmetro (para operadores)
    TypeInfo* param_type;    // Tipo do parâmetro (para operadores)
    bool is_inline;          // true se definido com '=>'
    char* body_raw;          // Conteúdo do corpo ou expressão inline para o gerador de código
} ExtensionMemberNode;

// Nó principal do bloco extend na AST
typedef struct ExtensionASTNode {
    int type;                // NODE_TYPE_EXTENSION
    char* self_param_name;   // Ex: "self"
    TypeInfo* self_type;     // O tipo que está sendo estendido (ex: "string" ou "str")
    ExtensionMemberNode* members;
    int member_count;
} ExtensionASTNode;

// Assinaturas públicas do Parser de Extensões
ExtensionASTNode* parse_type_extension(Parser* parser);
void free_extension_ast_node(ExtensionASTNode* node);

#endif // PARSER_EXTENSIONS_H