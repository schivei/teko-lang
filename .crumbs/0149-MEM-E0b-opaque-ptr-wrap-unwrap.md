---
seq: 0149
crumb-id: MEM-E0b
milestone: M5
gate: "[dry]"
reseed-class: "fixpoint-rebuild"
deps: []
sources:
  - ".crumbs/0011-SM-G5-marshall-opaque-ptr.md"                       # the recovered design (opaque ptr + wrap/unwrap)
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:259-357"  # §6 Marshall opaque ptr
  - "DECISION_LOG.md:1166"                                            # D130 addendum — ptr/uptr opaque + wrap/unwrap
  - "DECISION_LOG.md:680"                                             # old C-vs-Teko fork on region_alloc_tagged (now dead)
  - "src/checker/type.tks:51-54"                                      # Ptr still GENERIC (inner); Uptr atomic
  - "src/runtime/arena.tks:965"                                       # region_lookup already exists
  - "src/checker/di.tks:373"                                          # di_type_id (the one project hash)
---

# 0149 · MEM-E0b — opaque `ptr`/`uptr` + `__wrap`/`__unwrap` (recover 0011; tag lands in Teko arena)

> Recover the deliberated Marshall design (`0011-SM-G5`): make `ptr`/`uptr` OPAQUE atomic types with two
> methods — `__wrap<T>()` (FALLIBLE, arena-liveness+type-tag checked, safe re-entry) and `__unwrap<T>()`
> (INFALLIBLE raw exposure) — the SAFE marshalling between pointer representations the owner named. The
> tag rides the arena allocation header; `ptr`/`uptr` stay bare words (no fat pointer, no ABI change).
> The old C-vs-Teko fork (DECISION_LOG:680) is DEAD — the arena is 100% Teko (D128), so
> `region_alloc_tagged` lands in `arena.tks`, NOT a C patch. NOT new design — recovery.

## Goal

Replace the generic `Ptr<T>` family with a single OPAQUE non-generic `ptr`/`uptr`, plus
`[u]ptr.__wrap<T>(): T | error | null` (checked cast IN, no `unsafe`) and `[u]ptr.__unwrap<T>(): T`
(raw exposure OUT). The opaque pointer is STRUCTURALLY incapable of arithmetic (no element width) →
kills `p+n`/`p[n]` by construction. The tag lives in the ARENA ALLOCATION HEADER (a `u64` header slot),
not the pointer word — `ptr`/`uptr` remain bare machine words (C-repr `void*`/`uintptr_t`). INERT
(byte-identical) until a `__wrap` reads the tag; `src/`'s FFI migration to opaque `ptr` is the reball
(`0091 SM-S4` / `MEM-W6`). **State (verified):** `type.tks:51 Ptr = struct { inner }` is STILL generic —
this crumb makes it atomic; `Uptr` (`:54`) is already atomic; `region_lookup` (`arena.tks:965`) and
`di_type_id` (`di.tks:373`) already exist.

## Where

- `src/checker/type.tks:51` — `Ptr` becomes ATOMIC (remove `inner`); `Uptr` unchanged; keep `:177`
  `Uptr` same-kind-only equality; add the same for the atomic `Ptr`.
- `src/checker/resolve.tks` — delete the `unsafe_carrying_at` `Ptr`-recurses-into-inner arm (an opaque
  `ptr` carries no pointee). Reject `p+n`/`p[n]` with "opaque pointer has no arithmetic".
- `src/checker/scope.tks` — `__wrap<T>`/`__unwrap<T>` resolved as METHODS on `ptr`/`uptr` (receiver
  `[u]ptr.__wrap<T>()`), `T` a REQUIRED explicit type argument; `__wrap` result `T | error | null`,
  `__unwrap` result `T`.
- `src/lir/lower.tks` — `__wrap<T>` lowers to null-test → `region_lookup(addr)` → header-tag compare →
  branch value/null/error; `__unwrap<T>` → bare reinterpret (zero instructions beyond the type change).
- `src/runtime/arena.tks` — ADDITIVE `region_alloc_tagged(r, n, tag): ptr` (Teko — the arena is Teko,
  D128): the tag is a `u64` header slot alongside the existing `ar_region_alloc_w`. Inert until read.
- `src/checker/di.tks:373` — REUSE `di_type_id` verbatim for `type_tag` (ONE hash project-wide).

## How

1. Make `ptr`/`uptr` atomic + opaque; delete the recurse-into-inner arm; the monomorph surface shrinks
   (no `Ptr<T>` instantiations). Confirm arithmetic is rejected structurally.
2. Add the tag to the arena header via `region_alloc_tagged` (Teko); `ptr`/`uptr` stay bare words.
3. Add the methods (`T` explicit):

```teko
/**
 * ptr_wrap — the FALLIBLE checked re-entry of an opaque address into the typed world (`p.__wrap<T>()`),
 * the SAFE marshalling between pointer representations. Three dynamic UB-free checks, in order (no
 * `unsafe`): (1) address 0 → `null`; (2) the address falls inside a LIVE region of the region tree
 * (`region_lookup`) — a dropped-region address → error ("pointer's arena is no longer live"); (3) the
 * header `type_tag == di_type_id(T)` → the `T` value, else error ("pointer tags type X, wrapped as T").
 * A foreign (non-teko-arena) address is not found → error — the honest refusal to vouch for memory teko
 * does not own.
 *
 * @param p  the opaque pointer to re-enter
 * @return   the `T` value, `null` (address 0), or `error` (dead arena / tag mismatch / foreign)
 * @throws   surfaced as `error` on dead-arena or tag mismatch, never UB
 * @since 0.3.1
 */
fn ptr_wrap<T>(p: ptr): T | error | null

/**
 * ptr_unwrap — the INFALLIBLE raw exposure OUT (`p.__unwrap<T>()`): a pure reinterpret of the address as
 * a `T`-typed value, NO check (zero instructions beyond the type change). Makes no safety claim; its
 * downstream use is governed by the `extern` contract, its arena residence by the ordinary escape story.
 *
 * @param p  the opaque pointer to expose
 * @return   the address reinterpreted as `T`
 * @since 0.3.1
 */
fn ptr_unwrap<T>(p: ptr): T
```

4. Lower them (null-test → `region_lookup` → tag compare → branch; `__unwrap` → reinterpret).
5. Service seam (§6.1a): user code CANNOT `__wrap`/`__unwrap` a service (the SM-G6 service-escape rule
   already blocks a service reaching a Marshall operand — no new mechanism).
6. Confirm byte-neutral: removing generic `Ptr<T>` changes bytes only for source that USES generic
   pointers; the tag path is inert until a `__wrap` reads it; `src/` FFI still uses raw until the reball.

## Rulings & laws

- **Teko-only + arena-is-Teko (D128):** the tag twin `region_alloc_tagged` lands in `arena.tks` (Teko) —
  the old FORK "C-vs-Teko for `tk_region_alloc_tagged`" (DECISION_LOG:680) is RESOLVED (most-recent-wins):
  NOT a `teko_rt.c` patch.
- **Safe-by-arena, not by assertion (§6.1):** `__wrap`'s three checks surface `null`/`error`, never UB,
  never an uncatchable panic — so `__wrap` needs no `unsafe`.
- **ONE hash project-wide:** `type_tag` reuses `di_type_id` (`di.tks:373`).
- **Byte-preserving until adopted (§9.1):** inert; does NOT drive the reseed alone — rides the reball.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`.

## Fixtures

The self-build does not yet call `__wrap`/`__unwrap` (FFI migrates in the reball) — isolated oracles per
dynamic outcome (rejection oracle allowed):

| fixture | asserts | expected |
|---|---|---|
| `marshall_wrap_tag_ok` | `p.__wrap<T>()` on a live tagged arena ptr returns the value | 0 |
| `marshall_wrap_tag_mismatch` | `p.__wrap<Other>()` returns `error` | 0 (error branch) |
| `marshall_wrap_dead_arena` | `__wrap` of a dropped-region address returns `error` | 0 (error branch) |
| `marshall_wrap_null` | `__wrap` of address 0 returns `null` | 0 (null branch) |
| `marshall_unwrap_infallible` | `p.__unwrap<T>()` exposes the value, no check | 0 |
| `opaque_ptr_no_arithmetic` | `p + 1` / `p[0]` on an opaque `ptr` rejected | EXPECT_COMPILE_FAIL |

## Gate

`[dry]` — compile + the six oracles + fixpoint (byte-identical; tag path inert). "Green" = `ptr`/`uptr`
atomic+opaque, `__wrap`/`__unwrap` type and lower, the four dynamic wrap outcomes correct, arithmetic
rejected, `[dry]` byte-identical. Reseed-class: `fixpoint-rebuild` (folds into RESEED-1 of `MEM-E5`).

## Deps

`—` (the service interaction relies on SM-G6 but is not a build dependency; batches with E0a/E1/E2/E3).

## Done when

`ptr`/`uptr` are opaque atomic (no generic family, no arithmetic), `__wrap<T>` performs the
null/arena-liveness/tag checks as `T | error | null`, `__unwrap<T>` is a bare reinterpret, the tag rides
the arena header via `region_alloc_tagged` (Teko, inert until read), the six oracles pass, and `[dry]`
is byte-identical.
