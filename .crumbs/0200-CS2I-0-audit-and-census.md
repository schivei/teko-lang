---
seq: 0200
crumb-id: CS2I-0
milestone: CS2I
gate: "— (measurement/audit, runs in series)"
reseed-class: "none"
deps: []
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:0-11"   # §0 decision
  - "docs/design/somas-grandes-do-compilador-para-interface.md:2.2"     # value->ref invariant
  - "docs/design/somas-grandes-do-compilador-para-interface.md:9"       # census
---

# 0200 · CS2I-0 — audit de imutabilidade + censo de nós

> Mede o ganho de memória e prova o invariante que torna a virada valor→referência sã.

## Goal

Duas medições/auditorias read-only que a onda inteira referencia mas NÃO bloqueia
nelas. (1) **Audit de imutabilidade** dos ADTs de IR (`Type`/`TExpr`/`TExprKind`/
`Statement`/`TStatement`/`ExprKind`) — prova que são construídos frescos e nunca
mutados in-place, o invariante de soundness da virada `struct`→`class`
(referência). (2) **Censo** que quantifica o −ΔGB. Não toca `src/` de forma que
mude bytes emitidos — é diagnóstico. Byte-preserving (nenhuma edição de produto).

## Where

- `src/checker/**`, `src/parser/**`, `src/lir/**`, `src/backend/**` — varredura
  read-only por `x.<campo> = …` onde `x` é um valor de um `@U()`-alvo ou impl.
- Instrumentação pontual do build seco (contadores em torno da construção de
  `TExpr`/`Type`) — TRANSITÓRIA, revertida ao fim do censo; não entra num reseed.

## How

1. **Audit:** listar todo sítio de atribuição-de-campo in-place sobre um dos ADTs de
   IR alvo. Espera-se ~0 (estilo SSA-like: o checker constrói nós novos, não muta).
   Cada achado é anotado com o crumb de conversão da sua união, onde vira
   construir-fresco (idioma NO-PUSHES / purge-na-reatribuição, CLAUDE.md).
2. **Censo (um único run instrumentado, extrai TUDO):** `N_TExpr` (nós tipados
   construídos), `N_Type_distintos` (classes de equivalência por `type_eq`),
   `sizeof(TExpr)`/`sizeof(@Type() inline)`/`sizeof(@TExprKind() inline)` hoje vs
   16 B fat-pointer, e o pico `teko: memory: peak <N> MB` do build seco baseline.
3. **Relatório** ao coordenador: o Δ estimado por-nó, o total, e o teto de interning
   (`N_Type_distintos / N_TExpr`).

## Rulings & laws

- **Teko-only:** a instrumentação, se em `.tks`, é transitória e não reseeda.
- **MENOS BUILD, MAIS CÓDIGO (dono 2026-08-24):** UM run instrumentado extrai todos
  os números; não N builds.
- **RATCHET D68:** este crumb estabelece o baseline maçã contra o qual toda
  conversão mede queda estrita.
- **Testes só no CI; nunca `teko test .`.** Medição = build seco instrumentado.

## Fixtures

`none — measurement only`

## Gate

`—` roda em série; produz relatório. Não altera bytes; não reseeda.

## Deps

`—`

## Done when

O relatório entrega `N_TExpr`, `N_Type_distintos`, os sizeofs, o pico baseline, e a
lista (idealmente vazia) de mutações in-place a converter — e o coordenador tem o
número que quantifica o −0,8…−1,5 GB.
