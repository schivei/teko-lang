#include "unity.h"
#include "codegen/tld_pe.h"
#include <stdio.h>
#include <stdlib.h>

void test_teko_linker_pe_coff_multi_architecture_signatures(void) {
    const char* filename = "output_pe_linker_test.exe";
    uint8_t mock_opcodes[] = { 0x90, 0xC3 }; // NOP, RET

    // 1. HOMOLOGAÇÃO WINDOWS X86_64
    bool success_x64 = tld_pe_write_executable(filename, mock_opcodes, sizeof(mock_opcodes), PE_MACHINE_AMD64);
    TEST_ASSERT_TRUE(success_x64);

    FILE* f = fopen(filename, "rb");
    TEST_ASSERT_NOT_NULL(f);
    TekoImageDosHeader dos;
    fread(&dos, sizeof(TekoImageDosHeader), 1, f);

    // Verifica a assinatura mestre MZ do DOS Stub
    TEST_ASSERT_EQUAL_HEX16(PE_DOS_MAGIC, dos.e_magic);

    // Pula para onde o cabeçalho PE real se posiciona e confere a assinatura "PE\0\0"
    fseek(f, dos.e_lfanew, SEEK_SET);
    uint32_t pe_sig = 0;
    fread(&pe_sig, 4, 1, f);
    TEST_ASSERT_EQUAL_HEX32(PE_SIGNATURE, pe_sig);
    fclose(f);
    remove(filename);

    // 2. HOMOLOGAÇÃO WINDOWS X86 (32-bit)
    bool success_x82 = tld_pe_write_executable(filename, mock_opcodes, sizeof(mock_opcodes), PE_MACHINE_I386);
    TEST_ASSERT_TRUE(success_x82);
    remove(filename);

    // 3. HOMOLOGAÇÃO WINDOWS ARM64 (Windows on ARM)
    bool success_arm64 = tld_pe_write_executable(filename, mock_opcodes, sizeof(mock_opcodes), PE_MACHINE_ARM64);
    TEST_ASSERT_TRUE(success_arm64);
    remove(filename);
}
