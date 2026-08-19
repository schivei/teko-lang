---
section: design
created: 2026-08-19
status: DESIGN — no product line. The three-part 0.3.1 SCOPED-MEMORY correction
        (owner spec 2026-08-19): (#1) scoped arena by FINE scope, (#2) native stream at all io
        points with buffers reclaimed by the scope arena, (#3) total purge of
        ::push/::empty/grow_inplace/with_cap. Extends the canonical arena spec; supersedes it only
        where the owner's new fine-scope + purge decree diverges (§1).
source: owner spec 2026-08-19 (scoped-memory correction) reconciled with
        docs/design/arena-especificacao-unica-0.3.1.md (source of truth), io-streaming-0.3.1.md
        (io decree 2026-08-19), modelo-de-memoria-por-escopo-0.3.1.md, arena-por-escopo-0.3.1.md,
        backend-memoria-por-funcao-0.3.1.md, lang-evolution-0.3.1-memory-and-surface.md (S0/S1/S2).
frozen: bootstrap/teko.c + the C checker/codegen/build twins are OUTPUT/FROZEN; the only editable C
        is src/runtime/teko_rt.{c,h} (runtime exception). New product work is `.tks` only. All work
        drains DIRECT into fix/retirement (no PR) — owner ruling 2026-08-15.
---

# Scoped arena, native stream, and the growable-primitive purge (0.3.1)

Architect, 2026-08-19. Base: `origin/fix/retirement` @ `78be487d`. DESIGN document — no product
line. This is the owner's three-part SCOPED-MEMORY correction, sequenced #1 (foundation) → #2 (stream
buffers hang off #1) → #3 (purge, because the arena replaces the growable primitives).

---

## 0. Reconciliation — what already exists (do NOT reinvent)

This correction does not start from zero. It EXTENDS the canonical model and SUPERSEDES it only at the
two points the owner's new decree moves. Cited, with the divergence made explicit.

| existing doc | what it owns | this doc's relation |
|---|---|---|
| `docs/design/arena-especificacao-unica-0.3.1.md` (owner ruling 2026-08-10) | THE source of truth: region mechanics (§1), the region tree lifecycle (§2), the AST **floor/piso** (§3), **elision** `need==0` (§4), **DPS** move-on-return (§5), the **four escape boundaries** (§6), concurrency/F1/F2 (§7), DI-as-arena (§8) | **EXTENDS.** #1 here is the *fine-scope* refinement of its §2/§4/§6 the owner now decrees; #3 here removes the growable primitives its §1.2 named (`grow_inplace`/`with_cap`). Where the two disagree on `grow_inplace`, **THIS wins** (§1, §5). |
| `docs/design/modelo-de-memoria-por-escopo-0.3.1.md` (2026-08-02) | the per-scope LANGUAGE model: the 5 lexical scopes, `ResidencePlan`, the `residence_plan` oracle, `region_enter`/`region_leave` | **REUSES verbatim.** The `ResidencePlan`/`residence_plan` oracle and `region_enter`/`leave` primitive are the plumbing #1 lowers onto. #1 adds a 6th scope (the OBJECT arena) and tightens the reclaim rule to the depth check (§3 here). |
| `docs/design/io-streaming-0.3.1.md` (owner decree 2026-08-19) | the native-stream io SURFACE (`FileStream`, `open_*`, `stream_read/write/seek/close`, `write_stream`/`read_stream`, `byte_ptr`), the syscall-per-OS map, the io-site migration census | **REUSES verbatim and BINDS to #1.** #2 here is NOT a second io surface — it is that surface with its 1024 B scratch buffer bound to the enclosing scope arena of #1 (§7 here). It also names its own §9 co-dependency ("closes together with the arena") — this doc is that arena. |
| `docs/design/arena-por-escopo-0.3.1.md` (2026-08-07, M0–M3) | the compiler CROSS-PHASE retention axis (`clone_tprogram`, front-region drop, `tk_str_concat_len_r`) | **ORTHOGONAL, complementary.** That axis frees the frontend overhang BETWEEN phases; #1 here frees dead scratch WITHIN each fine scope. Both compose; neither touches the other's hot file at its core. `tk_str_concat_len_r` (M2 there) is reused by #2 here for stream-adjacent str. |
| `docs/design/backend-memoria-por-funcao-0.3.1.md` | the compiler-scratch per-function region + the shared `tk_region_enter`/`leave` primitive | **SHARES the primitive.** One runtime `enter`/`leave`, two consumers (compiler scratch there; program-generated scope arenas here). Coordinate, do not duplicate. |
| `docs/design/lang-evolution-0.3.1-memory-and-surface.md` §5 (S0/S1/S2) | the allocation-seam evolution vocabulary | **THE seam this ties into** (§2 here). |

**The single divergence to state loudly:** `arena-especificacao-unica` §1.2/§6 keeps `tk_slice_grow_inplace`
(AL3/Model A) as the cure for the self-append boundary ("append without abandoning when there is cap").
The owner's 2026-08-19 correction **retires `grow_inplace` entirely** (#3), because #1 makes abandoning
FREE: the abandoned copy-grow halves die at arm exit in the scope arena, so the in-place mutation
primitive — which needs an F1 exclusive-borrow proof and a 3-word `{ptr,len,cap}` header the native
backend cannot yet represent (`lower.tks:4339`, `typer.tks:844`) — has no reason to exist. Where §6 of
the canonical spec assigns the self-append boundary to AL3, **this doc reassigns it to the scope arena
(#1)**. Everything else in the canonical spec stands.

---

## 1. The S0/S1/S2 seam this rides on (the allocation flow today)

The evolution names three allocation seams (`teko_rt.h:144-188`, `lang-evolution §5`):

- **S0 — the COMPILER-internal seam** in `core.h`: static-inline `tk_alloc`/`tk_realloc0`/`tk_free0`/
  `tk_alloc_copy`, internal linkage, over libc. Every allocation the compiler-as-a-C-program makes
  flows through this one point (`teko_rt.h:154-157`). It stays on libc until the list migration threads
  old-size through the header — out of this doc's scope; noted only as the frozen floor.
- **S1 — the RUNTIME seam** `teko_rt.c::tk_alloc` (`teko_rt.h:144-158`): now bump-allocates from the
  process ROOT region instead of `malloc`. Behavior-preserving because root is never dropped =
  today's leak (M.5). **This is the seam #1 re-points**: the default allocation target moves from
  "root, fixed" to "the CURRENT region" (`tk_region_current()`, `teko_rt.h:371-375`) — which under #1
  is the enclosing fine-scope arena, not root.
- **S2 — the REGION model** (`teko_rt.h:175-188`, `362-375`): `tk_region_new(parent)` /
  `tk_region_enter(child)` / `tk_region_leave()` / `tk_region_drop(child)` (bulk-free, O(1)), the
  per-task current-region stack, and the region-aware slice primitives (`tk_slice_push_r`, etc.).
  **#1 is the S2 keystone applied at FINE granularity.**

The flow #1 installs: **every default allocation lands in `tk_region_current()`; each fine scope makes
that current region its own child on entry and drops it on the arm's exit edge.** S0 is untouched
(frozen); S1's default target becomes current-region; S2's tree gains one child per executed fine scope.

---

## 2. Part #1 — the scoped arena, by FINE scope

### 2.1 The scope taxonomy — exactly which scopes get an arena

The canonical spec (`modelo-de-memoria-por-escopo §4`) enumerates FIVE lexical scopes. The owner's
2026-08-19 correction refines this to the FINE-scope taxonomy: not "the whole `if`" but **each arm**
separately, plus the object arena. The exact set that materializes an arena:

| # | fine scope | arena birth | arena death (arm exit edge) | notes |
|---|---|---|---|---|
| 1 | **bare block** `{ … }` | on entry, if `need>0` | on the block's exit edge | the base case |
| 2 | **loop arm** (body of `loop`/`loop while`/`for`) | on entry of EACH iteration | on EACH iteration's exit edge (back-edge) | per-iteration reclaim; NEVER flattened across the back-edge (`modelo §7` carve-out) |
| 3 | **`if` then-arm** | on arm entry, if `need>0` | on the then-arm exit edge | each arm is its OWN scope (the refinement) |
| 4 | **`elseif` arm** | on arm entry, if `need>0` | on the elseif-arm exit edge | distinct from the `if` arm |
| 5 | **`else` arm** | on arm entry, if `need>0` | on the else-arm exit edge | distinct arm |
| 6 | **`match`/`when` arm** | on arm entry, if `need>0` | on the matched arm's exit edge | one arena per arm, not per match |
| 7 | **object arena** (per instance) | when the instance is constructed | when the instance dies (its owner scope's exit edge, transitively) | the NON-lexical scope; §2.4 |
| — | **fn body** (frame) | on call entry | on the return edge | the canonical frame region; DPS (§5 canonical) routes the RETURN value up, not this |

**The elision rule is unchanged and governs 1–6:** a fine scope with `need==0` (no routable allocation
site inside it — `scope_touches_arena` false, `arena-especificacao-unica §4`) opens NO arena; the
`if x { return a }` leaf, a bare comparison arm, a guard block cost nothing. **Doubt → do not elide**
(conservative, leak-safe, never UAF). This is the same skip the native backend already proves with
`bracket_depth > 0` (`lower.tks:1637`).

**The floor governs the SIZE (1–7):** a scope that does allocate is born sized to its proven need +
header via `tk_region_new_sized_u(parent, need + header)` (`arena-especificacao-unica §3`), not the
64 KiB default. Elision is the `need==0` limit of the floor.

### 2.2 The arena lifecycle per fine scope

Uniform across 1–6 (the loop arm's "exit edge" is per-iteration; everything else once):

```
// on the arm's ENTER edge, when need>0:
child = region_new_sized(region_current(), floor(scope) + header)   // sized by the AST floor
region_enter(child)                                                 // tk_alloc now bumps into `child`
// ... arm body: every default alloc, every push copy-grow half, every str concat lands in `child` ...
// on the arm's EXIT edge:
region_leave()                                                      // current returns to the parent arena
region_drop(child)                                                  // bulk-free ALL of the arm's dead scratch — O(1)
```

- The `region_enter`/`region_leave`/`region_drop`/`region_new_sized` primitives already exist
  (`teko_rt.h:362-375`, `arena-especificacao-unica §2.1`, `modelo §14`). #1 adds NO runtime primitive;
  it emits the enter/leave/drop pair at each fine-scope boundary in the two lowerings.
- **What lands in the arm arena (reclaimed):** every local born-and-dead within the arm — the scratch
  of §0's dominant term: the abandoned copy-grow halves (`tk_slice_push_r(child)`), the concatenated
  `str` (`tk_str_concat_len_r(child)`), the boxed aggregate elements, the interpolation buffers, the
  io scratch buffer (#2). ALL of it dies at the arm's exit edge instead of leaking to root.
- **What does NOT land here (escapes → stays):** anything the escape check (§3) proves is referenced
  from a SHALLOWER scope. It is routed to that shallower region at birth (the LUB), never reclaimed
  here. The return value goes up by DPS (canonical §5), untouched by the arm drop.

### 2.3 The object arena (scope #7) — the non-lexical fine scope

The owner's list adds "PLUS an object arena." An object (struct/class instance) that owns interior
allocations — grown members, boxed fields, its `[]T`/`str` payloads — needs those interiors to live
exactly as long as the object, no longer. The object arena is that lifetime:

- **Birth:** when the instance is constructed, an object arena is (conceptually) the child region into
  which the instance's owned interior allocations bump. For a stack/scope-local instance this arena is
  the SAME as the enclosing fine-scope arena (no separate region — the instance dies with the scope,
  so its interiors do too). A distinct object arena materializes only when the instance's lifetime is
  DECOUPLED from a single lexical arm — i.e. it is moved/returned (DPS) or stored into a longer-lived
  container.
- **Death:** the object arena drops when the object dies — transitively, the exit edge of whatever
  scope the object's residence resolves to (the LUB of the object's uses, §3). Reclaiming the object
  reclaims its interiors in one O(1) drop.
- **Why it is a fine scope, not a special case:** it obeys the exact same birth/floor/escape/drop
  discipline as 1–6; its "arm exit edge" is the object's death instead of a lexical `}`. It is the
  arena spec's §5 DPS destination made explicit: the caller's storage for a returned aggregate IS the
  object arena, sized by the object's floor.

**Boundary with DPS (canonical §5):** DPS decides WHERE a returned aggregate is BORN (the caller's
current arena). The object arena is the same region viewed as "the instance's own reclaimable span."
No conflict: DPS places, the object arena reclaims, both name the caller's current region.

### 2.4 Object-arena vs the free-list forks

Two sub-questions the owner will rule on (forks §9): whether the object arena is (a) always the
enclosing scope arena (zero extra regions; the object simply dies with its scope) or (b) a dedicated
per-object region when the object outlives its birth arm (needed for objects stored into F2/root
containers with per-entry reclaim — the same "F2 per-entry free-list" `arena-especificacao-unica §7.8`
already requires for `chan`/journal). Recommendation: (a) by default (fine-scope arena IS the object
arena), (b) only for the F2/container case that already needs per-entry reclaim. This keeps #1 to
zero new region kinds for the common case.

---

## 3. The escape / safety analysis — the single region-depth check

### 3.1 The rule (what is reclaimable vs escaping, per scope)

The evolution already names this check. `concorrencia-adiantada-s8.md:44`: *"S2 scope regions + escape
check — per-fn done, block-arm not — the escape check BY DEPTH COMPARISON exists; the fine granularity
does not."* And `onde-esta-a-memoria-do-compilador.md:158` marks the same: `per-fn checked, block-arm
open`. **#1 is exactly the promotion of that depth-comparison escape check from per-function to
per-fine-scope.** There is ONE check, and it is a depth comparison:

> **The region-depth escape check (the single rule):** assign every fine scope a monotone region
> DEPTH along the current-region stack — the fn frame is depth 0, and each nested fine scope that opens
> an arena is depth+1 (the loop arm, the if-arm, the block, the object arena). An allocation BORN at
> depth `d` is **reclaimable at that scope's exit edge** if, and ONLY if, **every live reference to it
> is at depth ≥ d** (deeper-or-equal — i.e. dies with, or inside, the birth scope). If ANY live use is
> at depth `< d` (a SHALLOWER, longer-lived scope), the allocation **ESCAPES**: it is born instead in
> the shallowest such scope's region (the LUB of its uses), and depth `d`'s drop never touches it.

This is the `pt_join` LUB of `arena-especificacao-unica §1`/`modelo §1` expressed as an integer depth
comparison: the residence region = the shallowest (smallest-depth) scope that dominates all uses.
`spine.tks::pt_join` (`spine.tks:495`) already computes the LUB; the depth is its rank. The check is a
single `min` over use-depths, compared against the birth depth.

### 3.2 Why it is sound (never UAF — the preserved theorem)

`modelo §1` proves it and #1 inherits the proof verbatim: the residence = LUB(uses) DOMINATES every
use by definition of least-upper-bound, so every use occurs within the residence's lifetime; a UAF
requires a use AFTER the residence dies, impossible when the residence dominates all uses. The
depth-comparison is the same theorem: born-at-`d` and dropped-at-`d`'s-exit is safe iff no use outlives
depth `d`, i.e. no use at depth `<d`. **Doubt → escape to the shallower region (leak-safe, never UAF).**
This is the M.1 contract (`escape.tks:9-12`): "when the analysis cannot PROVE frame-local, treat as
escaping — a leak is safe, a use-after-free is a vulnerability."

### 3.3 The three concrete escape channels, per fine scope

Mapped onto the canonical four boundaries (`arena-especificacao-unica §6`), specialized to a fine scope:

| channel | example (inside an arm at depth d) | verdict | routing |
|---|---|---|---|
| **arm-local scratch** | `var t = concat(a, b)` used only inside the arm | reclaimable | born in `child`, dropped at arm exit |
| **returned up** (DPS) | `return Point{…}` in a tail arm | escapes to caller | born in caller's current arena (DPS, canonical §5); arm drop skips it |
| **stored into a shallower binding** | `outer = push(outer, x)` where `outer` is at depth `<d` | escapes to `outer`'s depth | born in `outer`'s region (the depth-`<d` arena); arm drop skips it |
| **sent cross-thread** | `chan.send(x)` | escapes to program (F2) | born in `tk_region_program()`; `is_unique_at` gate (canonical §7) |

The dominant win (§0's ~1.8 GB copy-grow) is the FIRST row: the abandoned halves of a `push` chain
whose result never leaves the arm are pure arm-local scratch, reclaimed at arm exit. That is why #3's
purge is safe — the peak-inflation the growable primitives create is exactly this reclaimable scratch.

### 3.4 What the checker produces (reuse the oracle)

No new analysis engine. `residence_plan(f)` (`modelo §14`, the `ResidencePlan` artifact) is EXTENDED to
emit, per binding and per allocation site, the fine-scope depth of its residence (the min-use-depth),
not just a coarse tier. Both lowerings consume the SAME plan (the invariant that C and native never
disagree, `escape.tks:405`). The extension is: the scope index the plan already carries becomes a
fine-scope index (arm-granular), and the tier gains the depth integer.

```teko
/**
 * ResidenceDepth — the region DEPTH at which a value resides: 0 is the fn frame, and each nested
 * fine scope that opens an arena is one deeper. A value is reclaimable at the exit edge of the
 * scope whose depth EQUALS its residence depth; a residence depth SHALLOWER than the birth scope
 * means the value escapes to that shallower arena (the LUB of its uses). This is the integer form
 * of `spine::pt_join`'s rank — the single region-depth escape check.
 *
 * @since 0.3.1
 */
pub type ResidenceDepth = u32

/**
 * fine_scope_residence — the region-depth escape check at FINE granularity. For each local binding
 * and each routable allocation site in `f`, computes the min-use-depth across the fine-scope tree
 * (arm-granular: each if/elseif/else/match arm, loop arm, bare block, and object arena is its own
 * depth). The site is reclaimable at the exit edge of the scope whose depth equals the returned
 * depth; a returned depth shallower than the birth scope routes the birth to that shallower arena.
 * Uses `spine::pt_join` (the transitive LUB, `spine.tks:495`) as the precise source and `escape.tks`
 * as the conservative fast path. Doubt resolves to the shallower depth (leak-safe, never UAF).
 *
 * @param f  the typed function to analyse
 * @return   the residence plan, each entry carrying its fine-scope residence depth
 * @since 0.3.1
 */
pub fn fine_scope_residence(f: checker::TFunction): checker::ResidencePlan

/**
 * scope_touches_arena — the elision predicate at fine granularity: true iff `body` contains at least
 * one routable allocation site (push/box/struct-init/array-lit/str-concat/tk_alloc). A fine scope for
 * which this is false opens NO arena (the `need==0` limit of the floor). Conservative: doubt returns
 * true (open the arena; leak-safe, never a redirected allocation). Mirrors the existing
 * `bracket_depth > 0` skip (`lower.tks:1637`).
 *
 * @param body  the arm/block body to test
 * @return      true when the scope must open an arena, false when it may be elided
 * @since 0.3.1
 */
pub fn scope_touches_arena(body: []@checker::TStatement()): bool
```

Existing functions touched: `escape.tks` `fn_escaping_vars`/`binding_is_block_local` (feed the plan),
`spine.tks` `pt_join`/`is_unique_at` (LUB + cross-thread gate), `modelo`'s `residence_plan` (extended
to carry depth), the two lowering entry/exit-of-arm emitters (§6).

---

## 4. Part #2 — native stream at ALL io points, buffers reclaimed by #1

### 4.1 The surface is already designed — bind it to the arena

`docs/design/io-streaming-0.3.1.md` (owner decree, same day) fully specifies the native-stream io
surface: `FileStream`, `open_read`/`open_write`/`open_append`, `stream_read`/`stream_write`/
`stream_seek`/`stream_close`, the easy helpers `write_stream`/`append_stream`/`read_stream`, the TOTAL
forms re-based on stream, the per-OS syscall map (Linux `syscallN`, macOS `from "System"`, Windows
`from "kernel32"`), and the single new builtin `teko::mem::byte_ptr`. **#2 does not redesign that
surface — it BINDS each stream's buffer to the enclosing fine-scope arena of #1.** Every io point
becomes a native stream (that doc's decree); this doc makes each stream's buffer die with its scope.

### 4.2 The buffer↔arena binding (the one thing #2 adds over io-streaming)

The io-streaming doc's read loop uses a 1024 B scratch from the arena (`io-streaming §4`):

```teko
var scratch: []byte = teko::mem::bytes_from_ptr(teko::mem::buf_ptr(CHUNK), CHUNK)
```

Today `buf_ptr(CHUNK)` bumps from `tk_region_current()`. Under #1, `tk_region_current()` inside a fine
scope IS that scope's arena — so the scratch buffer is BORN in the arm arena and RECLAIMED at the arm
exit edge, with zero code change to the io surface. The binding is structural: the buffer lifetime =
the scope lifetime, because the buffer allocates through the same current-region seam #1 re-points.

The rules that make this sound (all satisfied by construction):

1. **The scratch is written-in-place, never reassigned** (`io-streaming §9.2`): `stream_read` writes
   into `scratch` and returns a count; the buffer is not grown, so no copy-grow half is abandoned and
   the purge-on-reassign ownership semantics do not fire. The buffer is pure arm-local scratch —
   row 1 of §3.3, reclaimable.
2. **No `region_drop` occurs mid-drain** (`io-streaming §9.2`): the read loop runs entirely within one
   fine scope; the scope's arena is stable for the whole loop and drops only after the loop's exit
   edge. #1's per-arm discipline guarantees this — the drop is emitted at the arm boundary, never
   inside it.
3. **The FileStream handle is a scalar `i64`** (`io-streaming §2.3`): it holds no arena pointer, so a
   stream value that escapes the arm (returned, stored) carries no dangling buffer reference — the
   buffer is arm-local, the handle is a value. The escape of the handle is a scalar move (trivial),
   independent of the buffer's reclaim.

### 4.3 The write path — no root accumulation

`stream_write(ref s, data: ref []byte)` slices `data` by index and syscalls each ≤ CHUNK piece
(`io-streaming §4`); nothing accumulates, so there is no growing output buffer to leak. Where the
compiler builds output (the 22 MB `teko.c`), the end-game is codegen emitting DIRECT into the writer
(`io-streaming §9`) instead of the `csrc` copy-grow `str` — which is precisely a #3 purge target
(`tk_append_bytes_fo`/the `cb` builder). **#2 delivers the sink; #3 removes the accumulator; #1
reclaims whatever transient remains.** The three close together on the codegen tail, exactly as
`io-streaming §9` and `arena-especificacao-unica §1.2` both foretell.

### 4.4 str concat adjacent to streams

A `str` built for an io payload inside an arm (a path, a header line) routes through
`tk_str_concat_len_r(child, …)` (the `_r` twin from `arena-por-escopo` M2, `teko_rt.h`), landing in the
arm arena and dying at arm exit — not the root-leaking `tk_str_concat_len`. This is the §3.3 arm-local
row applied to io-adjacent strings; it removes the 66 MB unroutable-str term for the io path.

---

## 5. Part #3 — total purge of ::push / ::empty / grow_inplace / with_cap

### 5.1 Why they can go once #1+#2 land

The owner: *"these primitives are the source of the peak-inflation (transient over-allocation) the
arena is meant to reclaim, so once #1+#2 land they have no reason to exist."* The mechanism, made
precise:

- `with_cap` existed to PRE-SIZE and skip the 1→2→4→8 doubling ladder. #1's **AST floor** (`§3` canonical)
  births the scope arena at the proven need — pre-sizing is now the arena's job, so `with_cap` is
  redundant.
- `grow_inplace` existed to append WITHOUT abandoning (dodge the O(n²) abandoned halves). #1 makes
  abandoning FREE — the halves die at arm exit — so the in-place primitive (which needs an F1
  exclusive-borrow proof and a 3-word header the native backend cannot represent, `lower.tks:4339`)
  is unnecessary.
- `push` (the root-leaking value form `tk_slice_push`) leaked every grown buffer to root. #1 re-points
  growth at `tk_region_current()` = the arm arena, so every push half is reclaimed at arm exit. The
  root-leaking primitive has no reason to remain.
- `empty` seeded a zero-cap slice that forced the doubling from empty. #1's floor + `need==0` elision
  replaces it: an empty that never grows opens no arena; one that grows is pre-sized by the floor.

### 5.2 The purge table

| primitive | today | arena-backed replacement | call-site reality (latest tree) |
|---|---|---|---|
| **`teko::list::with_cap`** (`typer.tks:824`, `codegen.tks:2531`/`emit_list_with_cap`, `lower.tks:4340`; runtime `tk_slice_with_cap`/`_r` `teko_rt.h:1439-1449`) | pre-size a fresh len-0 buffer | **AST floor** — the scope arena is born at `need+header`; a presized `[]T` written by index needs no explicit cap. Remove the surface fn + the runtime twins. | staged-off; **no real user call sites** (only typer/codegen/lower plumbing) — cheap, mechanical |
| **`teko::list::grow_inplace`** (`typer.tks:835`, `codegen.tks:2562`/`emit_list_grow_inplace`, `lower.tks:4339`; runtime `tk_slice_grow_inplace` `teko_rt.h:1450-1456`) | in-place append under exclusive `ref` | **scope-arena reclaim** — abandon freely; the arm arena reclaims the halves at exit. Remove the surface fn + `tk_slice_grow_inplace` + the F1-borrow requirement it carried. | staged-off; **no real user call sites** (native has no lowering at all) — cheap, mechanical |
| **`teko::list::push`** (surface + `tk_slice_push` root form `teko_rt.h:1420`, `tk_slice_push_fo`, `tk_slice_push_r`) | copy-grow, root-leaking value form | **region-current growth** — growth lands in `tk_region_current()` (the arm arena) and dies at arm exit. Collapse `tk_slice_push`/`_fo` into the single `tk_slice_push_r(current)`. | **pervasive: ~2674 sites / 152 files.** Two-tier migration (§5.3) |
| **`teko::list::empty`** (surface) | zero-cap seed | **floor-presized / elided empty** — an empty that never grows opens no arena; one that grows is floor-sized. | **pervasive: ~2166 sites.** Two-tier migration (§5.3) |
| adjacent: `tk_append_bytes_fo` (`teko_rt.h:1457`), `tk_free_block` (`teko_rt.h:1460`) | the linear-`cb` byte accumulator + explicit park | subsumed by #2 stream-write (no accumulator) + arm-arena drop | codegen `cb` chain; migrates with the codegen-emit-direct of #2/§4.3 |

### 5.3 The migration order — two tiers (the honest split)

`grow_inplace` and `with_cap` are staged-off with no real user call sites; `push`/`empty` are pervasive
(~4840 combined sites). One hand-edit-per-site is neither safe nor a crumb. The migration is therefore
two tiers:

**Tier A — mechanical, lands WITH #1 (kills the peak-inflation, the owner's actual goal):**
1. Re-point the `push`/`empty` LOWERING at the region-current (arm) arena in both motors: `codegen.tks`
   emits `tk_slice_push_r(current)` (not `tk_slice_push` root); `lower.tks` targets `tk_region_current()`
   for the grown buffer. The user SURFACE is untouched (no 2674 edits); its transient buffers now die at
   arm exit. This is where the ~1.8 GB copy-grow term is reclaimed.
2. Remove `grow_inplace` + `with_cap`: the typer arms (`typer.tks:824,835`), the codegen emitters
   (`codegen.tks:2531,2562`, the `with_cap`/`grow_inplace` dispatch at `:2887,:2890,:6061`,
   the allow-list `:5706`), the native honest-stops (`lower.tks:4339,4340`), and the runtime twins
   (`tk_slice_with_cap`/`_r`/`tk_slice_grow_inplace`). No real caller breaks (staged-off).
3. Collapse `tk_slice_push`/`tk_slice_push_fo` into `tk_slice_push_r`: `tk_slice_push` becomes the
   `region==root` wrapper (already its contract, `teko_rt.h:1431`); `_fo` (free-old) is subsumed by
   arm-drop and removed. Runtime-only, additive-then-subtractive.

**Tier B — surface REMOVAL of `push`/`empty` as named primitives (a LANGUAGE-SURFACE ruling, fork §9):**
Genuinely removing `teko::list::push`/`empty` from the surface requires a replacement IDIOM for "build
a sequence" — a comprehension/collect form or a presized-index builder. That is a surface decision the
owner must define (fork F1). Until he does, Tier A already delivers the memory win (the primitives no
longer inflate the peak; they route through the arena). Tier B is the cosmetic/semantic completion.

**Migration sequencing within Tier B (once the idiom is ruled):** migrate leaf modules first
(`src/collections/*`, `src/list/list.tks` — the `List<T>::push` method at `list.tks:15` is the single
choke point that fans out to the 2674 sites), then the hot compiler files (`codegen.tks` 142,
`lower.tks` 234, `regression.tks` 57), each behind its own fixpoint gate. The self-compiling compiler
ENUMERATES every surviving reference when the primitive's root is removed (the expurgo methodology,
`expurgo-fixpoint-historico`) — that is the migration's own checklist.

---

## 6. The ordered CRUMB SEQUENCE (#1 → #2 → #3)

Each crumb is the smallest independently gate-able step. Gate legend: **[dry]** = compiles + `teko test`
scoped-run green + trivial fixpoint (no emit consumers yet); **[RITUAL]** = full gate: build gen2
`TEKO_BACKEND=native`, scoped regression green, **FIXPOINT gen2==gen3 byte-identical**, `TEKO_ARENA_OBS`
signal. Reseed only at a [RITUAL], never mid-crumb. Bootstrap-safe: no crumb teaches the compiler an
idiom its own seed lacks; the previous released `teko` builds gen1 unchanged. Native-first: every memory
number is the NATIVE peak (the wall lives there).

> NOTE (harness): do NOT run `teko test .` whole (the monomorph leak crashes the container). All
> regression fixtures below are `.tkr`, run ISOLATED, asserting native exit code / stdout — never the
> full-tree test.

### Part #1 — the foundation (crumbs A1–A5)

**A1 — extend the residence oracle to fine-scope depth.** New/extended `src/checker/residence.tks` (or
`spine.tks` extension): `fine_scope_residence(f)` + `scope_touches_arena(body)` + `ResidenceDepth` (§3.4).
Emits, per binding/site, the fine-scope depth. No consumer yet. Prefer a NEW file importing `spine` to
minimize collision with spine agents. **Type shapes:** the three signatures in §3.4. **Fixtures:** none
yet (oracle unconsumed). **Gate: [dry]** — trivial fixpoint. Ritual: NO.

**A2 — emit the fine-scope enter/drop in the C route.** `codegen.tks` reads the A1 plan and, for each
fine scope 1–6 with `need>0`, emits `tk_region_new_sized`/`enter` on the arm ENTER edge and
`leave`/`drop` on the arm EXIT edge (each `if`/`elseif`/`else`/`match` arm, loop arm, bare block —
generalizing the existing `_tkbr` value-arm drop `codegen.tks:5462` to ALL fine scopes). Default
allocation and slice growth target the arm region (not root). **Touches:** `emit_stmt`/`emit_branch_value`/
`emit_loop_while`, the `RegionFrame` stack (`codegen.tks:7955`), `cg_enclosing_region_expr` (`:7963`).
**Fixtures (`.tkr`, native exit code):**

| fixture | proves | expected exit |
|---|---|---|
| `arena_arm_block_dies` | a local used only in a bare block dies at block exit (churn N cycles; corruption = wrong value, not just leak) | 0 |
| `arena_if_arms_distinct` | a local in the then-arm and one in the else-arm each die at their OWN arm exit | 0 |
| `arena_elseif_arm` | a value in an `elseif` arm reclaimed at that arm's exit | 0 |
| `arena_match_arm` | one arena per match arm; the non-taken arms open none | 0 |
| `arena_loop_per_iter` | loop arena reclaimed EACH iteration; live-arena count ≈ 1 over 1M iters (not 1M) | 0 |
| `arena_elision_leaf` | `if x { return a }` leaf opens NO arena (need==0) | 0 |

**Gate: [RITUAL]** — gen2 native, scoped fixtures green, FIXPOINT gen2==gen3, `TEKO_ARENA_OBS`:
`scoped>0`, `reclaim>0`, live-arenas ≈ nesting depth. Ritual: YES.

**A3 — native route inherits the fine-scope lifecycle.** `lower.tks` reads the SAME A1 plan and emits
`region_enter(region_new_sized(current))` on each fine-scope enter edge, `region_leave`+`region_drop` on
each exit edge; per-iteration for the loop arm (never flattened across the back-edge). `buf_ptr` and the
default `tk_alloc` target `tk_region_current()` instead of the fixed `tk_region_root()`
(`lower.tks:3390-3437`, the literal "routes through tk_region_root()" the doc must fix). **Touches:**
`lower.tks` (very hot — coordinate with `backend-memoria`, the native-map agents). **Fixtures:** the A2
set re-run under native (same `.tkr`, native backend). **Gate: [RITUAL]** — FIXPOINT gen2==gen3
byte-identical; `TEKO_ARENA_OBS` scoped>0. Ritual: YES.

**A4 — the object arena (scope #7).** Thread the object's residence depth (A1) so an instance's owned
interiors bump into the instance's residence arena; for the common case this IS the enclosing fine-scope
arena (no new region), so A4 is mostly the plan wiring + the decoupled-object case (F2/container) behind
the fork F2 ruling. **Fixtures:** `arena_object_interiors_die` (an object's `[]T` member reclaimed with
the object), `arena_object_moved` (an object returned by DPS carries its interiors to the caller arena —
used after return; if the interiors stayed in the callee arm it would be UAF/corruption). **Gate:
[RITUAL].** Ritual: YES.

**A5 — `TEKO_ARENA_OBS` fine-scope attribution (measurement).** Extend the existing obs counters
(`teko_rt.c` obs) to break `scoped`/`reclaim` down per fine-scope depth, so A2–A4 are gate-able by
"where the peak fell." Runtime-read-only, additive. **Gate: [dry].** Ritual: NO.

### Part #2 — native stream, buffers on the arena (crumbs B1–B4)

Depends on: #1 A3 landed (so `tk_region_current()` inside an arm IS the arm arena) and the
io-streaming doc's crumbs 1–5 (surface). Design-ahead: B-crumbs can be WRITTEN against the io-streaming
declared shapes today; they land as io-streaming's surface lands.

**B1 — confirm buf_ptr binds to current region.** Verify (and fixture) that `teko::mem::buf_ptr(CHUNK)`
inside a fine scope allocates in the arm arena and is reclaimed at arm exit (the §4.2 binding). No new
code if A3 re-pointed `buf_ptr`'s target; else a one-line re-point. **Fixture:** `io_scratch_arm_reclaim`
(open a stream in a block, drain, exit block; the 1024 B scratch is gone — obs shows the arm drop
reclaimed it). **Gate: [dry]/[RITUAL]** per whether it moves bytes.

**B2 — route io-adjacent str through `tk_str_concat_len_r(current)`.** The 3 native concat/interp sites
(`lower.tks:11030,12920,12922`, from `arena-por-escopo` M2) pass the current region; an io path/header
built in an arm dies with the arm. **Gate: [RITUAL]** — FIXPOINT; obs `str unroutable` drops on the io
path. Ritual: YES.

**B3 — migrate the compiler's io READS to stream, arm-scoped scratch.** Per io-streaming crumb 7
(`assemble.tks` source read, `fmt.tks`, `project.tks`, `regression.tks`) — the read scratch is arm-local
(#1). Preserving. **Gate: [RITUAL]** (fixpoint). Ritual: YES.

**B4 — migrate the compiler's io WRITES to stream; codegen-emit-direct.** Per io-streaming crumb 8 +
§4.3: the 22 MB `teko.c` writer emits direct (no `csrc` copy-grow), the accumulator being a #3 target.
**Gate: [RITUAL]** — `teko.c` gen2==gen3 byte-identical. Ritual: YES.

### Part #3 — the purge (crumbs C1–C4, Tier A) + C5 (Tier B, fork-gated)

**C1 — re-point push/empty lowering at the arm arena (Tier A step 1).** Both motors emit
`tk_slice_push_r(current)` for `push`; the surface is untouched. This is where the copy-grow peak is
reclaimed. **Fixture:** `purge_push_reclaimed` (a push-chain inside an arm whose result stays local:
obs shows the abandoned halves reclaimed at arm exit, peak flat). **Gate: [RITUAL]** — FIXPOINT
gen2==gen3 (bytes identical; only the buffer's REGION changed, not its contents — the `al4a` structural
dedup proof, `arena-por-escopo §4.4`); obs `reclaim>0` on push chains. Ritual: YES.

**C2 — remove `grow_inplace` (Tier A step 2a).** Delete the typer arm (`typer.tks:835`), codegen
emitter (`codegen.tks:2562`, dispatch `:2890`), native honest-stop (`lower.tks:4339`), runtime
`tk_slice_grow_inplace` (`teko_rt.h:1450`, `teko_rt.c`). No real caller breaks (staged-off). **Fixture:**
`purge_grow_inplace_gone` (a program using the old surface fails to compile with a clear diagnostic —
REJECT fixture, native exit code of the checker error). **Gate: [RITUAL]** — FIXPOINT (nothing emitted
used it). Ritual: YES.

**C3 — remove `with_cap` (Tier A step 2b).** Delete the typer arm (`typer.tks:824`), codegen emitter
(`codegen.tks:2531`, dispatch `:2887,:6061`, allow-list `:5706`), native honest-stop (`lower.tks:4340`),
runtime `tk_slice_with_cap`/`_r` (`teko_rt.h:1439-1449`). **Fixture:** `purge_with_cap_gone` (REJECT).
**Gate: [RITUAL]** — FIXPOINT. Ritual: YES.

**C4 — collapse `tk_slice_push`/`_fo` into `_r` (Tier A step 3).** `tk_slice_push` becomes the
`region==root` wrapper over `tk_slice_push_r`; `tk_slice_push_fo` (free-old) removed (arm-drop subsumes
it); `tk_append_bytes_fo`/`tk_free_block` retire once codegen-emit-direct (B4) lands. Runtime-only,
additive-then-subtractive. **Gate: [RITUAL]** — FIXPOINT; obs shows no root-form push remaining.
Ritual: YES.

**C5 — [FORK-GATED] surface removal of push/empty (Tier B).** BLOCKED on fork F1 (the replacement idiom
ruling). When ruled: migrate `List<T>::push` (`list.tks:15`, the single choke point) + the collections,
then the hot compiler files, each behind its own fixpoint gate. Design-ahead deliverable NOW: the
migration census (§5.3), the choke-point identification, the honest-stop scaffolding. **Gate: [RITUAL]**
per module. Ritual: YES (when unblocked).

### Dependency order (the mandatory spine)

```
#1: A1 (oracle) → A2 (C route) → A3 (native route) → A4 (object arena) → A5 (obs)
                                        │
#2: (io-streaming surface crumbs 1-5) ─┴─> B1 → B2 → B3 → B4        [needs A3: current==arm arena]
                                        │
#3: A3 landed ─> C1 (re-point push) → C2 (grow_inplace) → C3 (with_cap) → C4 (collapse runtime)
                                        └─> C5 (Tier B surface removal) [BLOCKED on fork F1]
```

A1 before everything (no plan, no gate). A2/A3 before any #3 crumb (the arm arena must exist to reclaim
push). #2 needs A3. C1 before C2/C3/C4 (re-point before remove). C5 is fork-gated.

---

## 7. Risks and law tensions

| risk | produced by | resolution (law-first) |
|---|---|---|
| **R1 — a fine-scope drop with a live alias = UAF** (the arena-por-escopo class the canonical §2.2 warns of) | dropping an arm arena while a shallower binding still references into it | the depth check (§3) is the guard: born-at-`d` is dropped-at-`d` ONLY when no use is at depth `<d`; doubt → escape to the shallower arena. Never drop with a live shallower alias. FIXPOINT gen2==gen3 is the detector: a wrong reclaim corrupts a value → bytes diverge → fails BEFORE any UAF ships. |
| **R2 — bytes move region, leak into emitted output** | if codegen dedup depended on pointer identity | measured NOT to: dedup/mangle is STRUCTURAL (`cg_opt_key ==`, `al4a:39,110`; ids node-carried "would not survive copies", `al4a:50`). Region changes address, not the emitted value. Detector: fixpoint. |
| **R3 — loop-arm flattening accumulates** (the peak the owner fears) | `#arena_depth(N>1)` flattening across a back-edge | HARD carve-out (`modelo §7`): a loop arm is ALWAYS a materialization boundary; N>1 flattening coalesces only straight-line nested scopes, never a back-edge. Fixture `arena_loop_per_iter` is the detector. |
| **R4 — removing grow_inplace/with_cap breaks a caller** | a hidden real user of the staged-off primitives | measured: no real call sites (only typer/codegen/lower plumbing + runtime). The self-compile ENUMERATES any survivor on removal (expurgo). REJECT fixtures pin the new diagnostic. |
| **R5 — Tier B (remove push/empty) is a surface change without an idiom** | 4840 sites need a replacement form | do NOT force it: Tier A delivers the memory win with the surface intact; Tier B is fork-gated on F1. No HALT — the win lands without the surface ruling. |
| **R6 — collision in hot files** | `codegen.tks`/`lower.tks`/`typer.tks` | oracle in a NEW file; A2 (C) and A3 (native) separated; coordinate with `backend-memoria`, `arena-escopo-local`, the native-map agents. #3's runtime edits are additive-then-subtractive. |
| **R7 — teko_rt.{c,h} touched** | `tk_slice_push_r` collapse, obs extension | explicit runtime exception; all additive/behavior-identical (root wrapper delegates; obs only reads). No C emitted by the compiler. |
| **R8 — object arena adds a region kind** | scope #7 as a dedicated region | default is (a): the object arena IS the enclosing fine-scope arena (zero new kind); (b) dedicated only for the F2/container case that ALREADY needs per-entry reclaim (canonical §7.8). Fork F2. |

**Residual law tension forcing a HALT: NONE.** Teko-only honored (product in `.tks`; the runtime twins
are the named exception). W15 full-Javadoc on every snippet. Issue-100%: #1+#2+Tier-A deliver the whole
memory correction; Tier B is fork-gated but non-blocking for the win. FIXPOINT byte-identical is the
inviolable gate and the detector of every risk. Adjacent finding (the S0 core.h list migration threading
old-size through the header, `teko_rt.h:185-187`) is REPORTED, not turned into an issue by me.

---

## 8. What remains BLOCKED (design-ahead honesty)

- **#3 Tier B (C5)** is blocked on fork F1 (the replacement idiom for push/empty). Everything up to and
  including Tier A (C1–C4) — the actual peak-inflation kill — is UNBLOCKED and lands with #1.
- **#2 B1–B4** depend on the io-streaming surface crumbs 1–5 landing; those are themselves unblocked
  (`io-streaming §10` — leaf-new, compiles today). The buffer↔arena binding (B1) needs #1 A3.
- **The F2 per-entry free-list** for decoupled object arenas (fork F2 option b) shares the capability
  `arena-especificacao-unica §7.8` already requires for `chan`/journal — coordinate, do not duplicate.

Everything else — the oracle (A1), the C and native fine-scope lowering (A2/A3), the object-arena wiring
(A4), the obs (A5), the push re-point and the grow_inplace/with_cap removal (C1–C4) — compiles against
today's tree and needs no blocked API.

---

## 9. Open forks for the owner to rule on

**F1 — the replacement idiom for `push`/`empty` (gates #3 Tier B).** Tier A keeps the surface and routes
its growth through the arena (memory win landed). Tier B removes `teko::list::push`/`empty` as named
primitives — but that needs a replacement "build a sequence" idiom. Options: (a) a comprehension/collect
form (`[f(x) for x in xs]`), (b) a presized-index builder (`var ys = [_; n]; ys[i] = …`), (c) keep the
`List<T>::push` METHOD (arena-routed) and remove only the free-function `teko::list::push`/`empty`.
Recommendation: (c) — the method is the single choke point (`list.tks:15`), arena-routed it costs
nothing, and (a) can follow as sugar. **Rule: which idiom, and does Tier B happen this round or as a
fast-follow?**

**F2 — object arena: shared or dedicated region.** Is the object arena (scope #7) always the enclosing
fine-scope arena (option a, zero new region kind, object dies with its scope), or a dedicated per-object
region when the object outlives its birth arm (option b, needs the F2 per-entry free-list)?
Recommendation: (a) default, (b) only for the F2/container decoupled case. **Rule: (a)-only this round,
or (a)+(b)?**

**F3 — Tier A push re-point: does it wait for A3, or ship on the C route first?** C1 re-points BOTH
motors. If native (A3) slips, may C1 land on the C route alone (memory win on the C build) ahead of
native? Recommendation: keep C1 after A3 (native-first — the wall is native); C route alone buys nothing
under the native cap. **Rule: confirm native-first, or allow a C-route-first partial?**

**F4 — the `#arena_depth(N>1)` flattening override: this round or fast-follow?** `modelo §7` sets the
default to depth=1 (each fine scope its own arena — exactly #1). The N>1 flattening override (#476) is an
opt-in optimization with the loop back-edge carve-out (R3). Recommendation: fast-follow — #1's depth=1 is
already correct and fine; flattening only pays once a profile shows sub-arena overhead. **Rule: defer
#476 to fast-follow (confirm), or pull it now?**

**F5 — the S0 core.h list migration (adjacent, reported).** The compiler-internal seam (`core.h`
`tk_realloc0`/`tk_free0`) stays on libc until old-size is threaded through the list header
(`teko_rt.h:185-187`). It is NOT in this issue's scope (that is compiler-scratch, not program memory).
Recommendation: leave frozen; report as the next adjacent axis. **Rule: confirm out-of-scope for this
correction.**
