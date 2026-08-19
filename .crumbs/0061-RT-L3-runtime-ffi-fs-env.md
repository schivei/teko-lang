---
seq: 0061
crumb-id: RT-L3
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S16-FS]
sources:
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:65,67-68"   # §1.2 FFI host fs/env + time/date families
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:112-118"    # §2.1 L3 = FFI host fs/env + time/date, POSIX/Win32 leaf syscalls
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:203-214"    # §3.3 Etapa A — the fs half of win32_compat dies with L3
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:271-278"    # §4.3 struct-by-value FFI (SRES/URES) the host results cross
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:336-353"    # §6 TargetSymbol — per-target symbol selection
---

# 0061 · RT-L3 — runtime C→Teko L3: FFI host fs/env + time/date (POSIX/Win32 leaf syscalls)

> Close the L3 layer: file-system, environment, and time/date reach the host via leaf `extern fn` syscalls
> per-target — killing the fs half of `win32_compat.h`.

## Goal

L3 is the first OS-touching layer: **fs/env + time/date**, reached by leaf syscalls (`migracao…` §2.1). The
families still only in `teko_rt.c` (`migracao…` §1.2): fs/env — `tk_rt_read_file`/`_read_line`/`_read_stdin`/
`_stdin_eof`/`_getenv`/`_setenv`/`_write_file`(`_bytes`)/`_append_file`/`_chdir`/`_mkdir`/`_remove_file`/
`_getcwd`/`_list_dir`/`_last_index_of`/`tk_sort_names` (`teko_rt.c:2665-2960`); and time/date — `tk_wall_now_ns`/
`tk_jdn_to_ymd`/`tk_rt_date_*`/`_wall_days`/`_wall_ns_of_day`/`_wall_offset_minutes`/`_monotonic_ns`
(`teko_rt.c:4053-4149`). The syscall grounding for these — `open`/`stat`/`mkdir`/`getdents`/`clock`/`localtime`/
`getenv`/`getrandom`/`getrusage` — landed in **S16-FS** (FASE4, `0055`). RT-L3 migrates the runtime WRAPPERS to
Teko `extern fn` that select the host symbol PER TARGET (POSIX `chdir`/`getcwd`/`opendir` vs Win32 `_chdir`/
`_getcwd`/`FindFirstFileA`) via the `TargetSymbol` surface (`migracao…` §6), and lifts their `{ok,value,err}`
results across the FFI boundary as struct-by-value (`migracao…` §4.3). This **kills the fs half of
`win32_compat.h`** (`migracao…` §3.3 Etapa A): once the wrappers are per-target `extern`, the `#define chdir
_chdir` shims are orphaned. Byte-preserving for existing programs (fixpoint guards existing-case residence;
`migracao…` R8); a `fixpoint-rebuild` swap, no teaching reseed.

**BLOCKED (design-ahead, honest).** Behind the **native fixpoint closing** (`migracao…` banner), its dep
**S16-FS**, AND the **per-target `extern`-symbol-selection** surface (`TargetSymbol`), which today has no form
in the declaration and awaits ratification (`migracao…` §8, §3.3 Etapa A — the ONLY compiler blocker of the fs
half). The struct-by-value FFI ABI it consumes was recently resolved (`star-ref…` §4), so it is available. This
doc designs the wrappers, the `TargetSymbol` contract, and the fixtures; what stays blocked is the
`TargetSymbol` ratification.

## Where

- `src/runtime/teko_rt.c:2665-2960` — the fs/env family (`tk_rt_read_file`/…/`tk_sort_names`) — MIGRATE each to
  a Teko wrapper over the S16-FS syscall grounding, selecting the host symbol per target; C bodies go DEAD.
- `src/runtime/teko_rt.c:4053-4149` — the time/date family — MIGRATE `monotonic_ns`/`wall_now_ns` over
  `clock_gettime` (S16-FS), `jdn_to_ymd`/`civil_from_days`/offset as pure-Teko calendar arithmetic + a
  `localtime` leaf for the offset.
- `src/win32_compat.h:42-100` — the fs half (`chdir→_chdir`, `mkdir→_mkdir`, `getcwd→_getcwd`, `setenv→
  _putenv_s`, the dirent shim over `FindFirstFileA`/`FindNextFileA`) — goes ORPHANED once the wrappers are
  per-target `extern`; the file is NOT deleted here (it dies with its LAST consumer, the process half, at
  `0062` RT-L4 / M3).
- `src/runtime/teko_rt.tks` — home of the migrated fs/env/time wrappers.
- NEW decl: the `TargetSymbol` surface (per-target foreign-symbol selection) — see How §2.
- NO new user-facing fs/env/time surface: `teko::fs`/`teko::env`/`teko::time` names pre-exist; migration
  re-homes the body.

## How

1. **Migrate the fs/env wrappers to Teko over S16-FS.** `read_file`/`write_file`/`getenv`/`getcwd`/`chdir`/
   `mkdir`/`list_dir` call the grounded `open`/`stat`/`getdents`/`getenv` syscalls; results lift as struct-by-
   value `{ok,value,err}` honoring the resolved sret/register-pair classification (`migracao…` §4.3,
   `c-types-and-marshalling…` §5) — the runtime does not invent ABI, it consumes the backend's.
2. **Per-target symbol selection — the `TargetSymbol` surface** (`migracao…` §6). An `extern fn` annotated with
   `TargetSymbol` resolves the POSIX symbol on POSIX targets and the Win32 symbol on Windows, chosen at
   lowering by the build target. This is the ONLY compiler blocker of the fs half; the W15 contract the
   implementer copies verbatim:

```teko
/**
 * TargetSymbol — the per-target foreign-symbol selection the fs/process halves of the runtime need to kill
 * `win32_compat.h` without a C shim. A `TargetSymbol`-annotated `extern fn` resolves `posix` on POSIX targets
 * and `win32` on Windows, chosen at lowering by the build target — the form the declaration lacks today
 * (`arena-em-teko.md` §6, the Windows risk). Closes Etapa A of `migracao…` §3.3.
 *
 * @param posix  the libc symbol on a POSIX target (e.g. "chdir")
 * @param win32  the CRT/kernel32 symbol on Windows (e.g. "_chdir")
 * @return       the symbol binding the backend resolves per target
 * @since 0.3.1
 */
pub type TargetSymbol = struct {
    /** the symbol resolved on POSIX targets (Linux/macOS). */
    posix: str
    /** the symbol resolved on the Windows target. */
    win32: str
}
```

3. **Migrate time/date.** `monotonic_ns`/`wall_now_ns` over `clock_gettime` (S16-FS); `civil_from_days`/
   `jdn_to_ymd` as pure-Teko calendar arithmetic (leap-year correct — the S11 `localtime_civil` fixture);
   `wall_offset_minutes` via a `localtime` leaf.
4. **The fs half of `win32_compat.h` is orphaned** (`migracao…` §3.3 Etapa A): with the wrappers per-target
   `extern`, the `#define chdir _chdir` shims have no caller. The file survives only for its process-half
   consumer (L4).
5. **The link is the normal program link** (`migracao…` §2.2): the leaf syscalls resolve as undefined externals
   (`open`/`stat`/`getenv`/`clock_gettime`), like any libc symbol.
6. **Fixpoint byte-identity + per-target build.** `gen2==gen3` byte-identical on the host target proves fs/env/
   time callers did not shift; the Win32 leg must COMPILE (per-target: POSIX green, Win32 compiles) —
   `migracao…` §5 F4.

Reused (do NOT redeclare): the SRES/URES/SLRES result carriers (`{ok,value,err}`/`{ok,ptr,len}`, lifted as
struct-by-value), the S16-FS syscall grounding, `region_alloc` (L1) for boxed result strings.

## Rulings & laws

- **Teko-only:** L3 wrappers land in `src/runtime/teko_rt.tks`; the maintained-C exception is the BRIDGE the
  campaign retires (`migracao…` §1.4/R1). The `teko_rt.c` fs/env/time C goes DEAD; deletion is `0095` RM-C9 (M3).
- **W15 full Javadoc** on every touched declaration (including `TargetSymbol` + each member); flatten/extract;
  no inline `//`.
- **Removals = clean expurgo, NO tombstone:** the fs half of `win32_compat.h` is ORPHANED here, not deleted;
  the file dies with its last consumer (process half, L4/M3). Clean, tombstone-free.
- **No ABI invention (`migracao…` §4.3):** the runtime consumes the backend's resolved struct-by-value ABI for
  `{ok,value,err}`; it does not classify sret itself.
- **Honest per-target (`migracao…` §5 F4):** the fs half must compile on Win32; a POSIX-only migration that
  leaves Win32 uncompilable is not done — the `TargetSymbol` selection is the mechanism.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the `TargetSymbol` / wrapper signatures land.

## Fixtures

fs/env/time are OS-touching and not fully self-build-exercised — full isolated oracles (own==C during the
transition; `migracao…` §5 F4):

| fixture | asserts | expected |
|---|---|---|
| `l3_read_write_roundtrip` | write a file, read it back, bytes equal — via the migrated Teko wrappers, no `tk_rt_read_file` C symbol | `0` |
| `l3_list_dir_contains` | `list_dir("/")` (or a temp dir) contains a known entry; per-target symbol selected | `0` |
| `l3_env_roundtrip` | `set("TK","42")`; `get("TK")` parses to `42` | `42` |
| `l3_time_civil_leap` | `civil_from_days(0) == (1970,1,1)` and a known leap day decode correctly | `0` |
| `l3_win32_compiles` | the fs wrappers COMPILE for the Win32 target via `TargetSymbol` (compile-only leg) | `0` |

## Gate

`[fixpoint]` — build gen2 + the scoped fixtures + `gen2==gen3` byte-identity, PLUS the per-target compile of
the Win32 fs leg. "Green" = fs/env/time run in Teko over the S16-FS syscalls, results lift as struct-by-value,
the fs half of `win32_compat.h` is orphaned, the Win32 leg compiles, and the emitted `teko.c` is byte-identical
to before the swap. **Reseed-class:** `fixpoint-rebuild` (core-consumes; teaches nothing; no reseed harvested).

## Deps

`S16-FS` (`0055` — FASE4 fs+env+time+random: the `open`/`stat`/`mkdir`/`getdents`/`clock`/`localtime`/`getenv`/
`getrandom`/`getrusage` syscall grounding L3's wrappers stand on).

## Done when

fs/env + time/date run in Teko as per-target `extern` wrappers over the S16-FS syscalls with `{ok,value,err}`
struct-by-value results, the fs half of `win32_compat.h` is orphaned, the Win32 fs leg compiles, the fixtures
exit `0`, and a `[fixpoint]` build is `gen2==gen3` byte-identical.
