#include "unity.h"
#include "../src/symbol_table.h"
#include "../src/semantic_struct.h"
#include <string.h>
#include <stdlib.h>

// Valida escopos aninhados, redeclarações e a regra de mutabilidade baseada em let/mut
void test_symbol_table_scopes_and_mutability(void) {
    // 1. Cria escopo global e tenta inserir símbolos
    SymbolTableScope* global = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(global);

    bool s1 = symbol_table_insert(global, "MY_CONST", SYM_CONSTANT, NULL, false);
    TEST_ASSERT_TRUE(s1);

    // Impede redeclaração no mesmo nível
    bool s2 = symbol_table_insert(global, "MY_CONST", SYM_VARIABLE, NULL, true);
    TEST_ASSERT_FALSE(s2);

    // 2. Cria escopo local e valida sombreamento de escopo (Shadowing) válido
    SymbolTableScope* local = symbol_table_create_scope(global);
    bool s3 = symbol_table_insert(local, "idx", SYM_VARIABLE, NULL, true); // mut idx
    TEST_ASSERT_TRUE(s3);

    Symbol* lookup_idx = symbol_table_lookup(local, "idx");
    TEST_ASSERT_NOT_NULL(lookup_idx);
    TEST_ASSERT_TRUE(lookup_idx->is_mutable); // Valida se o compilador lembra que é 'mut'

    Symbol* lookup_const = symbol_table_lookup(local, "MY_CONST");
    TEST_ASSERT_NOT_NULL(lookup_const);
    TEST_ASSERT_FALSE(lookup_const->is_mutable); // Valida se encontrou subindo no escopo global e viu que é imutável

    symbol_table_free_scope(local);
    symbol_table_free_scope(global);
}

// Valida a aplicação do contrato de campos obrigatórios (required)
void test_required_properties_validation(void) {
    MessageProperty defined[1];
    defined[0].prop_name = "message";
    defined[0].is_required = true;
    defined[0].is_mutable = false;
    defined[0].prop_type = NULL;

    // Cenário Válido: Inicializou o campo obrigatório
    const char* init_good[] = {"message"};
    StructValidationResult res_good = validate_required_properties(defined, 1, init_good, 1);
    TEST_ASSERT_EQUAL_INT(STRUCT_ERR_NONE, res_good.error_type);
    free_struct_validation_result(res_good);

    // Cenário Inválido: Esqueceu o campo obrigatório
    const char* init_bad[] = {"wrong_field"};
    StructValidationResult res_bad = validate_required_properties(defined, 1, init_bad, 1);
    TEST_ASSERT_EQUAL_INT(STRUCT_ERR_MISSING_REQUIRED, res_bad.error_type);
    TEST_ASSERT_NOT_NULL(res_bad.error_message);
    free_struct_validation_result(res_bad);
}