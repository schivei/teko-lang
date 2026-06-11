#ifndef PARSER_FFI_H
#define PARSER_FFI_H

#include "parser.h"

// Expandindo os tipos de nós da AST para suportar o FFI
typedef enum {
    NODE_FFI_STRUCT = 100, // Evita colisão com os anteriores
    NODE_FFI_FUNCTION,
    NODE_FFI_BLOCK,
    NODE_FFI_INLINE_C,
    NODE_STRUCT_FIELD,
    NODE_FN_PARAM
} FFIASTNodeType;

// Estrutura para os campos de uma Struct no FFI (ex: name: str;)
typedef struct FFIStructField {
    char* field_name;
    char* field_type;
} FFIStructField;

// Estrutura para os parâmetros de funções externas (ex: p: ptr<ExternalStructure>)
typedef struct FFIFnParam {
    char* param_name;
    char* param_type;
} FFIFnParam;

// Estrutura do nó de uma Função Externa
typedef struct FFIFunctionNode {
    char* fn_name;
    char* return_type;
    char* alias;             // Preenchido se houver 'as "Alias"'
    FFIFnParam* params;
    int param_count;
} FFIFunctionNode;

// Estrutura unificada de nós do FFI para a AST
typedef struct FFIASTNode {
    int type; // Recebe valores do enum FFIASTNodeType
    char* from_lib; // Caminho da biblioteca (ex: "my.dylib", "another.so")
    union {
        // Dados para: extern struct Name { ... }
        struct {
            char* struct_name;
            FFIStructField* fields;
            int field_count;
        } ffi_struct;

        // Dados para: extern fn name() : type
        FFIFunctionNode ffi_function;

        // Dados para: extern { fn1(); fn2(); }
        struct {
            struct FFIASTNode** functions;
            int function_count;
        } ffi_block;

        // Dados para: extern """ c_code """ as { functions }
        struct {
            char* c_code_block; // Guarda o texto bruto contido entre as triplas aspas
            struct FFIASTNode** declarations; // Mapeamento das assinaturas Teko dentro do 'as { ... }'
            int declaration_count;
        } ffi_inline_c;
    } data;
} FFIASTNode;

// Assinaturas das funções do Parser de FFI
FFIASTNode* parse_extern_declaration(Parser* parser);
void free_ffi_ast_node(FFIASTNode* node);

#endif // PARSER_FFI_H