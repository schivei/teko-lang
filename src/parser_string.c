#include "parser_string.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Função auxiliar para contar ocorrências seguidas de um determinado caractere
static int count_consecutive_chars(const char* str, int start, char target) {
    int count = 0;
    while (str[start + count] == target) {
        count++;
    }
    return count;
}

// Desestruturador recursivo de blocos interpolados usando aridade de chaves (bX)
static void desubstitute_interpolation(ExtendedStringToken* ext_token, StringASTNode* node) {
    const char* text = ext_token->raw_content;
    if (!text) return;

    int len = (int)strlen(text);
    int target_arity = ext_token->bracket_arity;

    int cap = 4;
    node->data.interpolated_string.parts = (StringPartNode*)malloc(sizeof(StringPartNode) * cap);
    node->data.interpolated_string.part_count = 0;
    node->data.interpolated_string.bracket_arity = target_arity;

    int cursor = 0;
    int static_start = 0;

    while (cursor < len) {
        // Procura chaves de abertura '{'
        if (text[cursor] == '{') {
            int open_count = count_consecutive_chars(text, cursor, '{');

            // Valida se a quantidade de chaves atende exatamente o critério da aridade bX
            if (open_count >= target_arity) {
                // Se havia texto estático antes deste bloco, salva o fragmento estático
                if (cursor > static_start) {
                    if (node->data.interpolated_string.part_count >= cap) {
                        cap *= 2;
                        node->data.interpolated_string.parts = (StringPartNode*)realloc(
                            node->data.interpolated_string.parts, sizeof(StringPartNode) * cap);
                    }
                    int part_len = cursor - static_start;
                    char* part_text = (char*)malloc(part_len + 1);
                    strncpy(part_text, &text[static_start], part_len);
                    part_text[part_len] = '\0';

                    node->data.interpolated_string.parts[node->data.interpolated_string.part_count].type = NODE_STRING_PART_STATIC;
                    node->data.interpolated_string.parts[node->data.interpolated_string.part_count].content = part_text;
                    node->data.interpolated_string.part_count++;
                }

                // Ajusta o cursor pulando o excesso de chaves externas se valerem as mais internas
                // Exemplo: `{"{{a}}": "{{{B}}}"}`b2 -> se aridade é 2 e abriram 3 chaves, 1 é tratada como caractere estático
                int extra_brackets = open_count - target_arity;
                if (extra_brackets > 0) {
                    // Adiciona a chave sobressalente como texto estático
                    if (node->data.interpolated_string.part_count >= cap) {
                        cap *= 2;
                        node->data.interpolated_string.parts = (StringPartNode*)realloc(
                            node->data.interpolated_string.parts, sizeof(StringPartNode) * cap);
                    }
                    char* extra_text = (char*)malloc(extra_brackets + 1);
                    memset(extra_text, '{', extra_brackets);
                    extra_text[extra_brackets] = '\0';

                    node->data.interpolated_string.parts[node->data.interpolated_string.part_count].type = NODE_STRING_PART_STATIC;
                    node->data.interpolated_string.parts[node->data.interpolated_string.part_count].content = extra_text;
                    node->data.interpolated_string.part_count++;
                }

                cursor += open_count; // Move para dentro da expressão lógica
                int expr_start = cursor;

                // Varre procurando o fechamento correspondente '}'
                while (cursor < len) {
                    if (text[cursor] == '}') {
                        int close_count = count_consecutive_chars(text, cursor, '}');
                        if (close_count >= target_arity) {
                            int expr_len = cursor - expr_start;
                            char* expr_text = (char*)malloc(expr_len + 1);
                            strncpy(expr_text, &text[expr_start], expr_len);
                            expr_text[expr_len] = '\0';

                            // Adiciona a expressão lógica à lista de partes
                            if (node->data.interpolated_string.part_count >= cap) {
                                cap *= 2;
                                node->data.interpolated_string.parts = (StringPartNode*)realloc(
                                    node->data.interpolated_string.parts, sizeof(StringPartNode) * cap);
                            }
                            node->data.interpolated_string.parts[node->data.interpolated_string.part_count].type = NODE_STRING_PART_EXPRESSION;
                            node->data.interpolated_string.parts[node->data.interpolated_string.part_count].content = expr_text;
                            node->data.interpolated_string.part_count++;

                            cursor += close_count;
                            static_start = cursor; // Redefine início do próximo fragmento estático
                            break;
                        }
                    }
                    cursor++;
                }
                continue;
            }
        }
        cursor++;
    }

    // Salva o bloco final estático se restou algum texto após a última interpolação
    if (cursor > static_start) {
        if (node->data.interpolated_string.part_count >= cap) {
            cap *= 2;
            node->data.interpolated_string.parts = (StringPartNode*)realloc(
                node->data.interpolated_string.parts, sizeof(StringPartNode) * cap);
        }
        int part_len = cursor - static_start;
        char* part_text = (char*)malloc(part_len + 1);
        strncpy(part_text, &text[static_start], part_len);
        part_text[part_len] = '\0';

        node->data.interpolated_string.parts[node->data.interpolated_string.part_count].type = NODE_STRING_PART_STATIC;
        node->data.interpolated_string.parts[node->data.interpolated_string.part_count].content = part_text;
        node->data.interpolated_string.part_count++;
    }
}

// Analisa os metadados do token estendido e retorna o nó correto para a AST
StringASTNode* parse_advanced_string_expr(Parser* parser, ExtendedStringToken* ext_token) {
    if (!ext_token) return NULL;

    StringASTNode* node = (StringASTNode*)malloc(sizeof(StringASTNode));
    node->is_raw = ext_token->is_raw;
    node->is_multiline = ext_token->is_multiline;

    if (ext_token->type == TOKEN_STRING_LIT || ext_token->type == TOKEN_STRING_RAW_LIT) {
        node->type = NODE_STRING_LITERAL_EXPR;
        node->data.static_string.value = ext_token->raw_content ? strdup(ext_token->raw_content) : strdup("");
    }
    else if (ext_token->type == TOKEN_STRING_INTERPOLATED || ext_token->type == TOKEN_STRING_RAW_INTERP) {
        node->type = NODE_STRING_INTERPOLATED_EXPR;
        node->data.interpolated_string.parts = NULL;
        node->data.interpolated_string.part_count = 0;

        // Dispara o algoritmo de desestruturação lendo a aridade de chaves
        desubstitute_interpolation(ext_token, node);
    }

    return node;
}

// Liberação recursiva e segura contra vazamento de memória heap nas strings
void free_string_ast_node(StringASTNode* node) {
    if (!node) return;

    if (node->type == NODE_STRING_LITERAL_EXPR) {
        if (node->data.static_string.value) {
            free(node->data.static_string.value);
        }
    }
    else if (node->type == NODE_STRING_INTERPOLATED_EXPR) {
        if (node->data.interpolated_string.parts) {
            for (int i = 0; i < node->data.interpolated_string.part_count; i++) {
                if (node->data.interpolated_string.parts[i].content) {
                    free(node->data.interpolated_string.parts[i].content);
                }
            }
            free(node->data.interpolated_string.parts);
        }
    }
    free(node);
}