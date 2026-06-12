#include "unity.h"
#include "codegen/tld_symbols.h"
#include <stdlib.h>
#include <string.h>

void test_tld_static_dependency_injection_and_symbol_resolution(void) {
    TekoSymbolTable table;
    tld_symbols_init(&table);

    // Simula um buffer de código bruto onde o byte 4 representa os 4 bytes formais vazios de uma instrução 'call'
    uint8_t mock_code_buffer[32];
    memset(mock_code_buffer, 0x90, sizeof(mock_code_buffer)); // Inicia preenchido com NOPs

    // 1. DEFINE OS SÍMBOLOS: A função de injeção '@flows.notify' começa fisicamente no offset de byte 20
    bool def_ok = tld_symbols_define(&table, "@flows.notify", SYM_SERVICE, 20);
    TEST_ASSERT_TRUE(def_ok);

    // 2. REGISTRA A INVOCAÇÃO: No byte 4 do main, nós invocamos a dependência '@flows.notify'
    tld_symbols_reference(&table, "@flows.notify", 4);

    // Executa a resolução global simulando o endereço base padrão do Linux ELF64 (0x400000)
    bool resolve_ok = tld_symbols_resolve_and_inject(&table, mock_code_buffer, 0x400000);
    TEST_ASSERT_TRUE(resolve_ok);

    // 3. VALIDAÇÃO MATEMÁTICA DA INJEÇÃO:
    // Distância em bytes = Destino_Local (20) - (Patch_Local (4) + 4 bytes do operando) = 20 - 8 = 12 bytes!
    TEST_ASSERT_EQUAL_HEX8(12, mock_code_buffer[4]);
    TEST_ASSERT_EQUAL_HEX8(0,  mock_code_buffer[5]);
    TEST_ASSERT_EQUAL_HEX8(0,  mock_code_buffer[6]);
    TEST_ASSERT_EQUAL_HEX8(0,  mock_code_buffer[7]);
}
