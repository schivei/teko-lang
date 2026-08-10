# AST-computed arena model — impact & savings assessment (0.3.1)

**Base:** `fix/retirement` @ `4992c801` (root-map landed). **Scope:** READ + design-doc ONLY. No
product code. This is a **decision-support** pass — feasibility, quantified savings, risk, and one
convergence verdict per the owner's three-idea vision. Where a number is a guess, it says so.

> **The owner's vision, preserved verbatim (three ideas):**
> 1. *"cada arena tem seu piso pré-calculado em tempo de montagem da AST, calculando antecipadamente
>    qual o tamanho mínimo esperado em runtime. Assim cada arena já nasceria com um tamanho,
>    reduzindo realocação."*
> 2. *"pode chegar à conclusão que não há necessidade de uma arena em determinada região, no caso da
>    expressão não tocar a arena."*
> 3. *"tratar return sempre como alocação do valor diretamente na arena de quem chamou a função… a
>    função chamada teria a referência da arena do chamador e nosso return seria virtual — seta na
>    arena do caller e sai."*

---

## 0. Executive verdict up front

| idea | feasible today? | rough savings | build? |
|---|---|---|---|
| **1 — AST-computed arena floor** | PARTIAL. The AST proves a *lower bound*, not the runtime size; the DYNAMIC path (`#arena_size` + profiler p99.9) already sizes better for dynamic collections | **chunk-floor reading: tens of MB** (tail-waste class, ~26 MB / 21 k chunks). The **1.8 GB** the owner hopes to hit lives at the SLICE layer, not the region layer — reachable only for literal-count collections | LATER / fold into profiler |
| **2 — arena elision** | YES. Escape analysis + a static "touches-the-arena?" walk already have the inputs; one guard on `open_native_region`/`open_frame_region` | **saves the 64 KiB floor per elided leaf region** (`TK_REGION_DEFAULT_CHUNK`). % of regions elidable is a GUESS (est. 20–45 % of lexical scopes are alloc-free leaves) until measured | YES, cheap independent win |
| **3 — virtual return into caller's arena (destination-passing)** | YES, but it is a STRUCTURAL native return-ABI change. It *formalizes* the move-on-return model that already exists (`own_returned_value` + `frame_escape_guard`) | reclaims a large share of the **1926 MB root retention** on the return axis AND removes the retrofitted post-hoc box; **plus** it is a correctness fix (see §4) | **YES — this is the headline; build first, gated on pinning** |

**THE HEADLINE (two-birds):** **Idea 3 is not merely a memory optimization — it is the correct
native return-conveyance model, and it structurally closes the RETURN facet of the corruption deep
root the root-map names** (`native-variant-match-root-map-0.3.1.md` §3). Destination-passing makes a
returned aggregate be *born* in the caller's storage instead of being an address of a callee frame
slot that must be copied out. That directly removes the "returned past its frame slot" boundary — the
family `type_match` and `frame_sweep_inst` are classified into. It does **not** touch the *self-append
/ push-in-loop* boundary (`push_inst_block`), which remains AL3/`grow_inplace` territory. So the
honest count is **two of the three pinned blockers plausibly close on the return facet, one does
not** — Idea 3 spans the return boundary rigorously; AL3 spans the append boundary; together they
span the deep root. **Build/don't-build: BUILD Idea 3 first**, gated on cheap-pinning `type_match`/
`frame_sweep_inst` (root-map C2/C3) to confirm their fault is the return-facet before the ABI change.

**Owner addendum folded in (`-> ref T` removal):** Idea 3 eliminates the `-> ref T` return form
entirely, and the corpus proves this is nearly free — **zero production `-> ref T` functions exist**
(one probe, two rejection-regressions). It STRENGTHENS Idea 3: the migration removes a feature rather
than adding one. Detail in §4.5.

---

## 1. The current model, measured (what the three ideas plug into)

### 1.1 A region is a chunk LIST, not a realloc buffer — this reframes Idea 1

`tk_region_alloc` (`teko_rt.c:2034`) bumps within the head chunk; when the request does not fit it
**PREPENDS a fresh chunk** of `max(request, TK_REGION_DEFAULT_CHUNK)` (`teko_rt.c:2061`,
`TK_REGION_DEFAULT_CHUNK = 64 KiB`, `teko_rt.h:155`). **A region growing NEVER copies its bytes** —
it links another chunk. So the "realocação" the owner's Idea 1 attacks is **not** in the region: the
region layer has no copy-grow at all.

The O(n²) copy-grow lives one layer down, at the SLICE: `tk_slice_push_r` (`teko_rt.c:1272`)
allocates a NEW larger buffer, `memcpy`s old→new, and **abandons the old buffer inside the region**.
With reclaim 0 %, every abandoned doubling-half stays resident until the region drops. This is the
~1.8 GB the AL-Wave targets (`al1-proof-report.md`: 1138 MB copy-grow measured in one symbolized
run; ~1.8 GB peak attributed). **This distinction is decisive for Idea 1 (§2).**

The in-place cure already exists as a primitive: `tk_slice_grow_inplace` (`teko_rt.c` / `teko_rt.h:1287`,
the F3 "Model A" append) appends without abandoning **when cap is available** — this is AL3's
`push(&x, v)` ref-push. It is present but not yet the pervasive emit path.

### 1.2 The retention picture (profiler §7, verified against source)

| quantity | value | where |
|---|---|---|
| root (never freed) | **1926 MB** | task-root, dropped only at process exit (`teko_rt.h:133`) |
| reclaim ratio | **0.0 %** | nothing dropped mid-build |
| scoped (freed at drop) | **0.0 MB** | scopes open regions, free nothing useful |
| str unroutable | **66 MB / 2.17 M buffers** | `tk_str_concat_len` has no `_r` twin on the native path |

The wall is **retention, not iteration**, and — critically — **memory is NOT the native fixpoint
wall**: the self-emit crashes at ~1.3 GB on a by-address UAF, not OOM. So all three ideas are framed
as **build-memory + throughput + (for Idea 3) correctness**, never as a fixpoint unblocker for the
OOM story. (C-route peak ~1638 MB; native ~3.3 GB with a TProgram clone; measured per-generation
drift 1638→2742 MB.)

### 1.3 The return conveyance that ALREADY exists — Idea 3 extends this, not greenfield

The native backend already holds "an aggregate's value IS an address" (`frame_escape.tks:26`,
NATIVE-AGG-SLICE-BY-ADDRESS) and already has a copy-out-at-return discipline:

- `own_returned_value` (`lower.tks:11715`) — before the `ret`, if the value is an aggregate, it emits
  a FRESH copy (`box_aggregate_value_at` / `returned_aggregate_box_bytes`) into storage that outlives
  the frame. The doc is explicit that the copy must be **callee-side** because a caller-side copy runs
  after the frame is popped and lands on the very bytes it reads (`lower.tks:11706`).
- `binding_conveys_escape` (`lower.tks:1510`) + the `emit_region_leaves … emit_region_enters` bracket
  — the "move-on-return M2" that detours an escaping binding's allocations down to the conveyance
  anchor `rr` (the caller's region at the NP6 move).
- `frame_escape_guard` (`frame_escape.tks:56`) — the INVERSION instrument: over the own-native corpus
  it names **41 functions** that return a frame address WITHOUT the copy, **0 WITH it**, and it still
  caught two escapes no fixture saw (a returned closure, a zero-width array literal).

**Read this carefully: the compiler already implements a leaky, retrofitted version of Idea 3.**
`own_returned_value` is copy-out-AFTER-the-fact; Idea 3 is construct-in-place-FROM-the-start. The
guard's very existence — and its two surprise escapes — is the evidence that the post-hoc copy is a
patch, not a model.

---

## 2. Idea 1 — AST-computed arena floor (pre-sized arenas)

### 2.1 Feasibility vs the current model

Two readings, and they land in very different places:

**(a) Pre-size the REGION chunk (literal reading of "arena tem seu piso pré-calculado").** The AST
can compute a lower bound on a region's first-chunk demand: sum the fixed-width allocations a scope's
statements provably make (struct literals of known layout, array literals of known element count,
box sites). Plug point: `open_frame_region` (`lower.tks:1392`) / `open_native_region`
(`lower.tks:1619`) would pass a computed floor to a `tk_region_new_sized_u(parent, floor)` instead of
letting the first alloc pull a default 64 KiB. **This is exactly what `#arena_size` presize already
does dynamically** (`codegen.tks:9832`, `cg_emit_arena_presize`) and what the profiler's newsvendor
p99.9 quantile sizes from measured watermarks (`o-profiler-…` §2.1). Idea 1 is the STATIC-floor
complement to that dynamic path.

**(b) Pre-size the SLICE capacity.** This is the one that would touch the 1.8 GB: allocate a slice at
its final capacity so the doubling ladder — and its abandoned halves — never happens. The AST proves
final capacity ONLY for literal-count collections (an array literal, a fixed fan-out, a
`for x in fixed`), never for a dynamic accumulation loop (the dominant copy-grow sites:
`inline_rw_block`, `resolve_type`, `type_param_table`, `cg_lift_block` — `al1-proof-report.md`). For
those the final size is a runtime fact; the honest tool is AL3 `grow_inplace` (amortized, no
abandonment) + the profiler's p99.9 floor, not a static AST number.

### 2.2 Quantified impact

- **Chunk-floor reading (a):** removes `posix_memalign` churn and tail waste. Tail waste is measured
  at **26.6 MB / 21 134 chunks = 1.32 KB/chunk** (`o-profiler-…` §2.1). Pre-sizing a region that
  today pulls N default chunks to pull 1 right-sized chunk saves ~(N-1) × per-chunk overhead + the
  allocator calls. **Order of tens of MB of resident tail + a throughput win on `posix_memalign`
  count.** It does **not** touch the 1.8 GB (that is abandoned slice halves inside chunks, not chunk
  count).
- **Slice-cap reading (b):** for literal-count collections only, eliminates their doubling
  abandonment. **Bounded and small** — the al1 census found ~92 % of push-sites are genuinely dynamic
  (`al1-proof-report.md`), so the statically-final subset is the minority ~8 %. GUESS: single-digit
  percent of the 1.8 GB. The 1.8 GB is AL3's number, not Idea 1's.

### 2.3 Risk

- **Byte-preservation:** LOW. A region born larger emits the same content bytes; the fixpoint
  `gen2==gen3` is byte-identity of the self-emit, which does not depend on chunk sizing. The
  arena-por-escopo doc's byte-preservation argument (`arena-por-escopo-0.3.1.md` §4.4: emit is not a
  function of a buffer address) carries over.
- **Under-floor UAF:** NONE — a floor is a lower bound; the region still grows past it normally.
  Over-floor is leak-safe (reserved-not-used), the same asymmetry the profiler's newsvendor cost
  models (`o-profiler-…` §2.1, `c_sobra = 1 byte/byte`).

### 2.4 Verdict on Idea 1

Feasible in reading (a), modest (tens of MB). Reading (b) is where the owner's 1.8 GB hope lives, but
the AST cannot deliver it for the dynamic majority — **AL3 already owns that number**. Recommendation:
do NOT build Idea 1 as a standalone static analysis. Fold the static-floor as an *input to the
profiler's existing `#arena_size` mechanism* (a static lower bound seeds the `Confidence::Thin` case
where the dynamic sample is absent), and let AL3 `grow_inplace` own the slice-cap win. **Lowest
priority of the three.**

---

## 3. Idea 2 — arena elision

### 3.1 Feasibility vs the current model

**HIGH, and cheap.** The inputs already exist:

- The escape check (`escape.tks`) already walks a function classifying frame-local vs escaping.
- The static coverage-style walk (`o-profiler-…` §1.1: `cov_walk_expr`/`cov_walk_stmt` precedent)
  already enumerates per-scope sites.
- `open_native_region` already has a *skip* precedent — the `bracket_depth > 0` guard (`lower.tks:1637`)
  emits no `tk_region_new_u`/`enter` and pushes no frame when inside a conveyance bracket, and
  `close_native_region` mirrors it (`lower.tks:1667`). **The elision machinery is a proven pattern in
  the same function.**

What has to be added: a predicate `scope_touches_arena(body) -> bool` — true iff the scope's
statements contain at least one routable allocation site (a `push`/`box`/struct-init/array-lit/str-
concat/`tk_alloc`). When false, `open_native_region`/`open_frame_region` skip the
`tk_region_new_u`/`enter_u` (and the paired `drop_u`), exactly as the bracket-skip already does. This
is a pure additive guard; the region stack stays balanced by the same symmetry.

### 3.2 Quantified impact

Each USED region costs **64 KiB minimum** (`TK_REGION_DEFAULT_CHUNK`; the profiler's §2.2 "brutal"
finding: a block holding 200 bytes costs 64 KiB — a 300× loss). Elision removes that floor for every
alloc-free leaf scope. **Savings = 64 KiB × (elided regions live simultaneously).** Because the cost
is per *simultaneously-live* region (bounded by depth, not total count — `o-profiler-…` §0.3), the
resident win is `64 KiB × depth-of-alloc-free-leaves`. This is modest in absolute MB but removes
allocator churn on the hot path (every leaf scope entry/exit today emits new/enter/leave/drop).

**% elidable is a GUESS: 20–45 % of lexical scopes** are alloc-free leaves (an `if x { return a }`,
a comparison arm, a guard block). This must be MEASURED by the static walk before the number is
trusted — the walk gives the denominator (`o-profiler-…` §1.1), and the profiler's dynamic pass gives
which of those were hot.

### 3.3 Risk

- **Byte-preservation:** LOW-MEDIUM. Eliding a region removes `tk_region_new_u`/`enter`/`leave`/`drop`
  instructions from the emit — that DOES change emitted bytes, but deterministically, so `gen2==gen3`
  holds (the same class as any lowering change: gen1≠gen2 expected, gen2==gen3 preserved). Guard:
  gate on the full native ladder.
- **UAF:** LOW. Eliding a region that allocates NOTHING routable cannot orphan a pointer — there is no
  allocation to strand. The predicate must be CONSERVATIVE (doubt → do not elide), the same
  soundness posture as `escape.tks:9-12` ("doubt → escaping"). A missed routable site would route its
  alloc to the enclosing region (leak-safe), never UAF — provided the elision only drops the region
  wrapper, never redirects an existing alloc.
- **Composition with move-on-return:** CLEAN. An alloc-free scope conveys nothing, so the conveyance
  brackets never fire inside it.

### 3.4 Verdict on Idea 2

**Build it — small, safe, independent.** It is a guard in the same function that already has the
skip pattern. It is the cheapest of the three and composes with everything. Priority: SECOND (after
Idea 3's design, but it can land in parallel since it touches only the open/close guard).

---

## 4. Idea 3 — virtual return into caller's arena (destination-passing) — THE HEADLINE

### 4.1 Feasibility vs the current model

**Feasible, and it is the RIGHT model — but it is a structural native return-ABI change.** Idea 3
says: the callee receives a reference to the caller's arena (a destination), and `return` writes the
value THERE and exits — no callee-local frame slot to copy out of. This is **destination-passing
style (DPS)** for aggregate returns.

The current model (`own_returned_value`, §1.3) already computes the missing half — it knows a return
is an aggregate (`returned_aggregate_box_bytes`), it knows the box size, and the conveyance anchor
`rr` is *already the caller's region at the NP6 move* (`lower.tks:1100`, `:1407`). Idea 3 turns
"construct in a callee frame slot, then box a copy into `rr`" into "construct directly into a
caller-provided destination in the caller's region." **The plumbing (ret_type layout, caller-region
handle, the box-size query) is all present.** What changes:

- **The LIR call ABI** gains a hidden first argument for aggregate-returning functions: a destination
  pointer in the caller's current region.
- **`lower_return`** (`lower.tks:7245`) stops calling `own_returned_value` and instead threads the
  value's construction into `ctx.ret_dest`.
- **Tail merges feeding the return** (`lower_block_value` / `lower_match` tail, `lower.tks:10798`)
  target the SAME `ret_dest`, so an `if`/`match` value in tail position builds each arm directly into
  the destination — this is the facet that matters for `type_match`/`frame_sweep_inst` (§4.4).
- **The caller** (`lower_call`, `lower.tks:1740`) allocates the destination via `region_current_vreg`
  (`lower.tks:1594`) before the call and passes it.

### 4.2 Type / function shapes the implementer will add (Teko, full-Javadoc)

```teko
/**
 * fn_returns_aggregate — does `f`'s declared return type need a destination slot under DPS?
 *
 * True exactly when the return is an aggregate whose whole-type width is a fact — the SAME query
 * `own_returned_value` uses today (`returned_aggregate_box_bytes` non-null). A register-value return
 * (scalar/enum/pointer) is returned in a register and needs no destination, so DPS never applies.
 *
 * @param f    the function whose return convention is being decided
 * @param ctx  the lowering context (its registered layouts and declared-variant table)
 * @return     true iff `f` must receive a caller-provided destination for its return value
 * @since 0.3.1
 */
fn fn_returns_aggregate(f: checker::TFunction, ctx: LowerCtx) -> bool

/**
 * with_ret_dest — a copy of `ctx` whose return destination is `dest` (the caller-provided slot in the
 * caller's region that this function's `return` writes into), the region-axis twin of `ctx_with_regions`.
 *
 * `null` restores today's box-on-return behaviour, so a register-value function and the pre-migration
 * path are byte-identical — the DPS path is entered ONLY when the caller passed a destination.
 *
 * @param ctx   the lowering context
 * @param dest  the destination VReg (a pointer into the caller's current region), or null
 * @return      the context whose `ret_dest` is `dest`
 * @since 0.3.1
 */
fn with_ret_dest(ctx: LowerCtx, dest: u32 | null) -> LowerCtx

/**
 * lower_return_into_dest — the VIRTUAL return: construct the return value directly in `ctx.ret_dest`
 * (the caller's region) and emit the bare `ret`, replacing `own_returned_value`'s post-hoc box.
 *
 * This is the copy-out-at-conveyance discipline the root-map (§3) names, applied to the RETURN
 * boundary BY CONSTRUCTION: the value is never an address of a callee frame slot, so there is nothing
 * to copy out and nothing for `frame_escape_guard` to catch. A tail `if`/`match` feeding the return
 * lowers each arm into the same `ret_dest`, closing the tail-merge-into-return facet in one stroke.
 *
 * @param ctx  the lowering context (its `ret_dest` set by the caller's DPS call)
 * @param r    the typed return statement
 * @return     the lowered statement output, or a NAMED honest-stop for an unsupported return shape
 * @throws     when the return value cannot be placed into the destination (out-of-subset shape)
 * @since 0.3.1
 */
fn lower_return_into_dest(ctx: LowerCtx, r: checker::TReturn) -> LowerStmtOut | error

/**
 * alloc_call_dest — the caller half: reserve a destination for an aggregate-returning call in the
 * CALLER's current region (`region_current_vreg`), so the callee's virtual return writes into storage
 * the caller already owns and that outlives the call.
 *
 * The destination lives in the caller's current region, NOT a fresh child that could be dropped from
 * under the returned value — that would re-open the arena-por-escopo UAF (§4.3). It is exactly the
 * region the caller would itself allocate the result into after a copy today.
 *
 * @param ctx     the lowering context at the call site
 * @param callee  the function being called (for its return layout)
 * @return        the context (with the destination alloca emitted) and the destination VReg
 * @since 0.3.1
 */
fn alloc_call_dest(ctx: LowerCtx, callee: checker::TFunction) -> Lowered
```

Existing functions touched: `lower_return`/`lower_return_fat` (`:7245`/`:7278`), `lower_call`
(`:1740` dispatch), `lower_block_value`/`lower_match` tail placement (`:10798`), and the RETIREMENT of
`own_returned_value` (`:11715`) on the DPS path. `frame_escape_guard` (`frame_escape.tks:56`) becomes
trivially satisfied for DPS functions and stays as the inversion net for the residual register/closure
cases.

### 4.3 Quantified impact / savings

- **Move/copy removed:** every aggregate return today pays `own_returned_value`'s box — a fresh alloc
  + `tk_mem_copy`/`tk_slice_elem_box` of the whole aggregate, callee-side, on EVERY return. DPS
  removes that copy entirely (the value is built in place once). The al1 census shows the return/box
  axis is pervasive across the checker (`inline_rw_block` 117 MB, `type_block` 56 MB, `mono_block`
  58 MB copy-grow — much of it value-threaded returns). GUESS: **a meaningful fraction of the return-
  axis share of the 1926 MB root**, because a DPS return in the caller's region also means the value
  no longer needs to escape to root at all — it lands where it is used and dies with the caller's
  scope. Honest bound: this is the strongest retention lever after AL3, but the exact MB is unmeasured
  until the DPS path exists (the `own_returned_value` box volume is the number to instrument first,
  and the profiler's `copy_bytes` field, `o-profiler-…` §8, is designed to carry it).
- **Retention reclaimed:** because the return value is caller-region-resident, the frontend scratch
  that today escapes to root *because it fed a return* can now die in the caller's scope. This is the
  arena-por-escopo doc's own observation (`arena-por-escopo-0.3.1.md` §"O que fica FORA"): the
  per-scope model *"would REMOVE the need for the clone — the scratch would die in the scope instead
  of surviving to the phase drop."* **Idea 3 is that per-scope model for the return channel.**

### 4.4 The convergence verdict — does Idea 3 close the corruption deep-root?

The root-map (`native-variant-match-root-map-0.3.1.md` §3) names the deep root:

> *aggregates are the address of a per-instruction frame slot, and are not reliably copied-out at
> conveyance boundaries — a match/if merge block-arg, a slice self-append, a return past the slot's
> region, or a push in a loop.*

Four boundaries: **merge / self-append / return / push-in-loop.** Mapping Idea 3 onto each:

| boundary | pinned blocker | does DPS close it? |
|---|---|---|
| **return past frame slot** | (the shared facet) | **YES — by construction.** The value is born in caller storage; there is no callee frame slot to return past |
| **tail merge feeding a return** | `type_match` (returns `TExpr` through `match_join_anchor` merge in tail), `frame_sweep_inst` (returns `FrameSet` through `return if …`) | **YES, plausibly** — each tail arm lowers into the shared `ret_dest`, so the merge writes the destination directly, never a per-arm frame slot |
| **self-append / push-in-loop** | `push_inst_block` (corrupt slice header in `append_inst`) | **NO** — this is the append boundary; AL3 `grow_inplace` owns it, not DPS |
| **non-tail merge / stored-then-returned** | (residual) | PARTIAL — needs the copy-out discipline generalized beyond returns |

**Verdict: Idea 3 rigorously closes the RETURN boundary and, for tail merges (the shape both
`type_match` and `frame_sweep_inst` actually have), the merge-into-return boundary too — so it is a
strong candidate to fix TWO of the three pinned blockers, IF pinning confirms their fault is the
return facet rather than a non-tail merge or a payload-bind offset.** It does not touch
`push_inst_block` (self-append). This is genuinely a **two-birds** finding: the same change buys
build-memory savings AND the correct return-conveyance model that retires the retrofitted
`own_returned_value` + `frame_escape_guard` patch. It reframes Idea 3 from "memory optimization" to
"the native return model," which changes its priority to FIRST.

**The honest boundary on the two-birds claim:** the root-map explicitly did NOT disassemble the three
faults (cost budget), so whether `type_match`/`frame_sweep_inst` fault on the *return* vs the *merge
block-arg* vs a *payload-bind offset* is unconfirmed. DPS closes return + tail-merge; it does not
close a payload-bind offset bug. **Gate Idea 3's build on root-map crumbs C2/C3 (cheap-pin those two)
— if they pin to the return/tail-merge facet, Idea 3 is the systemic fix the root-map's §3 hoped for
but honestly refused to bank on. If they pin to payload-bind, Idea 3 still buys the memory and the
return-correctness, but is not the whole corruption story.**

### 4.5 The `-> ref T` elimination (owner addendum)

**What `-> ref T` is used for today (grepped):** ONLY the *identity pass-down* — a function may
return one of its own `ref` PARAMETERS unchanged (`typer.tks:5373` `check_ref_return_passdown`).
Every other reference return (a `&x` of a local, a `ref` local, a stored field) is a conservative
**honest-stop**, rejected pending the transitive-escape spine (`ref_passdown_error`, `typer.tks:5408`).

**Production users: ZERO.** The only `-> ref` occurrences in the whole tree (excluding worktrees):
- `examples/regressions/own_native/src/ref_bind/probe.tks:89` — `hand_back(ref ch: Ch) -> ref Ch`,
  a probe of the pass-down itself.
- `examples/regressions/diagnostics/src/c52_ref_return_local_rejected/case.tks:4` — a REJECTION test.
- `examples/regressions/diagnostics/src/c49_ref_null_local_escape_rejected/case.tks:4` — a REJECTION
  test.

No stdlib, no compiler-source function returns a reference. The feature is vestigial.

**Does DPS subsume all `-> ref T` cases?** YES. The identity pass-down returns the caller's own `ref`
parameter — storage the caller already owns and can re-derive itself. Under DPS every aggregate
return already lands in the caller's storage, so a *reference*-typed return is redundant: the caller
has the value where it wanted it. The one honest-stop case (`ref` to a local) never worked anyway.
**No genuine case needs distinct reference-return semantics** once returns are caller-region-resident.

**Surface removed:** the `Reference` return-type arm and its gate cluster — `check_ref_return_passdown`,
`check_ref_return_passdown_stmt`, `check_ref_return_passdown_inexpr`, `ref_passdown_error`,
`ref_value_is_passdown`, plus the two invocation sites (`typer.tks:5992`, `:6378`) and
`collect_ref_param_names`'s return-gate use — **~5 checker functions + 2 call sites + 1 diagnostic**,
the ref-return lowering path, and the `-> ref T` grammar/docs. One fewer return KIND in the type
system.

**F1 interaction:** after removal, `ref` survives ONLY on PARAMETERS (`&x`/`ref x` args — write-
through, requires a mutable source, `typer.tks:4351`; e.g. `grow_inplace(ref []T)`, the AL3 primitive).
`ref` becomes a CALLER→CALLEE borrowing device exclusively, never a CALLEE→CALLER escape device. **That
is a strictly cleaner, more consistent model:** the escape direction is handled uniformly by
destination-passing, and the borrow direction by F1 exclusivity — two orthogonal mechanisms instead of
`ref` straddling both.

**Strengthens or complicates Idea 3?** **STRENGTHENS.** The migration REMOVES a feature (fewer moving
parts in checker + lowering + grammar), and there are **zero production call sites to rewrite** — the
one probe is retargeted, the two rejection tests are retired or converted (the form ceasing to parse
IS the new rejection). It removes the single return-form whose semantics compete with DPS.

### 4.6 Risk

- **Byte-preservation (the fixpoint is inviolable):** SAFE for `gen2==gen3`. DPS changes the native
  return ABI deterministically, so gen1≠gen2 (expected — gen1 uses the seed's old lowering) but
  gen2==gen3 (both use DPS). It is NOT byte-preserving for the C-vs-own diff — but that diff already
  differs and is NOT the fixpoint (the root-map §3 flags exactly this; the inviolable invariant is
  self-emit determinism, not native==C). **This is the key risk-resolution the root-map left open:
  DPS is compatible with the fixpoint precisely because the fixpoint is self-consistency.**
- **UAF / aliasing — the exact hazard the arena-por-escopo pass hit:** the falsified pass
  (`arena-por-escopo-0.3.1.md`; wrapper +765 MB, drop = UAF) failed because it BULK-DROPPED a region
  while move-on-return still aliased it. **DPS does NOT drop anything under the callee** — it redirects
  *where the callee writes*, into a destination the caller allocated in the caller's OWN current
  region (`alloc_call_dest`, §4.2). There is no premature drop, so the arena-por-escopo failure mode
  does not recur. Against F1 exclusivity: the destination is write-once, callee-exclusive until the
  return, then caller-exclusive after — this is F1's borrow discipline applied to the return channel,
  and it is SOUND where region-per-frontend was not. The destination MUST be the caller's current
  region, never a fresh child (a child dropped at scope exit could strand the returned value — the
  guard is in `alloc_call_dest`'s doc).
- **Composition with move-on-return:** DPS SUBSUMES the move-on-return conveyance for the return case
  — `binding_conveys_escape`'s bracket detour to `rr` becomes unnecessary for returns (the value is
  already in caller storage). Escaping BINDINGS/ASSIGNS via non-return channels still need their
  handling, so the bracket machinery is not fully retired, but the return axis leaves it. The frame-
  region model composes cleanly because DPS removes returns from the set of things the frame region
  must convey out.

### 4.7 Verdict on Idea 3

**Build it — FIRST — gated on cheap-pinning.** It is the correct native return model, it retires a
retrofitted patch, it is the strongest post-AL3 retention lever, it eliminates a vestigial feature
(`-> ref T`), and it plausibly closes two of the three pinned corruption blockers. The gate is
root-map C2/C3 (pin `type_match`/`frame_sweep_inst` to confirm the return facet) — do that BEFORE the
ABI change so the change is aimed, not speculative.

---

## 5. Does Idea 3 subsume/replace AL3 and the abandoned arena-por-escopo?

- **AL3 (ref-push / `grow_inplace`):** NO — complementary. AL3 owns the self-append / push-in-loop
  boundary; DPS owns the return / tail-merge boundary. Together they span the deep root's four
  boundaries. `push_inst_block` needs AL3, not DPS. Build both.
- **arena-por-escopo (FALSIFIED bulk-clone pass):** DPS makes it UNNECESSARY rather than replacing it.
  That pass tried to bulk-drop the whole frontend region after a total `TProgram` clone; it failed to
  compose with move-on-return. DPS (+ Idea 2 elision) is the finer, per-value discipline that lets
  frontend scratch die in-scope without a bulk clone — which the arena-por-escopo doc itself names as
  the deeper fix that would remove the clone (`arena-por-escopo-0.3.1.md` §"O que fica FORA"). Do not
  revive the bulk pass; pursue DPS.

---

## 6. Recommendation and crumb-sequence sketch

**Build order:** Idea 3 (gated) → Idea 2 (parallel, cheap) → Idea 1 (fold into profiler, lowest).

**Idea 3 crumb sketch (each independently gate-able; ritual = full native ladder where noted):**

1. **D0 — instrument the return-box volume.** Extend the profiler/`frame_escape` to count and size
   `own_returned_value`'s boxes (the `copy_bytes` field, `o-profiler-…` §8). Gate: builds, `teko
   test .`, trivial fixpoint. Produces the baseline MB that justifies D2+. Ritual: NO.
2. **D1 — pin the two return-facet blockers (root-map C2/C3).** Cheap-pin `type_match` +
   `frame_sweep_inst`; confirm return/tail-merge facet. Gate: faulting load attributed. Ritual: NO
   (repro only). **This is the go/no-go for the ABI change.**
3. **D2 — DPS ABI + `lower_return_into_dest` + `alloc_call_dest`**, DPS entered only when a
   destination is passed (`ret_dest = null` = today's path, byte-identical). Tail merges target
   `ret_dest`. Gate: **FULL native ladder, FIXPOINT gen2==gen3.** Ritual: YES.
4. **D3 — retire `own_returned_value` on the DPS path**; `frame_escape_guard` stays as the inversion
   net for residual register/closure returns. Gate: FIXPOINT + `frame_escape_guard` clean + the two
   pinned blockers green. Ritual: YES.
5. **D4 — remove `-> ref T`** (owner addendum): drop the `Reference` return arm + gate cluster
   (§4.5), retarget the one probe, retire the two rejection tests. Gate: FIXPOINT + `teko test .`;
   the form ceasing to parse is the new rejection fixture. Ritual: YES.

**Regression fixtures to add (inputs → native exit codes):**

| fixture | asserts | exit |
|---|---|---|
| `dps_aggregate_return_value_correct` | a fn returning a struct built in a tail `if` reads back correct fields at the caller | 0 |
| `dps_variant_match_return` | the `type_match` shape (variant returned through a tail match) no longer corrupts | 0 |
| `dps_frameset_if_return` | the `frame_sweep_inst` shape (`return if …` of an aggregate) no longer SIGSEGVs | 0 |
| `dps_no_frame_escape` | `frame_escape_guard` reports 0 offenders on the DPS corpus | 0 |
| `dps_caller_dest_not_dropped` | a returned value survives the callee's scope exit (inversion: with the dest in a fresh child region it must FAIL) | 0 / (inversion fails) |
| `ref_return_form_rejected` | `fn f() -> ref T` no longer parses/typechecks | EXPECT_COMPILE_FAIL |
| `arena_elided_leaf_scope` (Idea 2) | an alloc-free `if` arm emits no `tk_region_new_u` | 0 |
| `arena_floor_presized` (Idea 1) | a literal-count collection is born at final cap, no doubling | 0 |

**Ritual points (full gate must pass):** after D2, D3, D4 (each changes the native return emit);
after Idea 2's elision guard; Idea 1's floor rides the profiler's existing gates.

**Risks + law tensions (recommended resolution):**
- *Fixpoint vs C-vs-own divergence:* RESOLVED — the inviolable invariant is `gen2==gen3` self-emit
  determinism, which DPS preserves; native≠C is already true and not the fixpoint. No HALT.
- *Aliasing/UAF:* RESOLVED by construction — destination in the caller's CURRENT region, no premature
  drop, F1 exclusivity on the write-once destination. The arena-por-escopo failure mode does not
  recur. No HALT.
- *Teko-only / frozen twins:* DPS is a `.tks` lowering change; the C runtime primitives it needs
  (`tk_region_alloc`, destination pointer) already exist — no new C, or at most a documented additive
  runtime helper, within the runtime exception. No HALT.

**No genuine unresolved law tension — nothing HALTs.** This is an assessment; the single owner-
decision gate is go/no-go on D1's pin result, which is engineering, not a law conflict.

---

## 7. What I did NOT measure (stated, not estimated)

1. The exact MB the DPS return path reclaims — D0 instruments it; every savings figure for Idea 3 in
   §4.3 is bounded by the `own_returned_value` box volume, which is unmeasured until D0 runs.
2. The % of lexical scopes that are alloc-free leaves (Idea 2's elision rate) — the static walk gives
   it; the 20–45 % is a GUESS.
3. Whether `type_match`/`frame_sweep_inst` fault on the return facet vs a payload-bind offset — the
   root-map did not disassemble them; D1 is the disambiguator and the go/no-go for the two-birds claim.
4. Idea 1's slice-cap subset size — al1 says ~8 % of push-sites are statically-final; the MB that
   converts to is not separately measured.
