---
seq: 0091
crumb-id: SM-S4
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [SM-S1]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:174-196"     # §4 `->` return operator → `:`
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:146-171"     # §3 remove `-> ref T` (already SM-G4)
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1207-1208"   # Phase S — S4
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1276"        # fixture `arrow_token_removed`
  - "src/lexer/token.tks:78"                                             # FatArrow (Arrow's neighbour; `=>` never touched)
---

# 0091 · SM-S4 — drop `->`/`Arrow` from lexer + `token.tks`; migrate `src/` FFI to opaque `ptr`

> Drop the `->`/`Arrow` token from the lexer + `token.tks` and its dead parser branch (all returns are `:` after
> SM-S1); migrate `src/` FFI to the opaque `ptr` boundary. Clean expurgo — NO tombstone. E1 expurgo reseed.

## Goal

The M2→M3 boundary opens here. After SM-S1 swept every `src/` + `.tkt` return to the unified `:` operator, the
`Arrow` (`->`) token and its parser branch are DEAD — nothing produces or consumes them. This crumb REMOVES
them cleanly: the lexer no longer tokenises `->`, `token.tks` drops the `Arrow` variant, and `parse_function`
drops the `Arrow`-accepting return branch (keeping only `Colon`). Concurrently it migrates `src/` FFI to the
opaque `ptr` boundary (a foreign `ptr` is crossed with `.__wrap<T>()`, §5 — already seeded), completing the
"no raw pointer arithmetic on the surface" posture. This is a byte-MOVER (the lexer/parser change moves emitted
bytes) driven by the E1 expurgo reseed `{SM-S4, SM-S5}`. Removal is CLEAN — an old `->` program gets the SAME
generic unexpected-token error a never-existent symbol would get; **NO tombstone / "`->` was removed"
diagnostic**. `=>` (`FatArrow`, `token.tks:78`) is NEVER touched.

## Where

- `src/lexer/lexer.tks` — remove the `->` scan arm (the two-char `-`+`>` → `Arrow` symbol). `--`/`-=`/`-` and
  `=>` (`FatArrow`) scans are untouched.
- `src/lexer/token.tks` — delete the `Arrow` `TokenKind` variant (historically `:85`; `FatArrow` at `:78`
  remains). This shifts no wire ordinal that a `.tkb` reader depends on for tokens (tokens are not serialized),
  but is a lexer-grammar change → byte-mover.
- `src/parser/parse_decl.tks` — in `parse_function`'s return parse, delete the `Arrow`-accepting branch (accept
  ONLY `Colon` after `)`). `ref` in PARAMETER position is untouched (F1 borrow direction survives).
- `src/` FFI sites — any `extern fn` still surfacing a raw/`ref`-return pointer is migrated to return the opaque
  `ptr`, crossed at the boundary with `.__wrap<T>()` (§5, seeded). `extern fn` itself is untouched.

## How

1. **Confirm the sweep is complete (build-first).** Before removing anything, verify `src/` + `.tkt` contain
   ZERO `->` return operators (SM-S1's postcondition) and zero `-> ref T` return arms (SM-G4 removed the arm).
   The self-compile enumerating any surviving `->` is the signal a sweep was incomplete — fix it BEFORE this
   reseed.
2. **Remove the lexer scan + token.** Delete the `->`→`Arrow` scan arm in `lexer.tks` and the `Arrow` variant in
   `token.tks`. `=>` (`FatArrow`) stays.
3. **Remove the dead parser branch.** In `parse_function`, delete the `is_kind_at(..., TokenKind::Arrow)` return
   branch, leaving `Colon`-only. No other `:`-after-`)` position exists, so `:` return stays unambiguous.
4. **Migrate `src/` FFI to opaque `ptr`.** Any `extern fn` still returning a raw/`ref` pointer on the surface
   returns the opaque `ptr`; the call-site crosses it with `.__wrap<T>()` (§5). This severs the last raw-pointer
   surface residue so SM-S5 (the `unsafe` deletion) has nothing left to contain.
5. **Clean expurgo — NO tombstone.** Do NOT add a "`->` was removed" rule, a deprecation shim, or a migration
   hint anywhere. An old-form program simply fails to lex/parse `->` as the generic unexpected-token error.
6. **Reseed at the [RITUAL].** Build gen2 native (the corpus is `:`-only), FIXPOINT `gen2==gen3` (the lexer/parse
   change is proven byte-stable across the fixpoint), reseed — the new seed no longer understands `->`.

## Rulings & laws

- **Teko-only:** lexer/parser `.tks`; the C bootstrap twins are FROZEN — this is a `.tks` grammar change; no C
  twin edited.
- **W15 full Javadoc** on any touched declaration; flatten; no inline `//`.
- **Removals = CLEAN expurgo, NO tombstone (owner ruling 3 / master-plan §M3):** the lexer no longer tokenises
  `->`, the parser has no production, the checker no rule, NO special diagnostic — same generic error as a
  never-existent token.
- **Build-first:** SM-S4 CANNOT precede the SM-S1 sweep — the `Arrow` branch is removed only after every `src/`
  return is `:`.
- **`=>` untouched:** `FatArrow` (`token.tks:78`) is a distinct token for match arms — never removed.
- **Consolidated expurgo E1 `{SM-S4, SM-S5}` (master-plan §M3 note):** SM-S4 is the first half; SM-S5 follows;
  they share the "migration-complete" precondition.
- **Safety:** NEVER `teko test .` (fixtures ISOLATED); build gen2 in a subshell with `ulimit -v 6815744` (a
  blown guard is a root-cause fix, never a raised ceiling); reseed ONLY at this [RITUAL]; FIXPOINT `gen2==gen3`
  byte-identical; sweep `.tkt`/`.tkr` after the grammar change; commit each green step.

## Fixtures

The self-build no longer contains `->` (SM-S1 swept it), so it cannot assert the reject — write the one
non-self-build path (the no-tombstone guard):

| fixture | asserts | expected |
|---|---|---|
| `arrow_token_removed` | a program using `fn f() -> T` no longer parses — the GENERIC unexpected-token error, NOT a bespoke "`->` was removed" message (no tombstone); a program using `fn f(): T` still compiles | `EXPECT_COMPILE_FAIL` |
| `ffi_opaque_ptr_wrap` | an `src/`-style `extern fn` returning opaque `ptr`, crossed with `.__wrap<T>()`, round-trips without `unsafe` | `0` |

## Gate

`[RITUAL]` — full native ladder + a genuine expurgo reseed. Build gen2 native (the `:`-only corpus), the two
fixtures green, FIXPOINT `gen2==gen3` byte-identical, reseed (the new seed no longer understands `->`). "Green"
= `->` no longer lexes/parses (generic error, no tombstone), `:` returns still compile, `src/` FFI is
opaque-`ptr`, gen2==gen3. **Reseed-class: expurgo.**

## Deps

`SM-S1` (all `:` swept — the precondition that the `Arrow` branch is dead before removal).

## Done when

`Arrow`/`->` is gone from the lexer + `token.tks` + `parse_function` (clean, no tombstone), `src/` FFI is
opaque-`ptr`, `arrow_token_removed` + `ffi_opaque_ptr_wrap` pass, the fixpoint `gen2==gen3` holds, and the
expurgo reseed is captured.

## Judgment calls (flagged for the implementer)

- **Arrow may already be absent on the base branch.** Measured on `origin/fix/retirement` @ `d89ff6b1`:
  `token.tks` has only `FatArrow` (`:78`), NOT `Arrow`; `lexer.tks` scans only `=>` (`:515` `FatArrow`), NOT
  `->`; `parse_decl.tks` has NO `Arrow` reference. This suggests an earlier wave (SM-G1 additive `:` + the SM-S1
  sweep) already landed the `Arrow`-token expurgo AHEAD of this manifest slot. **The implementer MUST re-measure
  at dispatch:** if `Arrow`/`->` is already gone, SM-S4's residual reduces to (a) confirming the token/branch
  are absent (a clean-state assertion, no edit), (b) the `src/` FFI→opaque-`ptr` migration, and (c) authoring
  `arrow_token_removed` as the guard. The reseed still fires as the E1 expurgo (it also carries SM-S5). This is
  a state-divergence flag, not a plan change — the crumb's INTENT (no `->` on the surface, clean, no tombstone)
  is delivered either way.
