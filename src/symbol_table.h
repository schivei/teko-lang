#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <stdbool.h>
#include "parser_types.h"

typedef enum {
    SYM_VARIABLE,
    SYM_CONSTANT,
    SYM_FUNCTION
} SymbolKind;

// Estrutura para representar uma entrada individual na tabela
typedef struct Symbol {
    char* name;
    SymbolKind kind;
    TypeInfo* type_info;
    bool is_mutable;           // Controla se foi declarada com let (false) ou mut (true)
    struct Symbol* next;       // Encadeamento para tratamento de colisões (Chaining)
} Symbol;

// Estrutura que representa um nível de escopo (Hierarquia Aninhada)
typedef struct SymbolTableScope {
    Symbol** buckets;
    int size;
    struct SymbolTableScope* parent_scope; // Aponta para o escopo pai (Escopo Superior)
} SymbolTableScope;

// Assinaturas públicas da Tabela de Símbolos
SymbolTableScope* symbol_table_create_scope(SymbolTableScope* parent);
bool symbol_table_insert(SymbolTableScope* scope, const char* name, SymbolKind kind, TypeInfo* type, bool is_mutable);
Symbol* symbol_table_lookup(SymbolTableScope* scope, const char* name);
Symbol* symbol_table_lookup_current_scope(SymbolTableScope* scope, const char* name);
void symbol_table_free_scope(SymbolTableScope* scope);

#endif // SYMBOL_TABLE_H