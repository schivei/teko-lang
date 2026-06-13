#include "emit_linux_riscv_common.h"

// RISC-V 64-bit (Linux). Thin wrapper: all logic is in emit_linux_riscv_common();
// only the 64-bit-specific widths/mnemonics/labels differ.
void emit_linux_riscv64(MetalContext* ctx, OpCode op, int32_t arg) {
    static const RiscvEmitParams p = {
        .bits = "64", .store = "sd", .load = "ld", .tag = "rv64",
        .frame = 32, .ra_off = 24, .s1_off = 16, .arena = 1024,
    };
    emit_linux_riscv_common(ctx, op, arg, &p);
}
