#include "parser_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void types_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Inicializa uma estrutura TypeInfo limpa
static TypeInfo* create_empty_type_info() {
    const auto info = (TypeInfo*)malloc(sizeof(TypeInfo));
    info->kind = NODE_TYPE_BASIC;
    info->base_name = NULL;
    info->is_nullable = false;
    info->is_array = false;
    info->is_array_elem_mut = false;
    info->generic_params = NULL;
    info->generic_param_count = 0;
    return info;
}

// Analisa recursivamente qualquer tipo ou identificador da linguagem expandindo o @
TypeInfo* parse_complete_type_info(Parser* parser) {
    TypeInfo* info = create_empty_type_info();
    if (!info) return NULL;

    // 1. Verifica modificador 'mut' para arrays
    if (parser->current_token.type == TOKEN_MUT) {
        info->is_array_elem_mut = true;
        types_advance(parser);
    }

    // 2. Captura e expande o nome base se for uma macro/namespace virtual @
    if (parser->current_token.type == TOKEN_MACRO_IDENT) {
        // Se iniciar com '@', extrai o fragmento após o '@' e adiciona o prefixo 'teko::'
        const char* raw_lexeme = parser->current_token.lexeme;

        if (raw_lexeme[0] == '@') {
            // Calcula o espaço: "teko::" (6 chars) + resto do lexeme (strlen - 1) + null terminator (1)
            int expanded_len = 6 + (int)strlen(raw_lexeme) - 1;
            info->base_name = (char*)malloc(expanded_len + 1);
            if (info->base_name) {
                strcpy(info->base_name, "teko::");
                strcat(info->base_name, &raw_lexeme[1]); // Pula o caractere '@'
            }
        } else {
            info->base_name = strdup(raw_lexeme);
        }

        info->kind = NODE_TYPE_GENERIC; // Tratado como caminho qualificado de macro/tipo
        types_advance(parser);
    }
    else if (parser->current_token.type == TOKEN_IDENTIFIER) {
        info->base_name = strdup(parser->current_token.lexeme);

        if (strcmp(info->base_name, "decimal") == 0) info->kind = NODE_TYPE_DECIMAL;
        else if (strcmp(info->base_name, "bigint") == 0) info->kind = NODE_TYPE_BIGINT;
        else if (strcmp(info->base_name, "arena") == 0) info->kind = NODE_TYPE_ARENA;
        else if (strcmp(info->base_name, "func") == 0) info->kind = NODE_TYPE_FUNC;

        types_advance(parser);
    } else {
        info->base_name = strdup("void");
    }

    // 3. Processa Parâmetros Genéricos <T, U>
    if (parser->current_token.type == TOKEN_LT) {
        if (info->kind != NODE_TYPE_FUNC) {
            info->kind = NODE_TYPE_GENERIC;
        }
        types_advance(parser);

        int cap = 4;
        info->generic_params = (TypeInfo**)malloc(sizeof(TypeInfo*) * cap);

        while (parser->current_token.type != TOKEN_GT && parser->current_token.type != TOKEN_EOF) {
            if (info->generic_param_count >= cap) {
                cap *= 2;
                info->generic_params = (TypeInfo**)realloc(info->generic_params, sizeof(TypeInfo*) * cap);
            }

            info->generic_params[info->generic_param_count++] = parse_complete_type_info(parser);

            if (parser->current_token.type == TOKEN_COMMA) {
                types_advance(parser);
            }
        }

        if (parser->current_token.type == TOKEN_GT) {
            types_advance(parser);
        }
    }

    // 4. Trata Nulabilidade
    if (parser->current_token.type == TOKEN_QUESTION) {
        info->is_nullable = true;
        types_advance(parser);
    }

    // 5. Trata Arrays
    if (parser->current_token.type == TOKEN_LBRACKET) {
        types_advance(parser); // Consome '['

        // Se houver um identificador de modo dentro (ex: 'a')
        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            // Guarda o modo (ex: "a") reaproveitando uma string na estrutura se necessário,
            // ou apenas avança validando a sintaxe para o escopo atual.
            types_advance(parser);
        }

        if (parser->current_token.type == TOKEN_RBRACKET) {
            info->is_array = true;
            types_advance(parser); // Consome ']'
        } else {
            fprintf(stderr, "[Erro Sintático] Linha %d: Esperado ']' para fechar o descriptor de tipo.\n", parser->current_token.line);
        }
    }

    return info;
}

// Analisa literais numéricos isolados
ArbitraryTypeASTNode* parse_arbitrary_numeric_literal(Parser* parser) {
    const auto node = (ArbitraryTypeASTNode*)malloc(sizeof(ArbitraryTypeASTNode));
    node->type = NODE_ARBITRARY_LITERAL;
    node->data.numeric_literal.raw_lexeme = NULL;
    node->data.numeric_literal.is_floating_point = false;

    if (parser->current_token.type == TOKEN_LIT_INT || parser->current_token.type == TOKEN_LIT_FLOAT) {
        node->data.numeric_literal.raw_lexeme = strdup(parser->current_token.lexeme);
        node->data.numeric_literal.is_floating_point = (parser->current_token.type == TOKEN_LIT_FLOAT);
        types_advance(parser);
    }
    return node;
}

// Liberação recursiva e segura de TypeInfo
void free_type_info(TypeInfo* info) {
    if (!info) return;
    if (info->base_name) free(info->base_name);
    if (info->generic_params) {
        for (int i = 0; i < info->generic_param_count; i++) {
            free_type_info(info->generic_params[i]);
        }
        free(info->generic_params);
    }
    free(info);
}

void free_arbitrary_type_ast_node(ArbitraryTypeASTNode* node) {
    if (!node) return;

    // Verificação exata do símbolo do enum
    if (node->type == NODE_ARBITRARY_LITERAL) {
        if (node->data.numeric_literal.raw_lexeme) {
            free(node->data.numeric_literal.raw_lexeme);
        }
    } else {
        if (node->data.type_decl.type_info) {
            free_type_info(node->data.type_decl.type_info);
        }
        if (node->data.type_decl.constraint_target) {
            free(node->data.type_decl.constraint_target);
        }
        if (node->data.type_decl.constraint_bound) {
            free(node->data.type_decl.constraint_bound);
        }
    }
    free(node);
}
