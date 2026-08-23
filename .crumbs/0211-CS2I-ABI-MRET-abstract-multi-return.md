---
seq: 0211
crumb-id: CS2I-ABI-MRET
milestone: CS2I
gate: "[fixpoint]"
reseed-class: "none | fixpoint-rebuild"
deps: []
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:0"   # decision item 5/6
  - "docs/design/somas-grandes-do-compilador-para-interface.md:5"
---

# 0211 · CS2I-ABI-MRET — método abstract/interface aceita retorno `(A,B)`

> O único item de maquinário da decisão (baixa urgência): ensina agora, usa depois.

## Goal

Hoje um método `abstract`/de-interface NÃO aceita assinatura de retorno-múltiplo
`(A,B)` — honest-stop "yet" em `parse_decl.tks:358`. Ensina a superfície AGORA
(afirmação 6 do dono; lei "ensino agora, uso depois"): remove o stop e liga o desugar-
para-struct de retorno-múltiplo ao caminho bodyless. Só morde onde um método de
interface devolver DOIS valores genuínos; "pode falhar" segue `T | error` (união, já
aceita). Aditivo/teaching.

## Where

- `src/parser/parse_decl.tks:358` — remover `if ret_types.len > 0 { return err_at(…
  "a bodyless … may not declare a multiple-return `( A, B )` signature yet") }`;
  preencher `ret_types` no ramo bodyless (`:359-360`) como o ramo com-corpo (`:366`).
- `src/checker/collect.tks` — o desugar `mret_struct_item`/`mret_return_struct_lit`
  (empacota `(A,B)` numa struct sintética mangled) — confirmar que alcança a
  assinatura de método bodyless (o método de interface passa a expor a struct
  sintética como retorno, e o override concreto casa).

## How

1. Remover o honest-stop; propagar `ret_types` no bodyless.
2. Garantir que o método concreto que dá override a um método-de-interface com `(A,B)`
   casa a mesma struct sintética (a assinatura mangled tem que bater entre abstract e
   impl). Verificar `method_sig_matches` (`collect.tks:721`) com `ret_types`.
3. NÃO tocar `T | error` (união, ortogonal — afirmação 4/5).

## Rulings & laws

- **Teko-only.** **Ensino agora, uso depois (CLAUDE.md):** ensina a superfície; o uso
  pesado é posterior mas in-plan.
- **NÃO DETECTAR O QUE NÃO EXISTE:** remover o honest-stop de um caso que passa a
  existir; sem tombstone.
- **W15; nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3`.**

## Fixtures

O self-build não tem hoje método de interface com `(A,B)` — accept isolado:

| fixture | asserts | expected |
|---|---|---|
| `iface_method_multi_return` | `abstract fn f(): (i64, i64)` numa interface, impl concreto, chamada com `var a,b = x.f()` destructura | 0 |

## Gate

`[fixpoint]` — aditivo; `gen2==gen3`. Reseed-class: `none` se byte-neutro (a superfície
não é exercitada pelo self-build), `fixpoint-rebuild` se o desugar mudar bytes emitidos.

## Deps

`—`

## Done when

Um método de interface pode declarar retorno `(A,B)`, o impl concreto casa, o fixture
`iface_method_multi_return` é exit `0`, build byte-consistente.
