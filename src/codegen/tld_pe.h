#ifndef TLD_PE_H
#define TLD_PE_H

#include <stdint.h>
#include <stdbool.h>

// Assinaturas mágicas fundamentais da Microsoft
#define PE_DOS_MAGIC         0x5A4D     // "MZ" [INDEX]
#define PE_SIGNATURE         0x00004550 // "PE\0\0" [INDEX]

// Identificadores de Máquina COFF (Machine Types)
#define PE_MACHINE_I386      0x014C     // Windows x86 32-bit [INDEX]
#define PE_MACHINE_AMD64     0x8664     // Windows x86_64 64-bit [INDEX]
#define PE_MACHINE_ARM64     0xAA64     // Windows on ARM 64-bit [INDEX]

// Características do arquivo PE
#define PE_CHAR_EXECUTABLE   0x0002     // Executable Image
#define PE_CHAR_32BIT_MACH   0x0100     // 32-bit word machine

// Magic numbers do Optional Header
#define PE_MAGIC_PE32        0x010B     // PE32 (32-bit executáveis)
#define PE_MAGIC_PE32_PLUS   0x020B     // PE32+ (64-bit executáveis)

// Permissões e características das seções PE
#define IMAGE_SCN_CNT_CODE               0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA   0x00000040
#define IMAGE_SCN_MEM_EXECUTE            0x20000000
#define IMAGE_SCN_MEM_READ               0x40000000
#define IMAGE_SCN_MEM_WRITE              0x80000000

#pragma pack(push, 1)

// Cabeçalho DOS legado de 64 bytes (IMAGE_DOS_HEADER)
typedef struct {
    uint16_t e_magic;    // PE_DOS_MAGIC
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;   // Deslocamento real para o cabeçalho PE (Normalmente 64 bytes)
} TekoImageDosHeader;

// Cabeçalho COFF padrão de 20 bytes (IMAGE_FILE_HEADER)
typedef struct {
    uint16_t Machine;              // Identificador de arquitetura de hardware
    uint16_t NumberOfSections;     // Contagem de seções
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader; // Tamanho do Optional Header seguinte
    uint16_t Characteristics;      // Flags de imagem executável
} TekoImageFileHeader;

// Cabeçalho Opcional de 32 bits (IMAGE_OPTIONAL_HEADER32)
typedef struct {
    uint16_t Magic;                // PE_MAGIC_PE32
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;  // RVA do ponto de entrada real
    uint32_t BaseOfCode;
    uint32_t BaseOfData;           // Exclusivo do modelo 32-bit
    uint32_t ImageBase;            // Endereço de carga preferencial na RAM (Virtual)
    uint32_t SectionAlignment;     // Alinhamento de memória RAM (Geralmente 0x1000)
    uint32_t FileAlignment;        // Alinhamento em disco (Geralmente 0x200)
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;          // Tamanho virtual total do executável na memória
    uint32_t SizeOfHeaders;        // Tamanho de todos os cabeçalhos juntos em disco
    uint32_t CheckSum;
    uint16_t Subsystem;            // 3 para console app, 2 para GUI Windows app
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;  // Tabelas de Data Directories (Geralmente 16)
} TekoImageOptionalHeader32;

// Cabeçalho Opcional de 64 bits (IMAGE_OPTIONAL_HEADER64 / PE32+)
typedef struct {
    uint16_t Magic;                // PE_MAGIC_PE32_PLUS
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;            // 64 bits para ponteiro virtual no Windows de 64 bits
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
} TekoImageOptionalHeader64;

// Cabeçalho de Tabela de Seções PE (IMAGE_SECTION_HEADER)
typedef struct {
    char     Name[8];             // Nome textual da seção (ex: ".text\0\0\0")
    uint32_t VirtualSize;          // Tamanho na RAM
    uint32_t VirtualAddress;       // RVA relativo à ImageBase
    uint32_t SizeOfRawData;        // Tamanho arredondado em disco
    uint32_t PointerToRawData;     // Offset físico do payload em disco
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;      // Flags de leitura, escrita e execução
} TekoImageSectionHeader;

#pragma pack(pop)

// Roteador polimórfico do Linker PE/COFF do Windows
bool tld_pe_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, uint16_t machine_type);

#endif // TLD_PE_H
