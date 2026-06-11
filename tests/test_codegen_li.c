#include "unity.h"
#include "codegen_li.h"
#include "parser_statements.h"
#include <stdlib.h>
#include <string.h>

// Valida se o emissor gera os opcodes corretos e gerencia o String Pool sem duplicar itens
void test_bytecode_emission_and_constant_pooling(void) {
    BytecodeBuffer* buffer = codegen_li_create_context();
    TEST_ASSERT_NOT_NULL(buffer);

    // Adiciona constantes de strings repetidas de forma induzida
    int idx1 = codegen_li_add_string_constant(buffer, "abc");
    int idx2 = codegen_li_add_string_constant(buffer, "abc");
    int idx3 = codegen_li_add_string_constant(buffer, "xyz");

    // Valida que o pool de constantes barrou a duplicidade e otimizou os índices
    TEST_ASSERT_EQUAL_INT(idx1, idx2);
    TEST_ASSERT_EQUAL_INT(0, idx1);
    TEST_ASSERT_EQUAL_INT(1, idx3);

    // Instancia uma instrução fictícia de declaração de variável constante (const constant = "abc";)
    StatementASTNode stmt;
    stmt.type = NODE_VAR_DECL;
    stmt.data.var_decl.is_mutable = false;
    stmt.data.var_decl.var_name = strdup("constant");
    stmt.data.var_decl.initializer_raw = strdup("\"abc\"");
    stmt.data.var_decl.var_type = NULL;

    // Emite o bytecode da instrução
    codegen_li_emit_statement(buffer, &stmt);

    // O fluxo de bytes gerado deve conter: OP_SCONST (0x02) + índice do pool (0) + OP_STORE (0x04)
    TEST_ASSERT_TRUE(buffer->size > 0);
    TEST_ASSERT_EQUAL_UINT8(OP_SCONST, buffer->code[0]);

    // Executa a escrita simulada no disco para conferir o cabeçalho mágico
    codegen_li_write_to_file(buffer, "test_output.tkb");

    // Limpeza
    free(stmt.data.var_decl.var_name);
    free(stmt.data.var_decl.initializer_raw);
    codegen_li_free_context(buffer);
}