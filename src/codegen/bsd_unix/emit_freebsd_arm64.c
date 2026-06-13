#include "../emit_arm64_gas_common.h"

// FreeBSD arm64 (ELF). Thin wrapper over the shared GAS arm64 emitter.
void emit_freebsd_arm64(MetalContext* ctx, OpCode op, int32_t arg) {
    static const Arm64GasEmitParams p = {
        .banner        = "ARM64 (FreeBSD OS)",
        .sym_prefix    = "",
        .str_label     = ".L_bsd_str_",
        .adrp_suffix   = "",
        .add_reloc     = ":lo12:",
        .add_suffix    = "",
        .tag           = "bsd_arm",
        .halt_comment  = "    ;; [FreeBSD ARM64 Halt]: native sys_exit via svc interrupt\n",
        .halt_svc_num  = 1,
        .spawn_comment = "    ;; --- [FreeBSD ARM64 Spawn via pthread_create] ---\n",
        .chan_comment  = "    ;; --- [FreeBSD ARM64 Arena-Based Channel Allocation] ---\n",
    };
    emit_arm64_gas_common(ctx, op, arg, &p);
}
