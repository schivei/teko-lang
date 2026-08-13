# Plano — Onda Traits: trait-decorador achatável + aposentadoria das structural traits (§9.4)

> **Status:** DESIGN. Read+design apenas — NENHUM `.tks` de produto editado, NENHUM build, NENHUM
> reseed, NENHUM `teko test .` (fuga de memória do `monomorph` — CRASHA O CONTAINER; nunca correr,
> nem guardado). Este documento É o artefacto; o único commit desta crumb é ele próprio.
> **Branch:** `fix/retirement` (drena sequencial, SEM PRs).
> **Fonte de lei:** `docs/design/mudancas-superficie-0.3.1.md` §9.4 (SELADO — desenha-se à volta, não
> se re-abre).
> **Irmãos modelo:** `docs/design/plano-secao9-operadores-e-value-type-methods.md` (mesma estrutura; é
> a DEPENDÊNCIA da camada interface-operador §5 abaixo), `docs/design/plano-secao9A-method-overload.md`
> (a colisão-salvo-sobrecarga §3.4 desenha contra a sua forma declarada), `plano-secao9-properties.md`
> (`get`/`set` no corpo do decorador).
> **Lei permanente:** Teko-only (.tks), W15 + Javadoc-completo em TODA declaração, law-first, reseed
> disciplinado (`cc -std=c2x`, `--no-verify`).
> **Dependência declarada:** a camada interface-operador + contrapartida (§5, deliverable §5) assenta
> sobre **operadores + métodos-em-value-type** (`plano-secao9…`, a aterrar AGORA). Tudo o resto — a
> redefinição do decorador e a REMOÇÃO das structural traits — é INDEPENDENTE e adianta-se já. O que
> fica bloqueado está marcado **[BLOQUEADO em §9-operadores]**.

---

## 0. Rulings SELADOS (LEI — desenha-se à volta, não se re-abre)

### 0.1 O que uma `trait` É (decorador achatável)
1. **Só métodos-com-corpo:** properties `get`/`set` (que são métodos), factories `static fn`, métodos
   de instância. **ZERO campos. ZERO membro sem-corpo (bodyless).**
2. **NÃO é tipo:** sem `var`/parâmetro/retorno/campo tipado por trait; sem dispatch; sem trait-object.
   Reside APENAS (a) na definição da própria trait e (b) no `&`-compose da definição de um tipo.
   **NUNCA** em constraint.
3. **Contrato estrutural sobre o host:** os backing fields/métodos que os corpos referenciam têm de
   existir no host composto → **erro-de-composição NOMEADO** se faltar. Convenção `_nome` ↔ property
   `nome` (o getter lê o backing `self._nome`, NUNCA a property — senão recursão).
4. **`self` = o implementador** (o tipo que compõe), como valor (`self._x`) e como tipo
   (`static fn new(): self`).
5. **`&` compõe; achata em `class` (selada/virtual/abstrata), `struct` e `service`.**
6. **Colisão** entre traits compostas (mesmo nome de método) = **erro de compilação, SALVO sobrecarga
   válida** (aridade+tipo distintos, §9A). Sem override silencioso.
7. **Sem `match` sobre trait:** trait não tem discriminante; nome de trait em *subject* OU *case* de
   match = **erro NOMEADO**. Traits compostas idem.
8. **Constraint = interface-only, SEM exceção.**

### 0.2 A aposentadoria das *structural traits*
9. `Eq`/`Ord`/`Hash`/`Clone`/`Default` (+ sinónimos `Hashable`/`Comparable`) — **APOSENTADAS.** Eram
   *compiler-shadow* (nomes hardcoded cujos corpos o compilador sintetizava campo-a-campo). **Uso real
   em `/src/**/*.tks` = ZERO** (confirmado §1.5). **REMOVER:** a síntese (`synth.tks` inteiro), os
   reconhecedores (`is_structural_trait`/`structural_trait_canonical`, `resolve.tks`), o split de
   derive (`collect.tks`), a satisfação de constraint (`monomorph.tks`). Deleção **behavior-preserving**
   (zero uso vivo).

### 0.3 A capacidade renasce como INTERFACE-com-operador (§5 — depende de §9)
10. `Eq`/`Ord`/… voltam como **interfaces cujo contrato é um operador** (`operator __eq`/`__lt`/…, §9),
    cumpridas ESCREVENDO o operador (visível). O genérico constrange na interface e **despacha o
    operador através de T** (por vtable) — destrava `Map<K: IEq & IHash, V>`.
11. A interface **OBRIGA a contrapartida:** igualdade por negação (`__eq` ⇒ `__ne` = `!(==)`), ordem por
    reflexão (`__lt` ⇒ `__gt` = operandos trocados; `__le` ⇒ `__ge`). O contrato lista AS DUAS
    assinaturas; um **trait-decorador opcional** entrega o corpo óbvio da contrapartida (zero shadow).

### 0.4 O quadro final — UM construto de capacidade sobra ao lado da interface
| construto | é tipo? | pode constraint? | corpo |
|---|---|---|---|
| **interface** | sim (dispatch) | **sim** | assinaturas (o dev implementa) |
| **trait-decorador** | **não** (achata) | **não** | métodos-com-corpo (o dev escreve) |

---

## 1. Estado de HOJE — o que uma `trait`/`TraitBody` é, e a maquinaria a remover (file:line)

### 1.1 A `trait` de hoje é MAIS do que o decorador selado permite
- **AST** (`src/parser/ast.tks:704`): `pub type TraitBody = struct { fields: []Field; methods:
  []Function; consts: []ConstDecl }`. Carrega **`fields`** (proibido no §9.4) e as `methods` PODEM ser
  **bodyless** (proibido). `TypeBody |= TraitBody` (`ast.tks:705`).
- **Parser** (`src/parser/parse_decl.tks`): `trait` é CONTEXTUAL — só declara quando `Ident("trait")`
  é imediatamente seguido de `{` (`parse_decl.tks:1049`). `parse_trait_fields` (`:988`) é
  STRUCT-SHAPED: aceita campos `name: T` interleaved E métodos, e chama
  `parse_member_fn_or_accessor(tokens, p, /*allow_bodyless=*/true)` (`:998`) — logo aceita
  **requisitos bodyless**. Aceita `const` membros (`:1010`).
- **Trait como TIPO/DISPATCH (TR1) — tudo a REMOVER (§0.1 ruling 2):**
  - `atom_surface` (`resolve.tks:861`): `is_trait_name` → `trait_methods_by_name` (trait como
    superfície de CONSTRAINT).
  - `constraint_surface`/atom collection (`resolve.tks:1024`): um trait atom contribui o próprio nome
    como constraint.
  - Upcast de valor (`resolve.tks:1366`, `:1482`): um struct/class conforma a um trait `to` e faz
    up-cast a ELE como valor (trait-object).
  - Slice de contrato (`typer.tks:4847,4857`): `[]Trait` — trait como elemento de slice tipado.
  - Arg upcast (`typer.tks:6080,6086`): passar um class onde o parâmetro é um trait.
  - `typer.tks:2017`: "a trait value IS a contract value — W10b.D3 reuse" (dispatch dinâmico).
- **`is_trait_name`** (`resolve.tks:1520`): o predicado de kind. FICA (o fold ainda o precisa), mas os
  call-sites de TIPO/CONSTRAINT/DISPATCH acima passam a ERRO nomeado (§3.5, §3.6).
- **`iface_methods_by_name`** (`collect.tks:1371`): roteia `TraitBody => trait_methods_by_name`
  (`:1375`) — a superfície kind-agnóstica que serve constraint/vtable. Depois do §0.1-2, o trait
  DEIXA de ser contrato de dispatch, logo esse desvio some (trait_methods_by_name passa a ser
  consumido SÓ pelo fold e pela checagem de contrato).

### 1.2 O FOLD de user-traits — o que fica (tightened) e o que morre
- **Fica, apertado:** `find_trait_body` (`collect.tks:1651`), `trait_methods_by_name` (`:1671`),
  `fold_trait_members` (`:1749`), `split_trait_derives` (`:1691`, sem o bucket `structural`),
  `fold_user_traits`/`fold_one_item`/`fold_struct_item`/`fold_class_item` (`:2002`, `:2040`, `:2060`,
  `:2082`).
- **Morre (bodyless-requirement — ZERO membro sem-corpo, §0.1-1):** `TraitDerive` (`:1679`),
  `record_user_trait_derives` (`:2117`), `check_trait_requirements` (`:1811`),
  `one_derive_requirement_diags` (`:1833`), `one_requirement_diag` (`:1870`). Todo o eixo de
  "requisito satisfeito pelo host" desaparece — um trait sem bodyless não tem requisito.
- **`fold_trait_members` (`:1749`) — o fold de CAMPOS morre:** o loop de `tb.fields` (`:1764-1776`) e a
  colisão de campo (`:1769`) removem-se (trait tem zero campos). Fica o fold de MÉTODOS (`:1789-1802`)
  e de `consts` (decisão D-consts, §3.7). A colisão de método (`:1793-1794`) passa a
  **colisão-salvo-sobrecarga** (§3.4).

### 1.3 A síntese structural (TR3) — REMOÇÃO INTEGRAL
- **`src/checker/synth.tks` — FICHEIRO INTEIRO.** É 100% maquinaria TR3: `synthesize_structural_methods`
  (pub, `:632`) + todos os construtores de AST sintética (`mk_expr`/`mk_var`/`mk_field`/`mk_binary`/
  `mk_compare`/`mk_method_call`/`mk_path_call`/`mk_int`/…/`mk_method`, `:10-228`) + os sintetizadores
  por-trait (`synthesize_eq`/`synthesize_hash`/`synthesize_compare`/`synthesize_clone`/
  `synthesize_default`, `:342-579`) + os helpers (`nested_derives`/`field_supported`/`eq_field_term`/
  `structural_method_name`/…). **Verificado:** os `mk_*` NÃO têm consumidor fora de `synth.tks` (grep
  §1.5) → **deletar o ficheiro inteiro** é limpo. Remover `synth.tks` do manifesto de build/módulos
  (`src/checker/` + qualquer `use teko::checker::synth`).
- **`resolve.tks`:** `is_structural_trait` (`:1539`), `structural_trait_canonical` (`:1553`), e o ramo
  `atom_surface` que devolve superfície vazia para um nome structural (`:863`) — REMOVER.
- **`monomorph.tks`:** o ramo structural em `constraint_atom_satisfied` (`:56-59`) — REMOVER (fica só o
  caminho interface/variant/nominal).
- **`collect.tks` (PASS 2 structural + TR2 field view):** `SplitImpls.structural` (`:1688`) e o bucket
  no `split_trait_derives` (`:1698-1702`); `FieldView`/`deriver_field_view` (`:1627`) + os helpers de
  field-view (TR2, consumidos SÓ pela síntese); `any_structural` (`:1922`) + o ramo structural em
  `program_needs_trait_fold` (`:1905-1906`); `synthesize_structural`/`synthesize_one_decl` (`:2196`,
  `:2160`) + `struct_with_methods`/`class_with_methods`/`method_name_set`/`append_methods`
  (`:2133`-`:1955`); a chamada PASS 2 em `fold_traits_collected` (`:2307-2308`). O skip structural em
  `check_one_interface`-path (`:1488`, `!is_structural_trait(iname)`) — o predicado some, logo o skip
  simplifica (fica só `!is_trait_name`).

### 1.4 A checagem de conformidade e o match
- **Conformidade de interface** (`collect.tks:1488`): hoje pula nomes que são trait OU structural. Após
  a onda, trait NUNCA está em `implements` como contrato-de-dispatch (foi achatado); o skip
  `!is_trait_name` fica como guarda defensiva.
- **Match** (`src/checker/match.tks`): `is_direct_case_of` (`:66`) e o caminho de subject/case de
  interface (`:48-83`). Um nome de trait pode HOJE aparecer como subject (via TR1 trait-object) ou
  case; o §0.1-7 exige um **erro NOMEADO** em ambas as posições (§3.6).

### 1.5 Prova de "zero uso vivo" (o que torna a remoção behavior-preserving)
- `grep -rE "&\s*(Eq|Ord|Hash|Clone|Default|Hashable|Comparable)\b" src/ --include=*.tks` → **só
  doc-comments** (`monomorph.tks:47`, `regr_group.tks:56`, `map.tks:9,13`). Nenhum `derive & Eq…` VIVO,
  nenhuma chamada `.eq()`/`.compare()`/`.hash()`/`.clone()`/`::default()` de origem structural. O
  `Map` já desistiu e é `str`-keyed (`map.tks` — o que a interface-operador §5 vem destravar).
- `mk_*` de `synth.tks`: sem consumidor externo → deleção de ficheiro limpa.
- **Cobertura de teste a varrer** (`src/checker/checker_test.tkt`): `structural_derive_over_scalars_…`
  (`:1038`), `structural_derive_over_nonderiving_nested_is_error` (`:1050`),
  `structural_derive_on_an_enum_is_error` (`:1064`), `structural_override_wins_over_synthesis`
  (`:1076`), `structural_constraint_atom_resolves_via_synonym` (`:1088`), e
  `field_view_struct_includes_trait_folded_fields` (`:1180`) + os helpers `trait_item`/`tr_fields1`/
  `fv_*`. Estes testam maquinaria removida → **deletar** (crumb 2), substituídos pelos fixtures §8.

---

## 2. Gramática / AST — os deltas do decorador (aditivo-restritivo, seed-safe)

**Decisão de forma (law-first):** o `TraitBody` PERDE `fields`; as `methods` deixam de aceitar
bodyless. Ambos são um APERTO de gramática — mas o corpus atual não tem traits com campos nem bodyless
VIVOS (§1.5), logo o seed constrói na mesma. (Janela aditiva: durante o sweep, a gramática velha
[fields/bodyless] ainda parseia para o seed velho reler o `src/`; o reseed captura o seed novo; só
então a rejeição entra. Aqui não há `src/` a migrar — nenhum trait de produto usa campos/bodyless —
então o aperto entra direto na crumb 3, sem janela.)

### 2.1 AST (`src/parser/ast.tks:704`)
```teko
/**
 * TraitBody — a `trait { <bodied members> }` DECORATOR body (§9.4): a flattenable bundle of
 * BODIED members ONLY, composed into a struct/class/service through the `&`-list. A trait is NOT a
 * type — it never reaches a value, param, return, field, constraint or dispatch position; it resides
 * only in its own declaration and in a composer's `&`-list. It carries ZERO fields (the value data
 * lives on the host) and ZERO bodyless members (every method has a body): instance methods, `get`/
 * `set` property accessors, and `static fn` factories. Member consts flatten as before (D-consts,
 * §3.7). The composition CONTRACT (backing fields the bodies read, e.g. `self._x`) is validated
 * against the HOST at compose time (`check_trait_composition_contract`), not declared here.
 *
 * @field methods  the interleaved bodied instance/accessor/static methods, in source order
 * @field consts   the static member consts (D-consts)
 * @since 0.3.1 (§9.4)
 */
pub type TraitBody = struct { methods: []Function; consts: []ConstDecl }
```
- `TypeBody` (`ast.tks:705`) mantém `| TraitBody` (a variant não muda de membros, só o `TraitBody`
  encolhe). O codec `.tkb` (C7.16) do `TraitBody` perde o campo `fields` — round-trip em paridade
  (`src/emit/tkb_*.tks`), teste de round-trip (§6, R-tkb).
- Todos os construtores de `TraitBody` (parser `:1051`, reconstrução `.tkb`, testes) deixam de passar
  `fields`.

### 2.2 Parser (`src/parser/parse_decl.tks`)
- `parse_trait_fields` (`:988`) → renomear conceptualmente para `parse_trait_members` e:
  - **REJEITAR campos:** um `name: T` em posição de membro de trait = erro NOMEADO
    `"a trait declares no fields — the value data lives on the composing host (§9.4); move `<name>` to
    the host"`. (Hoje `:1009-1011` aceita um nome de campo.)
  - **REJEITAR bodyless:** chamar `parse_member_fn_or_accessor(tokens, p, /*allow_bodyless=*/false)`
    (era `true`, `:998`) — um método de trait TEM corpo. Diagnóstico
    `"a trait method must have a body — a trait is a decorator, not a contract of signatures (§9.4)"`.
  - **ACEITAR** `static fn` e `get`/`set` accessors (já roteados por `parse_member_fn_or_accessor`;
    confirmar que `is_static`/accessor passam com `allow_bodyless=false`). Consts: manter (D-consts).
- A recognição contextual de `trait` (`:1049`) NÃO muda (continua `Ident("trait")` + `{`). Um trait
  NÃO declara `&`-list própria (já é lei — `:1048`).

### 2.3 Sem tokens novos
`trait` continua contextual (não reservado); `self` continua não-reservado e resolve ao dono em posição
de tipo (o MESMO ponto que os value-type-methods §9 usam — reutiliza, não duplica).

---

## 3. Checker — a semântica do decorador (flatten-compose + contrato + guardas)

### 3.1 O flatten-compose — reutiliza o fold, apertado
O fold já ACHATA métodos de trait no deriver (`fold_trait_members`, `collect.tks:1749`) para struct e
class. Deltas:
- **Campos:** remover o loop `tb.fields` (`:1764-1776`) — não há campos a achatar.
- **Service:** um `service` usa `ClassBody` (`is_service`/`service_lifetime`), logo já flui por
  `fold_class_item` (`collect.tks:2082`). **Verificar** que um `service` no `&`-compose chega ao fold
  (o `split_trait_derives` corre sobre `cb.implements` de um service igual a um class) — se sim, §0.1-5
  (achata em service) sai de graça; senão, estender `fold_one_item` (`:2040`) para o body de service.
- **self=implementer:** o corpo achatado é re-tipado contra o deriver (`type_struct_methods`/
  `type_method` re-carimba contra o nome corrente — `collect.tks:987`), logo `self._x`/`self` resolvem
  ao host automaticamente. É o MESMO mecanismo `self`-como-tipo dos value-type-methods (§9). Sem
  trabalho novo — CONFIRMAR por fixture (§8 A1).

### 3.2 O contrato estrutural sobre o host — erro-de-composição NOMEADO (§0.1-3)
Novo check, corrido no compose (dentro/junto de `fold_trait_members`, sobre a tabela pré-fold):
```teko
/**
 * check_trait_composition_contract — validate that a HOST provides every BACKING member a composed
 * trait's bodies reference (§9.4 structural contract). A trait property getter reads the backing
 * `self._name` (never the property `name` — that recurses); the backing MUST exist on the host. For
 * each `self.<member>` FieldAccess in the trait's flattened method bodies whose `<member>` is not a
 * host field (nor a member the host or an earlier-composed trait provides), emit a NAMED
 * composition-error naming the trait, the host, and the missing backing — never a bare "unknown
 * field". The `_name` ↔ property `name` convention is documentation, not enforced spelling: the
 * check is purely "does the referenced backing exist on the host".
 *
 * @param host_name    the composing type's name (struct/class/service)
 * @param host_fields  the host's own fields (backings the trait bodies may read)
 * @param trait_name   the composed trait's name (for the diagnostic)
 * @param trait_body   the trait's method bodies to scan for backing references
 * @param table        the pre-fold type table
 * @return             null when every referenced backing exists, else the first named error
 * @since 0.3.1 (§9.4)
 */
fn check_trait_composition_contract(host_name: str, host_fields: []parser::Field, trait_name: str, trait_body: parser::TraitBody, table: TypeTable): null | error
```
**Nota de desenho:** o re-type genérico do corpo achatado JÁ falharia num backing ausente com um
"unknown field". Este check corre ANTES e produz o diagnóstico NOMEADO exigido pelo ruling; recomendo
um scan sintático de `self.<ident>` (FieldAccess cujo receiver é `self`) nos corpos da trait, cruzado
com os campos do host. Fixar a exata forma da mensagem em Javadoc.

### 3.3 Properties `get`/`set`, static fn — no corpo do decorador
Depende de `plano-secao9-properties.md` (get/set) e do suporte a `static fn` em membro de tipo (§4/§9).
Ambos já ATERRAM antes/junto desta onda. O fold achata um accessor/static como qualquer método bodied;
CONFIRMAR que `fold_trait_members` preserva `is_getter`/`is_setter`/`accessor_prop`/`is_static` do
`Function` ao empurrar para o host (é uma cópia do node — preserva). Fixture §8 A2/A3.

### 3.4 Colisão = erro SALVO sobrecarga válida (§0.1-6) — **[depende de §9A]**
Hoje duas traits com o mesmo nome bodied = erro incondicional (`collect.tks:1793-1794`). Novo:
```teko
/**
 * trait_method_collision — when two composed traits (or a trait and the host) provide a method of the
 * SAME name, is it a real collision, or a VALID overload set (§9.4 collision-unless-overload, §9A)?
 * A collision is REPORTED (named error: the host must resolve it) UNLESS the two signatures form a
 * valid overload — distinct parameter arity/types under §9A's `signatures_distinct` — in which case
 * BOTH flatten in as an overload set. Consumes §9A's declared distinctness predicate; until §9A lands
 * this returns `Collision` for every same-name pair (the current behavior, byte-identical).
 *
 * @param existing  the method already flattened (host's own, or an earlier trait's)
 * @param incoming  the trait method being flattened
 * @param table     the folded type table
 * @return          `Overload` (keep both), or `Collision` (named error)
 * @since 0.3.1 (§9.4)
 */
fn trait_method_collision(existing: parser::Function, incoming: parser::Function, table: TypeTable): TraitMemberDisposition
```
`TraitMemberDisposition = variant Overload | Collision` (novo). A inserção substitui o `else if
methods_have_name(methods, tm.name) { return error … }` (`collect.tks:1793`) por: se
`trait_method_collision(...) == Overload`, empurrar ambos; senão o erro nomeado. **[BLOQUEADO em §9A]**
para a metade "sobrecarga válida"; a metade "colisão = erro" adianta-se já (comportamento atual).
O micro-fork "struct redefinir método de trait" (§9.4: deferido ao arquiteto) — RESOLUÇÃO law-first:
o método próprio do host VENCE (é o override explícito), como hoje `methods_have_name(own_methods, …)`
já faz (`collect.tks:1790-1792`); NÃO é colisão. Documentar essa precedência.

### 3.5 Constraint = interface-only (§0.1-8) — trait em constraint = ERRO nomeado
- `atom_surface` (`resolve.tks:861`): o ramo `if is_trait_name(name, table) { return
  trait_methods_by_name(...) }` passa a **erro nomeado**: `"a trait is not a type and cannot appear in
  a constraint — use an interface (§9.4)"`.
- Constraint atom collection (`resolve.tks:1024`): o ramo trait → mesmo erro nomeado.
- `monomorph.tks::constraint_atom_satisfied`: sem o ramo structural (removido §1.3) e sem trait
  (agora inalcançável — o atom nunca resolve a um trait porque `atom_surface`/o parse de constraint já
  o rejeitou). Uma guarda defensiva (`is_trait_name` → false/erro interno) é opcional.

### 3.6 Sem match sobre trait (§0.1-7) — guarda NOMEADA em subject E case
Em `src/checker/match.tks`, no ponto onde o subject e cada case resolvem a um `Type`:
```teko
/**
 * reject_trait_in_match — a trait has no discriminant (§9.4): a trait name in a match SUBJECT or in a
 * left-of-arm CASE is a NAMED error, never a silent no-match. Called on the resolved subject type and
 * on each arm's resolved discriminant; a composed trait (flattened away) never reaches here as a Named
 * trait, so this fires only on a direct `trait` name a user wrote in a match position.
 *
 * @param t      the resolved subject or case type
 * @param table  the folded type table
 * @param where  "subject" or "case" — placed into the diagnostic
 * @return       null when `t` is not a trait, else the named error
 * @since 0.3.1 (§9.4)
 */
fn reject_trait_in_match(t: Type, table: TypeTable, where: str): null | error
```
Ligar: no typing do subject (antes de `expand_variant`) e em `is_direct_case_of` (`match.tks:66`) /
o resolve de cada case — se `is_trait_name(n.name, table)`, erro nomeado
`"cannot match on trait `<name>` — a trait has no discriminant (§9.4)"`.

### 3.7 D-consts (decisão ratificável) — member consts num trait
§9.4 lista "só métodos-com-corpo (properties get/set, static fn, métodos de instância)"; NÃO menciona
`const`. Um member const É bodied (tem valor) e é estado type-level (não campo de instância), logo
NÃO viola "zero campos, zero bodyless". **Recomendação law-first:** MANTER member consts no trait
(mínima churn — a maquinaria #594 já os achata em `fold_trait_members:1776-1783`, byte-preservante).
Alternativa (letra estrita "só métodos"): remover consts do corpo do trait. **Recomendo MANTER; marco
como ratificável — não bloqueia nenhuma crumb** (se o dono preferir remover, é deletar o loop de consts
+ o campo `TraitBody.consts`, uma troca localizada).

---

## 4. A REMOÇÃO da maquinaria structural (ficheiros + símbolos — behavior-preserving)

Ordem de deleção (cada passo isolado, o build fica verde entre eles porque o uso é zero):
1. **`monomorph.tks`** — o ramo structural em `constraint_atom_satisfied` (`:56-59`). Sem consumidor
   após — nenhum constraint resolve a um nome structural.
2. **`collect.tks`** — PASS 2 e TR2 field-view (a lista completa de §1.3): remover a chamada
   `synthesize_structural` de `fold_traits_collected` (`:2307-2308`), depois as funções PASS 2, depois
   `FieldView`/`deriver_field_view`/os helpers de field-view, `any_structural`, o bucket
   `SplitImpls.structural` e o ramo no `split_trait_derives`, o ramo structural em
   `program_needs_trait_fold`. Ajustar `fold_traits_collected` para: `collect_types` → (se precisa
   fold) `fold_user_traits` → devolver (sem PASS 2). O `check_trait_requirements` também morre (§1.2).
3. **`resolve.tks`** — `is_structural_trait` (`:1539`), `structural_trait_canonical` (`:1553`), o ramo
   `atom_surface` structural (`:863`). Ajustar `check_one_interface`-path skip (`collect.tks:1488`)
   para `!is_trait_name` só.
4. **`synth.tks`** — deletar o FICHEIRO INTEIRO + removê-lo do build/módulos + qualquer `use`.
5. **Testes** (`checker_test.tkt`) — deletar os `structural_*` e `field_view_*` (§1.5) + helpers órfãos.

**Prova de behavior-preserving:** nenhum `derive & Eq…` vivo, nenhuma chamada de método structural,
`mk_*` sem consumidor externo (§1.5). O self-build produz árvores idênticas exceto onde a maquinaria
morta corria (que nunca corria). Fixpoint fecha (§6).

---

## 5. A camada INTERFACE-OPERADOR + contrapartida (§0.3) — **[BLOQUEADO em §9-operadores]**

Assenta sobre `operator __eq`/`__lt`/… (o dispatch de dunder de `plano-secao9-operadores-e-value-type-
methods.md`, a aterrar). Adianta-se todo o desenho que NÃO precisa da API de operador fechar.

### 5.1 As interfaces de capacidade (corpus/stdlib — escritas em `.tks`, não maquinaria de compilador)
São declarações Teko normais numa lib de capacidade (ex. `src/traits/…` ou `src/core/…`), NÃO nomes
hardcoded (o oposto da structural). O contrato lista AS DUAS assinaturas (contrapartida obrigatória):
```teko
/**
 * IEq — the equality capability: a type conforms by WRITING the `__eq` operator (§9.4). The
 * counter-part `__ne` is MANDATORY (equality by negation, ruling 11); a conformer either writes it or
 * composes the `NeByEq` decorator for the obvious body. A generic `<T: IEq>` dispatches `==`/`!=`
 * through T's vtable — the real dispatch the retired structural `Eq` could never do.
 *
 * @since 0.3.1 (§9.4)
 */
type IEq = interface {
    operator __eq(left: self, right: self): bool
    operator __ne(left: self, right: self): bool
}

/**
 * IOrd — the ordering capability (§9.4). Each comparator obliges its REFLECTION: `__lt`⟂`__gt`
 * (swapped operands), `__le`⟂`__ge`. `<T: IOrd>` dispatches ordering through T.
 *
 * @since 0.3.1 (§9.4)
 */
type IOrd = interface {
    operator __lt(left: self, right: self): bool
    operator __gt(left: self, right: self): bool
    operator __le(left: self, right: self): bool
    operator __ge(left: self, right: self): bool
}
```

### 5.2 Os trait-decoradores opcionais de contrapartida (o corpo óbvio, zero shadow)
```teko
/**
 * NeByEq — the optional counter-part decorator for `IEq` (§9.4): flattens the obvious `__ne` body
 * `!(left == right)` into a composer that already writes `__eq`, so the type satisfies `IEq`'s
 * mandatory counter-part without hand-writing it. A type wanting a non-trivial `__ne` (e.g. NaN)
 * writes its own instead of composing this — no compiler magic.
 *
 * @since 0.3.1 (§9.4)
 */
type NeByEq = trait {
    operator __ne(left: self, right: self): bool { !(left == right) }
}

/**
 * GtByLt — flattens `__gt(l, r) = r < l` (ordering by reflection) into an `IOrd` composer that writes
 * `__lt`. Sibling decorators `LeByLt`/`GeByLt` supply `__le`/`__ge` by the same reflection.
 *
 * @since 0.3.1 (§9.4)
 */
type GtByLt = trait {
    operator __gt(left: self, right: self): bool { right < left }
}
```
**Nota:** estes decoradores só compilam quando `operator` (membro) + `self`-como-tipo-de-operando
existem — logo **[BLOQUEADO em §9-operadores]**. O ESQUELETO (o ficheiro `.tks` com os doc-comments +
as declarações) escreve-se HOJE como honest-stop se `operator` ainda não parseia; caso contrário, entra
como corpus real assim que §9 fecha. O implementer confirma na crumb 7.

### 5.3 Conformidade da contrapartida — a interface OBRIGA as duas assinaturas
A obrigação da contrapartida é DUPLA e vive em dois pontos JÁ existentes/planeados:
- **Na interface:** `IEq`/`IOrd` listam ambas as assinaturas; a conformidade de interface existente
  (`check_one_interface`, `collect.tks`) já exige que o conformer forneça TODAS — logo um tipo com
  `__eq` e sem `__ne` (nem via `NeByEq`) FALHA a conformidade a `IEq`. Sai de graça da estrutura de
  interface — **não precisa de código novo** além de escrever as interfaces §5.1.
- **No operador (§9, `check_operator_invariants`):** o `plano-secao9…` já manda a contrapartida ao nível
  do OPERADOR (define `__eq` num value-type ⇒ exige `__ne`). São **complementares**: a interface
  obriga quem CONFORMA; o `check_operator_invariants` obriga quem DEFINE. Nenhuma duplicação — a
  camada §5 só ACRESCENTA as interfaces `.tks`; o enforcement do operador é do §9.

### 5.4 O que a §5 destrava (a prova de valor)
`Map<K: IEq & IHash, V>` genérico: o constraint interface despacha `==`/`hash` através de K por vtable —
o que a structural (opaca-em-T, `resolve.tks:863` devolvia superfície vazia) nunca conseguiu. Fixture
§8 A6 exercita um `index_of<T: IEq>` real.

---

## 6. Segurança de FIXPOINT — o argumento byte-idêntico

**Remoção (§4) é behavior-preserving:**
- Structural: zero derive vivo, zero chamada de método structural, `mk_*` sem consumidor externo
  (§1.5). O código removido NUNCA corria no self-build → `bin-a` idêntico ao pré-remoção nas rotas
  vivas.
- TR1-dispatch (trait como tipo/constraint): o corpus do compilador não usa trait como tipo/constraint
  VIVO? **VERIFICAR** (crumb 1): grep por `: <TraitName>` em param/retorno/campo e por trait em
  `<T: …>`. Se algum uso vivo existir, é âmbito desta onda migrá-lo para interface OU reportar para
  cima (adjacente — não abrir issue). O desenho ASSUME zero (o §9.4 diz "zero uso vivo confirmado" só
  para a structural; a parte TR1-como-tipo precisa da mesma prova antes de cortar).

**Aperto de gramática (§2):** `TraitBody` perde `fields`, methods perdem bodyless. Se algum trait de
PRODUTO usa campos ou bodyless VIVO, o parser novo rejeita-o. **VERIFICAR** (crumb 1): grep por
`trait {` no corpus e inspecionar cada corpo. O §9.4 modela a trait velha como "contrato de
assinaturas" (que TINHA bodyless) — pode haver traits de produto com bodyless a migrar. Se houver:
migrar o bodyless para a interface correspondente (o requisito vira contrato de interface) na janela
aditiva. **Este é o maior risco de fixpoint — resolvido por inventário na crumb 1.**

**Codec `.tkb` (R-tkb):** o `TraitBody` encolhido round-trip em paridade; teste de round-trip antes de
qualquer semântica depender (crumb 2).

**Reseed:** o fixpoint (`bin-a == bin-b`) fecha quando (a) o inventário confirma zero uso vivo de
trait-como-tipo/campos/bodyless (ou o migra), e (b) a remoção é puramente de código-morto. §5 é corpus
de TESTE/stdlib (não maquinaria de compilador) → não afeta o auto-fixpoint do compilador.

---

## 7. Sweep `.tkt`/`.tkr`

- **`.tkt`** (`src/checker/checker_test.tkt`): deletar `structural_*`/`field_view_*` + helpers órfãos
  (`trait_item` com fields, `tr_fields1`, `fv_*`) — §1.5. Atualizar quaisquer testes de trait que
  construam um `TraitBody` com `fields` (o construtor perde o campo). Deletar/atualizar testes que
  exercitem trait-como-constraint ou trait-como-valor (TR1).
- **`.tkr`** (`examples/regressions/`): não há hoje um projeto de regressão de traits dedicado (o
  inventário da crumb 1 confirma). Os fixtures novos §8 entram como projetos novos. Se algum `.tkr`
  existente usa `& Eq…` ou trait-como-tipo, migrar/remover.
- **`bootstrap/teko.c`** (gémeo C CONGELADO): contém `is_structural_trait`/`synthesize_structural`
  (grep §fonte). NÃO se edita o gémeo congelado; a divergência é aceitável até o reseed capturar o seed
  novo a partir do `.tks` (o seed velho ainda tem a maquinaria, mas o `.tks` já não a chama → inerte no
  seed). Documentar no PROVENANCE do reseed.

---

## 8. Fixtures de regressão (inputs → códigos de saída nativos)

Layout (do §9B): projeto `examples/regressions/<nome>/` com `.tkp`, `.tkr`, `main.tks`; a REJEITAR
dobra em `examples/regressions/diagnostics/` com `src/<caso>/case.tks` + `Then diagnostic = "…"`. A
aritmética do `exit`/token codifica QUAL ramo correu (a axis-law: testa o token/exit, nunca um efeito
incidental).

### 8.1 ACEITAR — `examples/regressions/trait_decorator/` (independente de §9)
- **A1 — flatten de método de instância + self=implementer.** `trait Stamp { fn tag(): i64 { self._id
  * 10 } }` composto em `type Post = struct Stamp { _id: i64 }`. `Post { _id = 4 }.tag()` → `40`.
  Prova achatamento + `self._id` a resolver ao host. `exit 40`.
- **A2 — property get/set de trait sobre backing do host.** `trait Named { get name(): str
  { self._name } set name(v: str) { self._name = v } }` composto num struct com `_name: str`. Ler/
  escrever a property → token esperado. Prova get/set achatados sobre backing.  **[depende de
  §9-properties — se já aterrou, entra aqui; senão marca-se pendente]**
- **A3 — static fn factory de trait.** `trait Zeroed { static fn zero(): self { self { _n = 0 } } }`
  composto num struct com `_n: i64`. `Post::zero()._n` → `0`. Prova `static fn` + `self` como tipo.
- **A4 — colisão RESOLVIDA por override do host.** Duas traits `A`/`B` ambas com `fn k(): i64`; o host
  define o SEU `k()` → vence, sem erro. Prova a precedência do §3.4 (micro-fork). `exit` do host.
- **A5 — compose em `service`.** `service S singleton & SomeTrait { … }` achata o método do trait.
  Prova §0.1-5 (achata em service). `exit` codifica o método.
- **A6 — capacidade via interface-operador** `examples/regressions/capability_iface/`. `index_of<T:
  IEq>` sobre `[]Point` com `Point` a escrever `__eq` (+ `NeByEq`). Acha o índice → token. Prova o
  dispatch REAL por vtable que a structural nunca teve. **[BLOQUEADO em §9-operadores]** — o esqueleto
  do `.tkr`/`.tks` escreve-se já; corre quando §9 fecha.

### 8.2 REJEITAR — `examples/regressions/diagnostics/` (a maioria independente de §9)
- **R1 — no-match-over-trait (subject).** `match algo_de_tipo_trait { … }` → `Then diagnostic =
  "cannot match on trait"`. O DIAGNÓSTICO do §0.1-7. (Construir um subject que nomeie um trait requer
  um caminho — mais simples: um case que nomeia um trait, R2.)
- **R2 — no-match-over-trait (case).** um case de arm nomeia um trait → `Then diagnostic = "has no
  discriminant"`.
- **R3 — trait em constraint.** `fn f<T: SomeTrait>(x: T) { }` → `Then diagnostic = "cannot appear in a
  constraint"` (§3.5). Prova constraint=interface-only.
- **R4 — trait em posição de tipo (param/retorno/campo/var).** `fn f(x: SomeTrait) { }` → `Then
  diagnostic = "not a type"`. Prova §0.1-2 (trait não é tipo).
- **R5 — campo num trait.** `trait Bad { x: i64 }` → `Then diagnostic = "declares no fields"` (§2.2).
- **R6 — método bodyless num trait.** `trait Bad { fn k(): i64 }` (sem corpo) → `Then diagnostic =
  "must have a body"` (§2.2).
- **R7 — backing ausente no host (contrato de composição).** `trait Named { get name(): str
  { self._name } }` composto num struct SEM `_name` → `Then diagnostic` NOMEADO de composição citando
  o trait + host + `_name` (§3.2).
- **R8 — colisão real de traits (sem override, sem overload válido).** duas traits com `fn k(): i64`
  idêntico, host não define `k` → `Then diagnostic` de colisão exigindo o host resolver (§3.4). **[a
  metade "salvo overload válido" depende de §9A; a rejeição-por-defeito adianta-se]**

---

## 9. Sequência de crumbs (ordenada; cada uma gate-ável isoladamente)

Pontos de RITUAL (gate completo) marcados. Sequenciada por dependência de SEED: inventário → remoção
de código-morto → aperto de gramática → semântica do decorador → guardas → (bloqueado) camada
operador → fixtures → reseed. O bloco INDEPENDENTE (1-9) fecha SEM esperar §9; o bloco §9-dependente
(10-11) entra quando os operadores aterram.

1. **Inventário (read-only, sem edição de produto).** Grep vivo de: `& Eq/Ord/Hash/Clone/Default`;
   trait-como-tipo (`: <TraitName>` em param/retorno/campo/var); trait-como-constraint (`<T:
   <TraitName>>`); `trait {` com campos ou bodyless; qualquer `.eq()/.compare()/.hash()/.clone()/
   ::default()` de origem structural. **Produz a lista de migração** (esperada VAZIA para structural;
   a verificar para TR1/campos/bodyless). Se não-vazia: migrar para interface na janela aditiva ANTES
   de cortar, OU reportar para cima se fora de âmbito. — *fundamento do argumento de fixpoint §6.*
2. **Remoção da maquinaria structural (§4) + sweep de teste (§7).** `monomorph` → `collect` (PASS 2 +
   TR2) → `resolve` (`is_structural_trait`/`canonical`/`atom_surface`) → deletar `synth.tks` inteiro →
   deletar `structural_*`/`field_view_*` tests. Codec `.tkb` do `TraitBody` inalterado ainda (fields
   ainda presente). — *behavior-preserving: código-morto.* **RITUAL: gate completo.**
3. **AST + parser do decorador (§2).** `TraitBody` perde `fields`; codec `.tkb` em paridade + teste de
   round-trip; `parse_trait_members` rejeita campos e bodyless, aceita static/get/set. Todos os
   construtores de `TraitBody` deixam de passar `fields`. — *aperto: o inventário (crumb 1) garantiu
   zero uso vivo a partir.* **RITUAL: gate completo** (toca tipo central + codec).
4. **Flatten-compose apertado + service (§3.1).** Remover o fold de campos de `fold_trait_members`;
   confirmar service flui pelo fold; confirmar `self`/get/set/static preservados no push. Remover o
   eixo bodyless-requirement morto (`TraitDerive`/`check_trait_requirements`/…). — *inerte fora de
   traits.*
5. **Contrato de composição nomeado (§3.2).** `check_trait_composition_contract` + o scan de backing +
   a mensagem nomeada. — *dispara só num compose com backing ausente.*
6. **Guardas de não-tipo (§3.5, §3.6).** trait-em-constraint → erro nomeado (`atom_surface` +
   constraint collection); `reject_trait_in_match` em subject e case; remover TR1-dispatch
   (trait-como-valor/slice/arg-upcast — os call-sites §1.1). — *o inventário (crumb 1) confirmou zero
   uso vivo a cortar.* **RITUAL: gate completo** (remove superfície de tipo).
7. **Colisão-salvo-sobrecarga (§3.4).** `trait_method_collision` + `TraitMemberDisposition`; a metade
   "colisão = erro" (comportamento atual) adianta-se; a metade "overload válido" consome o predicado
   de §9A. — *inerte fora de traits co-compostas.* **[metade BLOQUEADA em §9A]**
8. **Fixtures ACEITAR independentes (§8.1 A1-A5) + REJEITAR independentes (§8.2 R1-R8, exceto A6/o
   ramo overload de R8).** Primeiro decorador REAL no corpus de teste. **RITUAL.**
9. **Reseed + PROVENANCE** do bloco independente (§10). **RITUAL.** — *fecha a onda decorador +
   remoção structural SEM esperar §9.*

— fronteira de dependência: as crumbs abaixo esperam `plano-secao9-operadores…` (e §9A para o overload)
aterrarem —

10. **[BLOQUEADO em §9-operadores]** Camada interface-operador (§5): escrever `IEq`/`IOrd`/… e os
    decoradores `NeByEq`/`GtByLt`/… como corpus `.tks`; confirmar conformidade obriga contrapartida via
    a estrutura de interface existente (§5.3). Fixture A6 (`capability_iface`) passa a correr.
11. **[BLOQUEADO em §9-operadores + §9A]** Fechar o ramo overload de R8 (§3.4) e o A4/A6 dependentes;
    reseed final se algo do bloco tocar superfície do compilador (não deve — §5 é corpus/stdlib).

---

## 10. Ritual de reseed + PROVENANCE (crumb 9 / final)

Só depois de todas as crumbs do bloco verde e do gate completo:
1. `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
   src/assert/assert.c -lm -o gen0`
2. `TEKO_BACKEND=c ./gen0 build . --no-verify --release` → `bin-a`.
3. Re-build com `bin-a` como seed → `bin-b`. **Fixpoint: `bin-a == bin-b`** byte-a-byte
   (`scripts/fixpoint_gate.sh`). A remoção é código-morto + aperto sobre zero-uso-vivo ⇒ o fixpoint
   DEVE fechar já na crumb 8.
4. Harvest: `bootstrap/teko.c` (novo seed, sem `synthesize_structural`/`is_structural_trait`) +
   `bootstrap/PROVENANCE` (novo hash/proveniência, notando a remoção structural + o encolhimento do
   `TraitBody`).
5. **NUNCA correr `teko test .`** (fuga de memória do `monomorph` — crasha o container). Gate por
   `--no-verify` + os `scripts/*.sh`.

---

## 11. Riscos + tensões de lei (com resolução recomendada)

- **R-TR1-uso-vivo (o maior risco de fixpoint).** O §9.4 confirma "zero uso vivo" SÓ para a structural;
  a parte "trait não é tipo/constraint" (TR1) precisa da mesma prova antes de cortar os call-sites de
  dispatch. **Resolução:** a crumb 1 (inventário) é PRÉ-REQUISITO — se houver trait-como-tipo vivo,
  migra-se para interface na janela aditiva OU reporta-se para cima (adjacente; NÃO abro issue). Sem
  tensão de lei — é execução disciplinada.
- **R-campos/bodyless de produto.** Traits de produto podem ter campos ou métodos bodyless (o modelo
  velho "contrato de assinaturas" TINHA bodyless). **Resolução:** inventário (crumb 1); um bodyless
  vivo migra para a interface correspondente (o requisito vira contrato de interface). Sem tensão de
  lei.
- **R-tkb (codec).** `TraitBody` encolhe → round-trip tem de bater. **Resolução:** crumb 3 fá-lo em
  paridade + teste de round-trip antes de a semântica depender. Sem tensão de lei.
- **R-contrato-nomeado vs re-type genérico (§3.2).** O re-type do corpo achatado já falharia num
  backing ausente com "unknown field"; o check nomeado corre ANTES para dar a mensagem do ruling.
  **Resolução:** scan sintático de `self.<ident>` cruzado com os campos do host; se o dono preferir
  confiar no "unknown field" genérico, é remover o check (o comportamento fica correto, só menos
  explícito). Recomendo o nomeado (a letra do §0.1-3). Sem tensão de lei.
- **R-D-consts (§3.7).** Manter vs remover member consts no trait. **Resolução recomendada:** manter
  (bodied, não-campo, maquinaria existente); ratificável, não bloqueia. Sem tensão de lei genuína.
- **R-dependência-§9 (a camada interface-operador §5).** `NeByEq`/`IEq`/… só compilam com `operator`
  membro + `self`-como-operando (§9). **Resolução:** o bloco independente (crumbs 1-9) fecha a onda
  decorador + remoção structural SEM §9; a §5 entra como corpus quando §9 aterra (crumb 10). O
  esqueleto `.tks` + doc-comments escreve-se já (honest-stop se `operator` não parseia). Explicitamente
  BLOQUEADO, sequenciado à volta.
- **R-colisão-overload (§3.4).** A metade "salvo sobrecarga válida" consome o predicado de distinção de
  §9A. **Resolução:** a metade "colisão = erro" (comportamento atual) adianta-se; o ramo overload
  liga-se quando §9A aterra (crumb 7/11). **[BLOQUEADO em §9A]** só para esse ramo.
- **Sem tensão de lei genuína identificada — NENHUM HALT necessário.** Todos os rulings do §9.4 encaixam
  num desenho law-first coerente: a remoção é código-morto provado, o decorador reutiliza o fold + o
  `self`-como-tipo dos value-type-methods, a capacidade renasce sobre a interface+operador já
  planeados. As duas dependências (§9-operadores para §5; §9A para o ramo overload da colisão) são de
  SEQUÊNCIA, não de lei.

---

## 12. O que fica BLOQUEADO (resumo honesto para o implementer)

Fecha JÁ, sem esperar ninguém: crumbs 1-9 — inventário, remoção integral da structural, encolhimento do
`TraitBody`, rejeição de campos/bodyless, flatten-compose apertado (struct/class/service), contrato de
composição nomeado, guardas trait-não-é-tipo (constraint/match/valor), a metade "colisão = erro", os
fixtures independentes (§8.1 A1-A5, §8.2 R1-R8 exceto o ramo overload), e o reseed do bloco.

Fica BLOQUEADO até `plano-secao9-operadores-e-value-type-methods.md` aterrar: a camada
interface-operador (§5 — `IEq`/`IOrd` + `NeByEq`/`GtByLt`, o fixture A6 `capability_iface`). O esqueleto
`.tks` + doc-comments escreve-se hoje. Fica BLOQUEADO até `plano-secao9A-method-overload.md` aterrar: o
ramo "salvo sobrecarga válida" da colisão (§3.4) e o fixture R8-overload/A4-overload.
