---
seq: 0031
crumb-id: SM-S1
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [SM-R1]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1201-1202"   # §10 Phase S — S1
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:191-196"     # §4.2 byte-preserving post-sweep
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1325"        # §12 fixpoint is the load-bearing gate
---

# 0031 · SM-S1 — sweep `src/` + `.tkt` to `:` returns

> Sweep `src/` + `.tkt` to `:` returns — the mechanical source rewrite of every `-> T` return to `: T`,
> against a seed that already accepts both.

## Goal

Mechanically rewrite EVERY return-type annotation in `src/` and the `.tkt` corpus from the old `-> T` form
to the unified `: T` form (`fn f(params): T { }`), now that SM-R1 (`0030`) has captured a seed that ACCEPTS
both `Arrow` and `Colon` (the additive G1 crumb, `0007`). This is a byte-preserving source sweep: `:` and
`->` lex to distinct tokens but parse to the IDENTICAL return-type AST node, so the rewritten source emits
the same bytes — the fixpoint `gen2==gen3` byte-identity is the proof. The `Arrow`/`->` acceptance STAYS in
the lexer/parser through this crumb (its removal from the lexer + `token.tks`, plus the FFI migration to
opaque `ptr`, is the SEPARATE M3 expurgo crumb SM-S4, `0091`). It core-consumes the SM-R1 seed (rebuilds
the compiler on it) and teaches nothing new → `fixpoint-rebuild`, not a teaching reseed.

## Where

- `src/**/*.tks` — every return annotation `) -> T` → `): T` (the mechanical rewrite; the whole compiler
  source).
- `src/**/*.tkt` + the `.tkt` corpus — every `-> T` return in the test/expectation files → `: T`.
- `src/parser/parse_decl.tks:359` — `parse_function` return parse (accepts `Arrow` OR `Colon` since G1) —
  UNCHANGED here; still accepts both (the `Arrow` branch is deleted in SM-S4, M3).
- `src/lexer/token.tks:85` — `Arrow // ->` — UNCHANGED (still lexes; removed in SM-S4). `:86` `FatArrow`
  (`=>`) NEVER touched.

NEW: no new surface; this is a pure mechanical source rewrite gated by byte-identity.

## How

1. **Rewrite return annotations only.** Replace `) -> T` with `): T` at every function/method/closure return
   site in `src/` and `.tkt`. Do NOT touch `=>` (`FatArrow`, match arms / lambdas) — it is a distinct token
   and unaffected. The `:` in param/field/var positions is already `:` (this crumb is the RETURN position).
2. **Leave the acceptance in place.** The parser still accepts `Arrow` OR `Colon` (`parse_decl.tks:359`,
   from G1); the lexer still emits `Arrow`. This crumb does NOT delete the old branch — that is SM-S4
   (`0091`, M3), after every `->` occurrence is gone from `src/` and the FFI is migrated to opaque `ptr`.
   Keeping acceptance means a stray un-swept `->` still compiles, so the sweep can land incrementally,
   fixpoint-gated per batch.
3. **Sweep the `.tkt`/`.tkr` corpus too.** Every return `->` in the test-expectation `.tkt` files and any
   `.tkr` fixture is rewritten to `:`, so the corpus matches the swept source (a `.tkt` sweep is mandatory
   after a syntax-surface change per the safety law).
4. **Byte-identity is the gate.** Because `:` and `->` parse to the same return-type AST node, the emitted C
   is byte-identical to pre-sweep. Build gen2 on the SM-R1 seed, run the scoped regression, and prove
   `gen2==gen3` byte-identical — the load-bearing proof that the rewrite changed no semantics.
5. **Commit per green batch.** Sweep in reviewable batches (by directory), each fixpoint-green and committed,
   so an infra hiccup loses at most one batch.

## Rulings & laws

- **Teko-only:** source `.tks`/`.tkt` rewrite; the parser/lexer are unchanged (no C twin touched).
- **W15 full Javadoc:** unaffected — the rewrite touches return operators, not doc-comments; no `//`
  introduced.
- **Byte-preserving after the sweep (§4.2):** `:` and `->` parse to the IDENTICAL AST node → same bytes; the
  fixpoint proves it.
- **`Arrow` removal is SEPARATE (SM-S4, M3):** this crumb keeps `->` acceptance; it is NOT a removal/expurgo.
- **Sweep `.tkt`/`.tkr` after the surface change (safety law):** the corpus is rewritten in lockstep.
- **Safety:** NEVER `teko test .`; build gen2 in a subshell with `ulimit -v 6815744`; commit each green
  batch; NO reseed (fixpoint-rebuild core-consumes the SM-R1 seed); fixpoint `gen2==gen3` byte-identical.

## Fixtures

`none — the fixpoint self-build exercises this`. The sweep rewrites the compiler's OWN source; building gen2
on the swept source and proving `gen2==gen3` byte-identical is exactly the exercise — a scoped `.tkr` would
be redundant (any parse regression fails the build directly).

## Gate

`[fixpoint]` — build gen2 on the SM-R1 seed + scoped regression + `gen2==gen3` byte-identity. "Green" =
every `-> T` return in `src/` + `.tkt` is now `: T`, the parser still accepts both (removal is SM-S4), the
build is byte-identical to pre-sweep (`gen2==gen3`). Reseed-class: `fixpoint-rebuild` (core-consumes the
SM-R1 seed; teaches nothing).

## Deps

`SM-R1` (the seed must ACCEPT `:` returns before the source is swept to them).

## Done when

Every return annotation in `src/` and the `.tkt` corpus is `: T`, `=>` is untouched, the `Arrow` acceptance
remains (its removal is SM-S4), and gen2 built on the SM-R1 seed is byte-identical (`gen2==gen3`).
