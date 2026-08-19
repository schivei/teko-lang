---
seq: 0033
crumb-id: SM-S3
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [SM-R1]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1205-1206"   # §10 Phase S — S3
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:199-258"     # §5 self/base/static grammar+checker
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:250-258"     # §5.4 byte-preservation
---

# 0033 · SM-S3 — sweep methods to `self`/`base`/`static`; remove loose-receiver parse

> Sweep methods to `self`/`base`/`static`; remove the loose-receiver parse + `allow_untyped_first` — rewrite
> the ~89 receiver sites to the synthetic-`self` form and delete the old loose-receiver acceptance.

## Goal

Complete the method-receiver migration: rewrite every method in `src/` to the new form — a synthetic `self`
receiver (no source-named receiver param), `base` for the superclass upcast, and the `static` modifier for
receiver-less methods — then DELETE the old loose-receiver parse path and the `allow_untyped_first`
acceptance that the additive G3 crumb (`0009`) kept accepting so the SM-R1 seed could parse old source. The
synthetic-`self` machinery is already in place (`inject_synthetic_receiver`, `consume_static_modifier`,
`parse_decl.tks:157,173`); this crumb sweeps the ~89 receiver sites + `synth.tks` to depend on it and
removes the loose-receiver fallback. Byte-preserving: `params[0]` is still the untyped receiver with its
`type_ann` rewritten to the struct name before codegen (§5.4), so the emitted C is byte-identical — the
fixpoint proves it. It core-consumes the SM-R1 seed → `fixpoint-rebuild`. This is the LOAD-BEARING sweep of
Phase S (the biggest mechanical rewrite; the byte-identity gate is what proves a corpus-wide method rewrite
survives the fixpoint).

## Where

- `src/**/*.tks` — the ~89 method receiver sites — rewrite each explicit loose receiver to the synthetic
  `self` form; add `static` on the receiver-less methods; use `base` for superclass upcalls.
- `src/parser/parse_decl.tks:34` — `parse_params` `allow_untyped_first` — REMOVED (the additive window that
  accepted BOTH old loose receiver AND synthetic `self`); after the sweep nothing needs the loose form.
- `src/parser/parse_decl.tks:157,173` — `consume_static_modifier` / `inject_synthetic_receiver` — KEPT
  (the synthetic-`self` machinery is now the ONLY path).
- `src/checker/typer.tks:3110-3128` — the synthetic `base: <Base> = <self upcast>` prepend — now
  unconditional for `has_base` methods.
- `src/codegen/synth.tks:199,314,365,418,475` — the method emitters — now emit `name="self"` + `is_static`
  directly (byte-neutral: `params[0]` receiver `type_ann` still rewritten to `Named{struct_name}` before
  codegen).
- `src/checker/collect.tks:459` — `synthesize_mret_structs` — UNCHANGED (multi-return synthesis is
  orthogonal to the receiver form).
- `src/**/*.tkt` — the method-declaration expectation corpus — rewritten to the swept form in lockstep.

NEW: no new surface; a source rewrite + a clean removal of the loose-receiver acceptance.

## How

1. **Rewrite the receiver sites.** For each instance method, drop the explicit loose receiver param and rely
   on the synthetic `self` (`inject_synthetic_receiver`); mark receiver-less methods `static`; use `base`
   for the superclass upcast (the rename G3 landed). `self` is CONTEXTUAL (plain `Ident`, meaning only
   inside a method body); `base` STAYS a plain identifier too (it is a live production local name elsewhere,
   §14 R2 — do NOT reserve it).
2. **Remove `allow_untyped_first` (clean expurgo).** After the sweep leaves no loose receiver in `src/`,
   delete the `allow_untyped_first` acceptance (`parse_decl.tks:34`) and the loose-receiver parse fallback —
   a clean expurgo of the parser path, NO tombstone diagnostic (nothing points at the old form). The
   synthetic-`self` path is now the only way to declare a method.
3. **Keep the emit byte-neutral.** `params[0]` remains the untyped receiver whose `type_ann` is rewritten to
   `Named{struct_name}` before codegen (§5.4); the method emitters (`synth.tks:199…475`) emit `name="self"`
   + `is_static` directly. The invariants `is_instance` (`typer.tks:745`), `method_sig_matches`
   (`collect.tks:721`), `is_static_method` (`di.tks:147`) are preserved.
4. **Byte-identity is the gate.** Build gen2 on the SM-R1 seed, run the scoped regression, prove
   `gen2==gen3` byte-identical — the proof the corpus-wide method rewrite changed no semantics. Commit per
   green batch (by directory); sweep `.tkt`/`.tkr` in lockstep.
5. **This is the load-bearing sweep.** Because it is the largest mechanical rewrite (~89 sites + `synth.tks`),
   the fixpoint byte-identity is the gate that matters most — a single mis-rewritten receiver would break
   `gen2==gen3` and be caught immediately.

## Rulings & laws

- **Teko-only:** source `.tks`/`.tkt` rewrite + parser removal `.tks`; no C twin.
- **W15 full Javadoc** unaffected; no inline `//` introduced.
- **Byte-preserving (§5.4):** `params[0]` receiver + `type_ann`-rewrite keeps the emit byte-identical; the
  fixpoint proves it.
- **`base` NOT reserved (§14 R2):** it stays a contextual identifier (live production local name); only
  `static` is a reserved keyword; `self` is contextual.
- **Removals = clean expurgo, NO tombstone (CLAUDE.md):** `allow_untyped_first` + the loose-receiver parse
  are deleted cleanly once the source is swept.
- **Safety:** NEVER `teko test .`; build gen2 in a subshell with `ulimit -v 6815744`; commit each green
  batch; NO reseed (fixpoint-rebuild); fixpoint `gen2==gen3`; sweep `.tkt`/`.tkr` in lockstep.

## Fixtures

`none — the fixpoint self-build exercises this`. The sweep rewrites the compiler's own ~89 method receivers
and removes the loose-receiver acceptance; building gen2 byte-identical IS the exercise. A mis-rewritten
receiver breaks `gen2==gen3` directly; the loose-receiver-now-rejected behavior is a removal the build
catches.

## Gate

`[fixpoint]` — build gen2 on the SM-R1 seed + scoped regression + `gen2==gen3` byte-identity. "Green" =
every method in `src/` + `.tkt` uses the synthetic `self` / `base` / `static` form, `allow_untyped_first` +
the loose-receiver parse are cleanly removed (no tombstone), the build is byte-identical (`gen2==gen3`).
Reseed-class: `fixpoint-rebuild`.

## Deps

`SM-R1` (the seed must accept the synthetic-`self` form before the ~89 sites are swept to it).

## Done when

Every method in `src/` + `.tkt` uses synthetic `self` / `base` / `static`, `allow_untyped_first` and the
loose-receiver parse are cleanly expurgated (no tombstone), the emit invariants hold, and gen2 on the SM-R1
seed is byte-identical (`gen2==gen3`).
