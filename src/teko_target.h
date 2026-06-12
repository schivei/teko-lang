#ifndef TEKO_TARGET_H
#define TEKO_TARGET_H

// Enumeração exaustiva de Arquiteturas de CPU suportadas para compilação metal e Web
typedef enum {
    ARCH_X86,           // Intel/AMD 32 bits (i386/i686)
    ARCH_X86_64,        // Intel/AMD 64 bits (amd64)
    ARCH_ARM32,         // ARM de 32 bits (armv7/armhf)
    ARCH_ARM64,         // ARM64 genérico
    ARCH_APPLE_SILICON, // ARM64 otimizado especificamente para chips Apple M-Series (Darwin)
    ARCH_RISCV32,       // RISC-V 32 bits (Sistemas embarcados modernos)
    ARCH_RISCV64,       // RISC-V 64 bits (Servidores e computação de ponta)
    ARCH_WASM32,        // WebAssembly 32 bits (Navegadores e ambientes Cloud Native)
    ARCH_WASM64,        // WebAssembly 64 bits (Memory-64 extension)
    ARCH_MIPS32,       // MIPS 32 bits (Roteadores e hardware de rede)
    ARCH_MIPS64,       // MIPS 64 bits
    ARCH_PPC64,         // PowerPC 64 bits (Mainframes e supercomputadores)
    ARCH_UNKNOWN
} TekoArch;

// Enumeração detalhada de Sistemas Operacionais e Ambientes de Runtime (Web)
typedef enum {
    OS_MACOS_DARWIN,    // Kernel Darwin (macOS nativo)
    OS_LINUX,
    OS_WINDOWS,
    OS_WASI,            // WebAssembly System Interface (OS abstrato para a Web)
    OS_BARE_METAL,      // Sem OS (Sistemas embarcados, Arduino, microcontroladores)
    OS_UNKNOWN
} TekoOS;

// Estrutura que define o Target Triple unificado para o LLVM
typedef struct {
    TekoArch arch;
    TekoOS os;
    char target_string[64]; // Ex: "riscv64-unknown-linux-gnu" ou "wasm32-unknown-wasi"
} TekoTarget;

// Assinaturas públicas do Gerenciador de Alvos Expandido
TekoTarget teko_target_detect_host(void);
TekoTarget teko_target_parse(const char* target_str);

#endif // TEKO_TARGET_H