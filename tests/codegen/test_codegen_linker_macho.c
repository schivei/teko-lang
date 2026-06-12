#include "unity.h"
#include "codegen/tld_macho.h"
#include <stdio.h>
#include <stdlib.h>

void test_teko_linker_macho_64_binary_signature_integrity(void) {
    const char* filename = "output_macho_linker_test_arm";
    uint8_t mock_macho_opcodes[] = { 0x90, 0xC3 }; // NOP, RET

    // 1. HOMOLOGAÇÃO APPLE SILICON (ARM64)
    bool success_arm = tld_macho_write_executable(filename, mock_macho_opcodes, sizeof(mock_macho_opcodes), CPU_TYPE_ARM64);
    TEST_ASSERT_TRUE(success_arm);

    FILE* f = fopen(filename, "rb");
    TEST_ASSERT_NOT_NULL(f);
    uint32_t magic_read = 0;
    fread(&magic_read, 4, 1, f);
    fclose(f);

    TEST_ASSERT_EQUAL_HEX32(MH_MAGIC_64, magic_read);
    remove(filename);

    // 2. HOMOLOGAÇÃO MAC INTEL (x86_64)
    const char* filename_intel = "output_macho_linker_test_intel";
    bool success_intel = tld_macho_write_executable(filename_intel, mock_macho_opcodes, sizeof(mock_macho_opcodes), CPU_TYPE_X86_64);
    TEST_ASSERT_TRUE(success_intel);

    FILE* f_intel = fopen(filename_intel, "rb");
    TEST_ASSERT_NOT_NULL(f_intel);
    uint32_t magic_read_intel = 0;
    fread(&magic_read_intel, 4, 1, f_intel);
    fclose(f_intel);

    TEST_ASSERT_EQUAL_HEX32(MH_MAGIC_64, magic_read_intel);
    remove(filename_intel);
}
