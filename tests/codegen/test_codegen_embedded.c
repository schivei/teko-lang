#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ====================================================================
// 1. NATIVE WEBASSEMBLY ENGINE TEST (WASM / WAT BARE-METAL)
// ====================================================================
void test_teko_aot_wasm_pure_emission_integrity(void) {
    const char* asm_path = "output_wasm_test.wat";

    TekoTarget target;
    target.arch = ARCH_WASM32; // Configures the WASM virtual processor
    target.os = OS_WASI;      // Abstract OS for Web/Servers
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // Injects the contiguous sequence of our ISA: Constant Load -> Channel -> Halt
    unsigned char mock_wasm_bytes[] = {
        0x01, 0x2A, 0x00, 0x00, 0x00, // OP_ICONST 42 (5 bytes)
        0x12                          // OP_CHAN_INIT (1 byte)
    };

    teko_metal_emit_program(ctx, mock_wasm_bytes, sizeof(mock_wasm_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);

    char* buffer = (char*)malloc(4096);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0, 4096);

    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    // Structured assertions validating the fidelity of the WebAssembly S-Expression grammar
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(module"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.const 42"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(memory 1)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(export \"main\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "Hello Teko"));

    free(buffer);
    remove(asm_path);
}

// ====================================================================
// 2. WASM ARENA ALLOCATOR + IN-MODULE CHANNELS + SPAWN/AWAIT HOOKS
// ====================================================================
// The O(1) arena is real linear-memory bump code. Phase 10.1: channels are now
// real in-module ring buffers in linear memory (no host import); only spawn and
// await still delegate to the host runtime (cooperative scheduler is Phase 10.2).
void test_teko_aot_wasm_arena_and_concurrency_hooks(void) {
    const char* asm_path = "output_wasm_arena_test.wat";

    TekoTarget target;
    target.arch = ARCH_WASM32;
    target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // ARENA_PUSH, ARENA_POP, SPAWN_ASYNC, CHAN_INIT, CHAN_PUT, AWAIT_INTENT, HALT
    unsigned char mock[] = { 0x30, 0x31, 0x10, 0x12, 0x13, 0x11, 0x00 };
    teko_metal_emit_program(ctx, mock, sizeof(mock));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(8192);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0, 8192);
    size_t bytes = fread(buffer, 1, 8191, file);
    buffer[bytes] = '\0';
    fclose(file);

    // Real arena allocator: a mutable global bumped by 1024-byte frames.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(global $arena_sp (mut i32)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "global.set $arena_sp"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.const 1024"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.add"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.sub"));

    // Phase 10.1: channels are real in-module ring buffers (no host import).
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.store offset=8"));   // CHAN_INIT writes cap
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.store offset=12"));  // CHAN_PUT writes buf[tail]
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.rem_u"));            // CHAN_PUT tail wrap (mod cap)
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.store offset=4"));   // CHAN_PUT advances tail
    // The channel host imports are gone (channels are in-module now).
    TEST_ASSERT_NULL(strstr(buffer, "call $teko_chan_init"));
    TEST_ASSERT_NULL(strstr(buffer, "call $teko_chan_put"));
    TEST_ASSERT_NULL(strstr(buffer, "\"chan_init\""));

    // Spawn / await still delegate to honest host-runtime hooks (Phase 10.2).
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(import \"teko_rt\" \"spawn\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call $teko_spawn"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call $teko_await"));

    free(buffer);
    remove(asm_path);
}
