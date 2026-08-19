---
seq: 0032
crumb-id: SM-S2
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [SM-R1]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1203-1204"   # §10 Phase S — S2
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:103-143"     # §2 let/mut → var (grammar + CF3)
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:138-143"     # §2.4 byte-preservation
---

# 0032 · SM-S2 — sweep to `var`; drop `let`/`mut` acceptance

> Sweep to `var`; drop `let`/`mut` acceptance — rewrite every `let`/`mut` local to `var`, then remove the
> soft-deprecated `let`/`mut` keywords from the parser.

## Goal

Complete the `let`/`mut` → `var` merge: mechanically rewrite every `let`/`mut` local declaration in `src/`
and `.tkt` to `var` (everything mutable; one keyword for all locals; type optional, inference stays;
`const` retained), THEN remove the soft-deprecated `let`/`mut` acceptance from the parser (they were kept
accepting through the additive G2 crumb, `0008`, so the SM-R1 seed parses old source; now that the source is
swept, the acceptance is dead and removed). Byte-preserving: `let`/`mut`/`var` all lower to the SAME
`Binding` AST (`BindKind` is intent, not safety — §1.4), so the rewritten source emits identical bytes; the
fixpoint proves it. Because CF3 (the const-fold single-assignment analysis) was re-based on
flow-single-assignment in G2, it survives the merge. It core-consumes the SM-R1 seed → `fixpoint-rebuild`.

## Where

- `src/**/*.tks` — every `let x = …` / `mut x = …` local → `var x = …` (the mechanical rewrite). Only 4
  residual `let`/`mut` decls remain in `src/` (mostly `src/parser/loop_head.tks:34,38,162` desugaring
  helpers that CONSTRUCT `Binding` nodes) — those construct-site `BindKind::Let`/`Mut` become `BindKind` on
  the unified node.
- `src/**/*.tkt` — every `let`/`mut` in the test/expectation corpus → `var`.
- `src/lexer/token.tks:15-16` — `Let` / `Mut` tokens — REMOVED after the sweep (clean expurgo, no
  tombstone); `var` lexes as `Ident` with text `"var"` (the parser recognizes it, `parse_decl.tks:329`).
- `src/parser/parse_decl.tks:168` — the `Let`-acceptance branch (`if is_kind_at(tokens, pos,
  lexer::TokenKind::Let) { return true }`) — REMOVED after the sweep.
- `src/parser/ast.tks:92` — `BindKind = enum { Let; Mut; Const }` — collapse the `Let`/`Mut` arms per §2
  (the intent distinction is gone; `Const` stays); update `loop_head.tks:34,38,162` constructors.

NEW: no new surface; a source rewrite + a clean removal of the dead `let`/`mut` acceptance.

## How

1. **Rewrite the locals.** `let x`/`mut x` → `var x` at every declaration site in `src/` + `.tkt`. `const`
   is UNTOUCHED (a distinct binding kind, retained). Inference is unchanged — a `var x = expr` with no type
   annotation still infers.
2. **Fold the `BindKind` construct-sites.** The four residual `BindKind::Let`/`BindKind::Mut` constructions
   in `loop_head.tks` (loop-variable desugaring) collapse to the unified mutable binding; `BindKind` keeps
   only `{ Var; Const }` (or equivalent), per §2's "everything mutable, `const` retained".
3. **Remove the dead acceptance (clean expurgo).** After the sweep leaves ZERO `let`/`mut` in `src/`, delete
   the `Let`/`Mut` tokens (`token.tks:15-16`), the `Let`-accept branch (`parse_decl.tks:168`), and the
   `BindKind::Let`/`Mut` arms (`ast.tks:92`) — a clean expurgo of lexer + parser + AST, NO tombstone
   diagnostic (nothing left points at the old keywords).
4. **CF3 survives (G2 pre-work).** The const-fold single-assignment analysis was re-based on
   flow-single-assignment in G2 (`cf3_fold_survives_let_merge`), so collapsing `Let`/`Mut` does not regress
   it — verify the CF3 fold still holds on the swept source.
5. **Byte-identity is the gate.** `let`/`mut`/`var` lower to the same `Binding`, so the emitted C is
   byte-identical. Build gen2 on the SM-R1 seed, run the scoped regression, prove `gen2==gen3`. Commit per
   green batch; sweep `.tkt`/`.tkr` in lockstep.

## Rulings & laws

- **Teko-only:** source `.tks`/`.tkt` rewrite + parser/lexer/AST removal `.tks`; no C twin.
- **W15 full Javadoc** unaffected; no inline `//` introduced.
- **`BindKind` is intent, not safety (§1.4):** `let`/`mut`/`var` share the mutable `Binding`; the merge is
  byte-preserving (§2.4).
- **Removals = clean expurgo, NO tombstone (CLAUDE.md):** the `let`/`mut` tokens + accept branch + AST arms
  are deleted cleanly once the source is swept; no deprecation diagnostic.
- **`const` retained; CF3 re-based (G2):** `const` untouched; `cf3_fold_survives_let_merge` holds.
- **Safety:** NEVER `teko test .`; build gen2 in a subshell with `ulimit -v 6815744`; commit each green
  batch; NO reseed (fixpoint-rebuild); fixpoint `gen2==gen3`; sweep `.tkt`/`.tkr` in lockstep.

## Fixtures

`none — the fixpoint self-build exercises this`. The sweep rewrites the compiler's own source and removes
dead acceptance; building gen2 byte-identical IS the exercise. (The `let`/`mut`-now-rejected behavior is a
removal — any un-swept `let` would fail the build, which the fixpoint catches; the CF3-survives property is
covered by G2's own `cf3_fold_survives_let_merge` fixture already in the seed.)

## Gate

`[fixpoint]` — build gen2 on the SM-R1 seed + scoped regression + `gen2==gen3` byte-identity. "Green" =
every `let`/`mut` local in `src/` + `.tkt` is now `var`, the `Let`/`Mut` tokens + accept branch + AST arms
are cleanly removed (no tombstone), CF3 survives, the build is byte-identical (`gen2==gen3`). Reseed-class:
`fixpoint-rebuild`.

## Deps

`SM-R1` (the seed must accept `var` before the sweep; G2's re-based CF3 must be in that seed).

## Done when

Every local in `src/` + `.tkt` is `var`, the `let`/`mut` keywords + acceptance + `BindKind` arms are cleanly
expurgated (no tombstone), `const` and CF3 are intact, and gen2 on the SM-R1 seed is byte-identical
(`gen2==gen3`).
