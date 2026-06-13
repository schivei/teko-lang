#ifndef EMIT_LINUX_RISCV_COMMON_H
#define EMIT_LINUX_RISCV_COMMON_H

#include "../codegen_metal.h"

// Per-width parameters that are the ONLY differences between the RISC-V 32-bit
// and 64-bit Linux emitters. Everything else (registers, syscall, arithmetic,
// control flow) is identical and lives in emit_linux_riscv_common().
typedef struct {
    const char* bits;   // "32" / "64"  (target banner)
    const char* store;  // store mnemonic: "sw" / "sd"
    const char* load;   // load mnemonic:  "lw" / "ld"
    const char* tag;    // local-label tag: "rv32" / "rv64"
    int frame;          // prologue frame size and chan-init bump: 16 / 32
    int ra_off;         // ra save offset: 12 / 24
    int s1_off;         // s1 save offset: 8 / 16
    int arena;          // arena frame size: 512 / 1024
} RiscvEmitParams;

void emit_linux_riscv_common(MetalContext* ctx, OpCode op, int32_t arg, const RiscvEmitParams* p);

#endif // EMIT_LINUX_RISCV_COMMON_H
