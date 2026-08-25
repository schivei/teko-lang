---
seq: 0125
crumb-id: RT-ENTRY
milestone: M2
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [S16-FS]        # needs the syscall grounding (0055) + rt_exit (0051); is the foundation 0124/0062 build on
sources:
  - "DECISION_LOG.md:963-969"                                       # D99 — antecipar a infra; capture argc/argv/envp from the OS ABI
  - "DECISION_LOG.md:850-856"                                       # D85 — the C must link with NO libc; the .o-direct native north
  - "DECISION_LOG.md:857-863"                                       # D86 — each platform's OWN linker + OS ABI (kernel32/libSystem), not libc
  - "DECISION_LOG.md:843-848"                                       # D84 — F6/F7 are INFRA to build, no shortcuts (this is that infra)
  - "DECISION_LOG.md:884-889"                                       # D90 — write Teko + reflect in codegen; the arena-control seam has two incarnations
  - "docs/design/plano-s16-expurgo-libc-completo.md:838"           # D83 scout: atexit → explicit finalizer; snprintf/exit via syscall
---

# 0125 · RT-ENTRY — our own `_start`: argc/argv/envp from the OS ABI, coherent C-route ↔ native (3 OSes)

> Own the process entry: emit our own `_start` (no crt0) that reads argc/argv/envp directly from the OS ABI
> — the initial stack (Linux), the LC_MAIN registers (macOS), `kernel32`/PEB (Windows) — feeds the Teko
> args/env overlays, runs vmain, and exits. The SAME code serves the C route (`-nostartfiles`) and the
> native `.o`. The foundation `0124` (env) and `0062` (exec) rest on. **Owner-directed 2026-08-25.**

## Goal

The `main(argc,argv,envp)`-param capture (my first RT-L4-ENV draft) was REJECTED by the owner: native has
NO `main(argc,argv,envp)` — without a crt0 the entry is `_start` and the kernel lays argc/argv/envp/auxv on
the INITIAL STACK; a `main` third-param is a crt crutch that DIES at native (D85 north: what we write in C
must hold when we emit the `.o` DIRECTLY, no cc/gcc/clang). This crumb builds the real infra: **our own
`_start`**, one mechanism, three OS-specific entry readers, coherent across the C route (transition) and the
native backend (destination). It captures argc/argv/envp from the OS ABI, seeds the args + env overlays
(`0124`), runs the renamed vmain, and terminates via `rt_exit` (syscall / `ExitProcess`). It replaces both
the C-route emitted `main` + `tk_set_args` (codegen) AND the native `native_entry_stub` `main` (`lower.tks`
`6717`), which today STILL relies on the system crt0 (`Scrt1.o`→`__libc_start_main`→`main`) — the crutch to
remove. **Byte-mover** for every emitted program (the entry shape changes) → a `fixpoint-rebuild` reseed;
the entry is a teach→use split (Gate). This ALSO migrates `args()` off `tk_set_args`/libc (it now reads the
captured argv), folding a previously-separate bite into the one mechanism, per the owner's "explore deep".

## Where

- `src/codegen/codegen.tks:9762-9859` — `emit_program_main_body`/`emit_program_main_cov`/`emit_test_main`/
  `emit_test_main_analyze` — STOP emitting `int main(int argc, char **argv)`. Emit instead: (a) the vmain
  body as `int teko_vmain(void)` (the current main body, minus the argc/argv plumbing); (b) a naked
  `_start` stub (per-`#os`/`#arch`, inline `__asm__` — the same technique the syscall stubs already use,
  `codegen.tks:9280-9311`) that captures the entry state and tail-calls the Teko handler `start_<os>`.
- `src/lir/lower.tks:6684-6730` — `wrap_native_entry` / `native_entry_stub` — replace the `main(argc,argv)`
  stub with a `_start` LFunc whose first inst is the new `stack_ptr` intrinsic (Linux) or reads x0..x3
  (macOS) / calls kernel32 (Windows); it calls `start_<os>` then never returns. `NATIVE_ENTRY_VMAIN_SYMBOL`
  becomes `teko_vmain`.
- `src/backend/isel_x86_64.tks`, `src/backend/isel_arm64.tks`, `src/backend/minst*.tks` — lower the new
  `stack_ptr` LIR op to `mov <reg>, %rsp` (x86) / `mov <reg>, sp` (arm64) as `_start`'s prologue-less first
  instruction; a naked-function attribute on the emitted symbol (no frame setup).
- `src/backend/objfile_elf.tks` / `objfile_macho.tks` / `objfile_coff.tks` — mark `_start` as the entry
  symbol (ELF: default `_start`, no e_entry in `.o` — the linker resolves; Mach-O: LC_MAIN/LC_UNIXTHREAD
  entry offset; PE: `AddressOfEntryPoint` / the linker's `/entry:`).
- `src/build/project.tks:614-657` (`build_cc_argv`, C route) — add `-nostartfiles` (drop crt0's `_start`);
  keep `-lc` transitionally (dead-but-present C), drop it at F9.
- `src/build/project.tks:1008-1097` (`link_object_elf_direct`/`_macho_direct`/`_pe_direct`, native route) —
  DROP `Scrt1.o`/`crti.o`/`crtn.o` + `-lc` (ELF), `crt1.o` (macho, keep `-lSystem`), the CRT default-lib
  (PE, keep `kernel32`); rely on our `_start` (ELF default entry; `-e` / LC_MAIN / `/entry:` elsewhere).
- `src/env/env.tks` — `capture_args`/`capture_envp` fed by `_start` (not a main param); `args()` re-homed to
  read captured argv (zero-libc strlen loop).
- `src/sys/sys.tks` — add `SYS_ARCH_PRCTL` (x86_64 158) + `ARCH_SET_FS` (0x1002) for Linux-x86 TLS setup;
  arm64 sets `TPIDR_EL0` via an `msr` instruction (no syscall). `SYS_EXIT_GROUP` already present.
- `src/runtime/teko_rt.c:2019,1088,259,1860` — the `_Thread_local` arena/task anchors + the
  `__attribute__((constructor))` crash handler + the atexit finalizers — NOT edited (D90); their
  initialisation MOVES into our `_start` (TLS bootstrap + `.init_array` walk + explicit finalizers). The C
  bodies go dead / are re-driven by `_start`, deleted at F9.

## How

The mechanism is ONE design — capture the process's argc/argv/envp from the OS ABI in a per-`#os` `_start`,
hand them to a portable Teko handler, run vmain, exit — with three OS-specific entry readers because the
three OS ABIs genuinely differ (D86: each platform's own ABI). The naked `_start` stub is the only per-arch
asm; everything after is portable Teko.

### 1. The portable handler (identical Teko for C route AND native)

```teko
/**
 * start_posix — the portable process entry after the naked `_start` stub captured the OS entry
 * state. Bootstraps thread-local storage (Linux), runs the `.init_array` constructors, seeds the
 * args + env overlays from the captured pointers, runs the renamed vmain, runs the explicit exit
 * finalizers (the atexit replacement — coverage dump, arena flush), and terminates via `rt_exit`.
 * NEVER returns. Shared verbatim by the C route (`-nostartfiles`) and the emitted native `.o`.
 *
 * @param argc  the argument count
 * @param argv  the address of the `char**` argument vector (NUL-pointer-terminated)
 * @param envp  the address of the `char**` environment block (NUL-pointer-terminated)
 * @param auxv  the address of the ELF auxiliary vector (0 on macOS) — reserved for page size/random
 * @since 0.3.1
 */
fn start_posix(argc: i64, argv: u64, envp: u64, auxv: u64)
```

Body (ordered): `tls_bootstrap()` (Linux only; §4) → `run_init_array()` (§4) → `capture_args(argc, argv)` +
`capture_envp(envp)` (`0124`) → `var status = teko_vmain()` → `run_exit_finalizers()` (§4) →
`teko::runtime::rt_exit(status)`. On Windows the argv/envp come from kernel32 (§3), so a thin `start_windows`
gathers them then calls the shared tail.

### 2. Linux (x86-64 + arm64) — read the initial stack

At `_start` the kernel has placed on the stack (SysV / the arm64 process ABI, identical layout):
`[argc][argv0]..[argvN][NULL][envp0]..[envpM][NULL][auxv...][AT_NULL]`. The naked stub captures `%rsp`/`sp`
(the pointer to `argc`) and passes it to a Teko decoder:

```teko
/**
 * decode_stack_linux — decode the Linux initial-stack block the kernel handed `_start` into
 * (argc, argv, envp, auxv). `sp` points at the `argc` word; argv follows, then a NULL, then envp,
 * then a NULL, then the auxv. Pure pointer arithmetic + `load_u64` — zero libc.
 * @param sp  the initial stack pointer captured at `_start` (points at `argc`)
 * @return    the decoded (argc, argv, envp, auxv) addresses
 * @since 0.3.1
 */
fn decode_stack_linux(sp: u64): (i64, u64, u64, u64)
```

`argc = load_u64(sp)`; `argv = sp + 8`; `envp = argv + (argc+1)*8`; `envp` is walked to its NULL to find
`auxv`. The naked stub (emitted per-arch):

```c
/* C route, Linux x86-64 — emitted verbatim (same __asm__ technique as the syscall stubs) */
__attribute__((naked, used)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n\t"          /* mark the outermost frame */
        "mov %rsp, %rdi\n\t"          /* arg0 = initial SP */
        "and $-16, %rsp\n\t"          /* 16-byte align before the call */
        "call teko_teko__env__start_linux\n\t"  /* never returns */
    );
}
```

`start_linux(sp)` = `decode_stack_linux` then `start_posix(...)`. On native, `_start` is the SAME logic
emitted as machine code: the new `stack_ptr` intrinsic (§5) is `_start`'s first instruction, then a call to
`start_linux`. arm64 uses `mov x0, sp` + `bl`.

### 3. macOS (arm64) + Windows (x86-64) — the OTHER two ABIs

**macOS (arm64):** a dynamically-linked Mach-O runs dyld first; with **LC_MAIN**, dyld calls the entry as
`entry(x0=argc, x1=argv, x2=envp, x3=apple)` AFTER initialising libSystem + TLS + image initializers. So on
macOS argc/argv/envp arrive in REGISTERS, not on our stack read, and dyld already did TLS/init — our
`start_macos` skips `tls_bootstrap`/`run_init_array` (dyld owns them) and calls the shared tail directly.
`-lSystem` stays (the OS ABI, D86); `crt1.o` is dropped (LC_MAIN + dyld replaces it). Raw `svc` is avoided
on macOS — the syscalls route through libSystem as they already do (`rtio`/`fs` `#os("macos")` legs).

```teko
/**
 * start_macos — the macOS LC_MAIN entry. dyld has already run libSystem init, TLS, and the image
 * initializers, and calls this with argc/argv/envp/apple in x0..x3. Seeds the overlays and runs
 * vmain; on macOS the exit finalizers run then control returns to dyld's caller (or `rt_exit`).
 * @param argc  the argument count (x0)
 * @param argv  the `char**` argument vector (x1)
 * @param envp  the `char**` environment block (x2)
 * @param apple  the Darwin `apple[]` array (x3) — executable path etc., reserved
 * @since 0.3.1
 */
fn start_macos(argc: i64, argv: u64, envp: u64, apple: u64)
```

**Windows (x86-64):** a PE with no CRT gets NO argc/argv/envp — the entry (`/entry:tk_start`) receives
nothing on the stack. `start_windows` gathers them from the OS ABI (kernel32, D86): `GetCommandLineW` +
`CommandLineToArgvW` (or an ANSI parse of `GetCommandLineA`) for argv, `GetEnvironmentStringsW` for the
environment block (a double-NUL-terminated `KEY=VALUE\0KEY=VALUE\0\0` block, decoded into the same overlay).
The PE loader initialises the TEB + `.tls` section without the CRT, so `_Thread_local` works with no manual
bootstrap. `kernel32` resolves through the import library (the Fase E import-lib linker — the same blocker
`0062`'s Win32 exec leg carries; the Windows entry LANDS with that leg, POSIX first).

```teko
/**
 * start_windows — the PE entry with no CRT. Gathers argv from `GetCommandLineW`/`CommandLineToArgvW`
 * and the environment from `GetEnvironmentStringsW` (kernel32 — the OS ABI, D86, not libc), seeds
 * the overlays, runs vmain, then `ExitProcess`. The PE loader already set up the TEB/TLS.
 * @since 0.3.1
 */
fn start_windows()
```

### 4. What crt0 did that we must now do ourselves (the `-nostartfiles` implications)

Dropping crt0 removes three services crt0/glibc used to provide; each moves into `_start` (Linux; macOS gets
them from dyld, Windows from the PE loader):

- **TLS bootstrap (the hard one).** The arena control anchor `tk_g_arena_control` and `tk_g_current_task`
  are `_Thread_local` (`teko_rt.c:2019,1088`) — the heart of ALL allocation. glibc's crt sets `%fs`
  (x86-64) / `TPIDR_EL0` (arm64) to a TCB so `_Thread_local` loads resolve; with `-nostartfiles` nothing
  sets it → the first arena access faults. `_start` must install a minimal main-thread TCB: allocate a
  static/`mmap` TLS block sized to the module's `.tbss`+`.tdata` (with the TCB header), copy `.tdata`, then
  `arch_prctl(ARCH_SET_FS, tcb)` (x86-64, `SYS_arch_prctl` 158) / `msr TPIDR_EL0, x` (arm64, an
  instruction). This is REQUIRED for native too (the emitted `.o` needs the same bootstrap). macOS: dyld
  does it. Windows: the PE loader does it.

```teko
/**
 * tls_bootstrap — install the main thread's TLS block so `_Thread_local` loads resolve without a
 * crt (Linux only; macOS/Windows have it from dyld/the PE loader). Allocates the block sized to the
 * module's TLS image, initialises the TCB self-pointer, and sets `%fs` (x86-64, via
 * `SYS_arch_prctl`) / `TPIDR_EL0` (arm64, via `msr`). Idempotent; the F7 thread spawner reuses the
 * same block shape per child.
 * @since 0.3.1
 */
fn tls_bootstrap()
```

- **`.init_array` constructors.** crt0's `__libc_csu_init` walked `.init_array`; with `-nostartfiles` it is
  not run, so the `__attribute__((constructor))` crash handler (`teko_rt.c:259`) never installs. `_start`
  walks `__init_array_start`..`__init_array_end` explicitly (standard freestanding pattern), keeping
  existing constructors working through the transition. Signal/backtrace install fully de-C's at F8.
- **Exit finalizers (atexit replacement).** crt0's `exit()` ran atexit finalizers; we call `rt_exit`
  (exit_group) directly, which skips them, so the coverage-dump atexit (`codegen.tks:9787`) and the lazy
  finalizer (`teko_rt.c:1860`) would not fire. `_start` calls an explicit `run_exit_finalizers()` before
  `rt_exit` (D83 scout: "atexit → finalizer explícito"). The cov dump moves here (reads the covfile via the
  `0124` zero-libc `env::var`, not libc `getenv`).

### 5. The `stack_ptr` intrinsic (native coherence)

For the native `.o`, `_start` cannot be inline asm; the backend must emit the SP read as a real instruction.
Add a leaf LIR op / builtin `teko::sys::stack_ptr(): u64` lowered by isel to `mov <dst>, %rsp` (x86) /
`mov <dst>, sp` (arm64), only ever the first instruction of the naked `_start` (a symbol the backend marks
frameless). On the C route the same conceptual read is the naked-stub `__asm__` above. This is the ONE place
the two routes differ in emission; the value it produces (the initial SP) flows into the identical Teko
`decode_stack_linux`. This is the concrete meaning of "one mechanism, two incarnations" (the arena-seam
comment, `teko_rt.c:1026`).

```teko
/**
 * stack_ptr — the current stack pointer, as a raw address. ONLY valid as the first operation of the
 * naked `_start` (before any frame is established), where it equals the kernel-provided initial SP
 * pointing at `argc`. A codegen/backend intrinsic: inline asm on the C route, `mov reg, sp` on
 * native. Using it anywhere else is meaningless (a mid-function SP).
 * @return  the raw stack pointer address
 * @since 0.3.1
 */
extern fn stack_ptr(): u64
```

## Rulings & laws

- **Owner (2026-08-25), the redesign order:** native has no `main(argc,argv,envp)`; capture from the OS ABI
  via our own `_start`; the SAME code serves the C route (`-nostartfiles`) and the native `.o`; cover all
  three OSes, defer nothing to "the backend". This crumb is that infra.
- **D85 (zero-libc / native north):** what we write in C must hold when we emit the `.o` directly. `_start`
  + raw stack read + syscalls is that shape. Proof: `nm -u` shows no crt symbols (`__libc_start_main`,
  `__libc_csu_init`) and the entry is our `_start`.
- **D86 (each platform's own ABI):** Linux raw stack + syscalls; macOS LC_MAIN + libSystem (dyld owns
  TLS/init); Windows PE entry + kernel32 (loader owns TLS). NOT libc — the OS ABI.
- **D90 (method):** the entry logic is Teko (`start_*`, `decode_stack_linux`, `tls_bootstrap`); codegen +
  backend REFLECT it (naked stub + `stack_ptr` intrinsic + link flags). `teko_rt.c`'s `tk_set_args` and the
  `_Thread_local`/constructor/atexit machinery are NOT patched — they are re-driven/orphaned by `_start` and
  deleted at F9. The arena-control accessor's "two incarnations" seam is honoured, not broken.
- **W15 full Javadoc** on every declaration; flatten/extract; no inline `//`.
- **Transition posture:** `-nostartfiles` lands NOW (our `_start` is the entry); `-lc` (ELF) / the CRT
  default-lib (PE) stay transitionally for the still-present dead C (F7/F8 bodies) and drop at F9. This is
  the honest split; the zero-libc `nm -u` proof for the ENTRY (no crt) holds now, the full no-`-lc` proof is
  F9.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 4718592`; commit each green step;
  reseed ONLY at the [RITUAL] points below; fixpoint `gen2==gen3` byte-identical; MEM_PARANOID exit 0; RSS
  ratchet (flag the delta; C→Teko migration cost, reclaimed at F9); no `Co-Authored-By` trailer; sweep
  `.tkt`/`.tkr` after the entry-shape change.

## Fixtures

The entry path is exercised by EVERY program (the self-build fixpoint is the strongest oracle), but the
per-OS captures + `-nostartfiles` correctness need isolated pins:

| fixture | asserts | expected |
|---|---|---|
| `entry_argc_argv` | a program echoes `args().len` and `args()[1]` — argv captured from the stack (Linux) via `_start`, zero crt | `0` |
| `entry_envp_read` | a program reads an exported var via captured `envp` (feeds `0124`'s overlay), zero-libc | `42` |
| `entry_no_crt_nm` | build a program; `nm -u` shows NO `__libc_start_main`/`__libc_csu_init`; entry symbol is `_start` (harness) | `0` |
| `entry_tls_arena` | a program allocates heavily right after start — the `_start` TLS bootstrap made the `_Thread_local` arena control resolve (no fault) | `0` |
| `entry_init_array` | a `__attribute__((constructor))` runs before vmain under `-nostartfiles` (the `.init_array` walk) | `0` |
| `entry_exit_finalizer` | the explicit exit finalizer fires (a cov-style dump written) before `rt_exit` | `0` |
| `entry_native_start` | the NATIVE `.o` links with no crt (`stack_ptr` intrinsic emits `mov reg,sp`; `-e _start`), runs, exits 0 | `0` |

## Gate

`[RITUAL]` — the load-bearing entry change, a 2-reseed teach→use (the seed must EMIT the `_start`-capturing
entry before `0124`'s `var` may READ the captured env; else `gen1`, whose entry was emitted by the pre-change
seed, has no capture and the compiler cannot read its own env):

- **RESEED A (teach the entry):** land the codegen/backend `_start` emission + `-nostartfiles` + the link
  changes + `capture_args`/`capture_envp` STASHING (not yet consumed by `var`), with `var`/`set_var` STILL
  on the old bindings. The pre-change seed emits `gen1` with the old `main` — fine, `var` still old. `gen2`
  has `_start`. Reseed `bootstrap/teko.c = gen2`. `gen2==gen3`, MEM_PARANOID 0, `nm -u` no crt, native `.o`
  links crt-free.
- **RESEED B (use the entry):** `0124` switches `var`/`args`/… onto the captured overlays. The seed now
  emits the capturing `_start`, so `gen1` captures its own argc/argv/envp and self-compiles. Reseed
  `bootstrap/teko.c = gen2`.

"Green" = both the C route (`-nostartfiles`) and the native `.o` (crt-free, `-e _start`) start via our
`_start`, argc/argv/envp are captured from the OS ABI on all reachable targets (Linux now; macOS via
LC_MAIN; Windows entry with the Fase E leg), TLS/init_array/finalizers are handled, `nm -u` shows no crt,
and `gen2==gen3`. **Reseed-class:** `fixpoint-rebuild`. This is RESEED A of the RT-L4 wave; `0124` is B,
`0062` (POSIX exec) is C.

## Deps

`S16-FS` (`0055` — syscall grounding) + the landed `rt_exit` (`0051`/D94). Foundation for `RT-L4-ENV`
(`0124`) and `RT-L4` (`0062`), which co-land in the wave.

## Done when

Every emitted program starts via our own `_start` (C route `-nostartfiles`; native `.o` crt-free, `-e
_start`), argc/argv/envp are captured from the OS ABI per `#os` (Linux stack / macOS LC_MAIN registers /
Windows kernel32), the `_start` bootstraps TLS + walks `.init_array` + runs explicit exit finalizers, `args`
and env read the captured state zero-libc, `nm -u` shows no crt startup symbols, the fixtures pass, and both
reseeds are `gen2==gen3` byte-identical with MEM_PARANOID exit 0. The Windows entry lands with `0062`'s
Fase E leg (POSIX first).
