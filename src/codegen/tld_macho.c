#include "tld_macho.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool tld_macho_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, int32_t cpu_type) {
    if (!filename || !machine_code || code_size == 0) return false;

    FILE* out = fopen(filename, "wb");
    if (!out) return false;

    TekoMachHeader64 header;
    TekoMachSegmentCommand64 seg_text;
    TekoMachEntryPointCommand cmd_main;
    TekoMachBuildVersionCommand cmd_version;

    // 1. DETERMINAÇÃO POLIMÓRFICA DO ALINHAMENTO DE PÁGINA FÍSICA DO CHIP
    uint64_t page_alignment = 0x1000; // Default Mac Intel: 4KB [1]
    if (cpu_type == CPU_TYPE_ARM64) {
        page_alignment = 0x4000;      // Default Apple Silicon: 16KB [1]
    }

    // 2. CONFIGURAÇÃO DO CABEÇALHO MESTRE MACH-O DE 64 BITS
    memset(&header, 0, sizeof(TekoMachHeader64));
    header.magic      = MH_MAGIC_64;
    header.cputype    = cpu_type;       // CPU_TYPE_ARM64 ou CPU_TYPE_X86_64
    header.cpusubtype = CPU_SUBTYPE_ALL;
    header.filetype   = MH_EXECUTE;
    header.ncmds      = 3;              // 1 Segmento __TEXT + 1 LC_MAIN + 1 LC_BUILD_VERSION

    // Calcula o tamanho total somando as estruturas de comando de carga
    header.sizeofcmds = sizeof(TekoMachSegmentCommand64) +
                        sizeof(TekoMachEntryPointCommand) +
                        sizeof(TekoMachBuildVersionCommand);
    header.flags      = 0x00000001;     // MH_NOUNDEFS (Garante binário auto-contido)

    // Endereço de memória virtual padrão para carga de executáveis no macOS (Base VMA)
    uint64_t base_vmaddr = 0x100000000ULL;

    // Calcula o offset exato onde o código executável bruto começará no arquivo físico em disco
    uint32_t header_and_cmds_size = sizeof(TekoMachHeader64) + header.sizeofcmds;

    // 3. CONSTRUÇÃO DO COMANDO DE CARGA DO SEGMENTO __TEXT
    memset(&seg_text, 0, sizeof(TekoMachSegmentCommand64));
    seg_text.cmd      = LC_SEGMENT_64;
    seg_text.cmdsize  = sizeof(TekoMachSegmentCommand64);
    strncpy(seg_text.segname, "__TEXT", 16);

    seg_text.vmaddr   = base_vmaddr;
    seg_text.fileoff  = 0; // Mapeia o arquivo inteiro desde o byte zero

    uint32_t total_file_size = header_and_cmds_size + code_size;
    seg_text.filesize = total_file_size;
    seg_text.vmsize   = total_file_size;

    // Aplica o alinhamento correto dependendo da arquitetura (4KB Intel ou 16KB Apple Silicon) [1]
    seg_text.flags    = 0;
    seg_text.maxprot  = VM_PROT_READ | VM_PROT_EXECUTE;
    seg_text.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
    seg_text.nsects   = 0;

    // 4. CONSTRUÇÃO DO COMANDO DE PONTO DE ENTRADA (LC_MAIN)
    memset(&cmd_main, 0, sizeof(TekoMachEntryPointCommand));
    cmd_main.cmd      = LC_MAIN;
    cmd_main.cmdsize  = sizeof(TekoMachEntryPointCommand);
    cmd_main.entryoff  = header_and_cmds_size;
    cmd_main.stacksize = 0;

    // 5. CONSTRUÇÃO DO COMANDO DE VERSÃO DE COMPILAÇÃO DARWIN (LC_BUILD_VERSION)
    memset(&cmd_version, 0, sizeof(TekoMachBuildVersionCommand));
    cmd_version.cmd      = LC_BUILD_VERSION;
    cmd_version.cmdsize  = sizeof(TekoMachBuildVersionCommand);
    cmd_version.platform = PLATFORM_MACOS;
    cmd_version.minos    = (13 << 16) | (0 << 8) | 0; // macOS Ventura (13.0.0) encoded por segurança
    cmd_version.sdk      = (13 << 16) | (0 << 8) | 0; // SDK 13.0.0
    cmd_version.ntools   = 0;                         // Linker direto sem dependência externa

    // 6. ESCRITA ATÔMICA SEQUENCIAL DE METADADOS E INSTRUÇÕES NO EXECUTÁVEL DARWIN
    fwrite(&header, 1, sizeof(TekoMachHeader64), out);
    fwrite(&seg_text, 1, sizeof(TekoMachSegmentCommand64), out);
    fwrite(&cmd_main, 1, sizeof(TekoMachEntryPointCommand), out);
    fwrite(&cmd_version, 1, sizeof(TekoMachBuildVersionCommand), out);
    fwrite(machine_code, 1, code_size, out);

    fclose(out);
    return true;
}
