---
seq: 0205
crumb-id: CS2I-EXPRKIND
milestone: CS2I
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [CS2I-TEXPRKIND]
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:2.1"
---

# 0205 · CS2I-EXPRKIND — `@ExprKind()` → `interface ExprKind`

> O gêmeo não-tipado (parser): `Expr.kind` inline → fat-pointer.

## Goal

Converter `@ExprKind()` (27 membros, `src/parser/ast.tks:89`) para `interface
ExprKind`. `Expr.kind` (`ast.tks:90`) é o nó de expressão do parser. Byte-mover;
reseed. Sequenciado após os nós tipados (mesmo padrão, independente).

## Where

- `src/parser/ast.tks:1-88` — os 27 membros `Number`…`MacroCall` → `class & ExprKind`.
- `src/parser/ast.tks:89` — `macro ExprKind()` → `pub type ExprKind = interface { }`.
- `src/parser/ast.tks:90` — `Expr.kind: @ExprKind()` → `: ExprKind`.
- `match expr.kind { … }` em `parser/**`, `checker/collect.tks`, `checker/expr.tks`,
  `comptime_expand.tks`, `macro_expand.tks` — corpo inalterado.

## How

§2.1; interface + classes + campo fat-pointer; matches mantêm corpo; exaustividade via
CS2I-EXH. Converter mutação in-place do audit (improvável).

## Rulings & laws

- **Teko-only. Decisão do dono (afirmação 1). Valor→ref (§2.2).**
- **RATCHET D68: Δpico queda estrita. W15; nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3`.**

## Fixtures

`none — the fixpoint self-build exercises this`.

## Gate

`[RITUAL]` — reseed, `gen2==gen3`, Δpico não-crescente. `fixpoint-rebuild`.

## Deps

`CS2I-TEXPRKIND`

## Done when

`ExprKind` é interface, os 27 membros são classes, `Expr.kind` fat-pointer, reseed
`gen2==gen3`, pico cai.
