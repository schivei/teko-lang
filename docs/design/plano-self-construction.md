# Plano — self-construction (o gap do §9) via `.{ … }` target-typed

Status: DESIGN (arquiteto). Ruling do dono (2026-08-13): a via primária é a **construção
target-typed `.{ campo = valor }`** — um `.` unário PREFIXADO + bloco de inicialização que
**lowera para o TIPO ESPERADO do contexto**. Este doc desenha o plano em torno de `.{}`
(pressure-teste, não survey), com o RECON que localiza a falha atual e o blast-radius/ordem de
fixpoint source-only. Lei de referência: `mudancas-superficie-0.3.1.md` (§9 B construção,
§10.3 Intent, §13/item14 `self` ref implícito). Selados NÃO reabertos: §9.4, item 14, Intent
§10.3, §9.D (união `|` inline por extenso, abreviável por macro Família A `@Type()`).

---

## 0. TL;DR

- **Gap:** `static fn zero(): self { self { _n = 0 } }` erra com *"the function's final
  expression does not match its declared return type"* — **em qualquer tipo declarado sob um
  namespace** (todo o corpus do compilador + a Intent). Causa exata isolada abaixo (§1.4): o
  valor construído por `self { … }` recebe o nome do owner **cru/bare** (`env.owner_type`),
  enquanto o retorno `self` resolve para o nome **canônico qualificado** — e `widens_into`
  compara nomes de `Named` por igualdade EXATA de string. Bare `Counter` ≠ `ns::Counter` ⇒ falha.
- **Via primária (dono):** `.{ campo = valor }` constrói o **tipo-alvo do contexto** (retorno
  de `static fn` cujo tipo é `self`; slot `var x: Foo = .{…}`; argumento; campo). Reescreve o
  factory como `static fn zero(): self { .{ _n = 0 } }`. Sem nomear `self` nem o tipo.
- **Regra ratificada:** `.{}` é legal **somente num slot tipado** (um contexto que forneça um
  tipo-alvo). **Sem tipo-alvo ⇒ erro de compilação** (mensagem em §2.4).
- **Coexistência:** `Name { … }` (nominal) e `self { … }` continuam válidos; `.{}` é a forma
  target-typed. Nenhuma é removida.
- **Custo real:** o único plumbing NOVO é **propagar o tipo de retorno como `expected`** para
  as posições de cauda/`return` (hoje typadas sem `expected`). Binding-anotado e campo já
  propagam; argumento é um predicado a mais. **Zero mudança no codegen** (§3.3).

---

## 1. RECON — como a construção funciona hoje, e onde `self`-como-retorno falha

### 1.1 A construção nominal `Name { field = … }` — parse

O parser NÃO tem um nó de construção dedicado: reusa `StructLit`
(`src/parser/ast.tks:244`):

```
pub type StructLit = struct { type_path: Path; type_args: []TypeExpr; field_names: []str; field_vals: []Expr }
```

`parse_struct_lit` (`src/parser/parse_expr.tks:122-152`) recebe o `type_path` já parseado e lê
o bloco `{ nome = valor; … }`. O PREFIXO é sempre dispatchado ANTES em `parse_atom`
(`parse_expr.tks:457-508`):

- `self {` — `SelfKw` seguido de `LBrace`, com `allow_struct`, vira
  `parse_struct_lit(tokens, pos+1, self_sentinel_path(), [])` (linha 457-459). O
  `self_sentinel_path()` é um path de 1 segmento com o nome reservado `self` — nenhum tipo de
  usuário pode soletrá-lo (§4 reserva `self`), então é um sentinela inequívoco.
- `Name<A..> {` — path + type-args + `LBrace` (linha 486-488).
- `Name {` — path + `LBrace` (linha 497-498).
- `{` solto (sem path) — é um **Block-B** (`parse_block_expr`, linha 505-507), NUNCA uma
  construção. **Construção sempre carrega um prefixo de tipo.**

**Não existe hoje um `.` em posição de átomo (líder).** Em `parse_atom` nenhum ramo começa com
`Dot`; um `.` líder cai em *"expected an expression"* (`parse_expr.tks:508`). Em
`parse_postfix` (`parse_expr.tks:564-567`) o `.` EXIGE um receptor à esquerda e um nome depois
(`.field`/`.method`). Logo **`.{` é hoje um buraco de gramática livre** — nenhuma produção o
reclama (prova de não-ambiguidade em §2.1).

### 1.2 A construção — checagem (typer)

`type_struct_lit` (`src/checker/typer.tks:4460`) tipa a construção. O prefixo é resolvido por
`construct_target` (`typer.tks:4434-4438`):

```
fn construct_target(sl, expected, env, table): ConstructTarget | error {
    if is_self_sentinel(sl.type_path) { return self_construct_target(env, table) }   // self { … }
    if sl.type_args.len > 0 { return explicit_inst_target(sl, expected, env, table) } // Foo<A..> { … }
    bare_construct_target(sl, env, table)                                             // Foo { … }
}
```

- `is_self_sentinel` (`typer.tks:4298`): `path.segments.len == 1 && segments[0].name == "self"`.
- `self_construct_target` (`typer.tks:4349-4363`): usa `env.owner_type` como nome do alvo.
  Owner não-genérico ⇒ `ConstructTarget { name = env.owner_type; … }`. Owner genérico ⇒ nome
  phantom `Base__g__<params>` (`self_inst_spelling`), remapeado depois no mono pass.

O corpo compartilhado (linhas 4490-4551) exige EXATAMENTE os campos declarados (uma vez cada),
tipa cada valor de campo com `type_value_expected(sl.field_vals[found], ft, env, table)` (linha
4512 — **o campo já propaga `expected` para construções aninhadas**), e emite o nó final:

```
TExpr { kind = TStructInit { field_names = names; field_vals = vals }; type = Named { name = name }; … }   // typer.tks:4551
```

Ou seja, **o tipo do valor construído é `Named { name = <name do ConstructTarget> }`**.

### 1.3 `self` como TIPO em posição de retorno de `static fn`

Em `type_method` (`src/checker/typer.tks:7938`), o tipo de retorno é resolvido em
`typer.tks:7997-8001`:

```
var ret = if f.has_return && return_type_is_self(f.return_type) {
    Named { name = qualify(env.cur_ns, struct_name) }        // ← CANÔNICO qualificado
} else {
    match function_return(f, tbl, env.cur_ns) { Type as t => t; error as e => return e }
}
```

O `struct_name` é `td.name` (bare, ex. `Counter`); `env.cur_ns` é o namespace do item (posto por
`ienv = with_ns(cenv, it.namespace)`, `typer.tks:8917/9066`). Então para um tipo declarado em
`ns`, `ret = Named { name = "ns::Counter" }`.

A validação da cauda roda DEPOIS de tipar o corpo:
- `check_returns` (`typer.tks:8017`) → `check_return_stmt` para `return e`.
- `check_trailing_value` (`typer.tks:8018` → `7506-7518`) para a expressão-cauda (retorno
  implícito). Ambos chamam `yields_declared_return(value, ret, table)` (`typer.tks:7422-7424`):

```
fn yields_declared_return(value, ret, table): bool {
    assignable_to(value.type, ret, table) || numeric_widens_implicitly(value.type, ret) || literal_adopts(value, ret)
}
```

`assignable_to = widens_into` (`typer.tks:6806`). Para `Named`→`Named`, `widens_into_at`
(`resolve.tks:1328`) começa por `type_eq` (`type.tks:201`): `Named as na => match b { Named as
nb => na.name == nb.name; … }` — **igualdade EXATA de string**. Os ramos de upcast seguintes
(`resolve.tks:1362-1371`) só valem para CLASSES (interface/ancestral); um STRUCT não widen-a por
nada além de nome idêntico.

### 1.4 O ponto exato da rejeição (causa-raiz do `self { }`)

`env.owner_type` dentro do corpo é posto por `with_owner(env, declaring_class)`
(`typer.tks:7942`; `with_owner` em `scope.tks:134` grava o argumento CRU em `owner_type`). Para
um **struct** (não-classe), `declaring_class = td.name` **bare** (`type_struct_methods`,
`typer.tks:8667-8671`: o ramo `else => td.name`). Logo:

- valor de `self { … }` : `type = Named { name = "Counter" }`  (bare `env.owner_type`)
- retorno `self`        : `ret  = Named { name = "ns::Counter" }` (canônico, `qualify(ns, …)`)

`widens_into("Counter", "ns::Counter")` = **false** ⇒ `check_trailing_value` emite
*"the function's final expression does not match its declared return type"*
(`typer.tks:7514`). No namespace-raiz (`cur_ns == ""`) os dois coincidem — por isso o bug só
morde **tipos namespaced** (todo o corpus do compilador + a Intent). Para owner **genérico** há
uma segunda divergência do mesmo tipo: `self { }` produz o phantom `Base__g__…` enquanto o
retorno-`self` produz o base bare-qualificado (`typer.tks:7998` não gera phantom) — duas grafias
distintas do mesmo tipo.

> **REPORT (achado adjacente, não é um novo issue):** o bug do `self { }` é um defeito real e
> ORTOGONAL ao `.{}` — a forma nominal `self { }` (posição de construção selada em §13.2)
> continua a errar em tipos namespaced mesmo depois de `.{}` entrar. O conserto é de 1 linha:
> `self_construct_target` deve canonizar o owner via `receiver_canonical_name(env.owner_type,
> env.cur_ns, table)` em vez de usar `env.owner_type` cru (e o retorno-`self` genérico deve
> produzir o mesmo phantom). Incluído como crumb OPCIONAL (§4, C7) para o integrador decidir
> foldar; não bloqueia `.{}`.

### 1.5 Sítios do corpus que esperam o padrão

- **Intent §10.3** (`mudancas-superficie-0.3.1.md:847-849, 864-866`):
  `pub static fn new(…): self { self { _canceled = c; _failure = f } }` — a ÚNICA construção da
  Intent protegida. Hoje inconstruível (namespaced).
- **Factory de value-struct (item 14)** — `static fn`-fábrica que retorna `self` (ex.
  `zero()`/`make()`), o padrão que a onda-das-traits reportou quebrado.
- **`service … { static fn ctor(): self }`** — a conformidade IService já exige a assinatura
  (`collect.tks:2499`); o CORPO `ctor` que constrói `self` cai no mesmo gap.

### 1.6 Threading de `expected` que já existe (o que `.{}` reaproveita)

`type_value_expected` (`typer.tks:5642`) é o ponto único que dá um `expected` a um `StructLit`
(→ `type_struct_lit(sl, expected, …)`). Ele já é chamado em:

| Posição | Sítio | Propaga `expected`? |
|---|---|---|
| Campo de struct-lit | `typer.tks:4512` | **sim** (`ft`) |
| Binding anotado `var x: Foo = …` | `type_binding_value` `typer.tks:5886-5889` (`at`) | **sim** |
| Const anotado | `typer.tks:8743` | sim |
| Argumento (só lambda/iface-arr/composite-num) | `typer.tks:3287-3290` (`arg_wants_expected`, `5709`) | **parcial** |
| `return e` | `type_return` `typer.tks:6249-6252` (plain `type_expr`) | **NÃO** |
| Cauda de bloco (retorno implícito) | `type_block` `typer.tks:5394-5432` (plain `type_expr`) | **NÃO** |

As duas últimas linhas são o plumbing NOVO (§3.1).

---

## 2. Mecânica do `.{}` (a via do dono)

### 2.1 Parsing — `.{` é inequívoco

Adiciona-se UM ramo no início de `parse_atom` (`parse_expr.tks`, junto ao dispatch de `self {`
em 457): quando o token corrente é `Dot` e o seguinte é `LBrace`, emitir um `StructLit` com um
**path-sentinela vazio** (`type_path.segments.len == 0`) — o "dot sentinel":

```
if k == lexer::TokenKind::Dot && is_kind_at(tokens, pos + 1, lexer::TokenKind::LBrace) {
    return parse_struct_lit(tokens, pos + 1, dot_sentinel_path(), teko::list::empty())
}
```

onde `dot_sentinel_path()` devolve `Path { segments = teko::list::empty() }`. `parse_struct_lit`
já parseia `{ nome = valor; … }` verbatim — **zero nova lógica de bloco**.

**Não-ambiguidade (prova):**
- `parse_atom` hoje não tem ramo iniciando com `Dot` → `.{` não colide com nada em posição de
  átomo (`parse_expr.tks:508` é o `else` de erro).
- `parse_postfix` só consome `.` DEPOIS de um átomo (receptor à esquerda) e SEMPRE exige um NOME
  após o `.` (`parse_expr.tks:565-567`), nunca um `{` → `x.{` continua erro (correto: não há
  "construção sobre receptor").
- `..` (DotDot, spread) e um float `.5` são tokens/lexemas distintos de `Dot` `LBrace`: o
  caractere após o `.` é `.` ou dígito, nunca `{`. Sem colisão de maximal-munch.
- O ramo NÃO precisa do gate `allow_struct`: o `.` líder já delimita — não pode ser confundido
  com o `{`-bloco de um scrutinee `if`/`match`. Recomenda-se parseá-lo **incondicionalmente**
  (mais previsível que copiar o gate); um `.{}` num scrutinee simplesmente falhará no type-check
  por não produzir `bool`, como qualquer outro valor mal-posto.

**Representação AST:** reusar `StructLit` com `type_path` vazio evita um novo `ExprKind` e
reaproveita TODO o tail de checagem de campos de `type_struct_lit`. O sentinela vazio é
distinguível do `self` sentinel (`segments.len == 1`, nome `self`) e do nominal (`segments.len
>= 1`).

### 2.2 Inferência do tipo-alvo (checker)

Um novo predicado `is_dot_sentinel(path): bool` = `path.segments.len == 0`, e um novo ramo em
`construct_target` (`typer.tks:4434`), ANTES do teste de `self`:

```
fn construct_target(sl, expected, env, table): ConstructTarget | error {
    if is_dot_sentinel(sl.type_path) { return dot_construct_target(expected, env, table) }
    if is_self_sentinel(sl.type_path) { return self_construct_target(env, table) }
    if sl.type_args.len > 0 { return explicit_inst_target(sl, expected, env, table) }
    bare_construct_target(sl, env, table)
}
```

`dot_construct_target(expected, env, table): ConstructTarget | error` resolve o alvo a partir
de `expected`, reusando a maquinaria já existente:

1. `expected` é `Named { name }`:
   - Se `name` é o próprio owner corrente sob forma-phantom de um método genérico, delega para
     `self_construct_target` (mesma grafia phantom `Base__g__…`) — resolve o caso
     `static fn make(): self` num `Cell<T>`.
   - Senão, `type_table_find(table, name, "")` dá a `TypeDecl` concreta ⇒
     `ConstructTarget { name = name; decl = td; is_self = false }`. Instância genérica já
     estampada (`Foo__g__i64`) e phantom-instance seguem o MESMO tratamento de
     `explicit_inst_target` (`typer.tks:4388-4396`).
2. `expected` é qualquer OUTRA coisa (Void, Prim, Slice, união sem alvo único, etc.) ⇒
   **erro sem-alvo** (§2.4).

Como `dot_construct_target` produz um `ConstructTarget` com um `name` **canônico** (o de
`expected`, que JÁ é o nome canônico do retorno/slot), o `TExpr` final (`typer.tks:4551`)
sai com `type = Named { name = <canônico> }`, IDÊNTICO ao alvo — `widens_into` passa por
`type_eq`. **É exatamente isto que fecha o gap sem tocar `self { }`.**

### 2.3 Onde `expected` chega (as 5 posições de slot tipado)

`.{}` é legal SOMENTE onde há tipo-alvo. As 5 posições e o estado do threading:

1. **Retorno implícito de `static fn … : self`** (o headline) — NOVO threading (§3.1).
2. **`return .{…}`** — NOVO threading (§3.1).
3. **Binding anotado** `var x: Foo = .{…}` — JÁ funciona (`type_binding_value`, `typer.tks:5889`).
4. **Argumento** `f(.{…})` com tipo do parâmetro — predicado a mais (§3.1, crumb C4).
5. **Campo** `Outer { inner = .{…} }` — JÁ funciona (`type_struct_lit` field loop,
   `typer.tks:4512`).

### 2.4 Sem tipo-alvo → erro (RATIFICAR)

`.{}` num contexto sem tipo-alvo (ex. `var x = .{…}` sem anotação; `println(.{…})` sobre um
`params`/variádico sem tipo fixo; `.{…}` como scrutinee; `.{…}` num slot cujo `expected` é uma
união com >1 struct candidato) é **erro de compilação**, mensagem:

> `` `.{ … }` needs a known target type here — annotate the binding/return or write the type explicitly (`Name { … }`) ``

Emitida por `dot_construct_target` quando `expected` não resolve a um único `Named`
struct/classe construível. Ratifica-se: `.{}` **nunca** infere por back-flow de uso posterior;
o tipo-alvo vem SÓ do slot imediato. Isto mantém a checagem uni-direcional (a mesma disciplina
de `type_value_expected`), sem inferência global.

### 2.5 Coexistência com `Name { … }` e `self { … }`

- `Name { … }` (nominal, com/sem type-args): **inalterado**, continua a via explícita — a única
  que funciona SEM tipo-alvo no contexto (ex. dentro de um `println`, num scrutinee via bloco,
  numa expressão solta).
- `self { … }`: **permanece** como posição de construção selada (§13.2). Recomenda-se, junto,
  o conserto de 1 linha de §1.4 (crumb C7) para que também funcione namespaced — mas isso é
  ORTOGONAL a `.{}` e separável.
- `.{ … }`: a via **target-typed**, primária nos 5 slots tipados. Nos factories de `self` e na
  Intent, `.{}` **substitui** `self { }` na grafia recomendada (reescrita em §3.4).

### 2.6 Interação com item 14 (value-struct, `self` ref implícito)

`.{ … }` é uma **materialização nova** — idêntica em runtime a `Name { … }`/`self { … }`: aloca
o valor fat (cabeçalho `uptr`/auto-ponteiro, §13.2) e preenche os campos. Como `.{}` reduz ao
MESMO `TStructInit` (§3.3), o codegen já emite o cabeçalho/ponteiro-para-si na construção; a
cópia na atribuição/passagem continua a ser nova materialização com novo ponteiro (value
semantics preservada). `.{}` **não** introduz identidade nem heap — é value-type, como a via
nominal. Num `static fn` (sem receptor) `.{}` cria a instância do zero; num método de instância
`.{ … }` cria uma **nova** instância (distinta do `self`-receptor ref).

### 2.7 Interação com a Intent (§10.3)

A Intent é `struct` protegido; a única construção é `pub static fn new(): self`. Com `.{}`:

```teko
/**
 * new — the runtime's sole constructor for a resultless Intent (§10.3): builds a fresh
 * Intent whose outcome fields are seeded from the awaited task's disposition.
 *
 * @param c  the cancellation flag the runtime writes
 * @param f  the cancellation reason, or null on a clean completion
 * @return   a freshly materialized Intent (the target type is `self`)
 * @since    §10.3
 */
pub static fn new(c: bool, f: error | null): self {
    .{ _canceled = c; _failure = f }
}
```

O `expected` do retorno é `self` = `Named{ns::Intent}`; `.{}` materializa exatamente esse tipo,
com os backing `_canceled`/`_failure` privados preenchidos na construção (o único lugar onde um
campo readonly/privado pode ser escrito, §13.3). Nenhuma menção ao nome `Intent` nem a `self` no
corpo — a proteção fica intacta. A união `error | null` do parâmetro é grafia §9.D inline por
extenso (abreviável por macro Família A `@…()`, fora do escopo deste doc).

---

## 3. Blast-radius + ordem de fixpoint (source-only; gate byte-identidade)

### 3.1 O único plumbing novo — propagar o tipo de retorno como `expected`

Hoje `type_return` e a cauda de `type_block` tipam sem `expected`. Para `.{}` em posição de
retorno, o tipo de retorno precisa estar em mãos ANTES de tipar o valor. Abordagem recomendada
(mínima e localizada): **carregar o alvo de retorno no `Env`** — um campo novo `ret_expected:
Type` (espelha `owner_type`), posto em `type_method`/`type_function` a partir do `ret` já
computado, e lido nas duas posições de cauda.

- `Env` (`scope.tks:72`): + `ret_expected: Type`. Atualizar `env_empty`/`seal`/`with_ns`/
  `with_file`/`with_owner` (5 construtores) para carregar o campo (default `Void {}`), e um
  novo `with_ret_expected(env, t)`.
- `type_method` (`typer.tks`): após computar `ret` (7997-8001), tipar o corpo com
  `with_ret_expected(local, ret)` (idem `type_function` para free fns com retorno anotado).
- `type_return` (`typer.tks:6249-6252`): tipar `r.value` via
  `type_value_expected(r.value, env.ret_expected, env, table)` quando `env.ret_expected` não é
  `Void`; senão o `type_expr` de hoje. (O check pós-hoc `check_returns` permanece — agora passa,
  porque o valor nasceu no alvo.)
- `type_block` cauda (`typer.tks:5394-5432`): SÓ a ÚLTIMA statement, quando é `ExprStmt` cujo
  `expr` é um dot-sentinel (ou struct-lit) e `env.ret_expected != Void`, tipar via
  `type_value_expected(es.expr, env.ret_expected, …)`. Para NÃO poluir blocos que não são cauda
  de função (corpo de loop, bloco não-cauda), `ret_expected` deve ser **limpo** (`Void`) ao
  descer em contexto não-cauda: `type_loop` (`typer.tks:6266`) tipa o corpo com
  `with_ret_expected(env, Void{})`; a cauda de `if`/`match` que É valor de função MANTÉM o alvo
  (elas já roteiam por `type_expr` → threading pode passar via um `type_expr_expected` fino, ver
  crumb C3). Disciplina: `ret_expected` vale só na cadeia de posições-cauda do corpo da função.

Alternativa considerada (rejeitada): passar `expected` como parâmetro explícito por toda a
cadeia `type_block`/`type_statement`/`type_return`. Rejeitada por churn de assinatura muito
maior (dezenas de sítios) sem benefício sobre o campo de `Env` (o `owner_type` já provou o
padrão).

### 3.2 Argumento `.{}` — um predicado a mais

Em `typer.tks:3287-3290` o arg `ti` só roteia por `type_value_expected` para
lambda/iface-arr/composite-numérico. Adicionar `arg_is_dot_init(dargs[ti])` (dot-sentinel) — e,
de brinde, struct-lit — à disjunção, para o parâmetro `f.params[ti]` virar `expected`. Inerte
para args que já tipam sozinhos (nominal struct-lit ignora um `expected` redundante).

### 3.3 Codegen — ZERO mudança

`.{}` reduz ao MESMO nó `TStructInit { field_names; field_vals }` com `type = Named{canônico}`.
O codegen consome `TStructInit` pela `e.type` resolvida (`codegen.tks:1346, 5180-5279, 7826,
9273, 9590, 11592`), **nunca** por um path de superfície. Como o checker já entrega o nome
concreto, o codegen não distingue `.{}` de `Name{}`/`self{}`. Nenhuma edição em
`src/codegen/`. Idem interpretador (aposentado). **O gate byte-identidade gen2==gen3 é
preservado por construção** — a AST typada é a mesma que a via nominal produziria.

### 3.4 Reescritas de corpus (source-only)

- Intent §10.3 (doc + eventual `.tks` da Intent quando ela entrar): `self { … }` → `.{ … }`
  nos dois `new` (`mudancas-superficie-0.3.1.md:847-849, 864-866`). Doc-only agora; o `.tks`
  ainda não existe (a Intent depende de §10.3/§11, DESIGN-AHEAD).
- Factories de value-struct do corpus (`static fn … : self { self { … } }`) → `.{ … }`.
  Levantar por `rg 'static fn .*: self' src` no momento da implementação (o parser aceita ambas
  as grafias na janela, então a varredura é incremental, sem big-bang).
- `service … ctor(): self` — idem.

### 3.5 Ordem de fixpoint (bootstrap-seed safe)

O seed é o `teko` binário anterior; o corpus não pode USAR `.{}` antes do seed o entender. Logo:

1. **C1 (parser)** e **C2/C3 (checker)** entram como CAPACIDADE, sem NENHUM uso de `.{}` no
   `src/` ainda. Build com o seed atual (que ignora a capacidade nova pois nada a usa).
2. **Reseed** — o novo `teko` passa a ENTENDER `.{}`.
3. **C4/C5/C6 + reescritas** (§3.4) — só DEPOIS do reseed o corpus pode escrever `.{}`.
   Ritual: gate cheio (compila + regressões + gen2==gen3) neste ponto.

Isto respeita "o corpus não usa feature ausente no seed": a capacidade precede o uso por um
reseed.

---

## 4. Sequência de crumbs (cada um gate-ável)

- **C1 — parse de `.{`** (`src/parser/parse_expr.tks`, `+ dot_sentinel_path`):
  novo ramo `Dot`+`LBrace` em `parse_atom` → `StructLit` com `type_path` vazio. Gate: parse-only
  (a AST typada de um `.{}` sem suporte no checker ainda erra — então C1 sem uso no corpus).
- **C2 — `is_dot_sentinel` + `dot_construct_target`** (`typer.tks`, junto a `4298`/`4349`):
  resolve alvo de `expected`; erro sem-alvo (§2.4). Reusa `explicit_inst_target`/
  `self_construct_target` para os casos genéricos/phantom.
- **C3 — threading de retorno** (`scope.tks` Env + `with_ret_expected`; `type_method`/
  `type_function`; `type_return`; cauda de `type_block`; `type_loop` limpa). O maior crumb.
  Introduzir um `type_expr_expected(e, expected, env, table)` fino que roteia struct/dot-lit por
  `type_value_expected` e o resto por `type_expr`, para a cauda de `if`/`match`.
- **C4 — argumento `.{}`** (`typer.tks:3287`): `+ arg_is_dot_init` na disjunção.
- **C5 — RESEED** (ritual). Depois: o corpus pode escrever `.{}`.
- **C6 — reescritas de corpus** (§3.4) + fixtures (§5). Ritual: gate cheio + gen2==gen3.
- **C7 (OPCIONAL, ortogonal — REPORT §1.4)** — conserto do `self { }` bare→canônico:
  `self_construct_target` usa `receiver_canonical_name(env.owner_type, env.cur_ns, table)`; e o
  retorno-`self` genérico produz o phantom. Fecha a forma nominal selada §13.2 em tipos
  namespaced. Separável; o integrador decide foldar em C2/C3 ou adiar.

---

## 5. Fixtures de regressão (inputs → exit code nativo esperado)

Formato `.tkr` (Gherkin, `docs/design/tkr-regression-format.md`): `When compile/run`, `Then`
exit-code. Programas mínimos (namespaced, para exercitar o gap real do §1.4). Exit 0 = OK.

1. **`dot_static_self_ok`** — factory target-typed no retorno-`self`, namespaced:
   ```teko
   type Counter = struct {
       _n: i64
       static fn zero(): self { .{ _n = 0 } }
       exp get n(): i64 { self._n }
   }
   fn main() { var c = Counter::zero(); if c.n == 0 { exit(0) } else { exit(1) } }
   ```
   Esperado: **exit 0** (era o caso que falhava no type-check).
2. **`dot_binding_annotated`** — `var x: Counter = .{ _n = 7 }` → checa `x.n == 7` → **exit 0**.
3. **`dot_arg`** — `fn take(c: Counter): i64 { c._n }`; `take(.{ _n = 5 })` == 5 → **exit 0**.
4. **`dot_field_nested`** — `Outer { inner = .{ _n = 3 } }`; lê `o.inner.n` → **exit 0**.
5. **`dot_return_stmt`** — `static fn one(): self { return .{ _n = 1 } }` → **exit 0**.
6. **`dot_generic_self`** — `Cell<T>` com `static fn of(v: T): self { .{ _v = v } }`;
   `Cell<i64>::of(9)` → **exit 0** (exercita o phantom via `expected`).
7. **`dot_no_target_err`** (compile-fail) — `var x = .{ _n = 0 }` sem anotação →
   **compile error** com a mensagem de §2.4. `Then compile fails`.
8. **`dot_scrutinee_err`** (compile-fail) — `.{…}` como subject de `if` → **compile error**
   (não-`bool`), garantindo que o parse incondicional não abre buraco semântico.
9. **`intent_shape_ok`** (quando a Intent entrar) — os dois `new` da Intent em grafia `.{}` →
   **exit 0** (construção protegida via `.{}` com backing privados).
10. **`nominal_still_ok`** (não-regressão) — `Name { … }` nominal e `self { … }` continuam a
    compilar/rodar como antes → **exit 0** (coexistência; guarda contra regressão da via nominal).

Adicionar, ainda, um teste de unidade `.tkt` em `src/parser/parser_test.tkt` para `.{` →
`StructLit` com `type_path` vazio, e em `src/checker` para `dot_construct_target` (alvo
resolvido de `expected`; erro sem-alvo).

---

## 6. Riscos + tensões de lei (com resolução)

- **R1 — `ret_expected` vazando para blocos não-cauda.** Se `ret_expected` não for limpo em
  corpo de loop / bloco não-cauda, um `.{}` mal-posto poderia herdar o alvo de retorno errado.
  Resolução: disciplina de set/clear de §3.1 (limpar em `type_loop`; manter só na cadeia de
  cauda). Coberto por fixture 8 e por um `.tkt` de bloco não-cauda. Baixo risco.
- **R2 — união como `expected`.** Um retorno `A | B` (dois structs) não dá alvo ÚNICO para
  `.{}`. Resolução (ratificada, §2.4): sem alvo único ⇒ erro sem-alvo; o dev escreve
  `Name { … }`. Consistente com a checagem uni-direcional. (Um `T | null` onde só `T` é
  construível PODE resolver a `T` — opcional; recomendo tratar como sem-alvo por simplicidade e
  reavaliar se o corpus pedir.)
- **R3 — owner genérico / phantom.** O retorno-`self` genérico hoje NÃO produz phantom
  (`typer.tks:7998`), enquanto `self_construct_target` produz. `dot_construct_target` deve
  espelhar o phantom quando `expected` é o owner corrente (§2.2). Fixture 6 trava isto. Se o
  crumb C7 (ortogonal) for foldado, alinha as duas grafias de vez.
- **T1 — tensão de lei §13.2 (self { } selado) × ruling `.{}`.** §13.2 nomeia `self { }` como
  posição de construção; o dono introduz `.{}` como via target-typed e reescreve a Intent para
  `.{}`. **Não é tensão real:** coexistem (§2.5) — `.{}` é target-typed, `self { }`/`Name { }`
  são nominais; nenhuma é removida. O ruling do dono é o mais recente e vence law-first. **Sem
  HALT.**
- **T2 — W15/Javadoc.** Todo `.tks` novo/tocado (parser ramo, checker fns, Env campo) leva
  Javadoc completo em CADA declaração. Os snippets deste doc já vêm nesse estilo para cópia
  verbatim.

Nenhuma tensão genuína permanece em aberto. Sem HALT.

---

## 7. Bloqueios / DESIGN-AHEAD

Nada BLOQUEADO. Todos os alvos (`.{}` parser, checker, threading, fixtures) são
implementáveis contra a superfície atual. A Intent em si (`intent_shape_ok`, fixture 9) depende
de §10.3/§11 entrarem — mas a FORMA da construção (`.{}`) e a reescrita doc-only já ficam
prontas aqui; o implementer da Intent copia verbatim quando ela abrir.
