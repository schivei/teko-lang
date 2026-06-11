#include "lexer_string.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Avanço local do ponteiro do lexer
static char str_peek(Lexer* lexer) { return lexer->source[lexer->cursor]; }
static char str_peek_next(Lexer* lexer) {
    if (lexer->source[lexer->cursor] == '\0') return '\0';
    return lexer->source[lexer->cursor + 1];
}
static char str_advance(Lexer* lexer) {
    char c = lexer->source[lexer->cursor];
    if (c != '\0') lexer->cursor++;
    return c;
}

// Analisa se há um sufixo bX (ex: b2, b3) ao final do delimitador de fechamento
static int parse_bracket_suffix(Lexer* lexer) {
    int arity = 1; // Aridade padrão se omitido
    int original_cursor = lexer->cursor;

    if (str_peek(lexer) == 'b') {
        str_advance(lexer); // Consome 'b'
        if (isdigit((unsigned char)str_peek(lexer))) {
            char num_buf[16] = {0};
            int idx = 0;
            while (isdigit((unsigned char)str_peek(lexer)) && idx < 15) {
                num_buf[idx++] = str_advance(lexer);
            }
            arity = atoi(num_buf);
        } else {
            // Se encontrou 'b' solto sem número, reverte o cursor
            lexer->cursor = original_cursor;
        }
    }
    return arity;
}

ExtendedStringToken lex_advanced_string(Lexer* lexer) {
    ExtendedStringToken token;
    token.type = TOKEN_STRING_LIT;
    token.bracket_arity = 1;
    token.is_multiline = false;
    token.is_raw = false;
    token.raw_content = NULL;

    int start_pos = lexer->cursor;

    // 1. Detecta o prefixo RAW (\$)
    if (str_peek(lexer) == '$') {
        token.is_raw = true;
        str_advance(lexer); // Consome '\$'
    }

    char delimiter = str_peek(lexer); // Pode ser '"' ou '`'
    bool is_interpolated = (delimiter == '`');

    if (delimiter != '"' && delimiter != '`') {
        token.type = TOKEN_UNKNOWN;
        return token;
    }
    str_advance(lexer); // Consome o delimitador inicial

    // 2. Detecta se é bloco Multilinha (Triplo delimitador: """ ou ```)
    if (str_peek(lexer) == delimiter && str_peek_next(lexer) == delimiter) {
        token.is_multiline = true;
        str_advance(lexer); // Consome o segundo
        str_advance(lexer); // Consome o terceiro
    }

    int content_start = lexer->cursor;

    // 3. Varre o corpo interno da string
    while (str_peek(lexer) != '\0') {
        char c = str_peek(lexer);

        if (c == '\n') {
            lexer->line++;
        }

        // Trata escapes com backslash (\), mas APENAS se não for uma RAW string ($)
        if (c == '\\' && !token.is_raw) {
            str_advance(lexer); // Consome '\\'
            if (str_peek(lexer) != '\0') {
                str_advance(lexer); // Consome o caractere escapado (ex: \n, \", \\)
            }
            continue;
        }

        // Verifica o fechamento do bloco
        if (token.is_multiline) {
            if (c == delimiter && lexer->source[lexer->cursor + 1] == delimiter && lexer->source[lexer->cursor + 2] == delimiter) {
                int content_len = lexer->cursor - content_start;
                token.raw_content = (char*)malloc(content_len + 1);
                strncpy(token.raw_content, &lexer->source[content_start], content_len);
                token.raw_content[content_len] = '\0';

                str_advance(lexer); // Consome 1
                str_advance(lexer); // Consome 2
                str_advance(lexer); // Consome 3
                break;
            }
        } else {
            if (c == delimiter) {
                int content_len = lexer->cursor - content_start;
                token.raw_content = (char*)malloc(content_len + 1);
                strncpy(token.raw_content, &lexer->source[content_start], content_len);
                token.raw_content[content_len] = '\0';

                str_advance(lexer); // Consome o delimitador de fechamento
                break;
            }
        }
        str_advance(lexer);
    }

    // 4. Se for interpolada, tenta extrair o sufixo opcional 'bX' (ex: b2) colado no fechamento
    if (is_interpolated) {
        token.bracket_arity = parse_bracket_suffix(lexer);
        token.type = token.is_raw ? TOKEN_STRING_RAW_INTERP : TOKEN_STRING_INTERPOLATED;
    } else {
        token.type = token.is_raw ? TOKEN_STRING_RAW_LIT : TOKEN_STRING_LIT;
    }

    return token;
}

void free_extended_string_token(ExtendedStringToken* token) {
    if (token && token->raw_content) {
        free(token->raw_content);
        token->raw_content = NULL;
    }
}