---
seq: 0203
crumb-id: CS2I-TYPE
milestone: CS2I
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [CS2I-EXH, CS2I-PILOT]
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:2.1"  # mechanism
  - "docs/design/somas-grandes-do-compilador-para-interface.md:3.2"  # double dispatch
  - "docs/design/somas-grandes-do-compilador-para-interface.md:7"    # no wire bump
---

# 0203 · CS2I-TYPE — `@Type()` → `interface Type` (MARQUEE)

> O maior corte de memória: `TExpr.type` (136 B inline) → fat-pointer de 16 B, em
> ~862 sítios.

## Goal

Converter a união-macro central do checker, `@Type()` (14 membros), para
`interface Type`. É o marquee: `Type` embute em `TExpr.type` e em ~862 sítios de
anotação. Byte-mover; dirige o maior reseed da onda. O maquinário de soma anônima
`A | B` fica intacto — o membro portador `Variant` permanece (como classe).

## Where

- `src/checker/type.tks:41-58` — os 14 membros `Prim`/`Slice`/`Named`/`Variant`/
  `Func`/`Byte`/`Char`/`Str`/`Error`/`Void`/`Ptr`/`Uptr`/`Reference`/`Null`:
  `pub type Prim = struct { … }` → `pub type Prim = class & Type { … }` (idem todos).
  `Variant = class & Type { members: []Type }` — PORTADOR de soma anônima, PERMANECE.
- `src/checker/type.tks:59` — `macro Type() { lowering { … } }` →
  `pub type Type = interface { }` (sem métodos; §10-R6).
- **~862 sítios `@Type()`** em 27 arquivos (`grep -rF '@Type()' src`) → `Type`
  (mesmo ns) / `checker::Type` (cross-ns). Inclui `@Type() | null` → `Type | null`
  (niche) e `[]@Type()` → `[]Type`.
- `src/checker/type.tks:51-53` — `Ptr.inner: @Type() | null` → `Type | null`.
- Dispatch-duplo — corpo INALTERADO (§3.2): `type_eq:72`, `types_eq:99`,
  `ptr_inner_eq:65`, `unify` (`resolve.tks:1124`), `subst_type` (`resolve.tks:1087`),
  `collect_sig_type_params` (`resolve.tks:1146`), `type_mangle` (`resolve.tks:1381`).
- Serializer — `write_typebody`/`tkb_read` casam os mesmos membros; SEM bump (§7).

## How

1. `macro Type()` → `pub type Type = interface { }`.
2. Os 14 membros → `class & Type`, campos idênticos. `Variant.members: []Type`.
3. Renomear `@Type()` → `Type`/`checker::Type` em todos os sítios (atômico — a decl é
   única; todo consumidor vê a mesma forma). Construção `Prim { kind = … }` idêntica;
   match `Prim as p` idem.
4. Os dispatch-duplo mantêm o match aninhado — agora sobre impls selados (vtable).
   Exaustividade fecha via CS2I-EXH (nenhum arm ganha `_`).
5. `Type | null` = niche fat-pointer (`.data == NULL`); `Type | error` = união de 2.
   A verificação de nulo não muda.
6. Converter qualquer mutação in-place de `Type` listada no audit CS2I-0.
7. Confirmar sem bump de wire (§7): os tags de `write_typebody` são idênticos.

## Rulings & laws

- **Teko-only:** só `.tks`.
- **Decisão do dono (afirmações 1-4):** somas grandes → interface; `| null`/`| error`
  ficam uniões; `A | B` (o `Variant`) fica.
- **Valor→referência (§2.2):** classe→interface é o caminho que funciona; soundness
  pelo invariante de imutabilidade (audit CS2I-0).
- **RATCHET D68:** o MAIOR corte esperado; medir `teko: memory: peak` — queda estrita.
- **Sem bump de wire (§7):** representação ≠ forma de wire; NÃO bumpar TKB/TKH.
- **W15:** sem `//`; doc só onde `exp`.
- **Safety:** subshell `ulimit -v 4718592`; reseed só no `[RITUAL]`; `gen2==gen3`.

## Fixtures

`none — the fixpoint self-build exercises this` (o compilador É o consumidor massivo
de `Type`; miscompile quebra o fixpoint).

## Gate

`[RITUAL]` — build gen2, reseed, `gen2==gen3` byte-idêntico, **medir Δpico** (o maior
corte; ratchet exige queda estrita). Reseed-class: `fixpoint-rebuild`.

## Deps

`CS2I-EXH`, `CS2I-PILOT`

## Done when

`Type` é interface, os 14 membros são classes conformantes (incl. `Variant`
portador), todo `@Type()` virou `Type`, os dispatch-duplo compilam com match residual
selado, o self-build reseeda `gen2==gen3` e o pico cai.
