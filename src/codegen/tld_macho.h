#ifndef TLD_MACHO_H
#define TLD_MACHO_H

#include <stdint.h>
#include <stdbool.h>

// Assinatura mágica do formato Mach-O de 64 bits (Apple Darwin)
#define MH_MAGIC_64          0xFEEDFACF
#define MH_EXECUTE           0x2        // Tipo de arquivo: Executável final

// Identificadores de CPU da Apple
#define CPU_TYPE_X86_64      0x01000007
#define CPU_TYPE_ARM64       0x0100000C
#define CPU_SUBTYPE_ALL      0x0        // Subtipo genérico compatível

// Comandos de carga Mach-O (Load Commands)
#define LC_SEGMENT_64        0x19       // Define um segmento de memória de 64 bits
#define LC_MAIN              0x80000028 // Define o ponto de entrada principal
#define LC_BUILD_VERSION     0x32       // Comando de versão de compilação da plataforma Apple

// IDs de plataformas da Apple
#define PLATFORM_MACOS       1

// Flags de proteção de memória virtual (Mach VM Protection)
#define VM_PROT_NONE         0x0
#define VM_PROT_READ         0x1
#define VM_PROT_WRITE        0x2
#define VM_PROT_EXECUTE      0x4

// Estrutura física do Cabeçalho Mach-O de 64 bits (mach_header_64)
typedef struct {
    uint32_t magic;        // MH_MAGIC_64
    int32_t  cputype;      // Tipo de processador (ARM64 ou x86_64)
    int32_t  cpusubtype;   // Subtipo da CPU
    uint32_t filetype;     // MH_EXECUTE
    uint32_t ncmds;        // Número de comandos de carga
    uint32_t sizeofcmds;   // Tamanho total em bytes de todos os comandos de carga
    uint32_t flags;        // Flags de linkagem (ex: MH_NOUNDEFS)
    uint32_t reserved;     // Alinhamento obrigatório de 64 bits
} TekoMachHeader64;

// Estrutura do Comando de Carga de Segmento (segment_command_64)
typedef struct {
    uint32_t cmd;          // LC_SEGMENT_64
    uint32_t cmdsize;      // Tamanho total deste comando
    char     segname[16];  // Nome do segmento (ex: "__TEXT")
    uint64_t vmaddr;       // Endereço virtual inicial na RAM
    uint64_t vmsize;       // Tamanho do segmento alocado na memória virtual
    uint64_t fileoff;      // Deslocamento do segmento dentro do arquivo em disco
    uint64_t filesize;     // Tamanho real dos bytes no arquivo em disco
    int32_t  maxprot;      // Proteção máxima de memória permitida
    int32_t  initprot;     // Proteção inicial ao carregar
    uint32_t nsects;       // Número de seções estruturadas (0 para flat)
    uint32_t flags;
} TekoMachSegmentCommand64;

// Estrutura do Comando de Entrada Principal (entry_point_command / LC_MAIN)
typedef struct {
    uint32_t cmd;          // LC_MAIN
    uint32_t cmdsize;      // Tamanho do comando (24 bytes fixos)
    uint64_t entryoff;     // Offset do ponto de entrada relativo ao início do executável
    uint64_t stacksize;    // Tamanho inicial da pilha (0 se usar o padrão)
} TekoMachEntryPointCommand;

// Estrutura do Comando de Versão de Build da Apple (build_version_command)
typedef struct {
    uint32_t cmd;          // LC_BUILD_VERSION
    uint32_t cmdsize;      // Tamanho do comando (24 bytes fixos se ntools = 0)
    uint32_t platform;     // PLATFORM_MACOS
    uint32_t minos;        // Versão mínima do macOS (ex: 13.0.0 encoded)
    uint32_t sdk;          // Versão do SDK do macOS usado (ex: 13.0.0 encoded)
    uint32_t ntools;       // Contagem de ferramentas associadas (0 para linker independente)
} TekoMachBuildVersionCommand;

// Assinatura pública polimórfica do Linker Mach-O
bool tld_macho_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, int32_t cpu_type);

#endif // TLD_MACHO_H
