---
seq: 0210
crumb-id: CS2I-METHODS
milestone: CS2I
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [CS2I-TYPE, CS2I-TEXPRKIND]
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:3.1"
  - "docs/design/somas-grandes-do-compilador-para-interface.md:3.2"
---

# 0210 · CS2I-METHODS — single-dispatch `match` → método de interface

> A convenção "dispatch de método" que o dono quer; DEFERÍVEL mas in-plan (estilo,
> não memória).

## Goal

Migrar as operações SINGLE-dispatch sobre `Type`/`TExprKind`/… de free-fn-com-match
para MÉTODO de interface (o corpo do arm vira o corpo do método na impl). Double-
dispatch permanece free-fn (§3.2). O ganho de memória já foi capturado pela virada de
forma; isto é a convenção de estilo, encenada DEPOIS dos marquees, por operação.

## Where

- `src/checker/type.tks:61` `type_is_void`, `:110` `type_contains_ref`, `type_render`,
  o eixo-de-um-tipo de `type_mangle` — single-dispatch → método.
- Interface `Type` (`type.tks:59`, pós-CS2I-TYPE) ganha os `abstract fn`; cada um dos
  14 membros impl o método.
- **Double-dispatch permanece free-fn:** `type_eq:72`, `unify` (`resolve.tks:1124`),
  `subst_type`, `type_mangle` (eixo-par) — §3.2.

## How

Por operação single-dispatch, um passo verde:
1. Adicionar à interface o método abstrato:
```teko
/**
 * is_void — este tipo é o `Void` sentinela? Método single-dispatch que substitui a
 * free-fn `type_is_void`; cada impl responde por si (o `Void` true, os demais false).
 *
 * @return true sse o tipo concreto é `Void`
 * @since 0.3.1
 */
pub abstract fn is_void(self): bool
```
2. Cada membro impl (corpo = o arm de hoje): `Void` → `{ true }`, os demais → `{ false }`.
3. Substituir os chamadores `type_is_void(t)` por `t.is_void()`; remover a free-fn.
4. Repetir por operação. `type_eq`→método é OPCIONAL (§3.2) — decisão do dono; se
   sim, `abstract fn eq(self, other: Type): bool` com match residual no 2º operando.

## Rulings & laws

- **Teko-only. Decisão do dono (afirmação 1 — "dispatch de método").**
- **Double-dispatch fica free-fn (§3.2) — recomendação law-first, custo mínimo.**
- **DEFERÍVEL in-plan:** estilo, não memória; encenar após P0/P1; mesmo plano.
- **RATCHET D68: Δpico não-crescente (dispatch indireto ~mesma ordem). W15; nunca
  `teko test .`; `ulimit -v 4718592`; `gen2==gen3`.**

## Fixtures

`none — the fixpoint self-build exercises this` (o compilador chama cada método).

## Gate

`[RITUAL]` por grupo de método. Reseed, `gen2==gen3`, Δpico não-crescente.
`fixpoint-rebuild`.

## Deps

`CS2I-TYPE`, `CS2I-TEXPRKIND`

## Done when

As operações single-dispatch são métodos de interface, os chamadores usam `x.m()`, as
free-fns removidas, double-dispatch intacto como free-fn, reseed `gen2==gen3`.
