#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 1. TESTE LINUX INTEL 64-BIT
void test_teko_aot_linux_x86_64_pure_emission(void) {
    const char* asm_path = "output_linux_test.s";
    TekoTarget target = { .arch = ARCH_X86_64, .os = OS_LINUX, .target_string = "x86_64-unknown-linux-gnu" };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_linux_program[] = { 0x10, 0x12, 0x08 }; // Spawn -> Channel -> Div
    teko_metal_emit_program(ctx, mock_linux_program, sizeof(mock_linux_program));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "64-bit Linux"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "main:"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call pthread_create"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "cltd"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "idivl %ebx"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "popq %r12"));

    free(buffer);
    remove(asm_path);
}

// 2. TESTE LINUX INTEL 32-BIT
void test_teko_aot_linux_x86_32_pure_emission(void) {
    const char* asm_path = "output_linux_x86_test.s";
    TekoTarget target = { .arch = ARCH_X86, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_linux32_bytes[] = { 0x10, 0x00 }; // Spawn -> Halt
    teko_metal_emit_program(ctx, mock_linux32_bytes, sizeof(mock_linux32_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "Intel x86 (32-bit Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "pushl %edi"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call pthread_create"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "int $0x80"));

    free(buffer);
    remove(asm_path);
}

// 3. TESTE LINUX RISC-V 64-BIT
void test_teko_aot_linux_riscv64_pure_emission(void) {
    const char* asm_path = "output_linux_rv64_test.s";
    TekoTarget target = { .arch = ARCH_RISCV64, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_rv64_bytes[] = { 0x30, 0x08, 0x00 }; // Arena Push -> Div -> Halt
    teko_metal_emit_program(ctx, mock_rv64_bytes, sizeof(mock_rv64_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "RISC-V 64-bit (Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "sd ra, 24(sp)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "addi sp, sp, -1024"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "ecall"));

    free(buffer);
    remove(asm_path);
}

// 4. TESTE LINUX RISC-V 32-BIT
void test_teko_aot_linux_riscv32_pure_emission(void) {
    const char* asm_path = "output_linux_rv32_test.s";
    TekoTarget target = { .arch = ARCH_RISCV32, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_rv32_bytes[] = { 0x01, 0x0A, 0x00, 0x00, 0x00, 0x30, 0x00 }; // Iconst 10 -> Arena Push -> Halt
    teko_metal_emit_program(ctx, mock_rv32_bytes, sizeof(mock_rv32_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "RISC-V 32-bit (Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "sw ra, 12(sp)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "li a0, 10"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "lw ra, 12(sp)"));

    free(buffer);
    remove(asm_path);
}

// 5. TESTE LINUX POWERPC 64-BIT
void test_teko_aot_linux_ppc64_pure_emission(void) {
    const char* asm_path = "output_linux_ppc64_test.s";
    TekoTarget target = { .arch = ARCH_PPC64, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_ppc64_bytes[] = { 0x01, 0x4D, 0x00, 0x00, 0x00, 0x07, 0x00 }; // Iconst 77 -> Mul -> Halt
    teko_metal_emit_program(ctx, mock_ppc64_bytes, sizeof(mock_ppc64_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "PowerPC 64-bit (Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "stdu 1, -48(1)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "sc"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "blr"));

    free(buffer);
    remove(asm_path);
}

// 6. TESTE LINUX MIPS
void test_teko_aot_linux_mips_pure_emission(void) {
    const char* asm_path = "output_linux_mips_test.s";
    TekoTarget target = { .arch = ARCH_MIPS64, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_mips_bytes[] = {
        0x01,
        0x1E, 0x00, 0x00, 0x00,
        0x08,
        0x00
    }; // Iconst 30 -> Div -> Halt
    teko_metal_emit_program(ctx, mock_mips_bytes, sizeof(mock_mips_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "MIPS 32/64-bit (Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "addiu $sp, $sp, -32"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "div $v0, $v1"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "nop"));

    free(buffer);
    remove(asm_path);
}

// 7. TESTE LINUX ARM64
void test_teko_aot_linux_arm64_pure_emission(void) {
    const char* asm_path = "output_linux_arm64_test.s";
    TekoTarget target = { .arch = ARCH_ARM64, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_linux_arm64_bytes[] = { 0x12, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Chan -> Sconst 0 -> Halt
    teko_metal_emit_program(ctx, mock_linux_arm64_bytes, sizeof(mock_linux_arm64_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "ARM64 (64-bit Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "stp x29, x30, [sp, #-32]!"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "adrp x0, .L_linux_str_"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "svc #0"));

    free(buffer);
    remove(asm_path);
}

// 8. TESTE LINUX ARM32
void test_teko_aot_linux_arm32_pure_emission(void) {
    const char* asm_path = "output_linux_arm32_test.s";
    TekoTarget target = { .arch = ARCH_ARM32, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_linux_arm32_bytes[] = { 0x12, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };
    teko_metal_emit_program(ctx, mock_linux_arm32_bytes, sizeof(mock_linux_arm32_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "ARM32 (32-bit Linux ARMv7)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "push {r4, fp, lr}"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "ldr r0, =.L_linux_str_"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "svc 0"));

    free(buffer);
    remove(asm_path);
}
