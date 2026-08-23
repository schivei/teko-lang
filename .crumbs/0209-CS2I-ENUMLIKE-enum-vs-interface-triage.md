---
seq: 0209
crumb-id: CS2I-ENUMLIKE
milestone: CS2I
gate: "[fixpoint] | [RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [CS2I-EXH]
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:5"
---

# 0209 · CS2I-ENUMLIKE — triagem enum-vs-interface das uniões enum-like

> `ResidenceTier`·`PointsTo`·`BorrowedFrom`·`Unique`·`FSpecKind`·`ConstValueKind`·
> `RegAssignment` — por-tipo: enum puro (mais barato) ou interface.

## Goal

As uniões pequenas cujos arms carregam pouco/nenhum payload distinto: decidir
por-tipo entre `enum` puro (mais barato em runtime; `match` sobre enum já funciona,
`check_enum_pattern`) e interface (se há payload). Fecha a onda de conversão.

## Where

- `src/checker/residence.tks:18` `ResidenceTier` (5); `src/checker/spine.tks:18/28/33`
  `PointsTo`(5)/`BorrowedFrom`(4)/`Unique`(3); `src/parser/ast.tks` `FSpecKind`(3) +
  `src/checker/tast.tks:36` `TFSpecKind` (já piloto, 0202); `src/checker/
  comptime_fold.tks` `ConstValueKind`(5); `src/backend/regalloc.tks:523`
  `RegAssignment`(2).

## How

Por-tipo, decisão REGISTRADA (não "TODO"):
1. Arms SEM campo distinto (discriminante puro) → `enum { A; B; … }`; matches viram
   `match x { A => …; B => … }` (enum-member), o campo vira 4 B em vez de fat-pointer.
2. Arms COM payload (`TFSpecStatic{s}`, `ConstValueKind` variantes com dado) →
   interface, como as demais.
3. `RegAssignment` (`InReg`/`Spilled`): inspecionar campos — se `Spilled{slot}` tem
   payload e `InReg{reg}` também, interface; se ambos são só tag, enum.

## Rulings & laws

- **Teko-only. Decisão do dono (afirmação 1) — mas a decisão permite a forma mais
  barata (enum) quando não há payload; interface só onde há dado.**
- **RATCHET D68: Δpico não-crescente. W15; nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3`.**

## Fixtures

`none — the fixpoint self-build exercises this`.

## Gate

`[fixpoint]` (para os que viram enum sem mudar bytes emitidos de forma reseed-neutra)
ou `[RITUAL]` (os que viram interface / mudam representação). Por-tipo. `fixpoint-rebuild`.

## Deps

`CS2I-EXH`

## Done when

Cada união enum-like está classificada e convertida (enum ou interface), com a decisão
registrada, reseed `gen2==gen3`, pico não cresce.
