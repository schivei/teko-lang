# §16 — RUNTIME-PRELUDE INJECTION + the teko_rt.c-free L2 arena flip

Status: DESIGN (architect). Read-and-design ONLY — no product code is written here.
Base: `origin/fix/retirement` HEAD `25a86bdb`. Author: architect.
Supersedes: `plano-s16-arena-mmap.md` §4.2 (the weak-forwarder flip) and the abandoned branch
`origin/feat/s16-arena-switchover-l2` @ `8ce490c5` — both illegal under the freeze law.

> **Why this doc exists.** The arena L2 flip HALTED. Its single load-bearing mechanism —
> `plano-s16-arena-mmap.md` §4.2 + the abandoned branch — made the flip work by adding **weak
> `teko_teko__runtime__*` forwarders to `teko_rt.c` and prototypes to `teko_rt.h`**. The owner's
> freeze law (CLAUDE.md, 2026-08-17: `teko_rt.c`/`.h`/`win32_compat.h`/`assert.c`/`.h` are FROZEN —
> only DELETION, never edit) **forbids that mechanism**. So the flip's whole "how do ordinary user
> programs get an arena definition for the mangled Teko symbol" answer is gone. This doc replaces it
> with the owner's own vision made concrete (ruling 2026-08-17): *"o `teko.c` deve ter TUDO dentro
> dele — feito à mão em Teko ou via FFI da ABI do SO; zero C hand-written linkado."* The Teko runtime
> is **compiled INTO every emitted program's `teko.c`**, not linked from `teko_rt.o`.

---

## §0 — The exact block (ground truth, measured on HEAD `25a86bdb`)

1. **`src/runtime/arena.tks`** (namespace `teko::runtime`, mangled prefix `teko_teko__runtime__`) is
   the complete Teko-over-mmap arena, DORMANT. It compiles into the compiler's OWN self-image today:
   `bootstrap/teko.c` already carries every `teko_teko__runtime__ar_*` and
   `teko_teko__runtime__region_alloc` definition — **codegen emits every function in the program**
   (there is no dead-fn tree-shake before codegen; verified: the 197 `tk_region_alloc(` call sites
   still route to the C arena while the whole Teko arena sits emitted-but-unused). This is decisive:
   *injecting `arena.tks` into a program's file set is sufficient to get its symbols DEFINED in that
   program's `teko.c`.*
2. **`discover`** (`src/build/discover.tks`) walks ONLY the project's own `source` root
   (`dsc_walk(source, name)`). `arena.tks` lives under the COMPILER's `src/`, so it is in the
   compiler's own discover but in NO ordinary user program's discover. An ordinary program therefore
   has NO `teko::runtime` symbol.
3. **`cg_arena_sym`** (`codegen.tks:292`) is the single provider table the ~197 allocation call sites
   route through. It is OS-blind and program-blind — every kind resolves to a C `tk_*` export. Flip
   it to the Teko mangled symbols and EVERY emitted program calls `teko_teko__runtime__region_alloc`;
   ordinary programs have no definition ⇒ **link failure for all of them**. The weak forwarder was the
   only fallback; it is now illegal.
4. **`arena.tks` non-Linux arm** (`ar_mmap`/`ar_munmap`/`ar_oom` under `#else`) is a compile-only STUB
   that returns 0 ("no mapping"). Routing a macOS/Windows program to the Teko arena would OOM on the
   first allocation. The real macOS (`vm_allocate`/`mmap`-libSystem) / Windows (`VirtualAlloc`) memory
   source is a BLOCKED later §16 phase.
5. **The P2 seam already lives in `teko_rt.c`/`.h`** (`tk_arena_control_get`/`_set`/`tk_arena_paranoid`,
   declared as `extern fn … from "teko_rt"` at `arena.tks:1-26`). It needs **NO edit** — it is already
   present, frozen-but-alive under the maintained-C exception.
6. **Provider fns present**: `arena.tks` ships 12 of `CgArenaSym`'s 14 kinds. **Missing: `region_enter`,
   `region_leave`, `alloc`.** `region_current` is a single-frame STUB (returns root — no enter/leave
   current-region stack). The C twin (`teko_rt.c:2264-2281`, `:3065-3081`) backs these with a per-flow
   `cur_regions[64]`/`cur_rsp` stack; the Teko arena must grow the equivalent (crumb D, §3).

---

## §1 — THE INJECTION MECHANISM (the deliverable) — recommend option (b)

**One sentence: the build front-end injects the compiler-shipped runtime source set (`teko::runtime`
+ `teko::sys`) into EVERY non-self-image program's file set, deduped by namespace, so the Teko arena's
functions are compiled into that program's own `teko.c` exactly as they already are in the compiler's
self-image — no linked C runtime for the arena, one self-contained `teko.c`.**

### 1.1 The three options, evaluated

- **(a) codegen ALWAYS emits a runtime prelude.** Codegen carries the arena as pre-emitted C text (as
  it already carries `cg_emit_syscall_helpers`) and appends it to every program. **REJECT.** The arena
  is 1349 lines of Teko; baking its C into codegen means hand-written C living in `codegen.tks`,
  drifting from `arena.tks`, needing manual regeneration on every arena edit — the exact "feito à mão
  em Teko" law it would violate. A codegen prelude is right for a 20-line syscall helper, wrong for the
  arena.
- **(b) an implicit runtime namespace compiled into every program via the file set (RECOMMENDED).** The
  front-end prepends the shipped `teko::runtime` + `teko::sys` `.tks` files to the discovered file set;
  they flow through parse → prune → check → monomorph → codegen like any source, emitting into the one
  `teko.c`. It is Teko all the way down, single-sourced from `arena.tks`, and it reuses the EXACT path
  that already puts the arena in the self-image. **This is the recommendation.**
- **(c) a pre-compiled runtime `.tkl`/`.tkb` merged via `load_deps`.** Faster (no per-build re-check),
  but the arena depends on `teko::sys`'s **target-conditional consts** (`SYS_MMAP` is `#arch`-split),
  and `plano-s16-monolith-cc-emit.md` §7/crumb-1 **honest-stops a target-conditional const at a `.tkb`
  boundary** (no `Pred` serialization). So the arena cannot cross a `.tkb` today. **DEFER** to a future
  "runtime as a real package" issue; not on this critical path. (This is Fork 2, §7.)

### 1.2 How (b) composes — the four properties the owner's ruling demands

- **Symbols always defined (compiler self-image AND ordinary programs).** Self-image: its own discover
  already yields `src/runtime/arena.tks`, so injection is SKIPPED for `teko::runtime`/`teko::sys` (the
  dedup guard, §1.3) — self-image `teko.c` is byte-unchanged, fixpoint intact. Ordinary program:
  injection adds `arena.tks`, its `region_alloc` et al. are emitted into that program's `teko.c` → the
  mangled symbol the flipped `cg_arena_sym` references is DEFINED locally. No linked arena, no weak
  forwarder, no `teko_rt.c` edit.
- **Single `teko.c`.** Injection is a FILE-SET operation; the injected files merge into the one
  `TProgram` (`assemble_sel`, `project.tks:462`) → one translation unit → one `teko.c`. Nothing new is
  linked for the arena logic.
- **Composes with the monolith cross-`#os` emission.** The injection decision is made ONCE, per build,
  target-agnostic (the C leg is a monolith). The injected `arena.tks` carries its own `#if(os=="linux")`
  arm and the injected `teko::sys` consts carry their `#arch` ladders; `plano-s16-monolith-cc-emit.md`
  emits BOTH arms as C `#if` ladders, so the one injected `teko.c` cross-compiles on every (arch, os) —
  exactly as the self-image already does. **Dependency edge:** cross-*arch* correctness of the injected
  arena RIDES on `monolith-cc-emit` landing (else the host-arch `SYS_MMAP` is inline-folded and wrong on
  the other arch — the same single-arch limitation the whole seed has today, not a new regression).
- **Emitted-C size.** Every ordinary program's `teko.c` grows by the whole arena (~1349 Teko lines →
  low-thousands of C lines) plus the arena-referenced `teko::sys` const ladders. Absolute cost is tens
  of KB per program; it is the literal price of "TUDO dentro dele". Bounded and fixed (it does not grow
  with the user's program). See Fork 3 (§7) for the shared-object alternative.

### 1.3 The injection point (shapes the implementer adds — `src/build/project.tks`)

The seam is `frontend_parse` (`project.tks:~440`, right where `main.tks` is already injected into
`files`). Add, with full Javadoc, unique tree-wide names:

```
/**
 * rt_inject_namespaces — the compiler-shipped runtime source namespaces that are compiled into every
 * ordinary program so the Teko-over-mmap arena (and every future migrated runtime piece) is DEFINED
 * inside that program's own `teko.c` — the §16 self-contained-`teko.c` mandate. `teko::runtime` is the
 * arena provider (`arena.tks`); `teko::sys` carries the const-only ABI values the arena references
 * (`SYS_MMAP`/`PROT_*`/`MAP_*`/`SYS_EXIT_GROUP`). Intrinsics (`syscallN`/`ptr_word`/`word_ptr`/
 * `load_u64`/`store_u64`) need no source — codegen already lowers them.
 *
 * @return  the runtime namespaces injected into every non-self-image program, in link order
 * @since §16
 */
fn rt_inject_namespaces(): []str

/**
 * rt_already_present — does the project's OWN discovered file set already declare `ns`? True for the
 * compiler self-image (its `src/runtime/arena.tks` yields `teko::runtime`, its `src/sys/sys.tks`
 * yields `teko::sys`), false for an ordinary user program. This is the DEDUP GUARD: injecting a
 * namespace a program already owns would double-define every symbol, so the self-image is skipped and
 * its emitted `teko.c` stays byte-identical (the fixpoint is untouched).
 *
 * @param files  the already-discovered project file set
 * @param ns     the candidate runtime namespace
 * @return       true iff some discovered file already carries namespace `ns`
 * @since §16
 */
fn rt_already_present(files: []SourceFile, ns: str): bool

/**
 * inject_runtime_prelude — prepend the compiler-shipped runtime source files (`rt_inject_namespaces`,
 * each staged beside the compiler binary in the runtime dir `ensure_rt_dir_abs` already resolves) onto
 * `files`, skipping any namespace `rt_already_present` reports (the self-image). The Teko runtime thus
 * rides EVERY ordinary program's compilation and emits into its one `teko.c`, replacing the linked C
 * arena — with no `teko_rt.c` edit and no weak forwarder.
 *
 * @param files  the project's own discovered file set (post `main.tks` injection)
 * @return       `files` with the runtime source prepended, or the first read/parse error
 * @throws       when a shipped runtime source file is missing from the runtime dir
 * @since §16
 */
fn inject_runtime_prelude(files: []SourceFile): []SourceFile | error
```

- **Where the source ships.** The runtime `.tks` set is added to the release bundle
  (`teko-bootstrap-src.tar.gz`, `scripts/package_release.sh`) and staged beside the binary
  (`scripts/ci_provision_teko.sh::stage_seed_runtime` → `.seed/runtime/…`, `.seed/sys/…`), which is the
  dir the compiler's own `ensure_rt_dir_abs` probe already resolves for `teko_rt.{c,h}`. The C runtime
  bundle keeps shipping unchanged; the Teko runtime source is ADDED beside it. No new probe.
- **Native leg.** Injection is file-set-level and backend-agnostic; the native leg also receives
  `arena.tks`, whose syscall lowering is an honest-stop today (Doc-2 terminal) — out of scope, no
  regression.

---

## §2 — PER-`#os` PROVIDER SELECTION WITHOUT TOUCHING FROZEN C

The Teko arena is real only on Linux (§0.4). macOS/Windows must keep the C arena (`teko_rt.o`,
frozen-but-alive) UNTIL their real memory source lands. `cg_arena_sym` is OS-blind and the C leg is a
MONOLITH (one `teko.c`, all targets) — so the selector cannot pick one provider at emit time; it must
emit a per-target C-preprocessor discriminator.

### 2.1 The discriminator — a per-kind macro + a preamble `#define` ladder (RECOMMEND)

Replace `cg_arena_sym`'s bare-symbol return with a **stable per-kind MACRO name** (e.g.
`TK_ARENA_region_alloc`), and emit ONE `#if` ladder into the preamble (next to
`cg_emit_syscall_helpers`) that binds every macro per-target:

```
/**
 * cg_emit_arena_provider_ladder — emit the per-`#os` arena PROVIDER ladder into the preamble: on a
 * Linux target every `TK_ARENA_*` macro binds to the Teko-over-mmap arena's mangled symbol
 * (`teko_teko__runtime__region_alloc`, …), defined by the injected `arena.tks`; on every OTHER target
 * it binds to the C arena export (`tk_region_alloc`, …) in the frozen-but-alive `teko_rt.o`, which
 * keeps macOS/Windows correct until their real memory source lands. Emitting a `#if defined(__linux__)
 * … #else … #endif` ladder (never a host-time choice) keeps the one `teko.c` a cross-compiling
 * monolith. The macros are ALL-OR-NOTHING per target: a target binds EVERY arena kind to ONE provider,
 * so an allocation and its matching drop never straddle two arenas (§2.2).
 *
 * @param buf  the preamble byte buffer
 * @return     `buf` with the provider ladder appended
 * @since §16
 */
fn cg_emit_arena_provider_ladder(buf: []byte): []byte
```

Emitted shape (all 14 kinds; `alloc`/`region_enter`/`region_leave` included):

```
#if defined(__linux__)
#define TK_ARENA_region_alloc        teko_teko__runtime__region_alloc
#define TK_ARENA_alloc               teko_teko__runtime__alloc
#define TK_ARENA_region_new          teko_teko__runtime__region_new
/* … region_drop, region_drop_subtree, region_root, region_current, region_enter,
      region_leave, arena_push, arena_pop, arena_commit, region_register, region_lookup … */
#else
#define TK_ARENA_region_alloc        tk_region_alloc
#define TK_ARENA_alloc               tk_alloc
#define TK_ARENA_region_new          tk_region_new
/* … the C tk_* twins … */
#endif
```

`cg_arena_sym(kind)` now returns `"TK_ARENA_" + kind_suffix`; the ~197 call sites are byte-unchanged
in shape (they already route through `cg_arena_sym`). This is the WHOLE selector: one localized table
+ one preamble ladder, **zero `teko_rt.c` edit**, monolith-faithful.

Rejected alternative: a codegen boolean "Teko-arena-present" flag chosen at emit time. It cannot
express a per-`#os` split inside one monolith `teko.c` (it would bake the host's choice). The `#if`
ladder is the only monolith-faithful discriminator.

### 2.2 The all-or-nothing law (the corruption hazard the task flags)

**Mixing a C `tk_region_enter`/`tk_region_leave` with a Teko `region_current` corrupts on drop.** The
C arena's current-region stack lives in `tk_task.cur_regions` (`teko_rt.c:1357`); the Teko arena's
lives in its mmap'd CONTROL block (§3) — **disjoint state**. If `region_enter` routed to C but
`region_current`/`region_alloc` routed to Teko, an allocation would bump a region the C stack never
told the Teko arena about, and a later `region_drop` would `munmap` a chunk the other arena believes it
`malloc`'d (or double-free). Therefore the provider ladder binds **every** kind to one provider per
target — never a per-kind split. This is why crumb D (§3) must complete the Teko `region_enter`/
`region_leave`/`alloc`/real-`region_current` BEFORE the flip: the Linux arm cannot bind those macros to
Teko symbols that do not yet exist.

---

## §3 — COMPLETE THE ARENA PROVIDER (crumb D — the current-region STACK)

`arena.tks` lacks `region_enter`, `region_leave`, `alloc`, and `region_current` is a root-only stub.
Mirror the C twin (`teko_rt.c:2264-2281`, `:3065-3081`): a per-flow bounded stack in the CONTROL block.

### 3.1 CONTROL-block layout additions (append after the free bins)

The free bins run `CTRL_FREE_BINS=1152 .. 33920` (= today's `CTRL_BYTES`). Append the current-region
stack AFTER them (the Teko CONTROL block is the arena's OWN mmap'd block — independent of the C
`tk_task`, so extending it is a pure `arena.tks` edit, no `teko_rt.c` touch):

```
/** Control field: the current-region stack pointer — how many child regions are entered (the C twin's
    `cur_rsp`). May exceed `CUR_STACK_SLOTS`; the push write is guarded, the count still increments, so
    push/leave stay balanced exactly as the C twin. */
const CTRL_CUR_SP: u64 = 33920

/** Control field: the base of the current-region stack — `CUR_STACK_SLOTS` region handles; `alloc`
    bumps from the TOP, or from the root when the stack is empty (the C twin's `cur_regions`). */
const CTRL_CUR_STACK: u64 = 33928

/** How many entered child regions the current-region stack holds; a deeper enter counts but saves no
    handle, matching the C twin's `TK_REGION_STACK_MAX`. */
const CUR_STACK_SLOTS: u64 = 64

/** The control block's total size after the current-region stack: the scalars, the mark stack, the
    free bins, and the 64-slot current-region stack, rounded to the arena's alignment. */
const CTRL_BYTES: u64 = 34448
```

(The mapping is kernel zero-filled, so `CTRL_CUR_SP` starts at 0 = "root is current" with no init
write — the same free-zero the other roots rely on.)

### 3.2 The three missing provider fns + the real `region_current` (full-Javadoc, implementer-verbatim)

```
/**
 * ar_cur_current — the region default allocation bumps from RIGHT NOW: the TOP of the current-region
 * stack, or the root when nothing is entered. Mirrors the C twin's guarded read (`cur_rsp` in
 * `(0, CUR_STACK_SLOTS]` reads a non-zero slot, else falls to root), so a stack that overflowed its
 * slots still resolves to a defined region.
 *
 * @param control  the control block's base address
 * @return         the current region's base address
 */
fn ar_cur_current(control: u64): u64

/**
 * ar_cur_enter — push `child` as the current region: write it into the stack slot at the current depth
 * (guarded by `CUR_STACK_SLOTS`) and increment the depth unconditionally, so a push past the cap still
 * balances a later leave, exactly as the C twin's `tk_region_enter`.
 *
 * @param control  the control block's base address
 * @param child    the child region to make current
 * @return         nothing
 */
fn ar_cur_enter(control: u64, child: u64)

/**
 * ar_cur_leave — pop the current region, decrementing the depth when it is non-zero (a leave with an
 * empty stack is a no-op), the C twin's `tk_region_leave`.
 *
 * @param control  the control block's base address
 * @return         nothing
 */
fn ar_cur_leave(control: u64)

/**
 * region_current — the region default allocation bump-allocates from right now (C ABI: `tk_region*
 * tk_region_current()`): the top of the per-flow current-region stack, or the root when nothing is
 * entered. Replaces the single-frame stub now that the stack exists (crumb D).
 *
 * @return  the current region handle
 */
pub fn region_current(): ptr

/**
 * region_enter — make `child` the current region (C ABI: `void tk_region_enter(tk_region*)`), the
 * move-on-return open bracket.
 *
 * @param child  the child region to enter
 * @return       nothing
 */
pub fn region_enter(child: ptr)

/**
 * region_leave — restore the previous current region (C ABI: `void tk_region_leave()`), the
 * move-on-return close bracket.
 *
 * @return  nothing
 */
pub fn region_leave()

/**
 * alloc — allocate `n` bytes in the CURRENT region (C ABI: `void* tk_alloc(size_t)`): the root-region
 * convenience the C twin's `tk_alloc` provides, now bumping from the current-region stack's top so a
 * scoped child receives the allocation when one is entered.
 *
 * @param n  the requested size in bytes
 * @return   the allocated block, never null
 */
pub fn alloc(n: u64): ptr
```

`alloc`'s body reuses `ar_region_alloc_w(control, ar_cur_current(control), n)` — NOT the public
`region_alloc(region_current(), n)` — to keep it in the allocation-free dialect (one `ar_control()`,
no double `ptr`/`u64` bridge). The obs-histogram side-effect the C `tk_alloc` carries
(`teko_rt.c:3075`) is diagnostic-only and is NOT reproduced (the arena core forbids the computed-string
path it would need; the volume gate does not depend on it).

Crumb D is **behavior-inert for the corpus** (the Teko arena is still dormant until §5 flips
`cg_arena_sym`), so it is a clean add-alongside reseed: the self-image emits the three new functions
but still runs on the C arena.

---

## §4 — THE RE-DESIGNED, `teko_rt.c`-FREE L2 FLIP (supersedes §4.2)

The stale `plano-s16-arena-mmap.md` §4.2 mechanism was: flip `cg_arena_sym` to the Teko symbols and
rely on **weak `teko_teko__runtime__*` forwarders in `teko_rt.c`** to satisfy ordinary programs. That
edits a frozen file — ILLEGAL. The abandoned branch `origin/feat/s16-arena-switchover-l2` @ `8ce490c5`
implemented exactly that (its diff adds the 14 weak forwarders to `teko_rt.c` + 14 prototypes to
`teko_rt.h`) and is hereby **SUPERSEDED**.

The re-designed flip has NO forwarder. Ordinary programs get the arena DEFINITION from injected source
(§1); per-target routing is the `#if` ladder (§2); the Linux self-image runs on the Teko arena while
macOS/Windows stay on the frozen-but-alive C arena. The pieces:

1. **§1 injection** guarantees `teko_teko__runtime__region_alloc` is DEFINED in every program that a
   Linux target could route to it.
2. **§2 ladder** routes Linux → Teko symbols, non-Linux → C `tk_*`, all-or-nothing per target.
3. **§3 crumb D** makes the Linux arm bindable (enter/leave/alloc/current all exist in Teko).
4. **Retire the `arena_push`/`arena_pop`/`arena_commit` builtin injections** (the Defect-#4 mined bare
   last-segment names, `arena-em-teko.md` §8.4) from `scope.tks::builtin_fn` and the codegen call
   table, at the flip — the calls now resolve to the `teko::runtime` namespaced fns via the ladder,
   closing Defect #4 permanently.
5. **The circularity holds at the flip** (unchanged from `arena-mmap` §4.3): the arena's own bodies are
   the §1 allocation-free dialect (mmap/load/store/word_ptr/ptr_word), so no arena fn re-enters the
   arena — proven by the self-image already emitting them.

### 4.1 Fixpoint expectation

The Linux self-image now runs its OWN runtime memory on the Teko-over-mmap arena. Allocation is
behavior-transparent (same distinct pointers, same bump/rewind semantics — the arena's volume gate
proves chunk-packing identity vs the C rule), so `tc1==tc2==tc3` must hold CLEAN. A divergence
`tc1≠tc2` is a real arena bug surfaced by the fixpoint → **HALT and fix, never reseed a non-converged
arena** (a subtly-wrong arena corrupts every emitted program). The C arena stays the fallback until
non-Linux migrates (deletion is deferred, §6).

---

## §5 — ORDERED CRUMB SEQUENCE (each independently gate-able)

Per-crumb gate (standing law): `TEKO_BACKEND=c` build of gen1 COMPILES + fixpoint `gen2==gen3` +
cross-check. Validation is COMPILE-only + fixpoint; tests run in CI only (`teko test .` OOMs locally;
`ulimit -v 6291456`, drop `-g`, one build at a time — §5.1 arena-mmap memory law). Reseed at the
RITUAL points only.

- **Crumb D — arena current-region stack (RESEED, inert = add-alongside).** §3: `region_enter`/
  `region_leave`/`alloc` + real `region_current` + the CONTROL current-region-stack layout. The Teko
  arena is still dormant (`cg_arena_sym` unflipped), so the self-image emits three new functions but
  runs on the C arena — behavior-inert, clean reseed (ACHADO-A temp-id shift only). Proof: fixture
  `RF5 current_region_stack` (§6) exercises enter/alloc-to-child/leave/alloc-to-root/drop-child,
  validated on the C leg via a promoted probe (like `arena_teko`), so it is provable BEFORE the flip.
- **Crumb I0 — runtime-prelude injection scaffolding (RESEED-neutral for the self-image).** §1.3:
  `inject_runtime_prelude` + `rt_already_present` dedup in `frontend_parse`; ship + stage the runtime
  `.tks` set (`package_release.sh`, `ci_provision_teko.sh::stage_seed_runtime`). DORMANT: the arena is
  injected into ordinary programs but `cg_arena_sym` still emits C `tk_*`, so the injected arena is
  emitted-but-unused in a user `teko.c` (precisely the self-image's current L1 state, now generalized
  to user programs). The self-image dedups the injection → its `teko.c` unchanged → **no compiler
  reseed** (front-end-only change; emit is byte-identical). Proof: fixture `RF3` (ordinary program's
  `teko.c` now CONTAINS `teko_teko__runtime__region_alloc` definition) + `RF1` still exits 0 on the C
  arena.
- **Crumb E — the per-`#os` flip (RESEED, load-bearing).** §2 + §4: `cg_arena_sym` → `TK_ARENA_*`
  macros; `cg_emit_arena_provider_ladder` preamble ladder (Linux→Teko, else→C); retire the
  `arena_push`/`pop`/`commit` builtin injections. NOW the Linux self-image AND every Linux ordinary
  program run on the Teko arena; macOS/Windows stay on the C arena. **RITUAL POINT** (§6).
- **Crumb F — C-arena symbol deletion (BLOCKED, deferred).** §6. Cannot delete `tk_region_alloc` &
  family from `teko_rt.c` while non-Linux still routes to them. Gated on the macOS/Windows Teko memory
  source (`vm_allocate`/`VirtualAlloc` via FFI — a BLOCKED later §16 phase). Named, off critical path.

Dependency edges: **D → E** (the Linux arm needs enter/leave/alloc/current to exist); **I0 → E** (the
flip's mangled reference needs the injected definition in ordinary programs); **monolith-cc-emit → E**
for cross-*arch* correctness of the injected `SYS_MMAP` ladder (else host-arch inline-fold is wrong on
the other arch — pre-existing single-arch limitation, sequence `monolith-cc-emit` with/before E for the
arm64 leg). D and I0 are independent of each other.

---

## §6 — REGRESSION FIXTURES + RITUAL POINTS

Fixture pattern (the landed syscall/arena crumbs'): `examples/regressions/<name>/` with `main.tks` +
`.tkp` (`kind="binary"`) + `.tkr`; compile `--no-verify --release`, `TEKO_BACKEND=c`,
`ulimit -v 6291456`; run; read `$?`. NEVER `teko test .`.

| # | fixture | body | native exit |
|---|---|---|---|
| RF1 | `user_prog_arena_alloc` | an ORDINARY project (own source root, NO `teko::runtime`/`arena.tks`): build a `[]i64` of 10000, concat strings, sum; exit 0. Proves an ordinary program allocates correctly — on the C arena before E, on the INJECTED Teko arena after E. | `0` |
| RF2 | `user_prog_region_lifecycle` | ordinary program: `region_new`, alloc into it, `region_drop`, alloc again into root, assert root data intact; exit 0. Exercises new/drop/root across the injection boundary. | `0` |
| RF3 | `emitted_c_has_arena` (compile+grep, local) | build an ordinary program with `TEKO_BACKEND=c`; assert its `teko.c` CONTAINS a `teko_teko__runtime__region_alloc(` DEFINITION and the `#if defined(__linux__)` `TK_ARENA_*` ladder; and (after E) NO bare `tk_region_alloc(` at a Linux-routed call site. | `0` |
| RF4 | `non_linux_stays_c` (compile+grep) | assert the provider ladder's `#else` arm binds `TK_ARENA_region_alloc` to `tk_region_alloc` — a macOS/Windows build still links the frozen-but-alive C arena. | `0` |
| RF5 | `current_region_stack` | enter a child region; `alloc` (lands in child); leave; `alloc` (lands in root); drop the child; assert the child's allocations are freed and the root's survive. Mirrors the C twin's `cur_regions` semantics (crumb D). | `0` (or `42` sentinel) |
| RF6 | `mem_paranoid_user` | `TEKO_MEM_PARANOID=1` on a Linux ordinary program that frees then re-reads a block: the poison word is observed, no live corruption; exit 0. Guards the injected arena's poison path. | `0` |
| RF7 | `self_image_fixpoint` (RITUAL, not a corpus fixture) | after E, the compiler self-image converges `tc1==tc2==tc3` with the Linux runtime memory on the Teko arena. | fixpoint PASS |

**Ritual points (where the FULL gate must pass):**

1. **After crumb D** — compiler-touching (`arena.tks` new fns shift temp-ids). Gate: gen1 compiles,
   `gen2==gen3`, RF5 passes on the C leg (promoted probe), reseed `bootstrap/teko.c`.
2. **After crumb E — THE RITUAL** (the load-bearing reseed). Gate: gen1 compiles; **3-gen fixpoint
   `tc1==tc2==tc3`**; `MEM_PARANOID` exit 0; full tree; the **cross-arch matrix** (linux/x86_64 AND
   linux/arm64 — RF1 exit 0 on both, proving the injected `SYS_MMAP` ladder picks the right number);
   RF1–RF6 green; `provenance_gate.sh` PASS; then reseed. This is the most dangerous reseed in the
   project (a subtly-wrong arena corrupts every emitted program) — HALT on any `tc1≠tc2`.

---

## §7 — RISKS, LAW TENSIONS, AND THE GENUINE FORKS

Resolved, law-first (NO HALT):

- **Freeze law (the whole reason this doc exists).** No `teko_rt.c`/`.h` edit anywhere: the arena
  DEFINITION comes from injected Teko source (§1), routing from a codegen `#if` ladder (§2), the P2
  seam is ALREADY present (§0.5). The weak-forwarder mechanism (§4.2 + the abandoned branch) is
  superseded and illegal. RESOLVED.
- **C/Teko arena corruption on drop.** RESOLVED by the all-or-nothing per-`#os` ladder (§2.2) + crumb D
  making the Linux arm fully bindable (§3). Never a per-kind split.
- **Non-Linux OOM (the stub arm).** RESOLVED: the `#else` ladder arm routes non-Linux to the C arena;
  the Teko stub is compiled-but-never-called there. Deletion of the C arena is deferred (crumb F) until
  the non-Linux memory source lands.
- **Emitted-C bloat.** Bounded, fixed per program; the owner's literal "TUDO dentro dele" mandate.
  (Fork 3 offers the shared-object alternative if the owner prefers.)
- **Reachability / dead-fn pruning.** Not a tension: codegen emits every function (§0.1, empirically the
  self-image already carries the whole dormant arena). The injected arena is emitted, not shaken out.

**The genuine forks (recommended default each; the owner wants forward motion — proceed on the defaults
unless overruled):**

- **FORK 1 — the P2 mutable process word: keep the maintained-C seam, or go pure-Teko now?** The
  injected arena still calls `tk_arena_control_get`/`_set`/`tk_arena_paranoid` (`extern fn … from
  "teko_rt"`), so EVERY ordinary program STILL links `teko_rt.o` for those 3 tiny fns (+ `tk_task`,
  `tk_panic`, `exit`, `fmt`) — "zero C linked" is NOT reached by the arena flip alone; it arrives at the
  §16 SWEEP. The purist alternative is a pure-Teko module-mutable word (`.bss`/thread-local) — a real
  language-surface addition. **DEFAULT: keep the maintained-C seam** (it already exists, freeze law
  permits it, it is the SAFEST per `arena-mmap` §1.3) and defer the pure-Teko module word to its own
  future crumb. Flag: the owner's "zero C linked" endstate eventually needs the pure-Teko word or the
  seam's migration in the SWEEP. This is the deepest fork.

- **FORK 2 — inject the runtime as staged SOURCE, or as a pre-checked library?** Source injection
  (§1, option b) re-checks ~1349 arena lines every build (small cost) but is the only form that works
  today, because the arena's target-conditional `teko::sys` consts CANNOT cross a `.tkb` boundary
  (`monolith-cc-emit` §7 honest-stop). A pre-compiled `.tkl` (option c) is faster but blocked on `.tkb`
  target-conditional-const serialization. **DEFAULT: staged source injection**; revisit the library form
  when "runtime as a real package" (the `.tkb` target-const crossing) is designed. REPORTED, not
  invented here.

- **FORK 3 — inline the arena into every `teko.c`, or link a shared Teko-compiled arena object?**
  Inlining (§1) makes each program self-contained (the literal mandate) at a fixed per-program size
  cost. The alternative compiles `arena.tks` ONCE into a per-target `libteko_arena.o` and links it —
  smaller `teko.c`, but one more linked object. That object is TEKO-compiled, not hand-written C, so it
  arguably satisfies "zero C hand-written linkado" while relaxing "TUDO dentro dele". **DEFAULT: inline
  (self-contained)**, because the owner's wording is explicit ("o `teko.c` deve ter TUDO dentro dele")
  and the bloat is bounded; surface the shared object only if per-program `teko.c` size becomes a
  measured problem.

No fork blocks progress; each has a law-first default. **NO HALT** — the design is fully drafted and
crumbs D/I0/E are buildable on the declared shapes. What remains genuinely BLOCKED is only crumb F
(C-arena deletion), gated on the non-Linux Teko memory source, which is a separately-tracked later §16
phase.

---

## §8 — Section index

- §0 Ground truth (the block: injection-not-linked, discover, `cg_arena_sym`, the stub, the P2 seam, the 3 missing fns)
- §1 The injection mechanism — option (b) recommended (front-end runtime-namespace injection, dedup guard, shapes)
- §2 Per-`#os` provider selection (the `#if` `#define` ladder; the all-or-nothing corruption law)
- §3 Complete the arena provider — crumb D (CONTROL current-region stack + enter/leave/alloc/current)
- §4 The `teko_rt.c`-free L2 flip (supersedes §4.2 + the abandoned branch; fixpoint expectation)
- §5 Ordered crumb sequence (D → I0 → E; F blocked)
- §6 Regression fixtures RF1–RF7 + the two ritual points
- §7 Risks + the 3 genuine forks (P2 seam; source-vs-library; inline-vs-shared-object)
