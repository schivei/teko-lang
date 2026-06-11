#include "parser_extensions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ext_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Analisa os membros internos do bloco extend (métodos e operadores inline)
static void parse_extension_members(Parser* parser, ExtensionASTNode* ext_node) {
    int cap = 4;
    ext_node->members = (ExtensionMemberNode*)malloc(sizeof(ExtensionMemberNode) * cap);
    ext_node->member_count = 0;

    while (parser->current_token.type != TOKEN_RBRACE && parser->current_token.type != TOKEN_EOF) {
        if (ext_node->member_count >= cap) {
            cap *= 2;
            ext_node->members = (ExtensionMemberNode*)realloc(ext_node->members, sizeof(ExtensionMemberNode) * cap);
        }

        ExtensionMemberNode* member = &ext_node->members[ext_node->member_count];
        member->name = NULL;
        member->return_type = NULL;
        member->param_name = NULL;
        member->param_type = NULL;
        member->is_inline = false;
        member->body_raw = NULL;

        // Caso 1: Sobrecarga de Operador -> operator +(string: str) : str => ...
        if (parser->current_token.type == TOKEN_OPERATOR) {
            member->type = NODE_EXTENSION_OPERATOR;
            ext_advance(parser); // Consome 'operator'

            // Captura o lexema do operador (ex: "+", "-", etc.)
            member->name = strdup(parser->current_token.lexeme);
            ext_advance(parser);

            // Processa o parâmetro único do operador
            if (parser->current_token.type == TOKEN_LPAREN) {
                ext_advance(parser); // Consome '('
                if (parser->current_token.type == TOKEN_IDENTIFIER) {
                    member->param_name = strdup(parser->current_token.lexeme);
                    ext_advance(parser);
                }
                if (parser->current_token.type == TOKEN_COLON) {
                    ext_advance(parser); // Consome ':'
                    member->param_type = parse_complete_type_info(parser);
                }
                if (parser->current_token.type == TOKEN_RPAREN) ext_advance(parser); // Consome ')'
            }
        }
        // Caso 2: Método de Extensão Normal -> append_line(string: str) : str { ... }
        else if (parser->current_token.type == TOKEN_IDENTIFIER) {
            member->type = NODE_EXTENSION_METHOD;
            member->name = strdup(parser->current_token.lexeme);
            ext_advance(parser); // Consome o nome do método

            // Pula os parâmetros do método de forma genérica para focar na estrutura
            if (parser->current_token.type == TOKEN_LPAREN) {
                ext_advance(parser);
                while (parser->current_token.type != TOKEN_RPAREN && parser->current_token.type != TOKEN_EOF) {
                    ext_advance(parser);
                }
                if (parser->current_token.type == TOKEN_RPAREN) ext_advance(parser);
            }
        } else {
            ext_advance(parser);
            continue;
        }

        // Captura o tipo de retorno comum a ambos: ': str'
        if (parser->current_token.type == TOKEN_COLON) {
            ext_advance(parser); // Consome ':'
            member->return_type = parse_complete_type_info(parser);
        }

        // Determina se o corpo é Inline (=>) ou Bloco ({})
        if (parser->current_token.type == TOKEN_ARROW) {
            member->is_inline = true;
            ext_advance(parser); // Consome '=>'

            // Captura tudo até o ponto e vírgula como expressão literal
            int expr_start = parser->lexer->cursor;
            while (parser->current_token.type != TOKEN_SEMICOLON && parser->current_token.type != TOKEN_EOF) {
                ext_advance(parser);
            }
            int expr_len = parser->lexer->cursor - expr_start;
            member->body_raw = (char*)malloc(expr_len + 1);
            strncpy(member->body_raw, &parser->lexer->source[expr_start], expr_len);
            member->body_raw[expr_len] = '\0';

            if (parser->current_token.type == TOKEN_SEMICOLON) ext_advance(parser);
        }
        else if (parser->current_token.type == TOKEN_LBRACE) {
            member->is_inline = false;
            ext_advance(parser); // Consome '{'

            // Captura o bloco respeitando o aninhamento de chaves
            int block_start = parser->lexer->cursor;
            int depth = 1;
            while (depth > 0 && parser->current_token.type != TOKEN_EOF) {
                if (parser->current_token.type == TOKEN_LBRACE) depth++;
                if (parser->current_token.type == TOKEN_RBRACE) depth--;
                ext_advance(parser);
            }
            int block_len = (parser->lexer->cursor - block_start) - 1; // ignora a chave de fechamento
            member->body_raw = (char*)malloc(block_len + 1);
            strncpy(member->body_raw, &parser->lexer->source[block_start], block_len);
            member->body_raw[block_len] = '\0';
        }

        ext_node->member_count++;
    }
}

// Ponto de entrada: extend(self: string) { ... }
ExtensionASTNode* parse_type_extension(Parser* parser) {
    ext_advance(parser); // Consome 'extend'

    ExtensionASTNode* node = (ExtensionASTNode*)malloc(sizeof(ExtensionASTNode));
    node->type = NODE_TYPE_EXTENSION;
    node->self_param_name = NULL;
    node->self_type = NULL;
    node->members = NULL;
    node->member_count = 0;

    // Processa a assinatura de amarração do receptor: (self: string)
    if (parser->current_token.type == TOKEN_LPAREN) {
        ext_advance(parser); // Consome '('
        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            node->self_param_name = strdup(parser->current_token.lexeme);
            ext_advance(parser); // Consome 'self'
        }
        if (parser->current_token.type == TOKEN_COLON) {
            ext_advance(parser); // Consome ':'
            node->self_type = parse_complete_type_info(parser); // Identifica "string"
        }
        if (parser->current_token.type == TOKEN_RPAREN) ext_advance(parser); // Consome ')'
    }

    // Processa o corpo contendo as definições das extensões
    if (parser->current_token.type == TOKEN_LBRACE) {
        ext_advance(parser); // Consome '{'
        parse_extension_members(parser, node);
        if (parser->current_token.type == TOKEN_RBRACE) ext_advance(parser); // Consome '}'
    }

    return node;
}

// Liberação de memória recursiva e segura
void free_extension_ast_node(ExtensionASTNode* node) {
    if (!node) return;
    if (node->self_param_name) free(node->self_param_name);
    if (node->self_type) free_type_info(node->self_type);

    if (node->members) {
        for (int i = 0; i < node->member_count; i++) {
            if (node->members[i].name) free(node->members[i].name);
            if (node->members[i].return_type) free_type_info(node->members[i].return_type);
            if (node->members[i].param_name) free(node->members[i].param_name);
            if (node->members[i].param_type) free_type_info(node->members[i].param_type);
            if (node->members[i].body_raw) free(node->members[i].body_raw);
        }
        free(node->members);
    }
    free(node);
}