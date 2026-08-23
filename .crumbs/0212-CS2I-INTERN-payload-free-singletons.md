---
seq: 0212
crumb-id: CS2I-INTERN
milestone: CS2I
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [CS2I-TYPE, CS2I-0]
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:2.3"
  - "docs/design/somas-grandes-do-compilador-para-interface.md:9"
---

# 0212 · CS2I-INTERN — singletons dos membros payload-free (censo-gated)

> Otimização de 2ª ordem: um objeto imortal por membro sem payload; `.data`
> compartilhado. Só se o censo mostrar volume.

## Goal

Os membros de `Type` sem payload (`Void`/`Byte`/`Char`/`Str`/`Error`/`Null`/`Uptr`)
são hoje construídos frescos a cada uso. Como classes imutáveis, podem ser SINGLETONS
(um objeto por membro, `.data` compartilhado por todo valor de interface daquele
membro) — corta a alocação por-uso. Gated no censo (CS2I-0): só vale se
`N_Type_distintos`/o volume de construção justificar.

## Where

- `src/checker/type.tks` — construtores centralizados dos membros payload-free de
  `Type`; um `const`/factory que devolve o `.data` do singleton imortal.
- Chamadores que constroem `Void { }`/`Byte { }`/… → passam pelo factory.

## How

1. Um singleton imortal por membro payload-free (arena de vida-do-processo ou estático).
2. A construção do membro devolve sempre o mesmo `.data` (o fat-pointer carrega a
   mesma vtable + o mesmo ponteiro). Como são imutáveis (invariante §2.2), o
   compartilhamento é seguro — nenhum consumidor muta.
3. Medir Δpico contra o baseline de CS2I-TYPE.

## Rulings & laws

- **Teko-only.** **Imutabilidade (§2.2) habilita o compartilhamento seguro.**
- **RATCHET D68:** só landa se BAIXAR o pico (estrito); gated no censo — se não render,
  NÃO landar (evita complexidade sem ganho).
- **W15; nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3`.**

## Fixtures

`none — the fixpoint self-build exercises this`.

## Gate

`[RITUAL]` — reseed, `gen2==gen3`, Δpico queda estrita (senão não landa). `fixpoint-rebuild`.

## Deps

`CS2I-TYPE`, `CS2I-0` (o censo decide se vale)

## Done when

Os membros payload-free de `Type` são singletons imortais compartilhados, o pico cai
medido, reseed `gen2==gen3` — OU o censo mostra que não rende e o crumb é arquivado.
