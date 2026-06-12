#ifndef TLD_ELF_H
#define TLD_ELF_H

#include <stdint.h>
#include <stdbool.h>

#define ELF_OS_FREEBSD   9   // Identificador de OS nativo do FreeBSD no e_ident
#define NT_FREEBSD_ABI_TAG 1 // Tipo de nota estrutural para o FreeBSD

// Constantes estruturais Universais ELF
#define ELF_MAGIC_0      0x7F
#define ELF_MAGIC_1      'E'
#define ELF_MAGIC_2      'L'
#define ELF_MAGIC_3      'F'
#define ELF_CLASS_32     1   // Nova classe para 32-bit
#define ELF_CLASS_64     2   // Classe para 64-bit
#define ELF_DATA_2LSB    1   // Little Endian
#define ELF_VERSION_CUR  1
#define ELF_OS_LINUX     3
#define ELF_TYPE_EXEC    2   // Arquivo Executável

// Constantes de Arquiteturas Mapeadas
#define ELF_ARCH_386     3   // Intel x86 32-bit
#define ELF_ARCH_ARM32   40  // ARM 32-bit
#define ELF_ARCH_X86_64  62  // Intel x86 64-bit
#define ELF_ARCH_ARM64   183 // AArch64 64-bit

#define PF_X             0x1 // Executar
#define PF_W             0x2 // Escrever
#define PF_R             0x4 // Ler
#define PT_LOAD          1   // Segmento carregável

// ====================================================================
// ESTRUTURAS FÍSICAS CANÔNICAS PARA ELF DE 32 BITS
// ====================================================================
typedef struct {
    unsigned char e_ident[16];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint32_t      e_entry;     // 32 bits de endereço virtual de entrada
    uint32_t      e_phoff;     // 32 bits de offset de Program Header Table
    uint32_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;    // Tamanho do cabeçalho 32-bit (52 bytes)
    uint16_t      e_phentsize; // Tamanho de uma entrada Phdr 32-bit (32 bytes)
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} TekoElf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset; // 32 bits
    uint32_t p_vaddr;  // 32 bits
    uint32_t p_paddr;
    uint32_t p_filesz; // 32 bits
    uint32_t p_memsz;  // 32 bits
    uint32_t p_flags;  // Flags ficam em posições diferentes no ELF32 vs ELF64
    uint32_t p_align;
} TekoElf32_Phdr;

// ====================================================================
// ESTRUTURAS FÍSICAS CANÔNICAS PARA ELF DE 64 BITS
// ====================================================================
typedef struct {
    unsigned char e_ident[16];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;     // 64 bits
    uint64_t      e_phoff;     // 64 bits
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;    // Tamanho do cabeçalho 64-bit (64 bytes)
    uint16_t      e_phentsize; // Tamanho de uma entrada Phdr 64-bit (56 bytes)
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} TekoElf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset; // 64 bits
    uint64_t p_vaddr;  // 64 bits
    uint64_t p_paddr;
    uint64_t p_filesz; // 64 bits
    uint64_t p_memsz;  // 64 bits
    uint64_t p_align;
} TekoElf64_Phdr;

// Assinaturas públicas polimórficas do Linker ELF
bool tld_elf32_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, uint16_t architecture);
bool tld_elf64_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, uint16_t architecture);

#endif // TLD_ELF_H
