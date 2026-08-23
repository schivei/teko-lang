---
seq: 0208
crumb-id: CS2I-SHARED
milestone: CS2I
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [CS2I-STMTS]
sources:
  - "docs/design/somas-grandes-do-compilador-para-interface.md:6"
  - "docs/design/somas-grandes-do-compilador-para-interface.md:7"
---

# 0208 · CS2I-SHARED — uniões com membro compartilhado / aninhado

> `@Decl()`·`@ItemKind()`·`@TItem()`·`@TypeBody()`·`@TypeExpr()`·`@Pattern()`·
> `@ConstraintExpr()`·`@PredKind()` — resolvidas por conformância múltipla.

## Goal

Converter as uniões restantes do parser/checker, incluindo as que têm membros
COMPARTILHADOS (`TypeDecl`/`Function`/`ConstDecl` em `Decl` E `ItemKind` E `TItem`) e
uniões ANINHADAS (`@Statement()` em `@ItemKind()`, `@TStatement()` em `@TItem()`). A
vantagem sobre o plano-variant: conformância de interface é muitos-para-um → sem
inline forçado. `@TypeBody()` toca serialização (§7). Byte-mover; reseed.

## Where

- `src/parser/ast.tks:254` `Decl` (5), `:258` `ItemKind` (7), `:228` `TypeBody` (11),
  `:163` `ConstraintExpr` (7), `:263` `PredKind` (6) → interfaces.
- `src/parser/type.tks:12` `TypeExpr` (6); `src/parser/pattern.tks:21` `Pattern` (9);
  `src/checker/tast.tks:97` `TItem` (5) → interfaces.
- Membros compartilhados → `class & IfaceA & IfaceB & IfaceC`.
- Aninhamento → `pub type ItemKind = interface : Statement { }` e
  `pub type TItem = interface : TStatement { }` (`extends` já existe,
  `parse_decl.tks:824`).
- Serializer `write_typebody`/`tkb_read` (`src/emit/tkb_write.tks:446`) — os tags são
  idênticos; SEM bump (§7).

## How

1. Declarar cada interface; membros de posse exclusiva → `class & Iface`.
2. Membro compartilhado (`TypeDecl` etc.) → conforma a TODAS as suas interfaces numa
   só decl (`class & Decl & ItemKind & TItem { … }`).
3. União aninhada → interface externa ESTENDE a interna; os matches casam ambos os
   eixos (o `implementors_of` é transitivo via `type_conforms_to`).
4. Verificar (§6): classe com ≥2 conformâncias gera ≥2 vtables; o box escolhe por
   tipo-esperado (`emit_as`). Confirmar serializer cobre os impls (CS2I-EXH), sem bump.
5. Converter mutação in-place do audit.

## Rulings & laws

- **Teko-only. Decisão do dono (afirmação 1). Valor→ref (§2.2).**
- **Conformância múltipla (§6):** resolve compartilhados sem inline (vantagem sobre variant).
- **Sem bump de wire (§7).**
- **RATCHET D68: Δpico queda estrita. W15; nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3`.**

## Fixtures

`none — the fixpoint self-build exercises this`.

## Gate

`[RITUAL]` por grupo — reseed, `gen2==gen3`, Δpico não-crescente. `fixpoint-rebuild`.

## Deps

`CS2I-STMTS`

## Done when

As 8 uniões são interfaces, membros compartilhados conformam a N interfaces,
aninhadas usam `extends`, serializer sem bump, reseed `gen2==gen3`, pico cai.
