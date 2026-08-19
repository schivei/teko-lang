---
seq: 0062
crumb-id: RT-L4
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RT-L3]
sources:
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:66"         # §1.2 FFI host process family
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:112-118"    # §2.1 L4 = process/pipes/redirect, fork/exec/CreateProcess + struct-by-value FFI
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:216-229"    # §3.3 Etapa B — process half of win32_compat dies with L4 + Fase E
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:279-281"    # §4.3 STARTUPINFOA/PROCESS_INFORMATION by value blocks L4
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:319"        # §5 F5 — L4 kills the process half of win32_compat
  - "docs/design/plano-s16-expurgo-libc-completo.md:240-241"       # §16-FASE6 process/exec/pipes — BLOQUEADO struct-FFI + linker
---

# 0062 · RT-L4 — runtime C→Teko L4: process/pipes/redirect (fork/exec/CreateProcess) — GATE §16-FASE6

> Close the L4 layer: subprocess spawn/pipes/redirect run in Teko over fork/exec (POSIX) and CreateProcess
> (Win32) — killing the process half of `win32_compat.h`; the §16-FASE6 gate.

## Goal

L4 is the hardest OS layer: **process/pipes/redirect**, reaching `fork`/`execvp`/`waitpid` (POSIX) and
`CreateProcessA` (Win32), the latter passing STRUCTS BY VALUE in the ABI. The family only in `teko_rt.c`
(`migracao…` §1.2): `tk_rt_run`/`_run_quiet`/`_spawn_redirected`/`_wait_one`/`_pipe`/`_pipe_read_fd`/
`_pipe_write_fd`/`_close_fd`/`_fd_wait_readable`/`_fd_fill`/`_fd_take_byte` + the redirect helpers
(`teko_rt.c:2969-3363`). RT-L4 migrates these to Teko: the POSIX half over `clone`/`execve`/`wait4`/`ppoll`
leaf syscalls; the Win32 half filling `STARTUPINFOA` and reading `PROCESS_INFORMATION` — structs passed/returned
BY VALUE (`migracao…` §4.3). This **kills the process half of `win32_compat.h`** (`migracao…` §3.3 Etapa B),
and since it is the LAST consumer, the file itself dies here (its fs half was already orphaned at RT-L3).
Byte-preserving for existing programs (fixpoint guards existing-case residence; `migracao…` R8); a
`fixpoint-rebuild` swap, no teaching reseed.

**BLOCKED (design-ahead, honest) — the heaviest.** Behind (1) the **native fixpoint closing**, (2) its dep
**RT-L3**, (3) the **struct-by-value FFI ABI COMPLETE (reverse-FFI included)** — the recently-resolved
`star-ref…` §4 ABI, which the `STARTUPINFOA`/`PROCESS_INFORMATION` by-value block hard-depends on, and (4) the
**own linker resolving the Win32 import library** (`kernel32`) — Fase E / camada-3 territory (`migracao…` §3.3
Etapa B; `plano-s16-expurgo…`:240-241 marks §16-FASE6 BLOQUEADO on exactly these). The POSIX half can migrate
once the fixpoint + RT-L3 close; the Win32 half waits for Fase E. This doc designs both halves, the fixtures,
and the honest split; what stays blocked is the Win32 import-lib linker (reported to the backend wagon, not a
new issue).

## Where

- `src/runtime/teko_rt.c:2969-3363` — the process/pipes/redirect family (`tk_rt_run`/`_run_quiet`/
  `_spawn_redirected`/`_wait_one`/`_pipe`/`_pipe_read_fd`/`_pipe_write_fd`/`_close_fd`/`_fd_wait_readable`/
  `_fd_fill`/`_fd_take_byte` + `_open_redirect`/`_child_bind_all`/`_next_nul_token`/…) — MIGRATE the POSIX half
  to Teko over `clone`/`execve`/`wait4`/`ppoll`; the C bodies go DEAD (deleted at M3).
- `src/win32_compat.h:119-336` — the process half (`tk_win32_quote_arg`/`tk_win32_spawnvp`/
  `tk_win32_spawn_redirected` (CreateProcessA + STARTUPINFOA/PROCESS_INFORMATION)/`tk_win32_wait_one`/
  `tk_win32_redirect_handle*`/`tk_win32_build_envblock`) — its migration is BLOCKED on struct-by-value FFI +
  the Win32 import-lib linker; when it lands, `win32_compat.h` (both halves now gone) is DELETED (the file's
  death commit, `migracao…` §3.3).
- `src/runtime/teko_rt.tks` — home of the migrated process wrappers.
- NO new user-facing surface: `teko::process` names pre-exist; migration re-homes the body. Reuses
  `TargetSymbol` (from RT-L3) for per-target symbol selection.

## How

1. **Migrate the POSIX half to Teko.** `run`/`spawn_redirected`/`wait_one`/`pipe`/`fd_*` over the leaf
   syscalls `clone`/`execve`/`wait4`/`ppoll` (from the §16 syscall grounding). Redirect helpers wire child
   fds; `fd_fill`/`fd_take_byte` stream a pipe. This half unblocks with the fixpoint + RT-L3.
2. **Design the Win32 half — blocked, but shaped.** `spawn_redirected` fills a `STARTUPINFOA` and reads a
   `PROCESS_INFORMATION`, both passed/returned BY VALUE — the heaviest struct-by-value case (`migracao…` §4.3).
   The Teko wrapper declares these as `extern type=struct` and passes them by value once the resolved ABI
   (`star-ref…` §4) + reverse-FFI are available; `CreateProcessA`/`DuplicateHandle`/`WaitForSingleObject`
   resolve through the own linker's Win32 import library (Fase E). Per-target symbol selection via
   `TargetSymbol` (RT-L3).
3. **The process half of `win32_compat.h` dies — and with it the file** (`migracao…` §3.3 Etapa B): it was the
   file's last consumer (fs half orphaned at RT-L3). The `#include "win32_compat.h"` stops and the file is
   deleted in the commit the Win32 half lands (a clean expurgo, coordinated with the M3 sweep `0096` if the
   Win32 half slips past M2 — reported, not forced).
4. **The link is the normal program link** (POSIX; `migracao…` §2.2): `clone`/`execve`/`wait4` resolve as
   undefined externals. The Win32 link needs the own import-lib linker (Fase E) — the honest dependency.
5. **Fixpoint byte-identity + per-target.** `gen2==gen3` byte-identical on POSIX proves subprocess callers did
   not shift; the Win32 half compiles once its deps land (`migracao…` §5 F5: POSIX own==C, Win32 compiled).

Reused (do NOT redeclare): `TargetSymbol` (RT-L3), the resolved struct-by-value FFI ABI (`star-ref…` §4), the
`{ok,value,err}` result carriers, `region_alloc` (L1) for boxed output.

## Rulings & laws

- **Teko-only:** L4 wrappers land in `src/runtime/teko_rt.tks`; the maintained-C exception is the BRIDGE the
  campaign retires (`migracao…` §1.4/R1). The `teko_rt.c` process C goes DEAD; `win32_compat.h` is DELETED when
  its last (process) consumer migrates — clean expurgo.
- **W15 full Javadoc** on every touched declaration; flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** `win32_compat.h` deletion (both halves gone) is clean and
  tombstone-free — no residual `#define`, no compat stub.
- **No ABI invention (`migracao…` §4.3):** the Win32 struct-by-value block consumes the resolved backend ABI
  (`star-ref…` §4); the runtime does not classify sret/by-value itself.
- **Honest blocked split (`migracao…` R4, §8):** the fs half (RT-L3) needed only per-target selection; the
  process half needs struct-by-value FFI + the Win32 import-lib linker (Fase E). Do NOT force the Win32 half
  before those close — it is REPORTED to the backend wagon, not a new issue.
- **argc/argv (`migracao…` R5):** the empty native `args()` is a backend/linker (crt0) concern, NOT runtime-C —
  L4 does not resolve it; reported to the backend wagon.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the wrapper signatures land.

## Fixtures

subprocess is OS-touching and not self-build-exercised — full isolated oracles (own==C on POSIX; Win32
compile-only until Fase E; `migracao…` §5 F5):

| fixture | asserts | expected |
|---|---|---|
| `l4_run_echo` | spawn a child that prints a known string, capture stdout, bytes match — via migrated Teko `run` | `0` |
| `l4_pipe_stream` | write N bytes through a pipe, read them back in order via `fd_fill`/`fd_take_byte` | `0` |
| `l4_wait_exit_code` | a child exiting `7` is reported as exit `7` by the migrated `wait_one` | `0` |
| `l4_win32_process_compiles` | the Win32 process wrappers COMPILE once struct-by-value + import-lib linker are present (compile-only leg) | `0` |

## Gate

`[fixpoint]` — build gen2 + the scoped fixtures + `gen2==gen3` byte-identity on POSIX, PLUS the Win32 process
leg compiling once its deps land. "Green" = the POSIX process/pipes/redirect run in Teko (own==C), the process
half of `win32_compat.h` is gone (file deleted with both halves migrated), and the emitted `teko.c` is
byte-identical to before the swap. This is the **§16-FASE6 gate**. **Reseed-class:** `fixpoint-rebuild`
(core-consumes; teaches nothing; no reseed harvested).

## Deps

`RT-L3` (`0061` — the fs/env layer + the `TargetSymbol` per-target selection the process half also uses).

## Done when

process/pipes/redirect run in Teko over fork/exec (POSIX, own==C) with the Win32 half shaped against the
resolved struct-by-value ABI + own import-lib linker, `win32_compat.h` is deleted (both halves migrated), the
POSIX fixtures exit `0`, the Win32 leg compiles once its deps land, and a `[fixpoint]` build is `gen2==gen3`
byte-identical.
