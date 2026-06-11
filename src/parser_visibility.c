#include "parser_visibility.h"
#include "parser_statements.h"
#include "parser_di.h"
#include "parser_ffi.h"
#include <stdio.h>
#include <stdlib.h>

static void vis_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Analisa e envelopa declarações globais aplicando as restrições de visibilidade
VisibilityASTNode* parse_global_declaration_with_visibility(Parser* parser) {
    VisibilityKind current_vis = VIS_INTERNAL;

    // 1. Intercepta e consome os modificadores explícitos se presentes
    if (parser->current_token.type == TOKEN_PUB) {
        current_vis = VIS_PROJECT_PUB;
        vis_advance(parser);
    } else if (parser->current_token.type == TOKEN_EXP) {
        current_vis = VIS_EXPORTED_EXP;
        vis_advance(parser);
    }

    // 2. Alocação tardia baseada em variáveis de controle para o nó envelopado
    void* inner_node = NULL;
    int inner_type = 0;

    // Captura e roteia de acordo com a estrutura subsequente
    if (parser->current_token.type == TOKEN_STRUCT) {
        // Se veio de um bloco extern, o FFI já gerencia. Se for estrutural pura:
        inner_node = parse_extern_declaration(parser); // Reutiliza mapeamento de bloco estrutural
        if (inner_node) inner_type = ((FFIASTNode*)inner_node)->type;
    }
    else if (parser->current_token.type == TOKEN_FN ||
            (parser->current_token.type == TOKEN_ASYNC && parser->peek_token.type == TOKEN_FN)) {
        inner_node = parse_statement(parser);
        if (inner_node) inner_type = ((StatementASTNode*)inner_node)->type;
    }
    else if (parser->current_token.type == TOKEN_INTERFACE) {
        // Passa falso no parâmetro legado de exportação já que o controle unificado assume agora
        inner_node = parse_di_interface(parser, (current_vis == VIS_EXPORTED_EXP));
        if (inner_node) inner_type = ((DIASTNode*)inner_node)->type;
    }
    else if (parser->current_token.type == TOKEN_SERVICE) {
        // Bloqueio de Convenção/Regra: Métodos e blocos de serviços não podem ser exportados (exp)
        if (current_vis == VIS_EXPORTED_EXP) {
            fprintf(stderr, "[Erro Sintático] Linha %d: Serviços não suportam o modificador 'exp'. Métodos não podem ser exportados.\n",
                    parser->current_token.line);
            return NULL;
        }
        inner_node = parse_di_service(parser);
        if (inner_node) inner_type = ((DIASTNode*)inner_node)->type;
    }

    // Se nenhuma estrutura válida foi interceptada após o modificador, aborta sem vazamento
    if (!inner_node) {
        return NULL;
    }

    // 3. Monta o nó estrutural de visibilidade unificado
    VisibilityASTNode* vis_node = (VisibilityASTNode*)malloc(sizeof(VisibilityASTNode));
    if (!vis_node) {
        // Fallback de segurança contra vazamento das estruturas internas caso o Heap falhe
        if (inner_type == NODE_FUNC_DECL || inner_type == NODE_VAR_DECL || inner_type == NODE_FOR_LOOP || inner_type == NODE_EXPR_STMT) {
            free_statement_ast_node((StatementASTNode*)inner_node);
        } else if (inner_type == NODE_DI_INTERFACE || inner_type == NODE_DI_SERVICE) {
            free_di_ast_node((DIASTNode*)inner_node);
        } else {
            free_ffi_ast_node((FFIASTNode*)inner_node);
        }
        return NULL;
    }

    vis_node->type = NODE_VISIBLE_DECL;
    vis_node->visibility = current_vis;
    vis_node->decorated_node = inner_node;
    vis_node->decorated_node_type = inner_type;

    return vis_node;
}

// Desalocação em cascata segura e tipada baseada no discriminator de nós internos
void free_visibility_ast_node(VisibilityASTNode* node) {
    if (!node) return;

    if (node->decorated_node) {
        int t = node->decorated_node_type;

        // Desvia para o destruidor correto baseado no enum do nó decorado
        if (t == NODE_FUNC_DECL || t == NODE_VAR_DECL || t == NODE_FOR_LOOP || t == NODE_EXPR_STMT) {
            free_statement_ast_node((StatementASTNode*)node->decorated_node);
        }
        else if (t == NODE_DI_INTERFACE || t == NODE_DI_SERVICE || t == NODE_DI_DECORATOR) {
            free_di_ast_node((DIASTNode*)node->decorated_node);
        }
        else if (t == NODE_FFI_STRUCT || t == NODE_FFI_FUNCTION || t == NODE_FFI_BLOCK || t == NODE_FFI_INLINE_C) {
            free_ffi_ast_node((FFIASTNode*)node->decorated_node);
        }
    }

    free(node);
}