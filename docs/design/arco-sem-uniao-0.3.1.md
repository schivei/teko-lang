# Arco "SEM UNIÃO" — eliminar toda união de VALOR (0.3.1)

> **Status:** DESENHO / architect-first. ZERO código de produto. Base `fix/retirement @ c5668381`.
> **Fonte-lei:** DECISION_LOG **D199** (o arco), **D198** (roadmap tipos-à-superfície), **D197/D192**
> (template str-reificação, provado), **D30** (interface value-type VIVA), **D68** (ratchet), **D187/D188**
> (zero-exceção-backend / lista fechada de builtins), **D131/D133/D134** (surface/provenance/VFS).
> NÃO reabre nada disso. Este doc **mapeia** a demolição, sequencia os crumbs escalonados-verdes, e
> **separa** o que é executável-já do que exige ratificação/HALT do dono.

---

## 0. Leis reproduzidas (governam CADA crumb)

- **NO-PUSHES / 4-naturezas / purge-na-reatribuição** (CLAUDE.md). Nenhuma conversão pode introduzir
  array dinâmico, `push`, `grow_inplace`, nem acumulador `x=[..x,y]`. Migração de assinatura em massa =
  pré-aloca `[n]T` do tamanho contado.
- **CHECKED vs UNCHECKED (dono 2026-08-28):** `exp` devolve `error` no caminho de falha; interno (`pub`/
  privado) **panica**. O `getter value()` do `null<T>` panica se ausente = coerente (é `pub`, unchecked,
  reusa o guard de deref-zero).
- **D187/D188 — lista FECHADA de builtin-legítimo:** (1) pontos de arena; (2) operador↔opcode; (3)
  reinterpret `wrap`/`unwrap`/`slice_view`; (4) `syscall` raw. **Tudo o mais é função de superfície pelo
  caminho GENÉRICO nas duas rotas.** `zero<T>` entra como **gêmeo escalar do `of_len<T>`** (D188-legítimo,
  memset-0 declarado como superfície) — é o único acréscimo à lista, e é primitiva-de-representação, não
  name-detect escondido.
- **D68 — RATCHET (estrito):** o pico do build seco (`teko: memory: peak <N> MB`) só pode CAIR. Flat =
  regressão. Como o arco é remodelagem (não pura redução), o piso é **NÃO-CRESCER**; cada crumb mede maçã-
  com-maçã e corrige antes de drenar. **Há dois pontos de crescimento-risco reais (§5.4) — desenhados com
  mitigação e sinalizados ao dono.**
- **Gate por crumb de compiler-core (D164/D166/D185/D191):** fixpoint gen2==gen3 byte-idêntico +
  gen0-do-seed-commitado builda o tip + ASan+UBSan limpo + 3 harnesses C standalone + grep zero-ref +
  varredura **árvore-inteira** (`src`+`cases`+`examples`+`tklib`+`tooling`+`main.tks`).
- **ADIANTAR O QUE FOR NECESSÁRIO (D154/D155):** construir o pré-requisito e fazer certo; nada de ponte
  transitória que morre em seguida.

---

## 1. CENSO — os consumidores de união (grep-ancorado, números MEDIDOS hoje)

### 1.1 A superfície de união no código
| forma | ocorrências | onde | vira |
|---|---|---|---|
| `\| error` (retorno falível) | **1637** em `src/` (1661 em todo `.tks`) | tree; núcleos `typer 145`, `lower 281`, `comptime_fold 88`, `math/checked 72`, `codegen 56`, `project 53`, `toml/yaml_parse 36/25`, `tkb_read 40` | `(null<T>, error)` multi-retorno |
| `\| null` (nulável) | **359** em `src/` | tree; `macro_expand 106`, `monomorph 70`, `parse_decl 94`, `typer 331` (inclui idioma) | `null<T>` |
| `\| CryptoError` / `crypto::CryptoError` | **138** | `src/crypto/**` | `class CryptoError & error` + `(null<T>, error)` |
| `\| NotUserType`/`NotSvcOp`/`NotPtrOp` | ~11 | checker (sentinelas de resolução) | destructar p/ `null<T>` ou pequeno resultado nomeado |
| consumo `match { … error as e => … }` / `null =>` | **2937** sítios em `src/` | tree inteira | multi-bind + `.has_value()` / downcast `errors.As` |

O `\| error` é **99% compiler-core** (1637 de 1661): o arco é uma **campanha de compiler-core**, reseed
iterativo, ratchet governando — não uma mudança de folha.

### 1.2 O tipo `Variant` e sua maquinaria (o que APOSENTA)
`type Variant = struct { members: []@Type() }` — `type.tks:80`; membro da macro `Type()` (`type.tks:94`).
**729 refs a "variant" em 37 arquivos.** Núcleos do byte-mover:

| camada | refs | peças-chave (verificadas) |
|---|---|---|
| `codegen/codegen.tks` | **244** | `UnionRepr = enum { Niche; InlineTag; BoxInArena; TagPtr }` (2372); `cg_type_is_niche_able` (2284), `cg_union_niche_member` (2295), `cg_niche_*` (2318-2351); `cg_variant_has_null` (2255); `emit_variant_wrap*` (5601-5678, o **union-injection**: exact/widen/transitive pass); `emit_as_r_in` (5228); `cg_emit_variant_ctype` (2654); `cg_emit_inline_variant_typedef` (9055); `emit_pat_test`/`cg_emit_tag_prefix` (6329/6275) |
| `lir/lower.tks` | **270** | lowering nativo do gordo/niche/tag |
| `lir/lower_const.tks` | **42** | const-eval de variante |
| `checker/resolve.tks` | **32** | `variant_member_admissible` (1242) + **honest-stop interface-em-união** (1250-1251); `resolve_type UnionType` (1289); `union_normalize_null`/`union_dedup` |
| `checker/typer.tks` | **29** | tipagem de `| error`/`| null`, "there is no tuple value" (1989) |
| `checker/match.tks` | **28** | análise de braço/downcast — `is_type_discriminant_of` (57-69), `is_interface_name`→`type_conforms_to` (a base do `errors.As`, JÁ existe) |
| `emit/{tkb_write,tkb_read,tkh,header,tkb_frame}` | 12 | tag de tipo serializado (round-trip) |
| `checker/{scope,monomorph,borrow,consteval,expr,collect,check_modules}` | ~25 | construção/checagem |

### 1.3 O ACHADO DECISIVO — as famílias de AST do compilador SÃO Variants
`macro Type() { lowering { Prim | Byte | Char | Slice | Named | Variant | Func | Error | Void | Ptr | Uptr | Reference | Null } }` (`type.tks:94`). O `@Type()` (um `MacroType`) é **expandido no pré-passe Family-A
num `UnionType` → `Variant`** (`resolve.tks:1331` guarda o caso não-expandido). O MESMO vale para
`@ExprKind()` (`ast.tks:89`), `@Statement()`, `@Decl()`, `@Pattern()`, `@TypeExpr()`, `@ConstraintExpr()`
(`ast.tks:163`), `@Item()`. **Toda a espinha AST/tipo do compilador é uma união de valor discriminada.**

Consequência dura: **abolir o `Variant` por completo alcança as famílias de AST** — elas teriam de virar
outra coisa (hierarquia de interface/OO com downcast). Isso é ordens de magnitude maior que error/null, tem
uma **tensão de memória própria** sob reclaim-0% (cada nó AST hoje é valor inline; como interface vira
fat-pointer boxeado), e **NÃO está deliberado em D199**. É o fork central do arco — **§8 (HALT)**. Por isso
o arco se PARTICIONA (§4): Fase I-II (error+null, executável-já) e Fase III (Variant genérico + famílias AST,
forked).

---

## 2. Decisões travadas (D199) — encodadas, não redeliberadas

1. **`error` = interface + tupla.** `exp global interface error { getter message(): str; getter inner(): null<error> }`, prelúdio-base VFS, nome reservado por provenance. `line`/`col`/`file`/`expected`/`actual` SAEM
   da interface. `Err` concreto `class Err & error` recebe a fábrica. Downcast por `match { ParseErr as p => }`
   (conformance JÁ roda — `match.tks:57-69`). Devolução falível = **`(null<T>, error)`** (RECOMENDAÇÃO d, §7).
2. **nulável = `null<T>`** (classe surface, ZERO união). `NULL<T>()`/`NULL()` + literal `null{}`. `x = null`
   (NullLit) MORTO — `null` é só TIPO; checagem = `.has_value()`.
3. **`zero<T>(): T`** = primitiva nova (memset-0, gêmeo de `of_len<T>`).
4. **Resolução pelo tipo esperado JÁ existe** (`dot_construct_target`, `explicit_inst_target`, overload). ADICIONAR
   inferência de type-arg **dirigida pelo retorno** para fn genérica de zero-args (`NULL()` inferir `<i64>` do
   expected).
5. **Constraints = ASSERÇÃO/narrowing positivo (RULING do dono, D199 atualizado — NÃO é proposta).**
   Constraint só narrowa (assere o que T satisfaz, sem alternativa). **`ConstraintOr` (`<T: A | B>`) MORRE;
   `notnull` MORRE** (nulabilidade agora é `null<T>` explícito → redundante). O RESTO fica intacto: form word,
   service lifetime, bound de interface/type/slice, `error`→"implementa a interface error", e o `&` (AND/
   interseção). Regra dura: nada de `any`/catch-all; "qualquer coisa" = genérico aberto `<T>` sem constraint.
   Detalhe executável em §7.1.

---

## 3. O DESENHO — o novo mundo (contratos que compilam)

### 3.1 `null<T>` — a classe nulável (Fase I)
Toda a maquinaria já existe: **classe genérica com campo de type-param** (`Map<V>`, `List<T>`,
`Dictionary<K,V>` provam), **resolução pelo alvo** (`dot_construct_target`), **guard de deref-zero**.
No prelúdio-base (`src/base/*_surface.tks`, ns `teko::base`, provenance-base):

```teko
/**
 * null — a base virtual da família nulável; o valor "ausente" sem tipo concreto,
 * endereçado pelo tipo esperado (`.{}` / literal `null{}`).
 */
exp global type null = virtual class { }

/**
 * null<T> — Option/Maybe: um `T` presente ou a ausência. ZERO união.
 * @param T o tipo transportado quando presente
 */
exp global type null<T> = class null {
    _value: T
    _setted: bool

    /**
     * has_value — true se um `T` está presente.
     * @return presença
     */
    fn has_value(): bool { self._setted }

    /**
     * value — o `T` presente; PANICA se ausente (unchecked, guard de deref-zero).
     * @return o valor presente
     */
    fn value(): T { self._value }

    /**
     * value — grava o `T` e marca presente.
     * @param val o valor a transportar
     */
    setter value(val: T) { self._value = val; self._setted = true }

    /**
     * new — o `null<T>` ausente (valor zerado, não-setado).
     * @return a instância ausente
     */
    static new(): self { .{ _value = zero<T>(); _setted = false } }
}

/**
 * NULL — o `null<T>` ausente, T inferido pelo tipo esperado do sítio.
 * @return a ausência tipada
 */
exp global fn NULL<T>(): null<T> { null<T>::new() }

/**
 * NULL — a ausência base sem tipo concreto (endereçada pelo alvo).
 * @return a ausência base
 */
exp global fn NULL(): null { .{ } }
```

`getter value()` no D199 vira **`fn value()`** enquanto o açúcar getter/setter (D4/D196) não estiver vivo —
o dono aceitou getter/setter como onda D196; **até lá, método `.value()` explícito**. (Anotado como
divergência-de-forma controlada, não fork: a semântica é idêntica.)

**Migração `T | null` → `null<T>`:**
- retorno/campo/param `X | null` → `null<X>`.
- construção do ausente: `null` (NullLit) → `NULL()` (alvo-dirigido) ou `null{}`.
- `match v { X as x => …; null => … }` → `if v.has_value() { var x = v.value(); … } else { … }`.
- `sole_sibling_is_null` (`ref T | null`) → ver §8-b (nulável de referência — sub-fork).

### 3.2 `error` interface + `Err` concreto (Fase II)
No prelúdio-base:

```teko
/**
 * error — o contrato de todo valor de erro. Nome reservado (provenance-base).
 */
exp global interface error {
    /**
     * message — a causa, no formato `file:line:col: causa`.
     * @return a mensagem
     */
    getter message(): str
    /**
     * inner — o erro encadeado, ausente se raiz.
     * @return o erro interno ou ausência
     */
    getter inner(): null<error>
}

/**
 * Err — o erro concreto padrão do compilador; carrega a fábrica.
 */
exp global type Err = class & error {
    _message: str
    _inner: null<error>

    getter message(): str { self._message }
    getter inner(): null<error> { self._inner }

    /**
     * of — erro simples de mensagem.
     * @param msg a causa
     * @return o Err
     */
    static of(msg: str): Err { .{ _message = msg; _inner = NULL() } }

    /**
     * loc — adorna a mensagem com `file:line:col:` (sucessor de err_loc).
     * @param msg a causa · @param file · @param line · @param col
     * @return o Err posicionado
     */
    static loc(msg: str, file: str, line: u32, col: u32): Err { .{ _message = $"{file}:{line}:{col}: {msg}"; _inner = NULL() } }

    /**
     * typed — anexa expected/actual à mensagem (sucessor de err_typed).
     * @param msg a causa · @param expected · @param actual
     * @return o Err tipado
     */
    static typed(msg: str, expected: str, actual: str): Err { .{ _message = $"{msg} (expected {expected}, got {actual})"; _inner = NULL() } }
}
```

`err_loc`/`err_typed` (name-detect em `scope.tks:548/552`; inline `tk_error_loc`/`tk_error_types` em
`codegen.tks:4271/4281`) → **fábrica `Err::loc`/`Err::typed`** (superfície, caminho genérico). `error { message = m }`
(→ `tk_error_make`, `codegen.tks:5145`) → `Err::of(m)`. `line`/`col`/`file`/`expected`/`actual` da interface
SAEM: o único leitor real é o LSP (`diagnostics.tks:206` lê `e.line/e.col`; `92` idem) → passa a **parsear o
prefixo `file:line:col:`** que `Err::loc` embute (o `strip_leading_location` já existe ali, é a metade da
maquinaria). `typer.tks:5508` (lê `inner.expected/inner.actual` para re-adornar) → o texto já vive na message.

### 3.3 Devolução falível `(null<T>, error)` — a máquina JÁ EXISTE
**Multi-retorno é real e completo:** `parse_multi_return_types` (`parse_decl.tks:227`, exige ≥2 tipos),
baixado a **struct manglada** (`mret_mangle`/`mret_struct_item`/`mret_named_type`, `collect.tks:399/414/427`;
`mret_rewrite_returns` reescreve os `return a, b`), com destructuring `var a, b = f()` (`type_multibind_mret`,
`typer.tks:4022`). **Zero máquina nova** para a forma de retorno.

Convenção:
- **sucesso** = `(present<T>, NULL<error>())` → `return v, NULL()`.
- **falha** = `(NULL<T>(), err)` → `return NULL(), Err::of(msg)`.
- **consumo** = `var v, e = f(); if e.has_value() { return NULL(), e }` (propaga) `else { …use v.value()… }`.
- **usar `v` de uma chamada que falhou** → `v.value()` PANICA (guard de ausente do `null<T>`) — mata o
  footgun do zero-value do Go **sem análise de fluxo**.

**Elegância que o arco desbloqueia:** como o `error` volta como **valor autônomo** (2º slot do multi-retorno),
ele **NUNCA é membro de união** → o honest-stop `resolve.tks:1250-1251` ("an interface cannot be a variant
member yet") **nunca precisa ser relaxado** (a lane 2 do roadmap vira MOOT), e o nicho-de-interface dormente
(§achado 2 do roadmap) não precisa ser cumprido. A abolição da união **dissolve** o gap de interface-em-união
em vez de consertá-lo.

### 3.4 `zero<T>(): T` — primitiva (Fase 0)
Gêmeo escalar de `of_len<T>` (`typer.tks:793/864`): checagem em `typer` (1 type-arg explícito ou inferido, T
storable, não-`Reference`/`Void`), emissão memset-0 em `codegen` (rota-C `(T){0}`) e `lower` (nativo). D188-
legítimo (representação, superfície declarada). Usos: `null<T>::new`, default de campo, acumulador zerado.

### 3.5 Inferência de type-arg dirigida pelo retorno (Fase 0)
Hoje a inferência de type-arg vem dos ARGUMENTOS; `NULL()` (zero-args) não tem de onde inferir `<T>`. O D199
manda **ensinar** a inferência pelo `expected` do sítio (`dot_construct_target(expected)` já entrega o alvo;
falta propagá-lo à instanciação genérica de fn de zero-args). Verificado: `explicit_inst_target`/
`dot_construct_target` existem em `typer.tks:2717`; a extensão é **ligar o `expected` à resolução de
`callee_type_args` vazio de uma fn genérica** — mudança localizada no `type_call`, não máquina nova. (Sem
essa peça, `NULL()` exigiria `NULL<i64>()` — que o dono vetou como workaround.)

---

## 4. CRUMB-SEQUENCE — escalonado-verde (template str D192-D197)

Princípio: **construir o novo (dormante) → coexistir → migrar consumidores por camada → aposentar o velho por
último**. "Dormante" = carrega SEM reseed (gate D164 fecha, o reseed do FLIP absorve). "FLIP" = reseed + ritual.

### FASE 0 — Fundação (aditiva, dormante/leaf)
- **C0.1 · `zero<T>` primitiva.** typer (gêmeo de `of_len`) + codegen memset-0 + lower. Aditivo; nada consome
  ainda. **Sem reseed** se emissão não muda C existente (mede byte-diff=0). Risco: BAIXO.
- **C0.2 · inferência de type-arg pelo retorno.** `type_call` liga `expected` a `callee_type_args` vazio.
  Aditivo (só habilita casos hoje rejeitados). **Sem reseed.** Risco: BAIXO-MÉDIO (tocar o resolvedor de
  chamada; medir que nenhuma resolução existente muda).
- **C0.3 · prelúdio `null`/`null<T>`/`NULL`/`NULL<T>` (`src/base/null_surface.tks`).** Novo arquivo `teko::base`,
  injetado VFS. Usa `zero<T>` (C0.1). NADA no `src/` usa ainda → dormante. **Reseed** (nova superfície no
  prelúdio de todo artefato). Ritual. Risco: MÉDIO (colisão de nome/provenance — `reserved_type_name` já
  reserva `null`? verificar e estender).
- **C0.4 · prelúdio `error` interface + `Err` (`src/base/error_surface.tks`).** Interface + classe concreta +
  fábrica. `error` já é reservado por provenance. Dormante (o `Error{}` variante ainda é o que roda).
  **Reseed.** Ritual. Risco: MÉDIO.
- **C0.5 · expurgo de constraint `ConstraintOr` + `notnull` (folha, ISOLADO).** Ver §7.1 — remoção pura de
  maquinaria não-usada (ZERO call-site na árvore). Independente do resto do arco; pode landar cedo. **Reseed**
  (muda o parser/checker/tkb → toca o C emitido). Risco: BAIXO (nada consome; o compilador enumera qualquer
  ramo morto). Fixture: `constraint_or_reject` (§6).

### FASE I — nulável: `T | null` → `null<T>`
- **C1.1 · lane errors.As (downcast por identidade-de-vtable).** Autônoma (roadmap lane 1): `LGlobalAddr`+
  `ICmpEq` comparando `value.vtable` com `&tk_vt_<Class>_<Iface>`; regra de tipagem no braço de `match` cuja
  etiqueta é classe conforme (a análise já existe — `match.tks:57-69`). Serve TODA interface. **Zero
  crescimento.** Reseed. Ritual. Risco: MÉDIO. (Vem cedo porque o downcast de `Err`/domain-errors depende
  dela na Fase II — adianta-se o necessário.)
- **C1.2 · ponte `type_eq` `Null{} ≡ Named{"teko::base::null"}` (DORMANTE).** Idioma já usado por Error/Null
  (`type.tks:127/136`). QN **INLINE** (lei D193 — nunca fn que retorna `str`). Nada produz o Named ainda.
  **Sem reseed** (absorve no FLIP). Risco: MÍNIMO.
- **C1.3 · predicados de dispatch reconhecem AS 2 FORMAS.** `is_null_type(t)` reconhece `Null{}` E
  `Named{null-QN}`; roteia os `match {Null=>}` de dispatch (codegen niche/tag, lower, tkb, typer). Byte-
  idêntico (nada produz o Named). Sub-crumbs por camada (codegen/lower/emit/checker). Reseed só se a camada
  medir drift. Risco: MÉDIO (codegen niche).
- **C1.4 · FLIP.** `null` deixa de ser `TokenKind::Null`/NullLit-expr e vira **TIPO** reservado; `T | null`
  resolve para `null<T>` (não mais `Variant` com membro `Null`); `NULL()`/`null{}` constroem o ausente. Os
  predicados de C1.3 reconhecem; a ponte C1.2 casa os `Null{}` residuais. **Muda o C emitido.** **Reseed
  obrigatório.** Ritual. Risco: **ALTO** (é o flip; + a mudança de rep niche→classe — §5.4).
- **C1.5 · varredura de consumidores `T | null`/`match{null=>}`/`x=null` (árvore-inteira).** Converter
  retornos, campos, params e sítios de match para `null<X>`/`.has_value()`/`.value()`/`NULL()`. Guiado por
  grep; NO-PUSHES na reescrita em massa. **Reseed.** Ritual. Risco: MÉDIO-ALTO (volume: 359 declarações +
  fatia dos 2937 consumos).
- **C1.6 · EXPURGO `Null{}` + niche-null.** Remove `Null{}` do enum `Type` + macro; `cg_variant_has_null`
  (2255), `cg_union_normalize_null` (2265), `cg_niche_null_*` (2318-2351), `named_type_is_null` (1164),
  `sole_sibling_is_null` (1232); ponte C1.2. O compilador ENUMERA o morto (D125/D181). **Reseed final da
  fase.** Ritual + grep zero-ref. Risco: MÉDIO-ALTO.

### FASE II — error: `T | error` → `(null<T>, error)` + interface
- **C2.1 · fábrica: `error{msg}`/`err_loc`/`err_typed` → `Err::of`/`Err::loc`/`Err::typed`.** Varredura de
  construção (árvore-inteira). Remove os name-detects `err_loc`/`err_typed` (`scope.tks:548/552`) e o inline
  `tk_error_loc`/`tk_error_types`/`tk_error_make` (codegen) — caem no genérico via fábrica de superfície.
  **Reseed.** Ritual. Risco: MÉDIO (volume de construção; `err_typed`/`err_loc` têm ~32 sítios diretos + o
  `error {` tem 466 no typer sozinho).
- **C2.2 · migração de retorno falível `T | error` → `(null<T>, error)` (POR MÓDULO, coupled com callers).**
  A grande. Cada fn `f(): T | error` → `f(): (null<T>, error)`; seus `return v`/`return e` → `return v, NULL()`
  / `return NULL(), e`; e TODO caller `match f() { T as x => …; error as e => … }` → `var x, e = f(); if
  e.has_value() {…} else {…}`. Encenar **por módulo folha-para-raiz** (encoding/crypto/io antes de checker/
  codegen), fixpoint como guarda. Domain-errors juntam aqui: `class CryptoError & error`, `T | CryptoError` →
  `(null<T>, error)` (downcast via C1.1 onde o tipo concreto importa). **Reseed por lote.** Ritual. Risco:
  **ALTO** (1637 retornos + fatia grande dos 2937 consumos; é o coração do byte-mover). Multi-retorno já
  existe → sem máquina nova, mas o VOLUME é a campanha.
- **C2.3 · FLIP `error` embutido→interface (lane 4 do roadmap) — SÓ APÓS MEDIR VAZÃO.** `Error{}`
  construção/leitura passam pela vtable do `Err`. **Portão-duro D68:** as ~118 leituras de campo do caminho
  quente de diagnóstico viram chamadas indiretas — MEDIR o pico ANTES; se subir, **estagiar** (mantém `Err`
  concreto embutido, adia a indireção até ser neutra). **Reseed.** Ritual. Risco: **ALTO** (vazão + rep).
- **C2.4 · EXPURGO `Error{}` variante.** Remove `Error{}` do enum + macro + ctype `tk_error` name-detect
  (codegen 1995/2026/2831); ponte type_eq de Error. **Reseed.** Ritual + grep zero-ref. Risco: MÉDIO-ALTO.

### FASE III — abolir o `Variant` genérico + famílias AST  ⚠️ FORKED (§8)
- **C3.x** (desenho, NÃO executável até ratificação): as famílias `@Type()`/`@Statement()`/`@Decl()`/
  `@Pattern()`/`@TypeExpr()`/`@ConstraintExpr()`/`@Item()` migram de macro-união-`Variant` para **hierarquia
  de interface/OO com downcast** (errors.As de C1.1 é a fundação). Só então `Variant` + `UnionRepr`
  (Niche/InlineTag/BoxInArena/TagPtr) + `emit_variant_wrap*` (union-injection) + `cg_type_is_niche_able`
  saem. **Tensão de memória severa sob reclaim-0%** (todo nó AST vira fat-pointer boxeado). **HALT §8-a.**

**Reseeds/rituais:** C0.3, C0.4, C0.5, C1.1, C1.4, C1.5, C1.6, C2.1, C2.2(por lote), C2.3, C2.4. **Total Fase
0-II: ~15 crumbs** (com sub-crumbs em C1.3 e C1.5/C2.2 por módulo). C0.5 (constraints) é folha-independente e
pode landar a qualquer momento.

---

## 5. Ordem, dependências, riscos, memória

### 5.1 Grafo de dependências
```
C0.1 zero<T> ─┐
C0.2 ret-infer┼─► C0.3 null<T> ──► C1.2 ponte ─► C1.3 predicados ─► C1.4 FLIP null ─► C1.5 sweep ─► C1.6 expurgo Null
              └─► C0.4 error-iface                                                                        │
C1.1 errors.As (autônoma, cedo) ───────────────────────────────────────────────────────────────────────┤
                                                                                                          ▼
                       C2.1 fábrica Err ─► C2.2 (null<T>,error) por módulo ─► C2.3 FLIP iface(medir) ─► C2.4 expurgo Error
                                                                                                          ▼
                                                              [HALT §8-a] ─► FASE III Variant + famílias AST
```
`null<T>` (Fase I) ANTES de `(null<T>, error)` (Fase II) — o retorno falível literalmente contém `null<T>`.
`errors.As` (C1.1) cedo — o downcast de `Err`/domain-errors depende dela.

### 5.2 O que é byte-mover de RISCO
- **C1.4 (FLIP null)** e **C2.2/C2.3 (error)** são os byte-movers de maior risco: mudam rep + emissão em massa.
  Mitigação: predicados-que-reconhecem-ambos ANTES do flip (grep de `Null =>`/`error as` remanescente em
  dispatch = 0 antes do flip); ordem estrita; medir byte-diff do `teko.c` por camada.
- **C2.2** é o maior VOLUME (1637+consumos). Mitigação: por módulo folha→raiz, fixpoint por lote, mecânico
  guiado por grep, NO-PUSHES na reescrita.

### 5.3 Custo de memória — NEUTRO na maioria, mas DOIS pontos de crescimento (D68)
1. **`null<T>` perde o NICHE.** Hoje `T | null` para T niche-able (ponteiro/fat-pointer/`Named` de classe)
   usa **niche** (`cg_type_is_niche_able`, `cg_union_niche_member`): ZERO byte extra (o ponteiro-null é o
   sentinela). `null<T>` como `{ _value: T, _setted: bool }` **adiciona o bool + padding** (~8 bytes) por
   ocorrência → para os muitos `Named | null`/`ptr | null` do compilador, é CRESCIMENTO. **Viola o ratchet.**
   **Mitigação desenhada (§8-c, decisão do dono):** ou (a) o compilador reconhece `null<T>` com T niche-able
   e ELIDE o `_setted` reusando o niche de T (mantém uma fatia da máquina de niche viva, contradizendo o
   expurgo total do niche) — recomendado; ou (b) aceitar o crescimento e compensar; ou (c) o dono ratifica o
   patamar. **Sem (a), a Fase I cresce o pico.**
2. **`error` interface (C2.3) = vazão.** As ~118 leituras do caminho quente de diagnóstico → chamadas
   indiretas por vtable + boxing do fat-pointer. Já governado por D68/D198-D3: **medir e estagiar**. Se subir,
   `Err` concreto embutido fica até a conversão ser neutra.

`(null<T>, error)` multi-retorno em si é neutro (struct manglada, mesma classe do que existe). O restante da
migração (construção/consumo) é preservante.

---

## 6. Fixtures de regressão (SÓ path que o self-build NÃO exercita)

Lei dura (CLAUDE.md): **nada de teste afirmativo para o que o self-build/fixpoint exercita** — o compilador
usa `null<T>`, `(null<T>,error)`, downcast e a fábrica ao se compilar. Só oráculos `.tkr` isolados de
**rejeição/erro** (path que o self-build nunca dirige):

- **`null_reserved_reject`** — `type null = i32` num arquivo de USUÁRIO (ns não-base) → `EXPECT_COMPILE_FAIL`
  "type 'null' is reserved" (exercita `check_reserved_type_redefs` no caminho de falha).
- **`error_reserved_reject`** — idem `type error = struct{}` de usuário.
- **`null_value_absent_panic`** — `NULL<i64>().value()` → **pânico em runtime** `file:line:col: value() on
  absent null<T>` (guard de ausente; o compilador nunca dispara, é backstop do usuário). 1 oráculo `.tkr` de
  runtime-panic.
- **`fallible_use_before_check_panic`** — `var v,e = f_que_falha(); v.value()` sem checar `e.has_value()` →
  pânico (mesmo guard; prova que o footgun-do-Go morre sem análise de fluxo).
- **`nonexistent_method_on_null`** — `NULL<i64>().foo()` → `no such method 'foo' on struct 'null'` (caminho de
  erro de `type_method_call`).
- **`interface_downcast_miss`** (C1.1) — `match e { WrongClass as w => … }` onde `WrongClass` não conforma →
  `'WrongClass' does not implement 'error'` (`discriminant_mismatch_error`, `match.tks:71-79`, caminho de
  falha).
- **`constraint_or_reject`** (C0.5) — `fn f<T: A | B>(x: T)` → `EXPECT_COMPILE_FAIL` (o `|` de constraint
  deixou de parsear; mensagem-de-termo nova). E `<T: notnull>` idem. Exercita o caminho de rejeição do parser
  de constraint que o self-build (sem esses construtos) nunca dirige.

NENHUM oráculo afirmativo além destes. Ao drenar, RECUSAR qualquer `.tkt`/`.tkr` novo não-nomeado aqui.

---

## 7. Decisões executáveis + a única ratificação pendente

### 7.1 Constraints = asserção/narrowing — DECIDIDO (D199, ruling do dono; NÃO é proposta)
Constraint passa a ser **asserção/narrowing positivo** (mais restritiva: assere o que T satisfaz, sem
alternativa). Reescrita CONCRETA:

- **`ConstraintOr` (`<T: A | B>`) — DELETAR.** Remoção pura: **ZERO call-site na árvore** (grep de `<T: A | B>`
  em signatures = 0; sem fixture). Sítios da MAQUINARIA a remover:
  - `parser/ast.tks:158` `type ConstraintOr` + `ast.tks:163` membro `ConstraintOr` da macro `ConstraintExpr()`.
  - `parser/parse_decl.tks:133-136` (construção do `ConstraintOr` + os dois erros "notnull não pode ser `|`
    alternativa") e `parse_decl.tks:110` (mensagem que cita "a disjunction").
  - `checker/resolve.tks:523` e `602` (braços `ConstraintOr`), `checker/monomorph.tks:63`
    (`constraint_satisfied` disjunção), `checker/merge.tks:52` (`constraint_equal`).
  - `emit/tkb_write.tks:261` (tag 2), `tkb_read.tks:610`, `tkb_frame.tks:225`.
- **`notnull` (`ConstraintNotNull`) — DELETAR.** Redundante (nulável agora é `null<T>` explícito; um `T` nu já
  é não-nulável). **ZERO call-site real** (só aparece na própria maquinaria/mensagens do parser). Sítios:
  - `parser/ast.tks:160` `type ConstraintNotNull` + `ast.tks:163` membro da macro.
  - `parse_decl.tks:106` (parse do termo `notnull`), `115-116` (`is_notnull_constraint`), `133/135` (os erros).
  - `resolve.tks:531` e `616`, `monomorph.tks:67` + `86` (`constraint_notnull_satisfied`), `merge.tks:54`.
  - `tkb_write.tks:262` (tag 4), `tkb_read.tks:613`, `tkb_frame.tks:226`.
- **INTACTO:** form word, service lifetime, bound de interface/type/slice, `error`→"implementa a interface
  error", e o `&` (AND/interseção). A mensagem de erro-de-termo de constraint (`parse_decl.tks:110`) reescreve
  para o novo conjunto (sem "disjunction", sem "notnull"): estilo compilador padrão.
- **Bump de tag tkb:** tags 2 (`ConstraintOr`) e 4 (`ConstraintNotNull`) saem do serializador de constraint —
  reindexar os tags restantes num único lote (round-trip lê/escreve o novo conjunto). Cabe no C0.5.

Executa em **C0.5** (folha, isolado, reseed). Risco BAIXO — remoção de maquinaria não-consumida; o compilador
enumera qualquer ramo morto ao auto-compilar (D125/D181).

### 7.2 (d) Modelo de retorno falível — RECOMENDO `(null<T>, error)` (ÚNICA ratificação-surface pendente)
A máquina EXISTE (multi-retorno + destructuring, `parse_decl.tks:227`/`typer.tks:4022`), ZERO peça nova.
Convenção: sucesso `(v, NULL())`, falha `(NULL(), err)`; `v.value()` de uma falha PANICA (reuso do guard de
ausente) → mata o footgun do zero-value do Go **sem análise de fluxo**. Alternativa (D199 anotou): `T`-inválido-
até-checar em compile-time (definite-assignment) — mais forte (erro de compilação, não pânico) porém exige
análise de fluxo NOVA (cara, byte-mover próprio no checker). **Recomendo (d).** — o dono ratifica a convenção.

---

## 8. FORKS GENUÍNOS — HALT (não deliberados; enunciados curtos, sem inventar decisão)

**(a) FAMÍLIAS DE AST = Variants — a abolição TOTAL do `Variant` alcança a espinha do compilador.**
`@Type()`/`@Statement()`/`@Decl()`/`@Pattern()`/`@TypeExpr()`/`@ConstraintExpr()`/`@Item()` são macro-uniões
que expandem para `Variant` (`type.tks:94`, `resolve.tks:1331`). D199 manda o `Variant` + toda a maquinaria
APOSENTAR, mas NÃO diz o que essas famílias VIRAM. O caminho natural é **hierarquia de interface/OO com
downcast** (a fundação errors.As de C1.1; casa com o modelo OO D196 virtual/override). **Tensão dura:** hoje
cada nó AST é **valor inline**; como interface vira **fat-pointer boxeado** → sob reclaim-0% (D130 ainda não
live), a AST inteira boxeada pode **explodir o pico** (a mesma vazão da lane-4-error, multiplicada por milhões
de nós). **Pergunta ao dono:** a Fase III (famílias AST → OO/interface) entra NESTE arco, ou é onda casada com
o modelo OO de membros (D196) e o modelo-de-memória-por-escopo (D130, para o boxing não vazar)? Enquanto não
ratificado, o arco entrega **Fase 0-II (error+null)** e PARA antes de tocar as famílias AST.

**(b) NULÁVEL DE REFERÊNCIA (`ref T | null`).** Hoje `sole_sibling_is_null` permite `ref T | null`
(`resolve.tks:1246`). Mas referência **não pode ser campo de classe** (lei: "a reference cannot be stored in a
struct/variant/collection", `resolve.tks:1247`) → `null<ref T>` seria ilegal (o `_value: ref T` viola a lei).
**Pergunta ao dono:** nulável-de-referência (a) some (usar `ptr` + checagem explícita), (b) vira uma forma
dedicada `null<ref T>` com carve-out na regra de campo, ou (c) o `ref`-nulável é sempre um `ptr` opaco? Não
deliberado.

**(c) `null<T>` NICHE vs patamar de memória (liga ao ratchet D68).** Converter o niche `T|null` (0 bytes
extra para T niche-able) em `null<T>`-classe (`{T, bool}`, +~8 bytes) **cresce o pico** — viola o ratchet
estrito. Manter uma fatia da máquina de niche viva (o compilador elide `_setted` quando T é niche-able)
contradiz o "expurgo total do niche". **Pergunta ao dono:** (a) `null<T>` ganha niche-interno (mantém
`cg_type_is_niche_able` para T niche-able, expurga só o resto), (b) aceita o crescimento e compensa noutro
eixo, ou (c) ratifica o patamar maior? Recomendo (a). Não deliberado — é decisão de ratchet.

---

## 9. Achados adjacentes — REPORTADOS (não viram issue)
- **`error.file`**: zero leituras/escritas na árvore (confirmado; roadmap §Achados-3). Ao surfacear, o método
  `file()` nasce sem chamador — não recriar.
- **`err_typed`/`err_loc` = name-detect (D187-ilegítimo).** Já eram alvo do expurgo de builtin; o arco os mata
  de graça (viram fábrica de superfície). Registrar como ganho colateral.
- **Multi-retorno "não é tuple value"** (`typer.tks:1989`): `(A,B)` só vale como RHS único de bind ou de
  return — logo `(null<T>, error)` NÃO cria tuple de 1ª classe (bom: menos superfície nova). Documentado.
- **`CryptoError` e domain-errors** (138 sítios) precisam implementar `error` no MESMO lote de C2.2 do módulo
  crypto — senão o `(null<T>, error)` do crypto não fecha. Anotado no crumb.
