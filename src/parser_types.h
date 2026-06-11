#ifndef PARSER_TYPES_H
#define PARSER_TYPES_H

#include "parser.h"

typedef enum {
    NODE_TYPE_BASIC = 300,   // i32, u8, str, char, etc.
    NODE_TYPE_DECIMAL,       // decimal de até 256 bits
    NODE_TYPE_BIGINT,        // bigint
    NODE_TYPE_ARENA,         // arena
    NODE_ARBITRARY_LITERAL,  // arbitrary decimal and bigint
    NODE_TYPE_GENERIC,       // Ex: intent<u8> ou list<mut dec>
    NODE_TYPE_FUNC           // Ex: func<i32, void>
} ArbitraryTypeASTNodeType;

// Estrutura que representa um tipo completo e complexo na AST
typedef struct TypeInfo {
    int kind;                 // ArbitraryTypeASTNodeType
    char* base_name;          // Nome base do tipo (ex: "intent", "i32", "map")
    bool is_nullable;         // true se tiver '?' (Elvis notation)
    bool is_array;            // true se tiver '[]'
    bool is_array_elem_mut;   // true se o elemento do array for 'mut' (ex: mut dec[])
    char* file_mode;

    // Parâmetros de tipos genéricos ou argumentos de func<>
    struct TypeInfo** generic_params;
    int generic_param_count;
} TypeInfo;

// Nó da AST para literais e declarações
typedef struct ArbitraryTypeASTNode {
    int type;
    union {
        struct {
            char* raw_lexeme;
            bool is_floating_point;
        } numeric_literal;

        struct {
            TypeInfo* type_info;
            char* constraint_target; // Para cláusulas 'where T : struct'
            char* constraint_bound;  // O tipo da restrição (ex: "struct")
        } type_decl;
    } data;
} ArbitraryTypeASTNode;

// Assinaturas públicas atualizadas
TypeInfo* parse_complete_type_info(Parser* parser);
ArbitraryTypeASTNode* parse_arbitrary_numeric_literal(Parser* parser);
void free_type_info(TypeInfo* info);
void free_arbitrary_type_ast_node(ArbitraryTypeASTNode* node);

#endif // PARSER_TYPES_H