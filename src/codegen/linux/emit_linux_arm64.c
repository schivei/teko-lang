#include "../emit_arm64_gas_common.h"

// Linux arm64 (AAPCS64, ELF). Thin wrapper over the shared GAS arm64 emitter.
void emit_linux_arm64(MetalContext* ctx, OpCode op, int32_t arg) {
    static const Arm64GasEmitParams p = {
        .banner        = "ARM64 (64-bit Linux)",
        .sym_prefix    = "",
        .str_label     = ".L_linux_str_",
        .adrp_suffix   = "",
        .add_reloc     = ":lo12:",
        .add_suffix    = "",
        .tag           = "linux_arm",
        .halt_comment  = "    ;; [Linux ARM64 Halt]: native sys_exit via svc interrupt\n",
        .halt_svc_num  = 93,
        .spawn_comment = "    ;; --- [Linux ARM64 Spawn via pthread_create] ---\n",
        .chan_comment  = NULL,
    };
    emit_arm64_gas_common(ctx, op, arg, &p);
}
