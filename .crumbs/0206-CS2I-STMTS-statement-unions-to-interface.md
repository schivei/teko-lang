---
seq: 0206
crumb-id: CS2I-STMTS
milestone: CS2I
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [CS2I-EXPRKIND]
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:2.1"
  - "docs/design/somas-grandes-do-compilador-para-interface.md:6"
---

# 0206 · CS2I-STMTS — `@Statement()` + `@TStatement()` → interfaces

> Os dois nós de statement (parser + tipado) inline → fat-pointer.

## Goal

Converter `@Statement()` (13 membros, `src/parser/ast.tks:154`) e `@TStatement()`
(11 membros, `src/checker/tast.tks:69`) para interfaces. Embutidos em todo corpo
(`[]@Statement()`/`[]@TStatement()`). Byte-mover; reseed. Prepara o interface-extends
que CS2I-SHARED usa (`ItemKind : Statement`, `TItem : TStatement`).

## Where

- `src/parser/ast.tks:154` — `macro Statement()` → `pub type Statement = interface { }`;
  os 13 membros `Binding`/`Assign`/… → `class & Statement`.
- `src/checker/tast.tks:69` — `macro TStatement()` → `interface`; os 11 membros
  `TBinding`/`TAssign`/… → `class & TStatement`.
- `[]@Statement()`/`[]@TStatement()` e matches em toda a árvore — corpo inalterado.

## How

§2.1 para as duas uniões (independentes uma da outra). Matches mantêm corpo;
exaustividade via CS2I-EXH. Converter mutação in-place do audit.

## Rulings & laws

- **Teko-only. Decisão do dono (afirmação 1). Valor→ref (§2.2).**
- **RATCHET D68: Δpico queda estrita. W15; nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3`.**

## Fixtures

`none — the fixpoint self-build exercises this`.

## Gate

`[RITUAL]` — reseed, `gen2==gen3`, Δpico não-crescente. `fixpoint-rebuild`.

## Deps

`CS2I-EXPRKIND`

## Done when

`Statement` e `TStatement` são interfaces, seus membros são classes, os corpos são
`[]Iface`, reseed `gen2==gen3`, pico cai.
