#include "tld_elf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ====================================================================
// EMISSOR NATIVO PARA ARQUITETURAS DE 32 BITS (ELF32)
// ====================================================================
bool tld_elf32_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, uint16_t architecture) {
    if (!filename || !machine_code || code_size == 0) return false;

    FILE* out = fopen(filename, "wb");
    if (!out) return false;

    TekoElf32_Ehdr ehdr;
    TekoElf32_Phdr phdr;

    // 1. CONFIGURAÇÃO DO CABEÇALHO ELF32
    memset(&ehdr, 0, sizeof(TekoElf32_Ehdr));
    ehdr.e_ident[0] = ELF_MAGIC_0;
    ehdr.e_ident[1] = ELF_MAGIC_1;
    ehdr.e_ident[2] = ELF_MAGIC_2;
    ehdr.e_ident[3] = ELF_MAGIC_3;
    ehdr.e_ident[4] = ELF_CLASS_32; // Identifica como classe de 32 bits
    ehdr.e_ident[5] = ELF_DATA_2LSB;
    ehdr.e_ident[6] = ELF_VERSION_CUR;
    ehdr.e_ident[7] = ELF_OS_LINUX;

    ehdr.e_type      = ELF_TYPE_EXEC;
    ehdr.e_machine   = architecture; // ELF_ARCH_386 ou ELF_ARCH_ARM32
    ehdr.e_version   = ELF_VERSION_CUR;

    // Endereço virtual base de carregamento padrão para sistemas de 32 bits no Linux (VMA)
    uint32_t base_vaddr = 0x08048000;

    ehdr.e_ehsize    = sizeof(TekoElf32_Ehdr); // 52 bytes físicos
    ehdr.e_phentsize = sizeof(TekoElf32_Phdr); // 32 bytes físicos
    ehdr.e_phnum     = 1;
    ehdr.e_phoff     = ehdr.e_ehsize;

    ehdr.e_entry     = base_vaddr + ehdr.e_ehsize + (ehdr.e_phnum * ehdr.e_phentsize);

    // Pool estático .rodata simplificado para paridade de recursos da linguagem
    const char* mock_strings[] = { "Hello Teko", "32Bit_Core_Active" };
    uint32_t rodata_size = 0;
    for (int i = 0; i < 2; i++) rodata_size += strlen(mock_strings[i]) + 1;
    uint8_t* rodata_buffer = (uint8_t*)malloc(rodata_size);
    if (rodata_buffer) {
        uint32_t cursor = 0;
        for (int i = 0; i < 2; i++) {
            uint32_t len = strlen(mock_strings[i]) + 1;
            memcpy(&rodata_buffer[cursor], mock_strings[i], len);
            cursor += len;
        }
    }

    // 2. CONSTRUÇÃO DO PROGRAM HEADER (ELF32 layout)
    memset(&phdr, 0, sizeof(TekoElf32_Phdr));
    phdr.p_type   = PT_LOAD;
    phdr.p_offset = 0;
    phdr.p_vaddr  = base_vaddr;
    phdr.p_paddr  = base_vaddr;
    phdr.p_flags  = PF_R | PF_W | PF_X; // Permissões unificadas

    uint32_t total_payload_size = code_size + rodata_size;
    uint32_t total_file_size = ehdr.e_ehsize + (ehdr.e_phnum * ehdr.e_phentsize) + total_payload_size;

    phdr.p_filesz = total_file_size;
    phdr.p_memsz  = total_file_size;
    phdr.p_align  = 0x1000; // Alinhamento de página padrão de 4KB

    // 3. GRAVAÇÃO DOS BLOCOS ATÔMICOS DE 32 BITS
    fwrite(&ehdr, 1, sizeof(TekoElf32_Ehdr), out);
    fwrite(&phdr, 1, sizeof(TekoElf32_Phdr), out);
    fwrite(machine_code, 1, code_size, out);

    if (rodata_buffer) {
        fwrite(rodata_buffer, 1, rodata_size, out);
        free(rodata_buffer);
    }

    fclose(out);
    return true;
}

// ====================================================================
// EMISSOR NATIVO PARA ARQUITETURAS DE 64 BITS (ELF64)
// ====================================================================
bool tld_elf64_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, uint16_t architecture) {
    if (!filename || !machine_code || code_size == 0) return false;

    FILE* out = fopen(filename, "wb");
    if (!out) return false;

    TekoElf64_Ehdr ehdr;
    TekoElf64_Phdr phdr[2]; // Expandido para 2 Program Headers (1 para LOAD, 1 para NOTE do FreeBSD)

    memset(&ehdr, 0, sizeof(TekoElf64_Ehdr));
    ehdr.e_ident[0] = ELF_MAGIC_0;
    ehdr.e_ident[1] = ELF_MAGIC_1;
    ehdr.e_ident[2] = ELF_MAGIC_2;
    ehdr.e_ident[3] = ELF_MAGIC_3;
    ehdr.e_ident[4] = ELF_CLASS_64;
    ehdr.e_ident[5] = ELF_DATA_2LSB;
    ehdr.e_ident[6] = ELF_VERSION_CUR;

    // CASAMENTO UNIX/FREEBSD: Configura o campo de identificação de ABI nativa
    ehdr.e_ident[7] = ELF_OS_FREEBSD;

    ehdr.e_type      = ELF_TYPE_EXEC;
    ehdr.e_machine   = architecture;
    ehdr.e_version   = ELF_VERSION_CUR;

    uint64_t base_vaddr = 0x400000;

    ehdr.e_ehsize    = sizeof(TekoElf64_Ehdr);
    ehdr.e_phentsize = sizeof(TekoElf64_Phdr);
    ehdr.e_phnum     = 2; // Agora temos 2 Program Headers para carregar o binário UNIX de forma correta
    ehdr.e_phoff     = ehdr.e_ehsize;

    ehdr.e_entry     = base_vaddr + ehdr.e_ehsize + (ehdr.e_phnum * ehdr.e_phentsize);

    // Estrutura física exata da nota ABI exigida pelo Kernel do FreeBSD (16 bytes estruturais + payload)
    uint32_t freebsd_note[] = {
        4,             // Namesz: "FreeBSD" ocupará 4 bytes alinhados (com \0)
        4,             // Descsz: Tamanho do valor da nota (4 bytes do inteiro de versão)
        NT_FREEBSD_ABI_TAG, // Type: Identificador 1
        0x45455246,    // Name: "FREE" em Little Endian
        0x00445342,    // Name: "BSD\0" em Little Endian
        1400000        // Desc: Versão estável do Kernel alvo (Ex: FreeBSD 14)
    };
    uint32_t note_size = sizeof(freebsd_note);

    const char* mock_rodata_strings[] = { "Hello Teko", "CQRS_Service_Active" };
    uint32_t rodata_size = 0;
    for (int i = 0; i < 2; i++) rodata_size += strlen(mock_rodata_strings[i]) + 1;

    uint8_t* rodata_buffer = (uint8_t*)malloc(rodata_size);
    if (rodata_buffer) {
        uint32_t cursor = 0;
        for (int i = 0; i < 2; i++) {
            uint32_t len = strlen(mock_rodata_strings[i]) + 1;
            memcpy(&rodata_buffer[cursor], mock_rodata_strings[i], len);
            cursor += len;
        }
    }

    // PROGRAM HEADER 1: O segmento PT_LOAD soberano (.text + .rodata + .note)
    memset(&phdr[0], 0, sizeof(TekoElf64_Phdr));
    phdr[0].p_type   = PT_LOAD;
    phdr[0].p_flags  = PF_R | PF_W | PF_X;
    phdr[0].p_offset = 0;
    phdr[0].p_vaddr  = base_vaddr;
    phdr[0].p_paddr  = base_vaddr;

    uint32_t total_payload_size = code_size + rodata_size + note_size;
    uint32_t total_file_size = ehdr.e_ehsize + (ehdr.e_phnum * ehdr.e_phentsize) + total_payload_size;

    phdr[0].p_filesz = total_file_size;
    phdr[0].p_memsz  = total_file_size;
    phdr[0].p_align  = 0x1000;

    // PROGRAM HEADER 2: O segmento PT_NOTE (Diz expressamente ao Kernel UNIX que o binário é do FreeBSD)
    memset(&phdr[1], 0, sizeof(TekoElf64_Phdr));
    phdr[1].p_type   = 4; // PT_NOTE
    phdr[1].p_flags  = PF_R; // Apenas leitura
    phdr[1].p_offset = ehdr.e_ehsize + (ehdr.e_phnum * ehdr.e_phentsize) + code_size + rodata_size;
    phdr[1].p_vaddr  = base_vaddr + phdr[1].p_offset;
    phdr[1].p_paddr  = phdr[1].p_vaddr;
    phdr[1].p_filesz = note_size;
    phdr[1].p_memsz  = note_size;
    phdr[1].p_align  = 4;

    // ESCRITA ATÔMICA
    fwrite(&ehdr, 1, sizeof(TekoElf64_Ehdr), out);
    fwrite(&phdr, 1, sizeof(TekoElf64_Phdr) * 2, out);
    fwrite(machine_code, 1, code_size, out);

    if (rodata_buffer) {
        fwrite(rodata_buffer, 1, rodata_size, out);
        free(rodata_buffer);
    }

    fwrite(freebsd_note, 1, note_size, out);

    fclose(out);
    return true;
}
