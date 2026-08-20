# DPS keystone chain — ordered, serialized crumb plan (SM-A1 → SM-A2 → SM-A3 → SM-A4)

> ## ⛔ BLOCKED — DO NOT IMPLEMENT (open owner decision, raised 2026-08-20)
> A load-bearing gap was found on starting SM-A1 and VERIFIED against the code: the return-box
> machinery this plan targets (`own_returned_value`/`box_aggregate_value_at`/`tk_slice_elem_box`)
> lives ONLY in `src/lir/lower.tks`, exercised EXCLUSIVELY by the **native** backend. The **C route**
> (`src/codegen/codegen.tks`, `Backend::C`) emits aggregate returns as plain C `return <expr>;`
> (`emit_return`, `codegen.tks:6708-6733`) and gets destination-passing FOR FREE from the C ABI's
> struct-return (sret) — it never invokes the LIR box (`grep -c "tk_slice_elem_box(" bootstrap/teko.c`
> = **0**; `codegen.tks` references to LIR/`own_returned_value` = **0**). The assessment's own census
> confirms scope: **"C-route peak ~1638 MB; native ~3.3 GB"** (assessment:94), `own_returned_value` is
> the **native** copy-out (assessment:99-105, `frame_escape.tks:26` NATIVE-AGG-SLICE-BY-ADDRESS).
> **Therefore DPS's copy-elimination, correctness (`type_match`/`frame_sweep_inst`), and build-memory
> benefits all exist ONLY in the native backend — which is DEFERRED to M4.** The §1 operative gate here
> is C-route-only. This is **verdict (b)**: DPS-now collides with native-deferred. It is an OWNER
> decision (does DPS pull native forward, or is DPS native-milestone work?), surfaced verbatim in the
> report to the coordinator. **The technical crumb design below (anchors, ABI, fixtures) is CORRECT and
> stays as the native-backend plan of record — it does NOT change — but it must NOT be dispatched onto
> the M1 C-route reseed spine until the owner rules.**
>
> **Status:** architect deliverable (design + ordering only; NO product `.tks`, NO build, NO reseed).
> **Branch:** `fix/retirement` @ HEAD `3acd43c5`. **Owner:** schivei. **Milestone:** M1 (memory
> byte-movers), required-before `SM-R1`.
>
> **This plan implements the OWNER'S ratified DPS/arena design — it does not author a new one.** The
> design is settled in `docs/design/ast-computed-arena-assessment-0.3.1.md` §4 (the ABI, the fn/type
> shapes, the safety proof, the phased sequence D0–D6) and restated as the memory-model foundation in
> `docs/design/lang-evolution-0.3.1-memory-and-surface.md` §1 ("Nothing here is new design; it is the
> ratified conclusion of the assessment doc"). This plan quotes that design verbatim and turns it into
> gate-able crumbs with VERIFIED file:line anchors, signatures, fixtures, and reseed points. Where the
> pre-existing crumb docs (`.crumbs/0002`–`0005`) or the design docs carry STALE anchors, this plan
> supersedes them and flags each divergence (§7).

---

## 0. What the owner already decided (verbatim), and what this task orders

DPS is **arena design, confirmed required** (coordinator ruling relayed 2026-08-20: "arena design, not
optional and not deferrable … reduces UAF, OOB, OOM, and cuts copy/transfer"). This supersedes the
earlier holding pattern (`DECISION_LOG.md:683`, "keystones B/DPS = HOLD p/ supervisão do dono"). The
architect does NOT re-litigate the ABI, does NOT present options.

**The keystone, in the owner's words** (`ast-computed-arena-assessment-0.3.1.md:238-241`):

> *"the callee receives a reference to the caller's arena (a destination), and `return` writes the
> value THERE and exits — no callee-local frame slot to copy out of. This is **destination-passing
> style (DPS)** for aggregate returns."*

**Governing safety principle** (assessment:462, owner decision): *"A segurança não está no tipo da
variável, está na capacidade das arenas."* The DPS destination's write-once is
**single-writer-BY-CONSTRUCTION from the call/return control flow** (assessment:502-508), never a
`let`/`mut` fact.

**The UAF guard, ruled by the owner** (assessment:434-443): DPS *"does NOT drop anything under the
callee — it redirects where the callee writes, into a destination the caller allocated in the caller's
OWN current region … The destination MUST be the caller's current region, never a fresh child (a child
dropped at scope exit could strand the returned value)."* This is the exact failure mode the falsified
`arena-por-escopo` pass hit; DPS avoids it by construction.

**The owner's phased sequence** (assessment §6, lines 607-630) is D0…D6. This task orders the
**memory-byte-mover subset** of that sequence — the "keystone chain" — which is:

| this task | owner's phase | what it is |
|---|---|---|
| (prereq, DONE) | **D1** | pin `type_match`/`frame_sweep_inst` to the return facet = `SM-P1` |
| **SM-A1** | **D0** | instrument the return-box volume (baseline the copy DPS deletes) |
| **SM-A2** | **D2** | DPS ABI + `lower_return_into_dest` + `alloc_call_dest` (the keystone) |
| **SM-A3** | **D3** | retire `own_returned_value` on the DPS path; keep `frame_escape_guard` |
| **SM-A4** | **Idea 2** | arena elision via `scope_touches_arena` (independent, composes) |

Owner phases **D4** (remove `-> ref T`), **D5** (re-base CF3), **D6** (merge `let`/`mut`) are OUT OF
SCOPE here — they are `SM-G4` / `SM-G2` in the master plan and ride their own crumbs. §6 notes the
seam so the implementer does not accidentally couple them.

**D1 / SM-P1 verdict is already banked** (`docs/memory/0.3.1-native-p1-pin-type-match-frame-sweep.md`,
resolved `4f775e8d`): **DECOUPLE / memory-only.**
- `checker::type_match` = **RETURN / TAIL-MERGE facet → DPS closes it by construction (GO).**
- `lir::frame_sweep_inst` = **PAYLOAD-BIND facet → DPS does NOT close it (NO-GO)** — that is the
  self-append family (SM-A5 + root-map grind), not this chain.

So the "two-birds" claim narrows to **ONE bird**: SM-A2/A3 close `type_match` (the return/tail-merge
blocker) and buy the memory + return-correctness win; they do NOT close `frame_sweep_inst`. This is
already reflected in `.crumbs/0003` and is NOT a re-litigation — it is the banked pin result.

---

## 1. Standing constraints baked into every crumb below

1. **C-ROUTE ONLY.** This is NOT the deferred native backend (native is gated on memory stability, M4;
   CLAUDE.md:203-209). Every SM-A crumb edits `src/lir/lower.tks` (the LIR/native lowering). With
   `ret_dest == null` reproducing today, the *behavior* of C-emitted user programs is unchanged; but
   the **compiler's own source changes**, so the transliterated `teko.c` for `lower.tks` changes — that
   is what forces the reseed. **The operative ritual is the C-route self-reproduce fixpoint
   (`gen2.c == gen3.c` byte-identity), NOT the native ladder.** The owner design's "FULL native ladder,
   gen2==gen3" wording (assessment:617, and `.crumbs/0003`–`0005`) describes the *native* realization,
   which is DEFERRED to M4 / M5 `D1-T2` (physical DPS elision). See §7 divergence D-6.

2. **SERIALIZED + PER-CRUMB HAND-HARVEST RESEED.** Standing law (CLAUDE.md:243-245, coordinator
   2026-08-20) SUPERSEDES the master plan's "(folds R1)" for this chain: **each crumb that changes
   compiler source changes `teko.c` → each gets its OWN hand-harvest reseed.** Crumbs land ONE AT A
   TIME: implementer does crumb N → reseeds `bootstrap/teko.c` → commits/pushes `fix/retirement` →
   THEN crumb N+1 begins on top of that reseed. **NO parallel reseed-bearing work.** (SM-A1 is `[dry]`,
   no reseed — see its row.)

3. **RESEED PROCEDURE (gen0 from the seed, like CI — NEVER `fetch_teko.sh`).** Law CLAUDE.md:195-201
   (provenance gate REVOKED, CLAUDE.md:238-242). The agent builds gen0 from the committed
   `bootstrap/teko.c`: `CC=clang scripts/build_gen1_from_c.sh` → gen0 → gen1 → gen2 → gen3; assert
   `gen2.c == gen3.c` (`scripts/fixpoint_gate.sh`); on green, **reseed** `bootstrap/teko.c := gen2`'s
   emitted C. `scripts/fetch_teko.sh` fails structurally in agent sandboxes (403) — never call it.

4. **MEMORY GUARD `ulimit -v 4194304` (4 GiB) = INVIOLABLE** (CLAUDE.md:210-216, owner 2026-08-20,
   lowered 6.5→4 GiB). All builds run in a subshell under this cap. **DPS is expected to REDUCE peak**
   (it deletes the `own_returned_value` box copy on every aggregate return; assessment:333-342). A
   blow-up above 4 GiB is a **signal to fix the inflation, NEVER a reason to raise the ceiling.** NOTE:
   `.crumbs/0002`–`0005` say `ulimit -v 6815744` (6.5 GiB) — STALE; use 4 GiB (§7 divergence D-7).

5. **NO PR.** `fix/retirement` already carries PR #110. Work reaches the branch by commit / cherry-pick
   only.

6. **NEVER `teko test .`** on the whole tree. Author scoped `.tkr` regression fixtures; run them
   scoped. Full gate = the C-route fixpoint above.

7. **Teko-only + full Javadoc.** All edits in `.tks`; the C twins are frozen (exception:
   `teko_rt.{c,h}` + assert). The runtime primitives DPS needs (`tk_region_alloc`, a destination
   pointer) already exist — **no new C** (assessment:657-659). Every new/edited declaration carries a
   `/** … */` Javadoc block (`@param`/`@return`/`@throws`/`@since`); NO inline `//`.

---

## 2. VERIFIED anchors (HEAD `3acd43c5`) — the roadmap was stale, these are ground truth

All line numbers below were read at HEAD. The pre-existing crumbs and design docs cite a DIFFERENT
(much larger) line space — those anchors are STALE and MUST NOT be used. Divergence table in §7.

| symbol | REAL location (HEAD 3acd43c5) | stale anchor in docs |
|---|---|---|
| `type LowerCtx` (struct to extend with `ret_dest`) | `src/lir/lower.tks:435-450` | (n/a — field is new) |
| `fn lower_return` (explicit `return e`; boxes at :2833) | `src/lir/lower.tks:2822` | `:7245` |
| `fn lower_return_fat` (fat/aggregate explicit return) | `src/lir/lower.tks:2840` | `:7278` |
| `fn lower_fn_body` — **implicit tail return, boxes at :5861** | `src/lir/lower.tks:5852` (box call `:5861`) | **MISSED by all crumbs** |
| `fn own_returned_value` (the box) | `src/lir/lower.tks:4460` | `:11715` |
| `fn box_aggregate_value_at` / `returned_aggregate_box_bytes` | `:4438` / `:4452` | (n/a) |
| `fn lower_call` (caller dispatch) | `src/lir/lower.tks:1240` | `:1740` |
| `fn region_current_vreg` (caller region handle) | `src/lir/lower.tks:647` | `:1594` |
| `fn open_native_region` (+ `bracket_depth>0` skip at :654 / :663) | `src/lir/lower.tks:653` | `:1619` / skip `:1637` |
| `fn open_frame_region` | `src/lir/lower.tks:578` | `:1392` |
| `fn lower_block_value` (tail block value) | `src/lir/lower.tks:3396` | `:10798` |
| `fn lower_match` / `lower_match_value` / `lower_match_arm_value` (tail arms) | `:4221` / `:4168` / `:4148` | `:10798` |
| `fn is_fat_type` / `is_register_value_type` | `:4255` / (widely used) | (n/a) |
| `fn frame_escape_guard` (the inversion net) | `src/lir/frame_escape.tks:9` | `:56` |
| `fn frame_sweep_inst` (PAYLOAD-BIND facet; NOT DPS) | `src/lir/frame_escape.tks:94` | (n/a) |
| profiler presize emit (`has_arena_size`/`cg_emit_arena_presize`) | `src/codegen/codegen.tks:7397` | `:9832` |

**CRITICAL anchor findings:**
- **A third box site exists.** `own_returned_value` is called at BOTH `lower_return:2833` AND
  `lower_fn_body:5861` (the implicit tail-value return — a function whose last statement is a tail
  expression, not an explicit `return`). Every crumb and the assessment name only
  `lower_return`/`lower_return_fat`; **the DPS routing and the box retirement MUST also cover
  `lower_fn_body:5861`**, or an implicit-tail aggregate return keeps boxing while an explicit one does
  not — an inconsistency that would fail `dps_aggregate_return_value_correct`. (`lower_return_fat:2840`
  itself does NOT call `own_returned_value`; it stores `fo.len` to `ret_len_slot` — the fat path is a
  separate conveyance, handled by SM-A2 step 4.)
- **`LowerCtx` has NO `ret_dest` field today** (struct is `:435-450`). SM-A2 adds it (owner's
  `with_ret_dest`, §4.2).
- **SM-A1's `tk_obs` / `#arena_size` channel is fictional** — there is no `tk_obs` channel and no
  `Confidence`/`#arena_size` symbol in `codegen.tks`. What exists is `has_arena_size`/`arena_size` on
  `checker::TFunction` and `cg_emit_arena_presize` (`codegen.tks:7397`), plus the runtime C fn
  `tk_slice_push_r` emitted at `codegen.tks:2522`. SM-A1 must define its own probe faithful to the
  owner's D0 intent ("count and size `own_returned_value`'s boxes"), NOT piggyback a nonexistent
  channel (§7 divergence D-5).

---

## 3. The owner's fn/type shapes (verbatim from assessment §4.2 — copy, do not re-author)

These are the signatures the implementer adds. They are the OWNER's, quoted from
`ast-computed-arena-assessment-0.3.1.md:262-323`. Implementer copies them verbatim in full-Javadoc
style. (The Javadoc bodies are already in the assessment; reproduced here as the load-bearing contract.)

```teko
/**
 * fn_returns_aggregate — does `f`'s declared return type need a destination slot under DPS?
 * True exactly when the return is an aggregate whose whole-type width is a fact — the SAME query
 * `own_returned_value` uses today (`returned_aggregate_box_bytes` non-null). A register-value return
 * (scalar/enum/pointer) is returned in a register and needs no destination, so DPS never applies.
 *
 * @param f    the function whose return convention is being decided
 * @param ctx  the lowering context (its registered layouts and declared-variant table)
 * @return     true iff `f` must receive a caller-provided destination for its return value
 * @since 0.3.1
 */
fn fn_returns_aggregate(f: checker::TFunction, ctx: LowerCtx): bool

/**
 * with_ret_dest — a copy of `ctx` whose return destination is `dest` (the caller-provided slot in the
 * caller's region that this function's `return` writes into), the region-axis twin of `ctx_with_regions`.
 * `null` restores today's box-on-return behaviour, so a register-value function and the pre-migration
 * path are byte-identical — the DPS path is entered ONLY when the caller passed a destination.
 *
 * @param ctx   the lowering context
 * @param dest  the destination VReg (a pointer into the caller's current region), or null
 * @return      the context whose `ret_dest` is `dest`
 * @since 0.3.1
 */
fn with_ret_dest(ctx: LowerCtx, dest: u32 | null): LowerCtx

/**
 * lower_return_into_dest — the VIRTUAL return: construct the return value directly in `ctx.ret_dest`
 * (the caller's region) and emit the bare `ret`, replacing `own_returned_value`'s post-hoc box.
 * A tail `if`/`match` feeding the return lowers each arm into the same `ret_dest`, closing the
 * tail-merge-into-return facet in one stroke.
 *
 * @param ctx  the lowering context (its `ret_dest` set by the caller's DPS call)
 * @param r    the typed return statement
 * @return     the lowered statement output, or a NAMED honest-stop for an unsupported return shape
 * @throws     when the return value cannot be placed into the destination (out-of-subset shape)
 * @since 0.3.1
 */
fn lower_return_into_dest(ctx: LowerCtx, r: checker::TReturn): LowerStmtOut | error

/**
 * alloc_call_dest — the caller half: reserve a destination for an aggregate-returning call in the
 * CALLER's current region (`region_current_vreg`), so the callee's virtual return writes into storage
 * the caller already owns and that outlives the call. The destination lives in the caller's current
 * region, NOT a fresh child that could be dropped from under the returned value — that would re-open
 * the arena-por-escopo UAF (§4.3).
 *
 * @param ctx     the lowering context at the call site
 * @param callee  the function being called (for its return layout)
 * @return        the context (with the destination alloca emitted) and the destination VReg
 * @since 0.3.1
 */
fn alloc_call_dest(ctx: LowerCtx, callee: checker::TFunction): Lowered
```

The elision predicate (owner's Idea 2, assessment §3 / umbrella §1.2):

```teko
/**
 * scope_touches_arena — true iff a scope's body performs ANY arena allocation (an array literal, an
 * aggregate materialization, a `region_alloc`, a DPS `alloc_call_dest`, or a call whose callee may
 * allocate into the current region). When FALSE for a leaf scope, the region is elided — no
 * `open_native_region`/`open_frame_region`, saving the 64 KiB floor. CONSERVATIVE: any doubt (an
 * opaque call, an unresolved capability, a non-leaf scope) returns TRUE, so the region is kept and
 * values route to the enclosing region — leak-safe, never UAF (assessment §3.3 "doubt → do not elide").
 *
 * @param body  the lowered scope body under analysis
 * @return      true iff the scope allocates into the arena (⇒ keep the region); false ⇒ safe to elide
 * @since 0.3.1
 */
fn scope_touches_arena(body: LBlock): bool
```

---

## 4. The ordered crumb sequence (serialized; one reseed at a time)

### SM-A1 — instrument the return-box volume (owner's D0)

- **Step.** Add a probe that COUNTS and SIZES every `own_returned_value` box emitted, plus the
  aggregate-return count, and reports the total. This is the baseline the DPS copy-elision (SM-A2)
  drives toward zero (owner D0, assessment:609-611: *"Extend the profiler/`frame_escape` to count and
  size `own_returned_value`'s boxes … Produces the baseline MB that justifies D2+"*).
- **Files / functions touched (verified).**
  - `src/lir/lower.tks:4460` `own_returned_value` — the box whose byte-size is summed. Its size is
    already computed by `returned_aggregate_box_bytes` (`:4452`); the probe accumulates that value.
  - Box CALL sites the counter fires at: `lower_return:2833`, **`lower_fn_body:5861`** (do not miss the
    implicit-tail site — §2). `lower_return_fat:2840` is the fat sibling (its len-store conveyance is
    counted separately or noted zero-box).
  - Report seam: the profiler presize path `src/codegen/codegen.tks:7397` (`cg_emit_arena_presize`), as
    the `Confidence::Thin`-equivalent lower-bound seed (umbrella §1.3). This is a READ into the existing
    presize input, not a new analysis.
- **Signatures added.** A small observability accumulator, e.g.
  `fn obs_return_box_record(bytes: u32)` and a report emitter — DEFINED FRESH (there is no existing
  `tk_obs` channel; §2). Gate the emission behind an env flag (an existing env-read pattern), so a
  normal build is byte-identical. NO ABI change, NO lowering-decision change.
- **Type/shape.** No `LowerCtx` field, no new `LOp`. Pure measurement.
- **Dependency edge.** Depends on `SM-P1` (DONE). Nothing depends on SM-A1 for correctness, but SM-A2
  reads its baseline to prove the win.
- **Fixtures.** `none` — the fixpoint self-build returns aggregates throughout its own source, so the
  self-build drives every box site; the probe is verified by its report, not a synthetic `.tkr` (it is
  observability, not a language capability with an error branch).
- **Gate / reseed.** `[dry]` — compile + trivial fixpoint; a NORMAL (non-probe) build is
  byte-identical. **Reseed-class: `none`** (the probe is behind an inert gate; if the gate compiles to
  identical bytes when off, `teko.c` does not move and no reseed is minted). If the probe's mere
  presence shifts emitted bytes, treat it as reseed-bearing and fold it into SM-A2's reseed rather than
  minting a separate one. **Ritual point: NONE.**

### SM-A2 — DPS ABI + `lower_return_into_dest` + `alloc_call_dest` (owner's D2 — THE KEYSTONE)

- **Step (owner's design, assessment §4.1/§4.2).** Aggregate-returning functions gain a hidden
  trailing destination parameter `ret_dest` (a pointer into the caller's CURRENT region). The caller
  (`lower_call`) reserves it via `alloc_call_dest` and passes it; the callee's `return` writes THROUGH
  it (`lower_return_into_dest`) and exits — no copy-out. **`ret_dest == null` reproduces today
  BYTE-IDENTICALLY**, so DPS is opt-in per call site and safe to land incrementally.
- **Files / functions touched (verified).**
  - `src/lir/lower.tks:435-450` `type LowerCtx` — **add field `ret_dest: u32 | null`** (owner's
    `with_ret_dest` writes it). Thread it through the `ctx_with*` copy constructors (`:458` onward)
    like the existing `region_stack`/`bracket_depth` fields.
  - `src/lir/lower.tks:1240` `lower_call` — when `fn_returns_aggregate(callee)`, call `alloc_call_dest`
    in the CALLER's current region (`region_current_vreg`, `:647`) and thread the dest as the hidden
    `ret_dest` arg; else pass `null`. Route BEFORE the existing symbol/arg dispatch (`:1254-1259`).
  - `src/lir/lower.tks:2822` `lower_return` — when `ctx.ret_dest != null`, delegate to
    `lower_return_into_dest`; else keep the `own_returned_value` box path (`:2833`) unchanged.
  - `src/lir/lower.tks:5852` `lower_fn_body` — **same routing at the implicit-tail box site (`:5861`)**:
    on `ret_dest != null`, lower the tail value into the dest instead of boxing.
  - `src/lir/lower.tks:2840` `lower_return_fat` — the fat/aggregate return: when `ret_dest != null`,
    write ptr/len into the dest rather than the current `ret_len_slot` store; else unchanged.
  - `src/lir/lower.tks:3396` `lower_block_value` and `:4168` `lower_match_value` (arms via
    `lower_match_arm_value:4148`) — the TAIL-MERGE conveyance: each tail arm lowers into the SHARED
    `ret_dest`, so a value returned through a tail `if`/`match` is born in the caller's arena. **This is
    exactly the `type_match` RETURN/TAIL-MERGE facet the SM-P1 pin confirmed DPS closes.**
  - `src/lir/lower.tks:4460` `own_returned_value` — bypassed on the DPS path (retired in SM-A3, not
    here).
  - `src/lir/frame_escape.tks:9` `frame_escape_guard` — untouched; DPS satisfies it BY CONSTRUCTION
    (value already in caller arena). Stays as the inversion net.
- **Signatures added.** `fn_returns_aggregate`, `with_ret_dest`, `lower_return_into_dest`,
  `alloc_call_dest` (verbatim §3). `alloc_call_dest` MUST allocate in `region_current_vreg`, NEVER a
  fresh child (owner's UAF guard, assessment:441-443).
- **`self` convergence (owner §8, assessment references).** The receiver `params[0]` already lowers
  by-address into `region_current_vreg`, structurally identical to `alloc_call_dest` — a
  `self`-mutating method IS a DPS write through the receiver channel. Note the shared discipline; NO
  separate codegen.
- **Dependency edge.** Depends on **SM-A1** (baseline) and the banked **SM-P1** GO on `type_match`.
- **Fixtures (owner's, assessment:634-643).** The self-build exercises DPS on every aggregate return,
  but the correctness/inversion oracles are NOT self-asserted — add these `.tkr`:

  | fixture | asserts | native exit |
  |---|---|---|
  | `dps_aggregate_return_value_correct` | a struct built in a tail `if` reads back correct fields at the caller | 0 |
  | `dps_variant_match_return` | the `type_match` shape (variant through a tail match) no longer corrupts | 0 |
  | `dps_frameset_if_return` | the `frame_sweep_inst` return shape (`return if …` of an aggregate) no longer SIGSEGVs | 0 |
  | `dps_no_frame_escape` | `frame_escape_guard` reports 0 offenders on the DPS corpus | 0 |
  | `dps_caller_dest_not_dropped` | a returned value survives the callee's scope exit (INVERSION: dest in a fresh child region must FAIL) | 0 / inversion fails |
  | `dps_dest_single_writer` | INVERSION: a synthetic 2nd live writer of the dest must FAIL borrow/exclusivity — proves aliasing safety is control-flow, not `let` | inversion fails |

- **Gate / reseed.** **RITUAL POINT.** Operative gate under C-route law (§1.1): build gen0 from
  `bootstrap/teko.c`, gen0→gen1→gen2→gen3 under `ulimit -v 4194304`, assert **`gen2.c == gen3.c`
  byte-identical** (`scripts/fixpoint_gate.sh`); the six fixtures pass at their expected exits; the
  SM-A1 box baseline drops. On green: **hand-harvest reseed `bootstrap/teko.c := gen2`**, commit, push
  `fix/retirement`. Only THEN does SM-A3 begin. (The native-ladder `gen2==gen3` the owner names is the
  M5 `D1-T2` realization — deferred, §7 D-6.)

### SM-A3 — retire `own_returned_value` on the DPS path (owner's D3)

- **Step (owner's design, assessment:618-620).** With DPS landed, the box is DEAD on the DPS path —
  the value is already born in the caller's arena. REMOVE the box emission on the `ret_dest != null`
  branch (clean dead-code removal per CLAUDE.md "NÃO EMITIR O QUE NÃO FLUI"; no tombstone). KEEP
  `frame_escape_guard` as the inversion net for residual register/closure returns.
- **Files / functions touched (verified).**
  - `src/lir/lower.tks:2833` (`lower_return`) and **`:5861` (`lower_fn_body`)** — remove the
    `own_returned_value` construction on the DPS branch; the write went straight through `ret_dest` in
    SM-A2. Do NOT delete the `own_returned_value` symbol (`:4460`) wholesale — the legacy
    `ret_dest == null` path (register-value functions, and any not-yet-DPS caller) still uses it.
  - `src/lir/frame_escape.tks:9` `frame_escape_guard` — KEPT verbatim; prove it stays clean.
- **Signatures added.** NONE — this crumb REMOVES a dead emission; no new `fn`/`type`.
- **Dependency edge.** Depends on **SM-A2** (DPS must be landed for the box to be dead).
- **Fixtures.** `none new` — the box retirement is exercised by the self-build (the compiler returns
  aggregates everywhere) and gated by the fixpoint; the escape/inversion oracle is carried by SM-A2's
  `dps_no_frame_escape` (0) and `dps_caller_dest_not_dropped` (inversion). Re-run SM-A2's set.
- **Gate / reseed.** **RITUAL POINT.** C-route fixpoint `gen2.c == gen3.c` under 4 GiB +
  `frame_escape_guard` clean + SM-A2's fixtures still green + the box no longer emits on the DPS path.
  On green: **hand-harvest reseed**, commit, push. THEN SM-A4.

### SM-A4 — arena elision via `scope_touches_arena` (owner's Idea 2)

- **Step (owner's design, assessment §3 / umbrella §1.2).** A `scope_touches_arena(body): bool`
  predicate ELIDES the region for provably alloc-free leaf scopes, saving the 64 KiB region floor per
  elided leaf (assessment:200-205). It guards the region openers EXACTLY as the existing
  `bracket_depth > 0` skip does. **Conservative: doubt → do NOT elide** (route to enclosing region —
  leak-safe, never UAF; assessment:218-222). INDEPENDENT of the DPS core (SM-A2/A3); composes with it.
- **Files / functions touched (verified).**
  - `src/lir/lower.tks:653` `open_native_region` — add the elision arm BESIDE the existing
    `bracket_depth > 0` skip (`:654` and `:663`): if the scope is a leaf AND
    `!scope_touches_arena(body)`, do not open the region.
  - `src/lir/lower.tks:578` `open_frame_region` — same gate.
  - NEW `fn scope_touches_arena(body: LBlock): bool` in `src/lir/lower.tks` (verbatim §3).
- **Signatures added.** `scope_touches_arena` (§3). Defaults to TRUE on any uncertainty.
- **Dependency edge.** Depends on **SM-A1** (the region-floor baseline the elision realizes). NOT
  dependent on SM-A2/A3 (may be AUTHORED in parallel), BUT its reseed-bearing build SERIALIZES after
  SM-A3's reseed — no two reseeds concurrent (§1.2). Sequenced LAST in this chain.
- **Fixtures.** Owner's (assessment:645):

  | fixture | asserts | native exit |
  |---|---|---|
  | `arena_elided_leaf_scope` | an alloc-free `if` arm runs correctly and emits no `tk_region_new_u` | 0 |

- **Gate / reseed.** **RITUAL POINT.** C-route fixpoint `gen2.c == gen3.c` under 4 GiB + the fixture at
  exit 0 + the region-floor volume drops. On green: **hand-harvest reseed**, commit, push. This closes
  the DPS keystone chain; the surface wave (SM-G*, COL-*) and SM-R1 proceed on top.

---

## 5. Serialization / reseed timeline (the dispatch order, explicit)

```
SM-P1 (DONE, banked 4f775e8d: DECOUPLE — type_match GO, frame_sweep NO-GO)
  │
SM-A1  [dry]      → no reseed (inert probe)               → commit/push fix/retirement
  │
SM-A2  [RITUAL]   → C-route gen2.c==gen3.c + 6 fixtures   → HAND-HARVEST RESEED → commit/push
  │                (bootstrap/teko.c := gen2)                  ↑ next crumb waits for this
SM-A3  [RITUAL]   → C-route fixpoint + frame_escape clean → HAND-HARVEST RESEED → commit/push
  │
SM-A4  [RITUAL]   → C-route fixpoint + arena_elided fixt  → HAND-HARVEST RESEED → commit/push
  │
(chain closed → SM-G* / COL-* / … → SM-R1)
```

**One reseed at a time (owner "um reseed de cada vez").** SM-A4 is design-independent of SM-A2/A3 and
its `scope_touches_arena` predicate may be AUTHORED in parallel, but its reseed-bearing build must not
run concurrently with A2/A3's — the coordinator dispatches builds strictly serially.

---

## 6. Seam to the rest of the owner's DPS sequence (NOT in this task — do not couple)

The owner's §6 sequence continues past this chain. Flagged so the implementer does not accidentally
pull them in:
- **D4 — remove `-> ref T`** (assessment §4.5): DPS SUBSUMES every `-> ref T` case (the identity
  pass-down returns caller-owned storage; under DPS the return already lands in caller storage). Zero
  production users. This is master-plan `SM-G4` — a separate crumb, rides its own reseed.
- **D5 — re-base CF3 on flow-single-assignment** (assessment §4.8): precondition of D6, byte-preserving.
- **D6 — merge `let`/`mut`** (assessment §4.8, owner decision): SAFE under DPS because the destination
  is single-writer-by-CONTROL-FLOW, not by `let` (assessment:502-516). Master-plan `SM-G2`. **The
  owner is explicit (assessment:518-527) that "DPS does not FORCE `let`/`mut` removal"** — it is
  compatible with keeping it; removal is a separate language-simplicity decision. Do NOT let SM-A2/A3
  touch `BindKind`.

---

## 7. Divergences found (current code vs the owner design docs / crumbs) — flagged, resolved

- **D-1 (anchors, ALL stale).** Every file:line in `.crumbs/0002`–`0005`, assessment §4.1/§4.2, and
  umbrella §1.1/§1.2 is stale by ~4000–7000 lines (they cite a `lower.tks` of 11700+ lines; HEAD is
  6577). **Resolution:** use the verified §2 table. This is documentation drift, not a design change.
- **D-2 (third box site MISSED).** `own_returned_value` is called at `lower_return:2833` AND
  `lower_fn_body:5861` (implicit tail return); the crumbs/assessment name only
  `lower_return`/`lower_return_fat`. **Resolution:** SM-A2 routing and SM-A3 retirement MUST cover
  `lower_fn_body:5861` too (baked into the steps above). Not a design conflict — an anchor gap.
- **D-3 (`frame_escape_guard` anchor).** Docs say `frame_escape.tks:56`; real is `:9`. `frame_sweep_inst`
  is `:94`. **Resolution:** §2 table.
- **D-4 (profiler anchor).** Docs say `codegen.tks:9832`; real presize emit is `cg_emit_arena_presize`
  at `:7397`. **Resolution:** §2 table.
- **D-5 (SM-A1 channel fictional).** No `tk_obs` channel, no `#arena_size`/`Confidence` symbol exists.
  **Resolution:** SM-A1 defines its own inert-gated probe faithful to owner D0's intent (count+size the
  boxes via `returned_aggregate_box_bytes`); this is an observability implementation detail, NOT an ABI
  decision, so no owner HALT. If the probe cannot be made byte-inert, fold its reseed into SM-A2.
- **D-6 (ritual = C-route, not native ladder).** Owner design + crumbs say "FULL native ladder,
  gen2==gen3". Standing law (CLAUDE.md:203-209, coordinator 2026-08-20) DEFERS native until memory
  stabilizes; the surface/reseed wave rides the **C route**. **Resolution:** the operative ritual NOW
  is the C-route self-reproduce `gen2.c == gen3.c` + hand-harvest reseed; the native-ladder proof of
  DPS is the M5 `D1-T2` (physical DPS elision) realization. This is a SEQUENCING law, not a change to
  the owner's ABI. `.crumbs/0003` itself already notes native gen2 was unbuildable at `4f775e8d`.
- **D-7 (memory guard 6.5→4 GiB).** Crumbs say `ulimit -v 6815744`; standing law is `ulimit -v 4194304`
  (CLAUDE.md:210). **Resolution:** use 4 GiB. DPS should REDUCE peak; a blow-up is a fix signal.
- **D-8 (reseed model: "folds R1" → per-crumb).** Master plan / crumbs mark SM-A2/A3/A4 as reseed-class
  "(folds R1)" (one reseed at SM-R1). Standing law (CLAUDE.md:243-245, coordinator 2026-08-20) is
  per-crumb hand-harvest, serialized. **Resolution:** per-crumb reseed wins for this chain (baked into
  §4/§5).

**No unfillable design gap, no HALT.** The owner's DPS ABI, safety proof, fn/type shapes, phasing, and
fixtures are fully specified in assessment §4 and consumed here verbatim. The only under-specified item
against current code is SM-A1's observability *mechanism* (D-5) — an implementation detail resolvable
law-first, not an owner decision. Every divergence above is documentation/anchor drift or a standing
sequencing law, resolved without re-litigating the design.
