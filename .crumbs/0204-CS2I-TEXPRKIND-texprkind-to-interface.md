---
seq: 0204
crumb-id: CS2I-TEXPRKIND
milestone: CS2I
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [CS2I-TYPE]
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:2.1"
  - "docs/design/somas-grandes-do-compilador-para-interface.md:5"
---

# 0204 · CS2I-TEXPRKIND — `@TExprKind()` → `interface TExprKind`

> O outro grande corte: `TExpr.kind` (~290 B inline) → fat-pointer de 16 B.

## Goal

Converter `@TExprKind()` (26 membros) para `interface TExprKind`. `TExpr.kind` é o
discriminante de TODA expressão tipada — o segundo maior consumo de memória por-nó
depois de `Type`. Byte-mover; dirige reseed. Depende de CS2I-TYPE (membros de
TExprKind têm campos `Type`, já interface).

## Where

- `src/checker/tast.tks:12-46` — os 26 membros `TNumber`…`TBlockExpr`:
  `pub type TNumber = struct { … }` → `class & TExprKind { … }` (idem todos; campos
  `Type` já interface pós-CS2I-TYPE).
- `src/checker/tast.tks:52` — `macro TExprKind()` → `pub type TExprKind = interface { }`.
- `src/checker/tast.tks:6` — `TExpr.kind: @TExprKind()` → `: TExprKind`.
- `@TExprKind()` restantes (`tast.tks`, `consteval_form.tks`) → `TExprKind`.
- `match texpr.kind { … }` em `typer.tks`/`codegen.tks`/`lower.tks`/`emit/**` — corpo
  inalterado; exaustividade fecha via CS2I-EXH.

## How

§2.1 aplicado; declarar interface, membros → classes, campo → fat-pointer, matches
mantêm corpo. Converter mutação in-place listada no audit (improvável). Sem bump de
wire (§7).

## Rulings & laws

- **Teko-only.** **Decisão do dono (afirmação 1).** **Valor→ref (§2.2).**
- **RATCHET D68:** medir Δpico — queda estrita (o outro grande corte).
- **W15; nunca `teko test .`; subshell `ulimit -v 4718592`; `gen2==gen3`.**

## Fixtures

`none — the fixpoint self-build exercises this`.

## Gate

`[RITUAL]` — reseed, `gen2==gen3`, Δpico não-crescente. `fixpoint-rebuild`.

## Deps

`CS2I-TYPE`

## Done when

`TExprKind` é interface, os 26 membros são classes, `TExpr.kind` é fat-pointer,
self-build reseeda `gen2==gen3`, pico cai.
