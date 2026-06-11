#include "unity.h"
#include "vm_debug.h"
#include "vm_core.h"
#include <stdlib.h>

void test_vm_debugger_breakpoint_interception_and_dap(void) {
    auto dbg = teko_debugger_create();
    TEST_ASSERT_NOT_NULL(dbg);

    // 1. Alimenta o mapa de símbolos lineares: Linha 10 do código = IP 5 da VM
    teko_debugger_add_symbol(dbg, 10, 5);

    // 2. A IDE solicita um breakpoint na linha 10
    teko_debugger_set_breakpoint(dbg, 10);
    TEST_ASSERT_EQUAL_UINT32(1, dbg->breakpoint_count);
    TEST_ASSERT_EQUAL_UINT32(5, dbg->breakpoints[0]);

    // 3. Simula o avanço do IP da VM. No IP 0 não deve pausar
    bool p1 = teko_debugger_should_pause(dbg, 0);
    TEST_ASSERT_FALSE(p1);

    // No IP 5, deve bater no breakpoint e disparar o congelamento!
    bool p2 = teko_debugger_should_pause(dbg, 5);
    TEST_ASSERT_TRUE(p2);
    TEST_ASSERT_TRUE(dbg->is_paused);

    // 4. Simula o envio de uma mensagem JSON-RPC de "continue" vinda da IDE via DAP
    unsigned char mock_code[] = {0x00};
    auto vm = teko_vm_create(mock_code, sizeof(mock_code), NULL, 0);

    const char* dap_json = "{\"seq\": 2, \"type\": \"request\", \"command\": \"continue\"}";
    teko_debugger_handle_dap_message(dbg, dap_json, vm);

    // O Debugger deve liberar o laço e remover o modo de step
    TEST_ASSERT_FALSE(dbg->is_paused);
    TEST_ASSERT_FALSE(dbg->is_stepping);

    // Limpezas
    teko_vm_destroy(vm);
    teko_debugger_destroy(dbg);
}