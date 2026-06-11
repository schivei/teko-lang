#ifndef PARSER_STRING_H
#define PARSER_STRING_H

#include "parser.h"
#include "lexer_string.h"

// Nós específicos para o motor de strings avançado na AST
typedef enum {
    NODE_STRING_LITERAL_EXPR = 610, // String estática pura
    NODE_STRING_INTERPOLATED_EXPR,  // String interpolada contendo blocos textuais e lógicos
    NODE_STRING_PART_STATIC,        // Parte estática da interpolação
    NODE_STRING_PART_EXPRESSION     // Expressão interna (ex: variável ou constante)
} StringASTNodeType;

// Representa um fragmento de uma string interpolada
typedef struct StringPartNode {
    int type; // NODE_STRING_PART_STATIC ou NODE_STRING_PART_EXPRESSION
    char* content; // Texto estático bruto ou lexema da expressão a ser resolvida
} StringPartNode;

// Nó principal de string na AST
typedef struct StringASTNode {
    int type; // StringASTNodeType
    bool is_raw;
    bool is_multiline;
    union {
        // Para strings literais estáticas puras (normais ou raw)
        struct {
            char* value;
        } static_string;

        // Para strings interpoladas desestruturadas
        struct {
            StringPartNode* parts;
            int part_count;
            int bracket_arity; // Aridade 'X' do sufixo bX
        } interpolated_string;
    } data;
} StringASTNode;

// Assinaturas públicas do Parser de Strings
StringASTNode* parse_advanced_string_expr(Parser* parser, ExtendedStringToken* ext_token);
void free_string_ast_node(StringASTNode* node);

#endif // PARSER_STRING_H