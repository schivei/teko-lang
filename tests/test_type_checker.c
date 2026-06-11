#include "unity.h"
#include "type_checker.h"
#include "symbol_table.h"
#include <stdlib.h>
#include <string.h>

// Teste 1: Valida se o Type Checker impede coerção inválida (i32 recebendo texto)
void test_type_compatibility_assignment(void) {
    SymbolTableScope* scope = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(scope);

    StatementASTNode var_stmt;
    var_stmt.type = NODE_VAR_DECL;
    var_stmt.data.var_decl.is_mutable = false;
    var_stmt.data.var_decl.var_name = strdup("age");
    var_stmt.data.var_decl.initializer_raw = strdup("\"vinte anos\"");
    var_stmt.data.var_decl.var_type = NULL;

    TypeInfo type_i32;
    type_i32.kind = NODE_TYPE_BASIC;
    type_i32.base_name = strdup("i32");
    var_stmt.data.var_decl.var_type = &type_i32;

    TypeCheckResult res = check_statement_types(scope, &var_stmt);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_INCOMPATIBLE_ASSIGN, res.error_kind);
    TEST_ASSERT_NOT_NULL(res.error_message);

    free_type_check_result(res);
    free(var_stmt.data.var_decl.var_name);
    free(var_stmt.data.var_decl.initializer_raw);
    free(type_i32.base_name);
    symbol_table_free_scope(scope);
}

// Teste 2: Valida o travamento de escrita em variáveis imutáveis (let)
void test_let_immutability_protection(void) {
    SymbolTableScope* scope = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(scope);

    TypeInfo type_i32;
    type_i32.kind = NODE_TYPE_BASIC;
    type_i32.base_name = strdup("i32");
    symbol_table_insert(scope, "idx", SYM_VARIABLE, &type_i32, false);

    StatementASTNode expr_stmt;
    expr_stmt.type = NODE_EXPR_STMT;
    expr_stmt.data.expr_stmt.expression_raw = strdup("idx = 1");

    TypeCheckResult res = check_statement_types(scope, &expr_stmt);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_IMMUTABLE_WRITE, res.error_kind);
    TEST_ASSERT_NOT_NULL(res.error_message);

    free_type_check_result(res);
    free(expr_stmt.data.expr_stmt.expression_raw);
    free(type_i32.base_name);
    symbol_table_free_scope(scope);
}

// Teste 3: Valida se o Type Checker rastreia strings que escapam e emite o aviso de promoção automática
void test_escape_analysis_region_promotion(void) {
    SymbolTableScope* scope = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(scope);

    StatementASTNode stmt;
    stmt.type = NODE_VAR_DECL;
    stmt.data.var_decl.is_mutable = false;
    stmt.data.var_decl.var_name = strdup("escaped_string");
    stmt.data.var_decl.initializer_raw = strdup("`resultado`");
    stmt.data.var_decl.var_type = NULL;

    perform_escape_analysis(scope, &stmt, "str");

    TypeCheckResult res = check_statement_types(scope, &stmt);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_NONE, res.error_kind);

    free_type_check_result(res);
    free(stmt.data.var_decl.var_name);
    free(stmt.data.var_decl.initializer_raw);
    symbol_table_free_scope(scope);
}

// Teste 4: Valida se literais numéricos comuns i32 podem ser promovidos para decimal/bigint implicitamente
void test_implicit_precision_coercion_for_arbitrary_types(void) {
    SymbolTableScope* scope = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(scope);

    StatementASTNode var_stmt;
    var_stmt.type = NODE_VAR_DECL;
    var_stmt.data.var_decl.is_mutable = false;
    var_stmt.data.var_decl.var_name = strdup("saldo");
    var_stmt.data.var_decl.initializer_raw = strdup("3000"); // Literal interpretado como i32 básico

    // Tipo alvo declarado como 'decimal' de alta precisão
    TypeInfo type_dec;
    type_dec.kind = NODE_TYPE_DECIMAL;
    type_dec.base_name = strdup("decimal");
    var_stmt.data.var_decl.var_type = &type_dec;

    // O Type Checker deve aceitar a atribuição aplicando a regra de coerção numérica implícita
    TypeCheckResult res = check_statement_types(scope, &var_stmt);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_NONE, res.error_kind);

    free_type_check_result(res);
    free(var_stmt.data.var_decl.var_name);
    free(var_stmt.data.var_decl.initializer_raw);
    free(type_dec.base_name);
    symbol_table_free_scope(scope);
}

// Teste 5: Valida que atribuições compostas (ex: >>=) em variáveis let são bloqueadas pelo Type Checker
void test_composite_assignment_immutability_protection(void) {
    SymbolTableScope* scope = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(scope);

    TypeInfo type_i32;
    type_i32.kind = NODE_TYPE_BASIC;
    type_i32.base_name = strdup("i32");
    symbol_table_insert(scope, "arr_idx", SYM_VARIABLE, &type_i32, false); // Imutável

    // Tenta uma operação de deslocamento de bits com atribuição composta: arr_idx >>= 1
    StatementASTNode expr_stmt;
    expr_stmt.type = NODE_EXPR_STMT;
    expr_stmt.data.expr_stmt.expression_raw = strdup("arr_idx >>= 1");

    TypeCheckResult res = check_statement_types(scope, &expr_stmt);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_IMMUTABLE_WRITE, res.error_kind);
    TEST_ASSERT_NOT_NULL(res.error_message);

    free_type_check_result(res);
    free(expr_stmt.data.expr_stmt.expression_raw);
    free(type_i32.base_name);
    symbol_table_free_scope(scope);
}

// Teste 6: Cobertura Semântica total do Operador Elvis (??)
void test_elvis_operator_coalescence_semantics(void) {
    // Tipo Esquerdo: str? (Nulável)
    TypeInfo left_str_nullable;
    left_str_nullable.kind = NODE_TYPE_BASIC;
    left_str_nullable.base_name = strdup("str");
    left_str_nullable.is_nullable = true;

    // Tipo Direito: str (Não-nulável, valor padrão)
    TypeInfo right_str;
    right_str.kind = NODE_TYPE_BASIC;
    right_str.base_name = strdup("str");
    right_str.is_nullable = false;

    // Cenário Válido: str? ?? str -> Deve retornar o tipo base "str" limpo
    TypeCheckResult res_good = validate_elvis_operator_types(&left_str_nullable, &right_str);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_NONE, res_good.error_kind);
    TEST_ASSERT_EQUAL_STRING("str", res_good.resolved_type->base_name);
    TEST_ASSERT_FALSE(res_good.resolved_type->is_nullable);
    free_type_check_result(res_good);

    // Cenário Inválido: Tentar aplicar ?? em um tipo que já não é nulável (i32 ?? i32)
    TypeInfo non_nullable_i32;
    non_nullable_i32.kind = NODE_TYPE_BASIC;
    non_nullable_i32.base_name = strdup("i32");
    non_nullable_i32.is_nullable = false;

    TypeCheckResult res_bad = validate_elvis_operator_types(&non_nullable_i32, &non_nullable_i32);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_INCOMPATIBLE_ASSIGN, res_bad.error_kind);
    TEST_ASSERT_NOT_NULL(res_bad.error_message);

    // Limpezas
    free_type_check_result(res_bad);
    free(left_str_nullable.base_name);
    free(right_str.base_name);
    free(non_nullable_i32.base_name);
}

// Teste 7: Cobertura Semântica Avançada de Encadeamento Seguro com Elvis (s?.length ?? 0)
void test_safe_navigation_with_elvis_coalescence(void) {
    // 1. Instancia o tipo de 's' como str? (Nulável)
    TypeInfo object_str_nullable;
    object_str_nullable.kind = NODE_TYPE_BASIC;
    object_str_nullable.base_name = strdup("str");
    object_str_nullable.is_nullable = true;

    // 2. Executa a navegação segura s?.length -> Deve propagar para i32?
    TypeCheckResult navigation_res = validate_safe_navigation_types(&object_str_nullable, "length");
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_NONE, navigation_res.error_kind);
    TEST_ASSERT_TRUE(navigation_res.resolved_type->is_nullable); // Deve ser nulável (i32?)
    TEST_ASSERT_EQUAL_STRING("i32", navigation_res.resolved_type->base_name);

    // 3. Executa a coalescência nula com o valor padrão à direita: i32? ?? i32
    TypeInfo right_default_i32;
    right_default_i32.kind = NODE_TYPE_BASIC;
    right_default_i32.base_name = strdup("i32");
    right_default_i32.is_nullable = false;

    // Invoca o validador do Elvis que criamos anteriormente passando o resultado do ?. e o literal 0
    // Externo extern de validate_elvis_operator_types declarado
    extern TypeCheckResult validate_elvis_operator_types(TypeInfo* left_type, TypeInfo* right_type);
    TypeCheckResult final_res = validate_elvis_operator_types(navigation_res.resolved_type, &right_default_i32);

    // O tipo final resultante deve ser purificado de nulabilidade (i32 puro)
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_NONE, final_res.error_kind);
    TEST_ASSERT_EQUAL_STRING("i32", final_res.resolved_type->base_name);
    TEST_ASSERT_FALSE(final_res.resolved_type->is_nullable); // Alvo de produção atingido!

    // Limpezas de memória heap do teste
    free_type_check_result(final_res);
    free_type_info(navigation_res.resolved_type);
    free_type_check_result(navigation_res);
    free(object_str_nullable.base_name);
    free(right_default_i32.base_name);
}