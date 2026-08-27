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
  - "src/runtime/teko_rt.tks:64-69"                                   # str_of_bytes — TODAY a copy-loop, the flagship target
  - "docs/design/arena-escopada-stream-expurgo-0.3.1.md"              # the str<->[]byte same-rep cast the expurgo wants
  - "DECISION_LOG.md:1166"                                            # D130 addendum — wrap/unwrap = safe pointer marshalling
---

# 0149 · MEM-E0b — opaque `ptr`/`uptr` + `__wrap`/`__unwrap` (recover 0011; flagship = zero-copy str↔[]byte)

> Recover the deliberated Marshall design (`0011-SM-G5`): make `ptr`/`uptr` OPAQUE atomic types with two
> methods — `__wrap<T>()` (FALLIBLE, arena-liveness+type-tag checked, safe re-entry) and `__unwrap<T>()`
> (INFALLIBLE raw exposure). **The flagship use TODAY (owner):** the ZERO-COPY `str`↔`[]byte` interop — a
> `str` IS `{ptr,len}` of bytes, so a `str`'s payload and a `[]byte` share the SAME rep; wrap/unwrap is
> the SAFE mechanism (arena-liveness checked) that reinterprets one as the other WITHOUT copying. This is
> the implicit same-rep cast the expurgo already wants (CLAUDE.md), and it kills the copy-loop that
> `str_of_bytes` is TODAY (`teko_rt.tks:64-69`). The tag rides the arena header; `ptr`/`uptr` stay bare
> words. The old C-vs-Teko fork (DECISION_LOG:680) is DEAD — arena is 100% Teko (D128). NOT new design.

## Goal

Replace the generic `Ptr<T>` family with a single OPAQUE non-generic `ptr`/`uptr`, plus
`[u]ptr.__wrap<T>(): T | error | null` (checked cast IN, no `unsafe`) and `[u]ptr.__unwrap<T>(): T`
(raw exposure OUT). The opaque pointer is STRUCTURALLY incapable of arithmetic (no element width) →
kills `p+n`/`p[n]` by construction. The tag lives in the ARENA ALLOCATION HEADER (a `u64` header slot),
not the pointer word — `ptr`/`uptr` remain bare machine words. **The concrete flagship consumer:** a
`str` is `{ptr,len}` of bytes and a `[]byte` is `{ptr,len}` of bytes — SAME rep. So `str`→`[]byte` and
`[]byte`→`str` (and an `[]str` payload viewed as its constituent bytes) are IDENTITY reinterprets, ZERO
copy — but doing them raw could hand back a dangling slice; wrap/unwrap makes it SAFE by checking the
address is still in a LIVE arena region. The checker accepts the same-rep `str`↔`[]byte` cast as implicit
(the expurgo's ask), lowered through the identity reinterpret, with wrap/unwrap the safe general
primitive under it (and for FFI / opaque↔typed). This eliminates the `str_of_bytes` copy-loop
(`teko_rt.tks:64-69`) — it becomes an identity reinterpret. INERT/byte-identical until adopted; the FFI
+ str/byte migration is the reball (`0091 SM-S4` / `MEM-W6`). **State (verified):** `type.tks:51 Ptr =
struct { inner }` is STILL generic — this crumb makes it atomic; `Uptr` (`:54`) atomic; `region_lookup`
(`arena.tks:965`), `di_type_id` (`di.tks:373`), `str_of_bytes`/`bytes_of_str` (`teko_rt.tks`) exist.

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
- `src/checker/typer.tks` — accept the same-rep `str`↔`[]byte` cast as IMPLICIT (identity reinterpret,
  no copy) — the flagship; wrap/unwrap is the safe primitive beneath it.
- `src/runtime/teko_rt.tks:64-69` — `str_of_bytes` (and `bytes_of_str`) become IDENTITY reinterprets
  (delete the copy-loop) once the same-rep cast lands (adopted in the reball, not here — kept inert).

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
| `marshall_str_bytes_zerocopy` | a `str` reinterpreted as `[]byte` and back shares the SAME ptr (zero copy — the byte address is identical, not a duplicate) — the flagship | 0 |
| `marshall_str_bytes_dead_arena` | reinterpreting a `str` whose arena was dropped, via `__wrap<[]byte>`, returns `error` (the safety wrap/unwrap adds over a raw reinterpret) | 0 (error branch) |
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
