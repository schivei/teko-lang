---
seq: 0202
crumb-id: CS2I-PILOT
milestone: CS2I
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [CS2I-EXH]
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:2.1"  # the mechanism
  - "docs/design/somas-grandes-do-compilador-para-interface.md:5"    # CS2I-PILOT row
---

# 0202 · CS2I-PILOT — `@TFSpecKind()` → `interface TFSpecKind`

> O menor alvo do compilador que exercita a máquina inteira (campo fat-pointer +
> match residual + reseed) — prova o padrão antes dos marquees.

## Goal

Converter a menor união-macro embutida do compilador para a forma nova: interface +
3 classes seladas conformantes + campo fat-pointer. Prova o mecanismo §2.1 ponta a
ponta com blast-radius mínimo (3 membros, 1 sítio-campo `TFSpec.kind`). Muda a
representação (union inline → fat-pointer) → **byte-mover** → dirige um reseed.

## Where

- `src/checker/tast.tks:33-36` — `TFSpecNone`/`TFSpecStatic`/`TFSpecDynamic`:
  `pub type TFSpecNone = struct { }` → `pub type TFSpecNone = class & TFSpecKind { }`
  (idem os outros dois, mantendo campos `s`/`args`); `macro TFSpecKind() { lowering
  { … } }` → `pub type TFSpecKind = interface { }`.
- `src/checker/tast.tks:37` — `TFSpec = struct { kind: @TFSpecKind() }` →
  `kind: TFSpecKind`.
- Todos os `match tfspec.kind { TFSpecNone => …; TFSpecStatic as s => …;
  TFSpecDynamic as d => … }` em `src/codegen/**`, `src/checker/**`, `src/emit/**` —
  corpo dos arms NÃO muda (§2.1 passo 4); confirmar que cada match cobre os 3 impls
  (CS2I-EXH garante a exaustividade).

## How

1. Declarar `pub type TFSpecKind = interface { }` (sem métodos — discriminação por
   vtable, §10-R6).
2. Cada membro vira `class & TFSpecKind` mantendo seus campos. Construção
   `TFSpecStatic { s = … }` é idêntica para classe.
3. Trocar o campo `TFSpec.kind` para `TFSpecKind`.
4. Confirmar que a exaustividade dos matches sobre `.kind` fecha via `implementors_of`
   (CS2I-EXH). Nenhum arm ganha `_` — a selagem cobre.
5. Se o audit CS2I-0 tiver listado mutação in-place de um `TFSpec`, converter para
   construir-fresco aqui.

## Rulings & laws

- **Teko-only:** só `.tks`; sem C twin.
- **Decisão do dono (afirmação 1):** somas grandes do compilador → interface.
  TFSpecKind é pequena mas é o piloto do padrão.
- **Valor→referência (§2.2):** classe→interface é o caminho que já funciona; sem spine.
- **RATCHET D68:** medir `teko: memory: peak` — deve NÃO-CRESCER (espera-se leve queda).
- **W15:** sem `//`; doc só onde `exp`.
- **Safety:** subshell `ulimit -v 4718592`; reseed só no `[RITUAL]`; `gen2==gen3`.

## Fixtures

`none — the fixpoint self-build exercises this` (o compilador usa `TFSpec` ao compilar
interpolação `$"…"`).

## Gate

`[RITUAL]` — build gen2, reseed, `gen2==gen3` byte-idêntico, medir Δpico
(não-crescente). Reseed-class: `fixpoint-rebuild`.

## Deps

`CS2I-EXH`

## Done when

`TFSpecKind` é interface, os 3 membros são classes conformantes, `TFSpec.kind` é
fat-pointer, o self-build reseeda com `gen2==gen3` e o pico não cresce.
