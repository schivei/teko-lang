#ifndef LEXER_STRING_H
#define LEXER_STRING_H

#include "lexer.h"

// Estrutura que estende os metadados do token para apoiar o parser na desestruturação de interpolações
typedef struct ExtendedStringToken {
    int type;                // StringTokenType
    char* raw_content;       // Conteúdo textual completo extraído da string
    int bracket_arity;       // Quantidade 'X' do sufixo bX (padrão: 1)
    bool is_multiline;       // Indica se usou triplas aspas/backticks
    bool is_raw;             // Indica se iniciou com o prefixo '$'
} ExtendedStringToken;

// Assinaturas para acoplar no motor de varredura
ExtendedStringToken lex_advanced_string(Lexer* lexer);
void free_extended_string_token(ExtendedStringToken* token);

#endif // LEXER_STRING_H