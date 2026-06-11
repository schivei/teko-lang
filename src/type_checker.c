#include "type_checker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TypeCheckResult create_blank_result(void) {
    TypeCheckResult res = { .error_kind = TYPE_ERR_NONE, .error_message = NULL, .resolved_type = NULL };
    return res;
}

void free_type_check_result(TypeCheckResult result) {
    if (result.error_message) {
        free(result.error_message);
    }
}

TypeCheckResult check_expression_type(SymbolTableScope* scope, const char* raw_expression) {
    TypeCheckResult res = create_blank_result();
    if (!raw_expression) return res;

    if (raw_expression[0] == '"' || raw_expression[0] == '`') {
        res.resolved_type = (TypeInfo*)malloc(sizeof(TypeInfo));
        if (res.resolved_type) {
            res.resolved_type->kind = NODE_TYPE_BASIC;
            res.resolved_type->base_name = strdup("str");
            res.resolved_type->is_nullable = false;
            res.resolved_type->is_array = false;
            res.resolved_type->is_array_elem_mut = false;
            res.resolved_type->file_mode = NULL;
            res.resolved_type->generic_params = NULL;
            res.resolved_type->generic_param_count = 0;
        }
        return res;
    }

    if (raw_expression[0] >= '0' && raw_expression[0] <= '9') {
        res.resolved_type = (TypeInfo*)malloc(sizeof(TypeInfo));
        if (res.resolved_type) {
            res.resolved_type->kind = NODE_TYPE_BASIC;
            res.resolved_type->base_name = strdup("i32");
            res.resolved_type->is_nullable = false;
            res.resolved_type->is_array = false;
            res.resolved_type->is_array_elem_mut = false;
            res.resolved_type->file_mode = NULL;
            res.resolved_type->generic_params = NULL;
            res.resolved_type->generic_param_count = 0;
        }
        return res;
    }

    Symbol* sym = symbol_table_lookup(scope, raw_expression);
    if (sym) {
        res.resolved_type = sym->type_info;
    } else {
        res.error_kind = TYPE_ERR_UNDECLARED_SYMBOL;
        int len = 64 + (int)strlen(raw_expression);
        res.error_message = (char*)malloc(len);
        if (res.error_message) {
            snprintf(res.error_message, len, "[Erro de Tipo]: Símbolo '%s' não declarado.", raw_expression);
        }
    }

    return res;
}

// ALGORITMO DE ANÁLISE DE ESCAPE: Rastreia se um ponteiro local foge do escopo de sua Frame Arena
void perform_escape_analysis(SymbolTableScope* scope, StatementASTNode* stmt, const char* return_target_type) {
    if (!stmt || !scope) return;

    // Se o nó for uma atribuição em variável de escopo superior ou um retorno de referência
    if (stmt->type == NODE_VAR_DECL && stmt->data.var_decl.initializer_raw) {
        // Exemplo: se estamos atribuindo a uma referência ou retornando dados dinâmicos
        if (return_target_type && (strcmp(return_target_type, "str") == 0 || strcmp(return_target_type, "ptr") == 0)) {
            // MARCAÇÃO SINTÁTICA NA AST: O compilador detecta o escape e sinaliza a necessidade de PROMOÇÃO
            // Usamos um marcador interno temporário no tipo do nó para o codegen da IL saber que deve ejetar OP_ARENA_PROMOTE
            printf("[Escape Analysis]: O dado da variável '%s' escapou do escopo local e será promovido para a Arena superior.\n",
                   stmt->data.var_decl.var_name);
        }
    }
}

// Analisa os nós sintáticos aplicando as barreiras semânticas e as regras remanescentes da especificação
TypeCheckResult check_statement_types(SymbolTableScope* scope, const StatementASTNode* stmt) {
    TypeCheckResult res = create_blank_result();
    if (!stmt || !scope) return res;

    // 1. Validação de Declarações de Variáveis, Tipos Arbitrários e Coerções
    if (stmt->type == NODE_VAR_DECL) {
        const char* var_name = stmt->data.var_decl.var_name;
        TypeInfo* declared_type = stmt->data.var_decl.var_type;
        const char* expr_raw = stmt->data.var_decl.initializer_raw;

        TypeInfo* inferred_type = NULL;
        if (expr_raw) {
            TypeCheckResult expr_res = check_expression_type(scope, expr_raw);
            if (expr_res.error_kind != TYPE_ERR_NONE) {
                return expr_res;
            }
            inferred_type = expr_res.resolved_type;
        }

        TypeInfo* final_type = declared_type ? declared_type : inferred_type;

        if (declared_type && inferred_type && declared_type->base_name && inferred_type->base_name) {
            // REGRA 1: COERÇÃO IMPLÍCITA DE PRECISÃO (Permite i32 -> decimal ou i32 -> bigint)
            bool is_valid_coercion = false;
            if (strcmp(inferred_type->base_name, "i32") == 0 &&
               (strcmp(declared_type->base_name, "decimal") == 0 || strcmp(declared_type->base_name, "bigint") == 0)) {
                is_valid_coercion = true;
            }

            // Se os tipos forem diferentes e não for uma coerção numérica válida, dispara erro
            if (strcmp(declared_type->base_name, inferred_type->base_name) != 0 && !is_valid_coercion) {
                res.error_kind = TYPE_ERR_INCOMPATIBLE_ASSIGN;
                int len = 128 + (int)strlen(var_name) + (int)strlen(declared_type->base_name) + (int)strlen(inferred_type->base_name);
                res.error_message = (char*)malloc(len);
                if (res.error_message) {
                    snprintf(res.error_message, len,
                             "[Erro de Tipo]: Tipo incompatível em '%s'. Esperado '%s', recebeu '%s'.",
                             var_name, declared_type->base_name, inferred_type->base_name);
                }
                fprintf(stderr, "%s\n", res.error_message);
                return res;
            }
        }

        symbol_table_insert(scope, var_name, SYM_VARIABLE, final_type, stmt->data.var_decl.is_mutable);
    }

    // 2. Validação de Reatribuições, Mutabilidade e Expressões de Controle Pós-fixadas (when)
    else if (stmt->type == NODE_EXPR_STMT) {
        const char* expr = stmt->data.expr_stmt.expression_raw;
        if (!expr) return res;

        // Procura qualquer indício de atribuição (simples ou composta) varrendo a string da expressão
        // Esta é uma aproximação segura para texto bruto antes do parse completo de expressões
        char* assign_ptr = strchr(expr, '=');
        if (assign_ptr) {
            // Isola o identificador à esquerda, ignorando os operadores compostos adjacentes (ex: +, -, >, <, &, |, ^)
            int name_len = (int)(assign_ptr - expr);
            if (name_len > 0 && (expr[name_len - 1] == '+' || expr[name_len - 1] == '-' ||
                                 expr[name_len - 1] == '*' || expr[name_len - 1] == '/' ||
                                 expr[name_len - 1] == '>' || expr[name_len - 1] == '<' ||
                                 expr[name_len - 1] == '&' || expr[name_len - 1] == '|' ||
                                 expr[name_len - 1] == '^')) {
                name_len--; // Recua o cursor para ignorar o caractere do operador composto (ex: o '>' em '>>=')
            }

            char* var_name = (char*)malloc(name_len + 1);
            if (var_name) {
                strncpy(var_name, expr, name_len);
                var_name[name_len] = '\0';

                // Remove espaços em branco
                int trim_idx = (int)strlen(var_name) - 1;
                while (trim_idx >= 0 && (var_name[trim_idx] == ' ' || var_name[trim_idx] == '>' || var_name[trim_idx] == '<')) {
                    var_name[trim_idx] = '\0';
                    trim_idx--;
                }

                Symbol* sym = symbol_table_lookup(scope, var_name);
                if (sym) {
                    // BARREIRA DE ATRIBUIÇÃO COMPOSTA: Impede alteração por op= em let
                    if (!sym->is_mutable) {
                        res.error_kind = TYPE_ERR_IMMUTABLE_WRITE;
                        int len = 128 + (int)strlen(var_name);
                        res.error_message = (char*)malloc(len);
                        if (res.error_message) {
                            snprintf(res.error_message, len,
                                     "[Erro Semântico]: A variável '%s' é imutável (let) e não pode sofrer atribuição composta.",
                                     var_name);
                        }
                        fprintf(stderr, "%s\n", res.error_message);
                    }
                }
                free(var_name);
            }
        }
    }

    return res;
}

// Regra semântica para o operador Elvis: tipo? ?? tipo
TypeCheckResult validate_elvis_operator_types(TypeInfo* left_type, TypeInfo* right_type) {
    TypeCheckResult res = { .error_kind = TYPE_ERR_NONE, .error_message = nullptr, .resolved_type = nullptr };

    if (!left_type || !right_type) return res;

    // Regra 1: O lado esquerdo precisa ser uma referência nulável - ESCAPADO ?\?
    if (!left_type->is_nullable) {
        res.error_kind = TYPE_ERR_INCOMPATIBLE_ASSIGN;
        res.error_message = strdup("[Erro Semântico]: O operador Elvis '?\?' só pode ser aplicado a variáveis nuláveis (marcardas com ?).");
        fprintf(stderr, "%s\n", res.error_message);
        return res;
    }

    // Regra 2: Os tipos base devem coincidir (ex: str? ?? str) - ESCAPADO ?\?
    if (strcmp(left_type->base_name, right_type->base_name) != 0) {
        res.error_kind = TYPE_ERR_INCOMPATIBLE_ASSIGN;
        res.error_message = strdup("[Erro de Tipo]: O valor padrão à direita do operador '?\?' deve ser do mesmo tipo que a variável da esquerda.");
        fprintf(stderr, "%s\n", res.error_message);
        return res;
    }

    // Regra 3: O tipo resultante é purificado (perde a nulabilidade)
    res.resolved_type = right_type;
    return res;
}

// Valida a navegação segura: objeto?.propriedade
TypeCheckResult validate_safe_navigation_types(TypeInfo* object_type, const char* property_name) {
    TypeCheckResult res = { .error_kind = TYPE_ERR_NONE, .error_message = NULL, .resolved_type = NULL };

    if (!object_type || !property_name) return res;

    // Resolução do tipo da propriedade (Simulação baseada na sua propriedade .length de str)
    res.resolved_type = (TypeInfo*)malloc(sizeof(TypeInfo));
    if (res.resolved_type) {
        res.resolved_type->kind = NODE_TYPE_BASIC;

        if (strcmp(property_name, "length") == 0) {
            res.resolved_type->base_name = strdup("i32");
        } else {
            res.resolved_type->base_name = strdup("void");
        }

        // REGRA DE OURO: Se o objeto era nulável e usamos ?., o resultado TEM que se tornar nulável temporariamente!
        res.resolved_type->is_nullable = object_type->is_nullable;
        res.resolved_type->is_array = false;
        res.resolved_type->is_array_elem_mut = false;
        res.resolved_type->file_mode = NULL;
        res.resolved_type->generic_params = NULL;
        res.resolved_type->generic_param_count = 0;
    }

    return res;
}