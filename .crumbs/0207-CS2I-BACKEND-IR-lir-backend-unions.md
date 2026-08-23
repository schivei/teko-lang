---
seq: 0207
crumb-id: CS2I-BACKEND-IR
milestone: CS2I
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [CS2I-EXH]
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:2.1"
  - "docs/design/somas-grandes-do-compilador-para-interface.md:5"
---

# 0207 · CS2I-BACKEND-IR — `@LOp()` · `@MInst()` · `@MInstX86()` · `@RegAssignment()`

> As uniões de IR do backend (LIR + instrução ARM64/x86 + reg-assign).

## Goal

Converter as uniões-macro do backend para interfaces. `@MInst()` (32) e `@MInstX86()`
(27) são as maiores por membro. Byte-mover; reseed. O backend/`lower.tks` já COMPILA
no self-build (escreve agora, roda depois — CLAUDE.md), então a virada compila e
reseeda pela rota C já; só a EXECUÇÃO native espera o marco.

## Where

- `src/lir/lir.tks:62` — `macro LOp()` → `interface`; 16 membros → `class & LOp`.
- `src/backend/minst.tks:228` — `macro MInst()` → `interface`; 32 membros → classes.
- `src/backend/minst_x86.tks:174` — `macro MInstX86()` → `interface`; 27 → classes.
- `src/backend/regalloc.tks:523` — `RegAssignment` (2 membros `InReg`/`Spilled`) —
  **avaliar `enum` puro** (delegado a CS2I-ENUMLIKE se sem payload distinto); senão
  interface.
- Matches sobre essas uniões em `lir/**`, `backend/**` — corpo inalterado.

## How

§2.1 por arquivo (grupos independentes). Matches mantêm corpo; exaustividade via
CS2I-EXH. `RegAssignment`: se `InReg`/`Spilled` têm payload → interface; se são
discriminante puro → `enum` (decisão registrada em CS2I-ENUMLIKE). Converter mutação
in-place do audit.

## Rulings & laws

- **Teko-only. Decisão do dono (afirmação 1). Valor→ref (§2.2).**
- **Escreve em Teko, emite em C; native roda depois do marco (CLAUDE.md).**
- **RATCHET D68: Δpico queda estrita. W15; nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3`.**

## Fixtures

`none — the fixpoint self-build exercises this` (o `lower.tks`/backend compila no
self-build; a EXECUÇÃO native é gated no marco, não neste crumb).

## Gate

`[RITUAL]` — reseed, `gen2==gen3`, Δpico não-crescente, agrupado por arquivo.
`fixpoint-rebuild`.

## Deps

`CS2I-EXH`

## Done when

`LOp`/`MInst`/`MInstX86` são interfaces (e `RegAssignment` interface-ou-enum), membros
são classes, reseed `gen2==gen3`, pico não cresce.
