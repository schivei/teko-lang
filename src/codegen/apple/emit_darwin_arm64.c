#include "../emit_arm64_gas_common.h"

// macOS Apple Silicon arm64 (Mach-O). Thin wrapper over the shared GAS arm64
// emitter — underscore symbols, @PAGE/@PAGEOFF relocations, comment-only halt.
void emit_darwin_arm64(MetalContext* ctx, OpCode op, int32_t arg) {
    static const Arm64GasEmitParams p = {
        .banner        = "Apple Silicon (macOS)",
        .sym_prefix    = "_",
        .str_label     = ".L_str_",
        .adrp_suffix   = "@PAGE",
        .add_reloc     = "",
        .add_suffix    = "@PAGEOFF",
        .tag           = "arm",
        .halt_comment  = "    ;; [AOT Halt]: Terminates program execution.\n",
        .halt_svc_num  = -1,
        .spawn_comment = "    ;; --- [AOT Async Spawn via pthread_create] ---\n",
        .chan_comment  = "    ;; --- [AOT Channel Allocation in the Arena] ---\n",
    };
    emit_arm64_gas_common(ctx, op, arg, &p);
}
