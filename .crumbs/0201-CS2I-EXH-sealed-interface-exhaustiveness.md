---
seq: 0201
crumb-id: CS2I-EXH
milestone: CS2I
gate: "[fixpoint]"
reseed-class: "none (folds)"
deps: []
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:4"   # §4 the one new machinery
---

# 0201 · CS2I-EXH — exaustividade de `match` sobre interface selada

> Enumera os implementadores de uma interface para fechar a exaustividade — a única
> lógica nova da onda; precede toda conversão.

## Goal

Hoje `match` sobre um valor de interface só é exaustivo com catch-all
(`exhaustive_type_subject` devolve `false` no ramo de interface salvo bind-all).
Depois que uma união-macro vira interface, os `match` que listam todos os membros
sem `_` (ex. `type_eq`) quebrariam. Este crumb adiciona a enumeração do conjunto
FECHADO de implementadores (gêmeo de `subclasses_of`) e cobre-o, preservando a
exaustividade "de graça" pela selagem-por-padrão. **Aditivo/inerte:** só adiciona um
caminho de ACEITAÇÃO — nenhum programa hoje válido muda de bytes. Byte-preserving.

## Where

- `src/checker/match.tks:511` — `exhaustive_type_subject` — estender o ramo
  `is_interface_name` (`match.tks:518-520`) para enumerar+cobrir os implementadores,
  espelhando o ramo polimórfico (`match.tks:514-516`).
- `src/checker/match.tks` — **NOVAS** `implementors_of`, `iface_covered` (gêmeas de
  `subclasses_of:457` / `class_covered:478`).
- Reusa: `is_interface_name`, `type_conforms_to` (`src/checker/resolve.tks`),
  `some_arm_names`, `name_last_segment`, `tt_len`/`tt_at`.

## How

1. Adicionar `implementors_of(iface, table)` — varre a type-table por toda classe
   NÃO-abstrata cuja conformância alcança `iface` (`type_conforms_to`, já transitivo
   em `extends`), em ordem de declaração.

```teko
/**
 * implementors_of — o conjunto FECHADO (whole-program) de classes concretas que
 * conformam à interface `iface`. Gêmeo de `subclasses_of` no eixo de interface: fecha
 * a exaustividade de `match` sobre um valor de interface (impls seladas por construção).
 *
 * @param iface  o nome canônico da interface
 * @param table  a type-table do programa (mundo fechado da compilação)
 * @return       os nomes das classes conformantes concretas, em ordem de declaração
 * @see          teko::checker::subclasses_of
 * @since 0.3.1
 */
fn implementors_of(iface: str, table: TypeTable): []str

/**
 * iface_covered — todo implementador do conjunto FECHADO de `iface` é nomeado por um
 * arm SEM guarda? Gêmeo de `class_covered`; um arm com `when` NÃO cobre seu caso.
 *
 * @param arms    os arms do match
 * @param impls   os implementadores concretos (de `implementors_of`)
 * @param table   a type-table
 * @param ref_ns  o namespace do sítio (resolução dos nomes de arm)
 * @return        true sse todo `impls[i]` é coberto por um arm sem guarda
 * @since 0.3.1
 */
fn iface_covered(arms: []parser::Arm, impls: []str, table: TypeTable, ref_ns: str): bool
```

2. No ramo `is_interface_name` de `exhaustive_type_subject`:
```
if is_interface_name(n.name, table) {
    if some_arm_names(arms, name_last_segment(n.name), table, ref_ns) { return true }
    return iface_covered(arms, implementors_of(n.name, table), table, ref_ns)
}
```
3. Fecho = classes conformantes visíveis no programa (mundo fechado; são para o
   compilador auto-compilando). A regra "`when` não cobre" reusa `!arms[i].has_when`.

## Rulings & laws

- **Teko-only:** só `src/checker/match.tks`.
- **Selagem-por-padrão (decisão do dono, afirmação 3):** o conjunto de impls é
  fechado; a enumeração é a extensão natural de `subclasses_of`. Deliberado — não fork.
- **NÃO DETECTAR O QUE NÃO EXISTE:** só adiciona cobertura de um caso que a superfície
  PRODUZ (match exaustivo sobre interface selada); nenhum ramo para caso impossível.
- **W15:** full Javadoc nas duas novas fns; sem `//`.
- **Fork protocol:** a nota §10-R4 (visível ao dev) é deliberada pela selagem; não HALT.
- **Safety:** nunca `teko test .`; build em subshell `ulimit -v 4718592`.

## Fixtures

O self-build (código válido) nunca dirige a REJEIÇÃO por não-exaustividade — oráculo
isolado obrigatório:

| fixture | asserts | expected |
|---|---|---|
| `iface_match_nonexhaustive` | `match` sobre interface selada faltando um impl, sem `_`, é rejeitado | EXPECT_COMPILE_FAIL |
| `iface_match_exhaustive_ok` | o mesmo match cobrindo todos os impls compila sem `_` | 0 |

## Gate

`[fixpoint]` — byte-preservante (só adiciona aceitação; nenhum programa válido muda de
bytes). "Green" = os dois fixtures + `gen2==gen3` byte-idêntico. Reseed-class: `none`.

## Deps

`—`

## Done when

`match` sobre uma interface selada é exaustivo SEM catch-all quando cobre todos os
implementadores; `iface_match_nonexhaustive` é `EXPECT_COMPILE_FAIL`; build byte-idêntico.
