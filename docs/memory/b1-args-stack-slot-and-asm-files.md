# `B1-args` — the stack-arg slot, and the `.s` escape hatch (owner, 2026-07-28)

## Why only Windows fails

    isel x86-64: B1-args — an integer call argument past the ABI's argument-register
    window needs the stack-arg slot (0.3.1)

Not instability, and NOT the harvested seed — the failure predates it (`aa5e380`, `f74ce40`; the
seed landed at `0ad247c`). It is an ABI window difference: **Win64 has four integer argument
registers (RCX/RDX/R8/R9), System V has six (RDI/RSI/RDX/RCX/R8/R9)**. The same call fits in
registers on Linux and macOS and overflows on Windows.

Owner ruling: *"Neste caso, tem que corrigir."*

## The fix — three connected places, not one

`src/backend/isel_x86_64.tks` holds TWO stops, and both must land together: a caller that pushes a
fifth argument is useless if the callee cannot read it.

1. **`pin_args_x86` (caller).** Args past `arg_reg(abi, GPR, n)` are stored to the outgoing area
   with `store_x86` into `MMem { base = RSP; offset = … }`. The Nth overflow argument sits at
   `shadow + (n - window) * 8` on Win64.
2. **`select_param_x86` (callee).** The mirror: a param past the window is LOADED from the caller's
   frame rather than moved out of a register.
3. **`compute_frame_layout_x86`.** It already reserves the shadow region; it must also reserve
   `max(outgoing overflow args)` across every call in the function, and keep the 16-byte alignment
   the ABI requires at the call site.

The machinery exists — `store_x86`, `load_x86`, `MMem`, `MFrameAddrX86`, and a frame layout that
already knows about shadow space. This is not new ground; it is careful ground.

**Why it was not done in the .31 wagon:** a wrong stack-arg lowering does not fail loudly, it
generates SILENTLY WRONG CODE, and only on Windows. That is the exact failure class this train has
been fighting all day (owner: *"se o erro é em runtime, ele pode não morar onde estamos olhando"*).
It needs a session with room to verify, not the tail of one.

## The `.s` idea — for `.32`, and for a DIFFERENT problem

Owner, 2026-07-28: *"aqui entra uma possibilidade para .32, ensinar agora a usar arquivos asm '.s',
ai na .32 basta usar o .s correto para cada caso, como em GO."*

Go selects assembly by filename suffix — `foo_amd64.s`, `foo_linux_amd64.s` — so a package can ship
a hand-written routine per target and the toolchain picks it. It is a real escape hatch, and worth
having: ABI shims, intrinsics, hot paths, anything the code generator should not have to learn.

**It does not replace this fix, and the distinction matters.** `B1-args` is not a routine anyone
wants to hand-write — it is the lowering of an ORDINARY Teko call that happens to carry more
arguments than the register window. No hand-written `.s` teaches the instruction selector to emit
that call. The two are complementary: `.s` is for code we choose to write by hand, the stack-arg
slot is for code the compiler must be able to write by itself.
