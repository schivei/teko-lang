#include "emit_linux_riscv_common.h"

// RISC-V 32-bit (Linux). Thin wrapper: all logic is in emit_linux_riscv_common();
// only the 32-bit-specific widths/mnemonics/labels differ.
void emit_linux_riscv32(MetalContext* ctx, OpCode op, int32_t arg) {
    static const RiscvEmitParams p = {
        .bits = "32", .store = "sw", .load = "lw", .tag = "rv32",
        .frame = 16, .ra_off = 12, .s1_off = 8, .arena = 512,
    };
    emit_linux_riscv_common(ctx, op, arg, &p);
}
