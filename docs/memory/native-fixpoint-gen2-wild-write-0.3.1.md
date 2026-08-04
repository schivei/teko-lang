# The native fixpoint (gen2 == gen3) — a wild-write heap corruption, not yet localized

**Branch**: `work-native` (based on `origin/fix/issue-fat-field-layout-resolved`, tip `90b926e7`
"raiz A — size a fat aggregate field off its resolved type"). Session date: 2026-08-04.

**Goal of this session**: close the native fixpoint (`gen2 == gen3`, native backend, per
`docs/memory/plano-acao-nativo-fixpoint-0.3.1.md`). **Not reached.** This records what was proven,
ruled out, and left open, so the next session does not repeat the reproduction cost (each full
self-host run costs 25s–150s+ depending on where it stops).

## What landed

1. `scripts/fixpoint_gate.sh`'s `build_gen` now wraps the NATIVE-backend compiler invocation in
   `setarch "$(uname -m)" -R` (ASLR off), guarded by `command -v setarch` (absent on macOS). This
   was the plan's item A4/(a) — the CI theory leg was crashing at the wrong, ASLR-shuffled site
   without it. Verified locally: identical crash site across every run, gdb-attached or not.
2. Item A4/(b) — "only harvest `teko.c` when gen2 native gives exit 0" — was **already correct**:
   every Linux leg's harvest step in `pr.yml` carries `if: success()`, downstream of the Fixpoint
   step's `verdict_fail`. No change needed.

## The reproduction chain (local, `setarch -R`, per the plan's §A1 recipe)

```sh
# 1. gen1 oracle: the ancient seed C (blob 20f5bcf2, theory/reseed-tip), compiled by cc
sh scripts/build_gen1_from_c.sh <20f5bcf2 blob, saved as .c> src $SCRATCH/oracle

# 2. gen1_fix: CURRENT source (this branch, with the fat-field fix), built by the oracle, C route
TK_RT_DIR=src/runtime TEKO_BACKEND=c $SCRATCH/oracle/teko . -o $SCRATCH/gen1_fix --no-verify --release
# exit 0 — confirms the fat-field fix (90b926e7) is sound; this matches the plan's claim.

# 3. gen2: CURRENT source, built by gen1_fix, NATIVE route, ASLR pinned off
TK_RT_DIR=src/runtime TEKO_BACKEND=native setarch x86_64 -R \
  $SCRATCH/gen1_fix/out/teko . -o $SCRATCH/gen2 --no-verify --release
```

**This session's gen2 did NOT reach exit 0** (the plan document, written by a prior session,
claims it did — see "Discrepancy with the plan" below). It crashes, deterministically, inside the
native codegen phase — well before the `collect_stmt_insts` (gen2→gen3, `resolve.tks:2573`) site
the plan names. That means the fixpoint is blocked one link earlier than the plan's own state
believed: gen1_fix → gen2 itself does not yet close natively, in this environment.

## Crash A — deterministic SIGSEGV in `tk_free_take`'s free-list pop (no extra diagnostics)

3/3 runs, byte-identical crash site, both under raw exec and under `gdb -batch -ex run`:

```
teko: FATAL signal — a generated program crashed (M.1).
#0 tk_region_alloc            (teko_rt.c, inlines tk_free_take)
#1 teko_teko.lir.frame_pass_args
#2 teko_teko.lir.frame_sweep_inst
#3 teko_teko.lir.frame_sweep_block
#4 teko_teko.lir.frame_sweep
#5 teko_teko.lir.frame_marks_of
#6 teko_teko.lir.func_returns_frame_address
#7 teko_teko.lir.frame_escaping_funcs
#8 teko_teko.lir.frame_escape_guard
#9 teko_teko.build.fuse_lower_item_x86
#10 teko_teko.build.emit_native_x86
```
(native-lowering item 372/6644: `fn teko::backend::honest_globals_x86` — i.e. whichever
compiler-of-the-compiler item happened to be mid-lowering when the corruption was READ, not
necessarily where it was WRITTEN.)

Disassembly of `gen1_fix/out/teko` (built with `cc -g`, so this crash executes as C, not as
teko-native code — `gen1_fix` is a C-route binary) at the fault address, `tk_region_alloc+0x28a`:

```
mov rdi, QWORD PTR [rax]      ; rax = 0x7972 at crash time — not a valid pointer
```

This is `tk_free_take`'s inlined free-list pop (`teko_rt.c` ~line 1485):
`tk_freenode *n = *bin; *bin = n->next;` — `n->next` dereferences a **corrupted freenode**. `rax`
holding `0x7972` (looks like two stray ASCII bytes, not an address) means something wrote through
a pointer that should have been dead into memory that a PARKED (freed, size-classed) block was
using to store its free-list `next` link.

## Crash B — with `TEKO_NATIVE_CHUNK_CANARY=1`: the build SUCCEEDS, then aborts at process exit

Enabling the existing chunk-header canary (env-gated, `teko_rt.c`, checks `r->head` on every
`tk_region_alloc` entry) shifts the heisenbug to a **different** site: the native build of gen2
completes (145.7s, produces a working `teko` binary — `--version` exits 0, no C emitted), and
*then*, after `built .../teko  145.7s` is printed, the process aborts:

```
double free or corruption (out)
```

gdb (`run` + `bt full`) on the same recipe with the canary on pinned the abort exactly:

```
#6 malloc_printerr (str="double free or corruption (out)")
#7 _int_free_merge_chunk (p=0x5555bc50c4c0, size=14927453983922815232)   ; garbage size — corrupted glibc chunk header
#8 __GI___libc_free (mem=0x5555bc50c4d0)
#9 tk_registry_free            (teko_rt.c:2007 — the chunk-free walk)
#10 tk_regions_free_all        (teko_rt.c:2066)
#11 tk_exit
#12 main
```

`tk_registry_free` (teko_rt.c:2007–2018) is the FINAL sweep — at normal process exit, it walks
*every still-live region* (everything never explicitly `tk_region_drop`'d during the run — the
compiler's root/persistent regions) and `free()`s every chunk. This is the first point in the
whole run that a chunk buried anywhere in any region's list, not just an active `r->head`, gets
touched — which is why the alloc-time canary (which only checks the current head) never tripped
even once across the full 145.7s run: **the corruption is not on a chunk that was ever head again
after being written to.**

Crash A and Crash B are almost certainly the **same underlying wild write** — a native-codegen
miscompilation that overruns some allocated block and clobbers either a neighboring `tk_chunk`'s
glibc malloc header or a parked `tk_freenode`'s `next` field — surfacing at whichever allocator
touchpoint happens to read the clobbered bytes first, which shifts with anything that perturbs
timing/layout (ASLR, `TEKO_NATIVE_CHUNK_CANARY`'s extra calloc'd table and per-alloc hashing, gdb).
This matches the character fixpoint_gate.sh's own diagnostic comments already describe ("the
wild-write cumulative", "region-balance and const-size suspects... cleared") — this is the same
bug prior sessions built `TEKO_NATIVE_CHUNK_CANARY`, `TEKO_NATIVE_REGION_CHECK`,
`TEKO_NATIVE_CALL_SAFETY_CHECK`, `TEKO_NATIVE_CONST_SIZE_CHECK` and `TEKO_NATIVE_CRASH_CORE` to
chase (`git log --oneline --all --grep=CHUNK_CANARY`) — and none of those diagnostics fired during
either crash in this session, meaning **every existing instrument still comes back clean** on this
particular manifestation. That is itself informative: whatever hypothesis each of those checks was
built to test (chunk-header corruption at alloc time, region-stack imbalance, a call clobbering a
live physreg, a fat-field const sized wrong) has been independently ruled out by its own checker's
silence, at least for this exact corruption.

## Hypothesis investigated and NOT confirmed

`frame_escape.tks`'s own functions (`frame_mark`, `frame_pass_args`, `frame_sweep*`,
`frame_marks_of`) are the last few frames standing in Crash A, and they share a suggestive shape:
a `FrameSet { marked: []bool; grew: bool }` threaded functionally through deep recursion, with
`frame_mark` doing `mut fs = set.marked; fs[v] = true; return FrameSet { marked = fs; ... }` — a
struct-returning function whose field is a fat (`[]bool`) slice. `src/lir/lower.tks`'s own
`own_returned_value`/`aggregate_box_bytes` (~10630–10825) box-copies a returned STRUCT's own flat
bytes only (the `{ptr,len}` pair for a nested fat field) — unlike a function that returns a fat
value DIRECTLY, which gets the NP4 "leave the frame region before building the value" bracket
(`lower_fn_tail_fat`/`lower_conveyed_tail_expr_stmt_fat`, ~11141–11225, whose own doc says
`own_returned_value`'s aggregate axis has no equivalent for the fat axis). If a struct's fat FIELD
is grown inside a callee whose own frame region gets dropped, the returned struct could carry a
dangling pointer into it — structurally the same bug class `frame_escape_guard` itself exists to
catch (0.3.1.0 "degrau agregado-copia"), just one level of nesting deeper (a fat field *inside* a
returned aggregate, not the aggregate or the fat value alone).

**Tested and did not reproduce.** A standalone project (`repro1.tkp`) isolating exactly this shape
— `FrameSet`, `frame_mark`, a fixpoint-style loop rebuilding `FrameSet{marked=set.marked;...}` and
re-threading it through `frame_mark`, `n=4000` — built and ran correctly under
`TEKO_BACKEND=native setarch x86_64 -R` (matches the C route's output, 572). Either the hypothesis
is wrong, or the trigger needs scale/heap-layout conditions (chunk reuse pattern, allocation size
class collisions) that only appear at real self-host size — consistent with the crash also being
scale-dependent overall (doesn't reproduce small, does reproduce full-tree). NOT ruled OUT, just
not confirmed cheaply.

Also checked and ruled OUT as a mechanism for `frame_mark` specifically: the "free old buffer on
self-append grow" optimization (`assign_frees_old`/`tk_slice_push_fo`, `src/checker/escape.tks`).
It requires `assign_is_self_append` — an assignment of the EXACT shape `xs = teko::list::push(xs,
item)`. `frame_mark`'s `fs[v] = true` is an index-assignment, not a push, so this optimization
never engages for it; whatever frees/corrupts the freenode `tk_free_take` later pops from Crash A,
it is not this mechanism, at least not via `frame_mark`.

## Discrepancy with the plan document

`docs/memory/plano-acao-nativo-fixpoint-0.3.1.md` (written by a prior/different session) states
gen1_fix → gen2 native reaches **exit 0** via this exact recipe, and that the residual blocking the
fixpoint is gen2→gen3 crashing in `checker::collect_stmt_insts` (`resolve.tks:2573`). This
session's reproduction of the SAME recipe, on this container, does not reach gen2 exit 0 at all —
it dies earlier, per Crash A/B above. Given the crash is a confirmed heisenbug that visibly moves
with `TEKO_NATIVE_CHUNK_CANARY` alone, it is plausible the plan author's environment (different
kernel/glibc/heap layout) happened to dodge Crash A/B and reach the `collect_stmt_insts` site
instead — both are consistent with one root cause (a native miscompilation causing a wild write)
whose exact symptom-site is a function of memory layout. This is not a contradiction so much as
the SAME bug sampled at a different point of its non-determinism.

## What would move this forward (not attempted here — cost/budget)

- A **poison-on-free** write (fill a chunk's/freenode's payload with a recognizable pattern in
  `tk_chunk_free`/wherever `mem::free` parks a block) plus a canary that snapshots and re-checks
  **every** chunk in a region's list on each `tk_region_drop`/`tk_registry_free` walk, not just
  `r->head` at alloc time — closes the exact blind spot Crash B exposes.
- `TEKO_NATIVE_CRASH_CORE=1` (already in `teko_rt.c`, re-raises for a real core dump) + `ulimit -c
  unlimited`, post-mortem analyzed with a debugger that can walk the corrupted heap's OTHER chunks
  to find which one is out of place, rather than only the one `free()` choked on.
- A systematic bisection of the ~6644 native-lowering items (checker/monomorph/lir/backend) via a
  binary-search harness that self-hosts progressively larger subsets of `src/`, to localize which
  SOURCE FILE's lowering first introduces the corruption, rather than which item happens to be
  executING when it's finally read.

## Environment used for reproduction

- 4 vCPU / 15 GiB RAM container, `cc` = gcc 13.3.0 (Ubuntu 24.04), `setarch` from util-linux.
- gen1 oracle: git blob `20f5bcf2` (`theory/reseed-tip`'s gen1 oracle, per the plan's §A1).
- All work under `$SCRATCH` (session scratchpad), never committed — regenerable from the recipe
  above in ~5 minutes total.
