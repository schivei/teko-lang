#ifndef EMIT_ARM64_GAS_COMMON_H
#define EMIT_ARM64_GAS_COMMON_H

#include "codegen_metal.h"

// Per-OS parameters distinguishing the GAS-syntax arm64 emitters (Linux, FreeBSD,
// macOS). The instruction bodies are identical; only these differ. (Windows arm64
// uses MASM-style directives and is intentionally NOT covered here.)
typedef struct {
    const char* banner;        // text after "Target: " in the banner
    const char* sym_prefix;    // "" (ELF) or "_" (Mach-O) — prefixes main / pthread_create
    const char* str_label;     // SCONST label prefix, e.g. ".L_linux_str_" / ".L_str_"
    const char* adrp_suffix;   // SCONST adrp suffix: "" (ELF) or "@PAGE" (Mach-O)
    const char* add_reloc;     // SCONST add reloc prefix: ":lo12:" (ELF) or "" (Mach-O)
    const char* add_suffix;    // SCONST add suffix: "" (ELF) or "@PAGEOFF" (Mach-O)
    const char* tag;           // local-label tag: "linux_arm" / "bsd_arm" / "arm"
    const char* halt_comment;  // full HALT comment line (leading spaces + trailing "\n")
    int halt_svc_num;          // x8 exit syscall number, or -1 for comment-only (macOS)
    const char* spawn_comment; // SPAWN comment line, or NULL
    const char* chan_comment;  // CHAN_INIT comment line, or NULL
} Arm64GasEmitParams;

void emit_arm64_gas_common(MetalContext* ctx, OpCode op, int32_t arg, const Arm64GasEmitParams* p);

#endif // EMIT_ARM64_GAS_COMMON_H
