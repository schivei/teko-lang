---
seq: 0124
crumb-id: RT-L4-ENV
milestone: M2
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [S16-FS, RT-ENTRY]  # 0055 fs/time half + 0125 the `_start` that captures argc/argv/envp from the OS ABI
sources:
  - "DECISION_LOG.md:963-969"                                       # D99 — the law: build the infra, env+exec close zero-libc together, no libc crutch
  - "DECISION_LOG.md:944-951"                                       # D97 — the env↔exec coupling that D99 resolves
  - "DECISION_LOG.md:924-933"                                       # D95 — env deferred (bootstrap barrier: environ_addr → 2 reseeds)
  - "DECISION_LOG.md:850-856"                                       # D85 — zero-libc criterion (link with NO libc; `environ`/getenv/setenv libc forbidden)
  - "DECISION_LOG.md:884-889"                                       # D90 — method: write Teko + reflect in codegen; NEVER edit teko_rt.c
  - "docs/design/plano-s16-expurgo-libc-completo.md:131"           # §1.5 #22 env (supersedes the "puro sobre environ" recipe under D85/D99)
  - "docs/design/plano-s16-expurgo-libc-completo.md:235-236"       # §3 FASE4 env row
---

# 0124 · RT-L4-ENV — envp capture at the entry-point + Teko-managed `environ` (zero-libc, POSIX first)

> Capture `envp` (the third `main` arg from the crt/kernel — NOT the libc `environ` symbol) into a
> program-resident, Teko-managed environment, and serve `var`/`set_var`/`cwd`/`chdir` over it with zero
> libc — the READ+WRITE half of the D99 co-land whose exec/propagation half is `0062` (RT-L4).

## Goal

`env` (`var`/`set_var`/`cwd`/`chdir`) does NOT close zero-libc alone: it is coupled to exec (D97). D99
rules the tension by LAW — build the infra so env and exec close zero-libc TOGETHER, no `extern char
**environ` crutch (that fures D85). This crumb delivers the env half: (1) capture `envp` at the emitted
entry-point (the new load-bearing codegen change — the emitted `main` grows a third parameter and one
capture call, touching EVERY emitted program); (2) a program-resident, Teko-managed environment (a
growable array in `region_program()`) that survives and grows on `set_var`; (3) `var`/`set_var`/`unset`
reading/writing that overlay, zero-libc; (4) `chdir`/`cwd` over `SYS_chdir`/`SYS_getcwd`. The exec side
that consumes this overlay to PROPAGATE a new var to children (`execve(path, argv, envp_teko)`) is `0062`
and co-lands in the same wave. **Byte-mover** for every emitted program (the `main` shape changes) → a
`fixpoint-rebuild` reseed; the entry-point change is a teach→use split (2 reseeds, below).

This crumb **supersedes** the stale env recipes: `0055`/`plano §1.5 #22` "puro Teko sobre `environ`" (needs
the libc `environ` symbol → fures D85) and `0061`/D-TS1 "env via `extern fn getenv/setenv`" (accepts libc →
D99 REJECTS as opção A/D). The mechanism is envp-capture, per D99.

## Where

- `src/codegen/codegen.tks:9762-9810` — `emit_program_main_body` — the emitted `main` becomes
  `main(int argc, char **argv, char **envp)` and, right after `tk_set_args`, calls the Teko capture symbol
  `teko_teko__env__capture_envp((uint64_t)(uintptr_t)envp)`. (Program + program-cov mains.)
- `src/codegen/codegen.tks:9812-9839` — `emit_test_main` — same third param + capture call (the test
  harness reads env too).
- `src/codegen/codegen.tks:9841-9859` — `emit_test_main_analyze` — same.
- `src/codegen/codegen.tks:9781,9835` — the **cov** mains emit libc `getenv("TEKO_TKCOV")` + `atexit`
  directly. Reroute the `getenv` to the captured env (`teko_teko__env__var_cstr`); the `atexit` finalizer
  stays (F8/unwind territory, out of this bite's zero-libc-proven artifact — the cov build is not the
  released artifact; REPORTED, not forced here).
- `src/env/env.tks:7-18` — `var`/`cwd`/`set_var`/`chdir` — drop the `extern … from "teko_rt"` bindings;
  re-home the bodies onto the Teko overlay (`var`/`set_var`) and syscalls (`cwd`/`chdir`). `args`/`version`/
  `nproc` stay as-is (args migration is a SEPARATE bite — reported).
- `src/env/env.tks` (NEW private surface) — `capture_envp`, the overlay (`env_ensure_init`, `env_find`,
  `env_get`, `env_put`, `env_remove`), the raw-envp scanner, `var_cstr` (the cov shim), and `env_snapshot`
  (the NUL-terminated `char**` builder that `0062`'s `execve` consumes).
- `src/sys/sys.tks:268-278` — `SYS_CHDIR`/`SYS_GETCWD` already present (x86_64/arm64); add nothing here.
- `src/runtime/arena.tks` — reuse the arena control block as the single-word anchor: add a `CTRL_ENVIRON`
  slot (Teko-managed, zero-libc, NO teko_rt.c edit) holding the overlay header pointer. `region_program()`
  (`arena.tks:774`) is the program-resident allocator for the overlay.

## How

The environment is captured ONCE at process start and served from a program-resident Teko structure. No
libc `environ`, no libc `getenv`/`setenv`. The overlay is the single source of truth after first touch, so
a new var is coherent for both `var` (read-back) and `0062`'s `execve` (propagation) — dissolving D97's
"realloc of environ leaves the captured envp stale" incoherence: we NEVER realloc the kernel block; we
copy-on-first-touch into a growable Teko array.

### 1. Capture at the entry-point (the load-bearing codegen change)

The emitted `main` grows a third parameter and stashes its address. `envp` is a `char**` — a NUL-pointer-
terminated vector of `char*`, each `"KEY=VALUE\0"` — handed by the crt/kernel (POSIX) exactly as `argv` is.
We pass its ADDRESS (a `u64`) to a Teko symbol; no new intrinsic is needed (the value is an ordinary C
pointer widened to `uint64_t`, mirroring how `rtio`/`arena` already move addresses through `u64`).

Emitted `main` (all four emitters) becomes:

```c
int main(int argc, char **argv, char **envp) {
    tk_set_args(argc, argv);
    teko_teko__env__capture_envp((uint64_t)(uintptr_t)envp);
    /* ... virtual-main body ... */
}
```

```teko
/**
 * capture_envp — stash the process environment block the crt/kernel handed the emitted `main` as
 * its third argument (a NUL-pointer-terminated `char**`, each entry `"KEY=VALUE"`). Called ONCE at
 * process start, before any `var`/`set_var`. Stores the raw block address in the arena control
 * block (`CTRL_ENVIRON` anchor); the overlay is materialised lazily on first `var`/`set_var`. This
 * is the D99 mechanism — the crt/kernel `envp`, NOT the libc `environ` symbol (which fures D85).
 *
 * @param envp  the address of the `char**` environment block (0 when the host gave none)
 * @since 0.3.1
 */
pub fn capture_envp(envp: u64)
```

### 2. The program-resident overlay (grows on `set_var`)

A tiny header in `region_program()` — `{ len: u64, cap: u64, data: u64 }` where `data` points to a
`region_program()` vector of `str` (each owned `"KEY=VALUE"`). The header pointer lives in the arena
control block (`CTRL_ENVIRON`), so it survives the whole program and is reachable from any leaf, zero-libc,
with NO teko_rt.c anchor (same pattern as `tk_arena_control_get/set`, but the slot is inside the already-
existing control block — a pure-Teko `ar_ctrl_set/get`, D90-clean).

- **Lazy init (`env_ensure_init`):** on first `var`/`set_var`/`env_snapshot`, if the overlay is empty,
  scan the raw captured `envp` (walk the `char**` until a NULL pointer; for each, copy the C string into a
  `region_program()`-owned `str`) into the overlay array. After this, the overlay is authoritative; the raw
  block is never read or realloced again.
- **`env_find(name)`:** linear scan for an entry whose bytes before `'='` equal `name`; returns the index
  or a not-found sentinel. (Env is small; linear is correct and simple.)
- **`env_get(name)`:** `env_find` → slice the value after `'='`.
- **`env_put(name, value)`:** `env_find` → replace the entry's `str` in place, else append `"name=value"`
  (grows the vector in `region_program()`; on capacity exhaustion, allocate a larger vector and copy — the
  program-resident grow).
- **`env_remove(name)`:** `env_find` → compact the entry out.

```teko
/**
 * env_ensure_init — materialise the Teko environment overlay from the captured raw `envp` the first
 * time the environment is touched. Idempotent: a non-empty overlay is left untouched. Each variable
 * is copied into `region_program()` as an owned `"KEY=VALUE"` string, so the overlay survives and
 * grows for the whole program without ever realloc-ing the kernel block (dissolving the D97 stale-
 * envp incoherence).
 * @since 0.3.1
 */
fn env_ensure_init()

/**
 * env_put — set `name` to `value` in the program-resident overlay, replacing any existing entry or
 * appending a new one; the overlay grows in `region_program()`. The new value is visible to a later
 * `var` AND to `0062`'s `execve` (which builds the child block from this overlay) — the propagation
 * D97 could not close over libc.
 * @param name   the variable name (no `'='`)
 * @param value  the value to bind
 * @since 0.3.1
 */
fn env_put(name: str, value: str)
```

### 3. Re-home the `exp` env API onto the overlay (zero-libc)

```teko
/**
 * var — the value of the environment variable `name`, from the program-resident Teko overlay
 * (captured `envp` on POSIX; `GetEnvironmentStringsA` on Windows). Zero libc — no `getenv`.
 * @param name  the variable to look up
 * @return      the value, or an error when unset
 * @throws      when `name` is not present in the environment
 * @since 0.3.1
 */
exp fn var(name: str): str | error

/**
 * set_var — bind `name` to `value` in the program-resident overlay, visible to later `var` and,
 * via `0062`'s `execve`, inherited by children. Zero libc — no `setenv`, no `putenv`.
 * @param name   the variable to set
 * @param value  the value to bind
 * @return       null on success
 * @throws       never on POSIX (the overlay always accepts); reserved for the Windows write path
 * @since 0.3.1
 */
exp fn set_var(name: str, value: str): error | null

/**
 * unset — remove `name` from the program-resident overlay (so children do not inherit it).
 * @param name  the variable to remove
 * @since 0.3.1
 */
exp fn unset(name: str)
```

### 4. `cwd`/`chdir` over syscalls (zero-libc)

```teko
/**
 * cwd — the current working directory as an owned absolute path, over `SYS_getcwd` (Linux) /
 * per-`#os` `extern fn` (macOS libSystem, Windows `GetCurrentDirectoryA`). Zero libc.
 * @return  the absolute path, or an error
 * @throws  when the host getcwd failed (`-errno`)
 * @since 0.3.1
 */
exp fn cwd(): str | error

/**
 * chdir — change the process working directory to `path`, over `SYS_chdir` (Linux) / per-`#os`
 * `extern fn`. The child of a later fork inherits the new cwd (no propagation plumbing needed —
 * cwd is process state that fork copies). Zero libc.
 * @param path  the directory to move to
 * @return      null on success
 * @throws      when the host chdir failed (`-errno`)
 * @since 0.3.1
 */
exp fn chdir(path: str): error | null
```

### 5. `env_snapshot` — the `char**` the exec half consumes

`0062`'s POSIX `execve` needs a NUL-terminated `char**` built from the overlay. Expose the builder here (it
lives next to the overlay it reads) so `0062` calls it with no cross-crumb reach into overlay internals.

```teko
/**
 * env_snapshot — build a NUL-pointer-terminated `char**` (each entry a NUL-terminated `"KEY=VALUE"`)
 * from the program-resident overlay, allocated in the given region, for `execve`'s third argument.
 * This is the PROPAGATION vehicle: a `set_var` before a spawn reaches the child because the child's
 * block is built from the same overlay (D99). Build BEFORE fork so the shared address space carries
 * it to the child.
 * @param region  the region to allocate the vector + strings in (the caller's spawn scratch)
 * @return        the address of the `char**` block (0-terminated)
 * @since 0.3.1
 */
pub fn env_snapshot(region: ptr): u64
```

### 6. Windows env (per-`#os`, lands with the Win32 exec leg — blocked on Fase E)

Without the CRT there is no `envp` main parameter on Windows, so `capture_envp` on `#os("windows")` reads
the block from `GetEnvironmentStringsA` (kernel32 — the OS ABI, MAINTAINED per D86, NOT libc). The overlay
model is identical; `env_snapshot` emits the Windows double-NUL-terminated environment block for
`CreateProcess`. Shaped here, LANDS with `0062`'s Win32 leg (Fase E import-lib linker) — POSIX first.

## Rulings & laws

- **D99 (owner 2026-08-25) — the law, not a decision:** build the infra; env+exec close zero-libc
  together; capture `envp` at the entry-point; `environ` managed in Teko, program-resident, grows on
  `set_var`; no `extern char **environ`. This crumb is the env half; `0062` is the exec half.
- **D85 (zero-libc):** proof is `nm -u` on the emitted object — NO `getenv`/`setenv`/`putenv`/`environ`/
  `chdir`/`getcwd` libc symbol undefined. Grep of `#include` sumido is necessary, not sufficient.
- **D90 (method):** the bodies go to `.tks`; codegen REFLECTS (emits the new `main` + calls the Teko
  symbols). `teko_rt.c` `tk_rt_getenv`/`setenv`/`getcwd`/`chdir` go DEAD (unused), deleted at F9 SWEEP —
  NEVER patched. The `CTRL_ENVIRON` anchor is a pure-Teko control-block slot, not a new teko_rt.c static.
- **Bootstrap teach→use (the D95 barrier, now viable):** the entry-point capture must be TAUGHT (seeded)
  before `var` may READ from it — else the compiler's own `gen1` (whose `main` was emitted by the pre-
  change seed, so has NO capture) would read an uninitialised overlay and fail. This forces a 2-reseed
  split (Gate). D99's envp-capture removes the NEW-INTRINSIC obstacle D95 hit (envp is an ordinary C
  pointer, no `environ_addr` intrinsic), but the teach→use ordering remains.
- **W15 full Javadoc** on every declaration (pub + private); flatten/extract; no inline `//`.
- **args() NOT migrated here (reported):** `tk_set_args`/`tk_rt_args` (strlen-touching) stay; args
  migration is a separate bite that can reuse this same entry-point capture. REPORTED up, not expanded.
- **cov/atexit (reported):** the cov `main`'s `getenv` reroutes to `var_cstr` here; the `atexit`-based cov
  dump is F8/unwind territory and the cov build is not the zero-libc-proven released artifact — REPORTED,
  not forced.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 4718592` (the measured real
  ceiling; a blown guard is a root-cause fix, never a raised ceiling); commit each green step; reseed ONLY
  at the [RITUAL] points below; fixpoint `gen2==gen3` byte-identical; MEM_PARANOID exit 0; RSS ratchet
  (this ADD is inherent C→Teko migration cost per the D95 pattern — flag the delta, reclaimed at F9); NO
  `Co-Authored-By` trailer; sweep `.tkt`/`.tkr` after the signature change.

## Fixtures

Env/cwd are OS-touching and not pinned as ASSERTIONS by the self-build — isolated `.tkr` oracles required
(the propagation fixture is the heart of what D97 blocked):

| fixture | asserts | expected |
|---|---|---|
| `env_capture_read` | a program reads a var the launcher exported (via captured `envp`, zero-libc `var`) and parses it → i32 | `42` |
| `env_set_readback` | `set_var("TK","7")` then `var("TK")` parses → i32 (overlay round-trip, no child) | `7` |
| `env_unset` | `set_var("TK","1")`, `unset("TK")`, `var("TK")` is unset → exit path proves error | `0` |
| `env_propagate_child` | parent `set_var("TK_CHILD","5")` then `process::run` a child that reads `TK_CHILD` and exits it — the NEW var reaches the child by inheritance (co-lands with `0062`) | `5` |
| `cwd_chdir_roundtrip` | `chdir("/tmp")`, `cwd()` ends with `tmp` (zero-libc `SYS_chdir`/`SYS_getcwd`) | `0` |
| `env_zero_libc_nm` | build a program using `var`/`set_var`/`cwd`; `nm -u` shows NO `getenv`/`setenv`/`environ`/`chdir`/`getcwd` libc undefined (harness fixture, mirrors the D95 proof) | `0` |

`env_propagate_child` depends on `0062`'s POSIX `execve`; it lands in the same wave (co-land). The others
land with this crumb's overlay + capture.

## Gate

`[RITUAL]` — this is the §16-FASE4-env ritual, a 2-reseed teach→use because the entry-point changes:

- **RESEED A (teach the capture):** land ONLY the codegen entry-point change (emit `main(argc,argv,envp)` +
  the `capture_envp` call, which merely STASHES the block) with `var`/`set_var` STILL on the old
  `tk_rt_*`/libc bindings. The pre-change seed emits `gen1` with the old `main` (no capture) — fine,
  because `var` is still libc in this step. `gen2` (emitted by the new codegen) has the capture. Reseed
  `bootstrap/teko.c = gen2`. `gen2==gen3`, MEM_PARANOID 0.
- **RESEED B (use the capture):** switch `var`/`set_var`/`unset`/`cwd`/`chdir` onto the overlay + syscalls
  (steps 2–4). Now the seed already emits the capturing `main`, so `gen1`'s own `main` captures `envp` →
  the compiler reads its own env (`TEKO_BACKEND`/`TEKO_CC`/…) over the overlay → self-compile succeeds.
  Reseed `bootstrap/teko.c = gen2`. `gen2==gen3`, MEM_PARANOID 0, `nm -u` zero-libc for env.

RESEED B may fold the overlay+write (steps 2–3) and cwd/chdir (step 4) into one commit (they share the
post-A seed and have no further teach→use split). The exec/propagation half (`0062`) is a THIRD reseed in
the wave. "Green" = env reads/writes over the Teko overlay, cwd/chdir over syscalls, the entry-point
captures `envp`, `nm -u` shows zero env libc, and `gen2==gen3` holds. **Reseed-class:** `fixpoint-rebuild`.

## Deps

`S16-FS` (`0055` — the fs/time/rusage half; env was its deferred straggler, D95). Co-lands with `0062`
(RT-L4 process/exec) for the `env_propagate_child` propagation.

## Done when

The emitted `main` captures `envp` (all four emitters), `var`/`set_var`/`unset` serve a program-resident
Teko overlay that grows on `set_var`, `cwd`/`chdir` run over `SYS_getcwd`/`SYS_chdir`, `env_snapshot` hands
`0062` the child block, `nm -u` shows no env/cwd libc symbol, the six fixtures pass (the propagation one
with `0062`), and both reseeds are `gen2==gen3` byte-identical with MEM_PARANOID exit 0.
