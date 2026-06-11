#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_teko_aot_darwin_arm64_pure_emission(void) {
    const char* asm_path = "output_darwin_arm64_test.s";

    TekoTarget target;
    target.arch = ARCH_APPLE_SILICON;
    target.os = OS_MACOS_DARWIN;
    strncpy(target.target_string, "aarch64-apple-darwin", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_full_program[] = {
        0x12,       // OP_CHAN_INIT
        0x30,       // OP_ARENA_PUSH
        0x10,       // OP_SPAWN_ASYNC
        0x31        // OP_ARENA_POP
    };

    teko_metal_emit_program(ctx, mock_full_program, sizeof(mock_full_program));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);

    char* buffer = (char*)malloc(4096);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0, 4096);

    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "Target: Apple Silicon"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "_main:"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "stp x29, x30, [sp, #-32]!"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "_pthread_create"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "sub sp, sp, #1024"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "ret"));

    free(buffer);
    remove(asm_path);
}

void test_teko_aot_darwin_x86_64_pure_emission(void) {
    const char* asm_path = "output_darwin_x86_test.s";

    TekoTarget target;
    target.arch = ARCH_X86_64;
    target.os = OS_MACOS_DARWIN;
    strncpy(target.target_string, "x86_64-apple-darwin", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_x86_program[] = {
        0x12, // OP_CHAN_INIT
        0x08, // OP_DIV
        0x10  // OP_SPAWN_ASYNC
    };

    teko_metal_emit_program(ctx, mock_x86_program, sizeof(mock_x86_program));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);

    char* buffer = (char*)malloc(4096);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0, 4096);

    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "Intel x86_64 (macOS)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "_main:"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "cltd"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "idivl %ebx"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call _pthread_create"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "popq %r12"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "ret"));

    free(buffer);
    remove(asm_path);
}
