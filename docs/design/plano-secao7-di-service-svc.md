# Plano — §7: Injeção de dependência `service`/`svc` com lifetimes de arena

> **Status:** DESIGN. Read+design (nenhum código de produto, nenhum reseed). Este documento é O
> ARTEFACTO; o único commit é ele próprio.
> **Fonte de lei:** `mudancas-superficie-0.3.1.md` §7 (linhas 226-273 — SAI/ENTRA/ENTRA/RESOLVE/
> exemplo). Backend do lifetime-de-arena: `arena-especificacao-unica-0.3.1.md` (Doc 1) §8. Constraint
> `T: service`: §9.2b (forward-dep — tratada abaixo). Precedente de intrínseco comp-time por
> reconhecimento-no-typer sem gramática nova: §5 (`plano-secao5-marshall.md`, `type_ptr_wrap`/
> `type_ptr_unwrap`). Precedente de remoção-de-superfície-preservando-o-mecanismo: §6
> (`plano-secao6-aposentar-unsafe.md`, "preserva `is_unique_at`, remove os probes de superfície").
> **Branch:** `fix/retirement` @ `8954d26e` (generics completos + §5 opaco `ptr`/`uptr` + §6 `unsafe`
> aposentado + bare block).
> **Rulings do dono incorporados:** (i) conflito mesmo-tipo-mesma-chave = ERRO DE COMPILAÇÃO (chaves
> distintas coexistem; nunca "último vence" silencioso); (ii) resolução POR THREAD — `singleton` na
> raiz da thread, `scoped` na sub-raiz da thread — é a faceta de arena, Doc 1 §8; (iii) o `service` é
> SEMPRE selado (não pode `virtual`/`abstract`; `sealed` é redundante e não se escreve).

Este plano tem DUAS partes nítidas, como o §5. **O implementador executa a PARTE A (superfície).** A
PARTE B (o binding lifetime→slot-de-arena por-thread) é o Doc 1 §8, que preenche a costura que a Parte A
deixa aberta com o corpo simplest-correct.

---

## 0. O que o §7 pede (recap normativo) e a REGRA DE FASE

**SAI (superfície da era-anotação):** a DI por anotação — `#singleton`/`#scoped`/`#transient` sobre
`type` (`TypeDecl.di_kind`), o overlay `#inject(...)` sobre `fn` (`Function.has_inject`/`injects`), o
campo-chave inerte (`has_di_key`/`di_key`), a pass inteira `src/checker/di.tks` (`choose_factory`,
`build_di_registry`, `check_inject_overlays`, `di_lower_use`, materializadores), e o `#singleton` de
BINDING (residência-raiz, `parse_stmt.tks` → `residence.tks`). Cobertura migra, não se perde.

**ENTRA:** a keyword `service` (SEMPRE selada); os três lifetimes `singleton`/`scoped`/`transient`
(agora escritos APÓS o nome do serviço, não como `#atributo`); um ctor `static`; os intrínsecos de
COMP-TIME `svc<T: service>(key: str | null = null): T` e `has_svc<T: service>(key: str | null = null):
bool` (o compilador substitui o call-site inline por-lifetime; SEM ABI de runtime). Chave ausente em
`svc` = `panic`; `has_svc` checa antes.

**RESOLVE:** resolução determinística em comp-time por uma TABELA ESTÁTICA NOMEADA (tipo, chave) →
provedor, construída na pré-walk (RE-FERRAMENTA a `build_di_registry` existente — NÃO é uma pass nova;
ver §GENUÍNO-1). Conflito mesmo-tipo-mesma-chave = erro na construção da tabela. **Regra de escape:** um
VALOR de serviço nunca é armazenado em campo, passado como argumento, nem retornado em código de usuário
— forçando o re-`claim` explícito via `svc<T>(...)`; o backend é isento; o que segura o serviço fica
arena-bounded.

### REGRA DE FASE (idêntica ao §5)

O §7 faz a SUPERFÍCIE AGORA: keyword `service`, lifetimes, `svc`/`has_svc` comp-time, regra de escape,
erro de conflito, remoção da DI-anotação. O **binding lifetime→slot-de-arena por-thread** (`singleton`→
raiz-da-thread, `scoped`→sub-raiz-da-thread) é o Doc 1 §8. Onde o lowering comp-time do `svc` tem de
colocar o valor num slot de arena, a Parte A deixa uma COSTURA NOMEADA — uma função de codegen cujo
corpo a Parte A preenche com o mais-simples-correto (slot na raiz-do-PROGRAMA / frame-da-função) e o Doc
1 §8 troca pelo slot raiz-da-thread / sub-raiz-da-thread. É o análogo direto da costura
`emit_ptr_wrap_guard` do §5.

---

## 1. Blast radius MEDIDO (sítios semânticos REAIS; o grep sobre-conta)

Bruto: `di_kind`/`DiKind`/`di_key` ~145 hits, `#singleton|#inject|...` ~83 hits — a maioria é a PRÓPRIA
implementação da pass DI, doc-comment, ou serialização. **Achado decisivo:** o corpus self-host `.tks`
usa ZERO anotações DI (`grep` de `#singleton`/`#inject`/`#scoped`/`#transient` fora de strings/comentários
em `src/**/*.tks` = 0). Só os testes `.tkt` exercitam a DI antiga (§7). Logo a migração de USO é toda em
`.tkt` (§7), e a remoção de MAQUINARIA não tem chamador de corpus a re-rotear.

### (a) ADICIONAR — keyword `service` + lifetimes

| # | sítio | arquivo:linha | ação |
|---|---|---|---|
| a1 | `keyword_kind` (mapa palavra→TokenKind); `class` = `TokenKind::Class` | `lexer/lexer.tks:331,360` | ADICIONAR reconhecimento de `service` → `TokenKind::Service` (ver Decisão D1: reservada) |
| a2 | enum `TokenKind` (Class no `:170`) | `lexer/token.tks:170` | ADICIONAR variante `Service` (apêndice — ordinal no fim, precedente `Variant`/`Class`) |
| a3 | dispatch de type-body (`trait`/`enum`/`variant`/`class`) | `parser/parse_decl.tks:746-750` | ADICIONAR branch `service` → `parse_service_body` |
| a4 | `parse_class_body` (selado default; `& I …`; membros) | `parser/parse_decl.tks:602-644` | REUSAR: `parse_service_body` = `parse_class_body` + lifetime-após-nome + `is_service` |
| a5 | lifetimes `singleton`/`scoped`/`transient` como palavras contextuais NO SLOT de lifetime | `parser/parse_decl.tks` (novo, junto a a3) | ADICIONAR `parse_service_lifetime` (contextual: só após o nome do serviço; ver D2) |
| a6 | novo enum de lifetime + campo no `TypeBody`/`TypeDecl` | `parser/ast.tks` (novo, junto a `DiKind` `:405`) | ADICIONAR `ServiceLifetime = enum { Singleton; Scoped; Transient }` + marca `is_service` no corpo de classe |

### (b) ADICIONAR — `svc`/`has_svc` comp-time + tabela-de-conflito + regra de escape

| # | sítio | arquivo:linha | ação |
|---|---|---|---|
| b1 | intrínsecos reconhecidos NO typer antes da resolução ordinária (`type_ptr_wrap`/`type_ptr_unwrap`) | `checker/typer.tks:990-1065` (padrão) | ADICIONAR `type_svc` + `type_has_svc` (mesmo molde, consomem `callee_type_args`) + dispatch em `type_call` |
| b2 | `Env` carrega `di: DiRegistry` + `with_di`/`with_injects` | `checker/scope.tks:72,84-90` | RE-FERRAMENTA: `di: DiRegistry` → `svc: ServiceRegistry` (mesmo slot; `injects` some) |
| b3 | pré-walk que constrói a tabela (`build_di_registry` + conflito) | `checker/di.tks:79-122` | RE-FERRAMENTA → `build_service_registry`: entradas `(tipo canónico, chave)`; conflito mesmo-tipo-mesma-chave = erro; chaves distintas coexistem |
| b4 | análise de escape existente (points-to, escaping-vars) | `checker/escape.tks` (`fn_escaping_vars` `:342` etc.) | ADICIONAR predicado type-directed "valor-de-serviço nos sinks store/arg/return" (novo, pequeno; NÃO é análise de fluxo — ver §GENUÍNO-2) |
| b5 | costura de slot-de-arena por-lifetime (`di_scope_expr`) | `codegen/codegen.tks:9814-9820` | RE-FERRAMENTA → `svc_scope_expr` (a COSTURA nomeada; ver §5) |
| b6 | id de tipo determinístico (FNV-1a) — a chave do slot | `checker/di.tks:373` (`di_type_id`) | MANTER (renomear opcional `svc_type_id`); é o tag do slot, reusado pela costura |

### (c) REMOVER — a DI por anotação da era anterior

| # | sítio | arquivo:linha | ação |
|---|---|---|---|
| c1 | `src/checker/di.tks` INTEIRO exceto `di_type_id` | `checker/di.tks:1-372` | DELETAR `DiProvider`/`DiInjectBind`/`build_di_registry`/`choose_factory`/`check_inject_overlays`/`di_lower_use`/`is_di_materializer_call`/`di_materializer_name`… ; PRESERVAR `di_type_id` (→ `svc_type_id`) |
| c2 | campos AST DI | `parser/ast.tks:296` (`Binding.di_kind`), `405` (`DiKind`), `407-411` (`InjectBinding`), `442-443` (`Function.has_inject`/`injects`), `572-574` (`TypeDecl.di_kind`/`has_di_key`/`di_key`) | DELETAR (sweep dos literais — ver c-sweep) |
| c3 | parse dos atributos DI de `type` | `parser/parse_decl.tks:896-953` (`parse_decl_attributes` DI-arms), `974-977` (`lifetime_kind_of`) | DELETAR os arms `#singleton`/`#scoped`/`#transient`/`#inject`/`@key` de TIPO/FN |
| c4 | parse do `#singleton` de BINDING (residência) | `parser/parse_stmt.tks:217-248` (`parse_binding_di`) | DELETAR (o binding-`#singleton` é superfície da era-anotação; residência-raiz passa a ser faceta do lifetime de serviço — ver D4) |
| c5 | literais `di_kind = DiKind::None` em Binding | `parser/loop_head.tks:84,98,407`; `parser/result.tks:32` | SWEEP (remover o campo) |
| c6 | consumo de `di_kind` na residência | `checker/residence.tks:268` (`is_singleton = b.di_kind==Singleton`), `166-167` (`residence_tier`) | RE-FERRAMENTA: `residence_tier` MANTÉM o param `is_singleton` (mecanismo de memória preservado, precedente §6); a FONTE do bool vira `false` agora (nenhum binding força raiz via anotação retirada) — o `service singleton`→raiz-da-thread liga-se pela costura no Doc 1 §8 |
| c7 | materializadores + exclusões `di_kind==Singleton` no codegen | `codegen/codegen.tks:4255,8050,8294,8585,9659-9820,11512,12327,12336` | DELETAR os materializadores/protos/dispatch DI e as exclusões de residência baseadas em `di_kind`; PRESERVAR `svc_scope_expr` (costura, de `di_scope_expr`) |
| c8 | serialização `.tkb` de `di_kind`/`has_di_key`/`di_key` | `emit/tkb_read.tks` (6 hits), `emit/tkb_write.tks` (arms correspondentes) | REMOVER I/O; **BUMPAR `TKB_EXPR_VERSION`+`TKB_PROGRAM_VERSION`** (`emit/tkb_frame.tks`) — procedimento-padrão R2 (§6) |
| c9 | consumidores residuais de `di_kind`/`DiKind`/materializador | `checker/collect.tks` (6), `comptime_fold.tks` (2), `consteval.tks` (1), `monomorph.tks` (1), `resolve.tks` (3), `synth.tks` (1), `tast.tks` (2), `typer.tks` (3) | SWEEP (remover os ramos/campos DI; a rede é o próprio checker rejeitar literal com campo em falta no dry build) |

---

# PARTE A — SUPERFÍCIE (implementa AGORA — 1 reseed, ZERO runtime C)

## A.1 Formas Teko que o implementador ADICIONA (full-Javadoc, copiar verbatim)

Declaração de serviço (selado por construção; lifetime após o nome; ctor `static`):
```teko
/**
 * Um serviço — uma classe SELADA (nunca `virtual`/`abstract`) com um lifetime de arena, resolvida em
 * comp-time por `svc<T>()`. O lifetime (`singleton`/`scoped`/`transient`) escreve-se APÓS o nome e
 * governa em que slot de arena o valor reside (Doc 1 §8, por-thread). A construção é o único `static`
 * `ctor` do corpo (o análogo do factory da DI antiga).
 *
 * @since 0.3.1
 */
service Clock singleton {
    /**
     * Constrói a instância do serviço — o ctor `static` que o `svc<Clock>()` inlina no call-site.
     * @return self  a nova instância de `Clock`
     */
    static fn ctor(): self { return Clock { } }

    /**
     * @return i64  o instante corrente (stub determinístico neste exemplo)
     */
    fn now(): i64 { return 0 }
}
```

Os dois intrínsecos comp-time (reconhecidos NO typer por forma-de-callee, como `__wrap<T>` do §5 —
SEM gramática nova; forma-2 `f<T>()` já existe):
```teko
/**
 * Resolve o serviço `T` em COMP-TIME e devolve a instância do slot do seu lifetime (singleton →
 * raiz-da-thread; scoped → sub-raiz-da-thread; transient → fresca). A `key` opcional desambigua
 * múltiplos provedores de uma interface: sem chave resolve por tipo, com chave constante resolve por
 * nome. `T` DEVE ser um `service` (constraint `T: service`, §9.2b — ver Decisão D3). Chave ausente
 * (nenhum provedor registado sob `(T, key)`) = `panic` no call-site.
 *
 * @param T          o tipo de serviço a resolver (type-arg; o intrínseco exige exatamente um)
 * @param key        a chave constante opcional; `null` = resolver por tipo
 * @return T         a instância resolvida (arena-bounded; nunca armazenável/passável/retornável)
 * @throws panic     quando não há provedor registado sob `(T, key)`
 * @since 0.3.1
 */
// intrínseco: svc<T: service>(key: str | null = null): T

/**
 * Verdadeiro sse existe um provedor registado sob `(T, key)` — o guarda para chamar `svc<T>(key)` em
 * segurança quando a presença não é garantida. Resolvido em comp-time (a tabela é estática).
 *
 * @param T          o tipo de serviço a checar
 * @param key        a chave constante opcional; `null` = por tipo
 * @return bool      true sse há provedor sob `(T, key)`
 * @since 0.3.1
 */
// intrínseco: has_svc<T: service>(key: str | null = null): bool
```

## A.2 Fns internas que o implementador ACRESCENTA / TOCA (Parte A)

- **Acrescenta (parser):** `parse_service_body` (= `parse_class_body` + `parse_service_lifetime` após o
  nome + marca `is_service`); `parse_service_lifetime(tokens, pos): Parsed<ServiceLifetime>` (contextual
  `singleton`/`scoped`/`transient`). REJEITAR `virtual`/`abstract` num `service` (é selado por lei).
- **Acrescenta (checker):** `type_svc(c, env, table): TExpr | NotSvcOp | error` e
  `type_has_svc(...)` — molde de `type_ptr_wrap` (`typer.tks:1030`): exigem 1 type-arg, resolvem `T`,
  exigem `T` service (D3), leem a tabela `env.svc`, materializam `panic`/`false` no miss;
  `build_service_registry(prog, table): ServiceRegistryOutcome` (re-ferramenta de `build_di_registry`,
  conflito `(tipo,chave)`); o predicado de escape `svc_value_escapes(...)` (b4).
- **Acrescenta (codegen):** `emit_svc` / `emit_has_svc` (lowering inline por-lifetime) + a COSTURA
  `svc_scope_expr` (A.3).
- **Toca (checker):** dispatch em `type_call` (branch `svc`/`has_svc` ANTES da resolução ordinária);
  `residence_tier` mantém `is_singleton` mas a fonte vira `false` (c6); `Env.di`→`Env.svc` (b2).
- **NÃO toca:** `src/runtime/teko_rt.{c,h}` (frozen) e `src/runtime/teko_rt.tks` — ZERO edição (§6).

## A.3 Codegen — e a COSTURA que o Doc 1 §8 vai ligar (o ponto central)

`svc<T>()`/`has_svc<T>()` são intrínsecos reconhecidos no typer (como `__wrap`/`__unwrap`), lowered
inline por-lifetime. O lowering REUSA a maquinaria de materialização de instância já existente
(`emit_di_materializer` fazia exatamente isto: build-via-factory + cache-por-lifetime + `tk_region_*`).
A ÚNICA parte que o Doc 1 §8 troca é ONDE o slot vive:

```
svc<T>()  →  ({ <slot> = svc_scope_expr(<lifetime de T>);
                tk_region_lookup(<slot>, <svc_type_id(T,key)>) ?: build_via_ctor_and_register(...); })
```

**A costura mora numa função de codegen (para o Doc 1 §8 achar):**
`svc_scope_expr(lifetime: parser::ServiceLifetime): str` em `src/codegen/codegen.tks` (derivada de
`di_scope_expr`, `:9814-9820`):
- **Parte A (simplest-correct AGORA):**
  - `Singleton` → `"tk_region_root()"` (raiz do PROGRAMA agora)
  - `Scoped` → a expressão de região do frame/bloco envolvente (`cg_enclosing_region_expr`, já existe)
  - `Transient` → sem slot (build fresco)
- **Doc 1 §8 troca DUAS linhas:** `Singleton` → slot da RAIZ-DA-THREAD; `Scoped` → slot da
  SUB-RAIZ-DA-THREAD. Nenhuma mudança de assinatura, typer, tipo ou fixture entre A e B — é o sentido
  da fase (idêntico à costura `emit_ptr_wrap_guard` do §5). `svc_type_id` (de `di_type_id`) é o tag do
  slot, materializado em comp-time; a assinatura da costura não muda no Doc 1.

## A.4 Regra de escape (b4) — type-directed, nos 3 sinks

Um `TExpr` cujo tipo é um `service` é REJEITADO quando é: (i) o RHS de um store em campo/atribuição
(`type_assign`); (ii) um argumento de chamada (`type_call`, pass de args); (iii) o valor de um `return`
(`type_return`). Diagnóstico único (D5). Isto NÃO é análise de fluxo — é uma checagem de PROPRIEDADE DE
TIPO nos 3 sítios sintáticos, usando a travessia de `escape.tks` como MOLDE mas não a sua análise de
residência (ver §GENUÍNO-2). Um `var c = svc<Clock>()` (bind local) é PERMITIDO; reusar exige novo
`svc<Clock>()` (o `claim` explícito da lei).

## A.5 `svc`/`has_svc` reusam a chamada genérica forma-2 (CONFIRMADO)

`svc<T>(key)` e `has_svc<T>(key)` parseiam como chamada genérica explícita `f<T>(...)` (forma-2,
construída nesta wave). `type_call` já popula `callee_type_args`; `type_svc`/`type_has_svc` consomem-no
tal como `type_ptr_wrap` consome `mc.type_args`. **NENHUMA gramática de chamada nova.** `svc`/`has_svc`
NÃO são tokens de lexer — são nomes de intrínseco reconhecidos por forma-de-callee em `type_call`
(como `ptr::__unwrap`), então não colidem com identificadores de utilizador fora do slot de callee.

## A.6 Fixtures — Parte A (AUTORADAS; suite NÃO executada — dry build + fixpoint é o gate)

**ACCEPT — oráculo nativo (exit = valor):**
| fixture | exercita | exit |
|---|---|---|
| `svc_singleton_resolves` | `service Clock singleton` + `svc<Clock>().now()` | valor de `now()` |
| `svc_transient_fresh` | `service Rng transient` + dois `svc<Rng>()` são instâncias frescas | contador esperado |
| `svc_scoped_in_block` | `service Buf scoped` resolvido dentro de `{ }` | valor conhecido |
| `svc_keyed_disambiguates` | dois provedores da mesma interface sob chaves distintas; `svc<I>("a")` vs `svc<I>("b")` | soma dos dois |
| `has_svc_true_then_svc` | `has_svc<Clock>()` == true → `svc<Clock>()` | valor |
| `has_svc_false_unregistered` | `has_svc<Ghost>()` == false (sem provedor) | 0 (ramo false) |
| `svc_local_bind_ok` | `var c = svc<Clock>()` (bind local PERMITIDO) | valor |

**REJECT — `EXPECT_COMPILE_FAIL`:**
| fixture | rejeita |
|---|---|
| `service_virtual_rejected` | `virtual service X singleton { }` (serviço não pode ser virtual) |
| `service_abstract_rejected` | `abstract service X singleton { }` |
| `svc_non_service_rejected` | `svc<i64>()` / `svc<PlainStruct>()` (T não é service — D3) |
| `svc_conflict_same_type_same_key_rejected` | mesmo `(tipo, chave)` registado duas vezes (erro de conflito duro) |
| `svc_escape_store_rejected` | `g = svc<Clock>()` (store de valor de serviço) |
| `svc_escape_arg_rejected` | `fun(svc<Clock>())` (passar serviço como argumento) |
| `svc_escape_return_rejected` | `return svc<Clock>()` (retornar valor de serviço) |

**Nota sobre `svc` de chave ausente = `panic`:** com uma chave/tipo NÃO registado em lado nenhum, o
`svc<T>(key)` é um erro de COMP-TIME (não há provedor a inlinar) → REJECT
`svc_missing_key_rejected`. A lei diz "chave ausente = panic"; como a resolução é comp-time e a tabela
é estática, um miss é diagnosticável em comp-time (preferível ao panic de runtime). **Ver D6 (o dono
ratifica: comp-time-error vs panic-de-runtime).** O `panic` de runtime só sobra se a chave for um valor
não-constante — mas a lei exige `key` CONSTANTE, então o miss é sempre comp-time.

## A.7 Sequência de crumbs + reseed (ADICIONAR primeiro, REMOVER por último)

O seed é o `teko` lançado anterior, que entende a DI antiga. O §7 ENSINA `service`/`svc` (idioma novo) e
REMOVE a DI-anotação. Como o corpus `.tks` não usa DI-anotação (medido), os crumbs de REMOÇÃO não
re-rotam nenhum chamador de corpus; o único idioma novo a semear é `service`/`svc`, e o corpus só o usa
nos `.tkt` (que não gatilham o seed). Reseed único no RITUAL FINAL.

- **C0 — [DOC] Este plano.** Banner de aposentadoria não se aplica (não há doc DI antigo — medido:
  nenhum design doc DI existe). Commit deste plano. Doc-only. **Ritual: NÃO.**

### ADICIONAR (superfície nova primeiro — para o corpus poder migrar)

- **C1 — [ADITIVO] keyword `service` + lifetimes + parse.** a1-a6: `TokenKind::Service`;
  `parse_service_body` (= class selado + lifetime-após-nome); rejeitar `virtual`/`abstract` num
  service. **Ficheiros:** `lexer/lexer.tks`, `lexer/token.tks`, `parser/parse_decl.tks`, `parser/ast.tks`.
  **Teach:** `service Name <lifetime> { … }`. **Gate — RITUAL COMPLETO** (parse muda). **Ritual: SIM.**
- **C2 — [ADITIVO] `svc`/`has_svc` comp-time + tabela + conflito + escape.** b1-b6:
  `build_service_registry` (conflito `(tipo,chave)`); `type_svc`/`type_has_svc` + dispatch em `type_call`;
  `svc_scope_expr` (costura, simplest-correct); `emit_svc`/`emit_has_svc`; predicado de escape (A.4);
  `Env.di`→`Env.svc`. **Ficheiros:** `checker/typer.tks`, `checker/scope.tks`, `checker/di.tks` (nova
  `build_service_registry`), `checker/escape.tks`, `codegen/codegen.tks`. **Teach:** `svc<T>()`/
  `has_svc<T>()` + regra de escape + conflito. **Gate — RITUAL COMPLETO** (fixtures A.6 verdes). **Ritual: SIM.**

### SWEEP (migrar o uso da DI antiga nos testes)

- **C3 — [SWEEP, test-corpus] Migrar/remover os `.tkt` que usam a DI antiga.** Ver §7 abaixo — a lista
  medida de `.tkt`. Migrar arms `#singleton`/`#inject`/`choose_factory`/`DiProvider`/`di_materialize`/
  `di_kind` → `service`/`svc` (cobertura REJECT/ACCEPT preservada) OU remover; de-registar casos
  removidos dos `.tkr` referentes e das arrays CORPUS em `scripts/*.sh`. **Ficheiros:** os `.tkt`
  listados, `.tkr`, `scripts/*.sh`. **Gate — RITUAL COMPLETO.** **Ritual: SIM.**

### REMOVER (a DI-anotação por último)

- **C4 — [REMOÇÃO] Deletar `src/checker/di.tks` (exceto `svc_type_id`) + os campos AST DI.** c1-c5, c9:
  deletar a pass DI antiga; remover `TypeDecl.di_kind`/`has_di_key`/`di_key`, `DiKind`, `InjectBinding`,
  `Function.has_inject`/`injects`, `Binding.di_kind`; sweep dos literais; remover ramos DI em
  collect/comptime_fold/consteval/monomorph/resolve/synth/tast/typer; parse de atributos `#singleton`/
  `#inject`/`@key` de tipo/fn (c3) e o `#singleton` de binding (c4). PRESERVAR `svc_type_id` (de
  `di_type_id`) e o param `is_singleton` de `residence_tier` (mecanismo de memória; fonte vira `false`).
  **Ficheiros:** `checker/di.tks`, `parser/ast.tks`, `parser/parse_decl.tks`, `parser/parse_stmt.tks`,
  `parser/loop_head.tks`, `parser/result.tks`, `checker/{collect,comptime_fold,consteval,monomorph,resolve,synth,tast,typer,residence}.tks`,
  `codegen/codegen.tks`. **Un-teach:** `#singleton`/`#inject`/`@key` deixam de parsear. **REJECT-fixtures**
  `singleton_annotation_rejected`, `inject_annotation_rejected`, `singleton_binding_rejected`.
  **Gate — RITUAL COMPLETO.** **Ritual: SIM.**
- **C5 — [REMOÇÃO/SWEEP] Serialização `.tkb` de DI.** c8: remover I/O de `di_kind`/`has_di_key`/`di_key`
  em `tkb_read.tks`/`tkb_write.tks`; **BUMPAR `TKB_EXPR_VERSION`+`TKB_PROGRAM_VERSION`** (`tkb_frame.tks`).
  **Ficheiros:** `emit/tkb_read.tks`, `emit/tkb_write.tks`, `emit/tkb_frame.tks`. **Gate — RITUAL COMPLETO**
  — crumb que MOVE bytes (serialização + versão); o fixpoint prova-o. **Ritual: SIM.**

- **RITUAL FINAL — dry build + reseed manual (1×) + fixpoint + provenance + self-suficiência.** Ver §10.

---

## 2. Contagem de reseed + justificação

**1 reseed** (no RITUAL FINAL). Justificação:

- O seed é o binário `teko` lançado anterior, que entende a DI antiga E é indiferente a `service`/`svc`
  novos (o corpus `.tks` NÃO usa DI-anotação nem `service` — só os `.tkt` usam, e testes não gatilham o
  seed). Logo cada crumb builda gen1 com o MESMO seed.
- **Crumb gatilho do fixpoint (onde os bytes se movem):** **C5** (serialização `.tkb` + bump de versão)
  é o decisivo — muda a forma emitida do `.tkb`. C1 adiciona parse (novo token), C4 remove parse/campos:
  como nada no corpus `.tks` usa nem a DI-anotação nem `service`, esses crumbs tendem a fixpoint
  byte-idêntico já; C5 é o que exige o BUMP. Cada crumb "Ritual: SIM" corre dry-build+fixpoint; o AVANÇO
  do seed committado é único (final), como no §5/§6.

---

## 3. Forward-dep §9.2b `T: service` — RECOMENDAÇÃO: constraint MÍNIMO AGORA

A lei escreve `svc<T: service>` e `has_svc<T: service>`. Duas rotas:

**Rota RECOMENDADA — constraint `service` mínimo AGORA (aterrissável sem o solver §9.2b completo):** o
mecanismo de constraint JÁ existe — `parse_constraint_atom` (`parse_decl.tks:112-118`) produz
`ConstraintAtom{name}`, checado nominalmente por `constraint_atom_satisfied`
(`monomorph.tks:82,96`). O intrínseco `svc<T>()` PRECISA, de qualquer forma, do predicado "T é um
service" (para rejeitar `svc<i64>()` e resolver). Duas edições pequenas alavancam esse MESMO predicado:
1. `parse_constraint_atom` aceita a keyword `service` (não só `Ident`) → `ConstraintAtom{name="service"}`
   (marcador reservado);
2. `constraint_atom_satisfied` trata o átomo `"service"` como "concrete é um tipo de service".

Isto torna `svc<T: service>` e o utilizador `fn get<T: service>(): T { return svc<T>() }` legais
EXATAMENTE como a lei soletra, sem o solver §9.2b completo. Custo: ~2 sítios; reusa o predicado que o
intrínseco já precisa.
```teko
/** Um átomo de constraint `service` é satisfeito sse `concrete` é um tipo de serviço. */
fn constraint_atom_satisfied_service(concrete: Type, table: TypeTable): bool {
    type_is_service(concrete, table)   // o mesmo predicado que `type_svc` usa para rejeitar não-serviços
}
```

**Rota ALTERNATIVA — `svc<T>` sem constraint agora, apertar em §9.2b:** o intrínseco checa "T é service"
INTERNAMENTE (o mesmo predicado), sem expor `T: service` na gramática de constraint. O utilizador escreve
`svc<Clock>()` (T concreto — funciona), mas `fn get<T: service>()` NÃO parseia até §9.2b.
```teko
// hoje (alternativa): a checagem vive dentro do intrínseco, não na constraint
var c = svc<Clock>()   // OK — type_svc rejeita se Clock não é service
// fn get<T: service>() { … }  // rejeitado até §9.2b ensinar o átomo `service`
```

**Recomendação: Rota MÍNIMA-AGORA.** É barata (reusa o predicado que o intrínseco já exige), aterra a
sintaxe exata da lei, e o §9.2b só GENERALIZA (átomos compostos, surface de conformância) sem re-fazer.
Fica BLOQUEADO ao §9.2b apenas o constraint-solver completo (átomos `service & I`, superfície de método
de serviço) — irrelevante para `svc`/`has_svc`.

---

## 4. A COSTURA de backend (o Doc 1 §8 preenche) — assinatura congelada AGORA

Uma costura, `svc_scope_expr(lifetime: parser::ServiceLifetime): str` em `src/codegen/codegen.tks`
(derivada de `di_scope_expr`, `:9814-9820`):
- **Parte A (agora):** `Singleton`→`"tk_region_root()"` (raiz do programa); `Scoped`→região do
  frame/bloco envolvente (`cg_enclosing_region_expr`); `Transient`→build fresco sem slot.
- **Doc 1 §8:** `Singleton`→slot da RAIZ-DA-THREAD; `Scoped`→slot da SUB-RAIZ-DA-THREAD. Troca DUAS
  linhas de corpo; `svc_type_id` (tag do slot) inalterado; nenhuma mudança de assinatura/typer/tipo/
  fixture. O runtime (`tk_region_root`/`tk_region_register`/`tk_region_lookup`) já existe — a Parte A já
  o usa; o Doc 1 §8 só re-aponta o slot para o nó de thread.

---

## 5. `teko_rt.tks` self-suficiência (VEREDITO)

`src/runtime/teko_rt.tks` NÃO contém maquinaria DI. As únicas ocorrências de "DI" (`:674-676`) são
NOTAS-de-comentário que descrevem as primitivas de arena `tk_region_register`/`tk_region_lookup`/
`tk_region_root` — declaradas como host-edge externs honestos — e citam "uma futura feature DI `#scoped`"
como o primeiro consumidor. Essas primitivas são EXATAMENTE as que o novo lowering de `svc` REUSA (via a
costura); não são removidas. **§7 exige ZERO edição de código a `teko_rt.tks`** (opcional: atualizar a
NOTA `:674` de "futura DI `#scoped`" para "`service`/`svc` (§7)" — puro comentário). A self-suficiência é
PRESERVADA: `teko_rt.tks` não depende do gémeo C para nenhum caminho §7, e não referencia nada da DI
antiga (`grep` de `di_kind`/`DiProvider`/`choose_factory`/`#inject` em `teko_rt.tks` = 0). **`teko_rt.c`/
`.h` frozen: intocados.** Fork CLEARED.

---

## 6. Test-corpus cleanup (MANDATÓRIO) — a lista MEDIDA

Corpus `.tks` self-host: ZERO uso de DI-anotação (medido) → nada a migrar em `src/**/*.tks`.
`examples/regressions/`: ZERO dirs usam `#singleton`/`#inject`/`#scoped`/`#transient` (medido) → nada a
inverter/de-registar lá.

Os `.tkt` que exercitam a DI antiga (contagem de arms DI por ficheiro, medida):

| `.tkt` | arms DI | destino |
|---|---|---|
| `src/checker/checker_test.tkt` | 22 | migrar ACCEPT/REJECT `#singleton`/`#inject`/registo/conflito → `service`/`svc`; escape → `svc_escape_*` |
| `src/parser/parser_test.tkt` | 13 | migrar parse de `#singleton`/`#inject`/`@key` → parse de `service Name lifetime` |
| `src/codegen/codegen_test.tkt` | 12 | migrar materializador/`di_scope_expr` → `svc_scope_expr`/`emit_svc` |
| `src/lir/lower_test.tkt` | 8 | migrar lowering DI → lowering `svc` |
| `src/checker/residence_test.tkt` | 7 | `#singleton`-binding→Root vira: sem-anotação (a fonte é `false` agora); manter as arms de `residence_tier(is_singleton=true→Root)` como teste do MECANISMO preservado |
| `src/checker/borrow_test.tkt` | 3 | migrar/remover arms DI |
| `src/checker/instantiate_order_test.tkt` | 3 | migrar/remover arms DI |
| `src/checker/spine_test.tkt`, `metrics_test.tkt`, `pt_census_test.tkt`, `comptime_fold_test.tkt`, `build/reachability_test.tkt`, `codegen/ffi_export_test.tkt` | (hits residuais) | remover referências a `di_kind`/`DiProvider`/materializador |

De-registar casos removidos dos `.tkr` referentes e das arrays CORPUS em `scripts/*.sh` (auditar
`scripts/*.sh` por nomes de caso DI removidos). **Cobertura migra, não se perde:** cada REJECT/ACCEPT da
DI antiga vira um fixture `service`/`svc` equivalente (§A.6). Feito em C3.

---

## 7. Decisões para o dono (recomendação, não poll — cada uma com exemplo)

- **D1 — `service`: RESERVADA vs contextual.** **Recomendo RESERVADA** (`TokenKind::Service`). Medição:
  as 4 ocorrências de `service` no corpus são TODAS prosa de doc-comment ("this service carries"),
  ZERO identificadores/paths — logo reservar não quebra nada, e uma keyword que INTRODUZ uma declaração
  (como `class`/`trait`) é sempre reservada, não contextual. (`unsafe`/`params` foram contextuais por
  serem MODIFICADORES/parâmetros; `service` é um construtor de declaração — pertence à classe do `class`.)
  ```teko
  service Clock singleton { … }   // `service` reservada, como `class`
  ```
- **D2 — lifetimes `singleton`/`scoped`/`transient`: contextual.** **Recomendo CONTEXTUAL** (só
  reconhecidos no SLOT de lifetime, imediatamente após o nome do serviço). São palavras comuns; reservá-las
  globalmente custaria caro sem ganho (não aparecem noutro contexto sintático).
  ```teko
  service Rng transient { … }   // `transient` só é keyword aqui
  ```
- **D3 — constraint `T: service`: MÍNIMO agora vs deferir a §9.2b.** **Recomendo MÍNIMO agora** (§3):
  ~2 edições reusam o predicado que o intrínseco já exige e aterram a sintaxe exata da lei.
- **D4 — o `#singleton` de BINDING (residência-raiz) sobrevive?** **Recomendo REMOVER a superfície,
  PRESERVAR o mecanismo** (precedente §6 com `is_unique_at`). `residence_tier` mantém o param
  `is_singleton`; a FONTE deixa de ser `b.di_kind==Singleton` e passa a `false` agora — a residência-raiz
  para `service singleton` liga-se pela costura no Doc 1 §8 (por-thread). O `#singleton`-de-binding solto
  é superfície da era-anotação que a lei retira.
  ```teko
  // antes (retirado):  var x #singleton = f()
  // depois:            service X singleton { … }   // a residência-raiz é do lifetime do serviço
  ```
- **D5 — texto EXATO dos diagnósticos REJECT** (o que as fixtures asseguram) — **Recomendo:**
  - service virtual/abstract: `"a service is always sealed; it cannot be 'virtual' or 'abstract'"`
  - `svc<T>` de não-service: `"svc<T> requires T to be a service (declared with 'service'); '{name}' is not"`
  - conflito: `"duplicate service registration for '{type}' under key '{key}': same-type-same-key collides at compile time (distinct keys coexist)"`
  - escape: `"a service value is arena-bounded: it may not be stored, passed as an argument, or returned — re-claim it with svc<T>() where needed"`
- **D6 — chave ausente: erro de COMP-TIME vs `panic` de runtime.** **Recomendo COMP-TIME-ERROR.** A lei
  diz "chave ausente = panic", mas a resolução é comp-time e a `key` é constante, então um miss é sempre
  diagnosticável em comp-time (estritamente melhor que um panic em runtime; `has_svc` continua o guarda
  para presença condicional). Se o dono quiser o panic literal de runtime, é a MESMA costura emitindo
  `tk_panic` em vez do erro — trivial de trocar. (Nenhum caminho da lei exige um miss silencioso.)
- **D7 — onde a costura coloca o slot em §7 (raiz-do-programa vs frame).** **Recomendo:** `Singleton`→
  raiz-do-programa (`tk_region_root()`), `Scoped`→frame/bloco envolvente, AGORA — simplest-correct; o
  Doc 1 §8 troca por raiz-da-thread / sub-raiz-da-thread (§4).

---

## 8. O ritual

Em CADA invocação de compilador:
1. `export TK_RT_DIR="$PWD/src/runtime"`.
2. **Dry build:** `TEKO_BACKEND=c <compiler> build . -o out --no-verify --release`.
3. **Reseed manual (RITUAL FINAL, 1×):** `bootstrap/teko.c` → binário; `TEKO_BACKEND=c binário build .
   --no-verify --release` → `OUT/teko.c`; **fixpoint byte-idêntico gen2==gen3** (o gate; sem correr a
   suite — as fixtures são AUTORADAS, não executadas).
4. **PROVENANCE:** atualizar o registo de proveniência do reseed.
5. **Check de self-suficiência de `teko_rt.tks` (explícito):** `grep` de `di_kind`/`DiProvider`/
   `choose_factory`/`#inject`/`#singleton` em `src/runtime/teko_rt.tks` = 0 (é 0 hoje; tem de permanecer
   0). Confirmar que `teko_rt.tks` builda/funciona sem depender do gémeo C para qualquer caminho §7.

---

## 9. Riscos e tensões de lei

| risco | mitigação |
|---|---|
| **R1 — remover `di_kind` quebra a residência** | NÃO: `residence_tier` PRESERVA o param `is_singleton` (mecanismo de memória); só a FONTE muda para `false` (D4). Nenhum binding de corpus usava `#singleton` (medido). O `service singleton`→raiz liga-se pela costura, Doc 1 §8. |
| **R2 — `.tkb` shift de ordinal** | Não é fork (ruling §6): remover `di_kind`/`di_key`/`has_di_key` da serialização + bumpar `TKB_EXPR_VERSION`/`TKB_PROGRAM_VERSION` (procedimento R2). Reseed builda de FONTE → fixpoint indiferente. |
| **R3 — sweep de literais `di_kind =`/`has_inject =` erra um** | O checker REJEITA um literal de struct com campo em falta no dry build ANTES do fixpoint — a rede é o próprio checker. |
| **R4 — `service` colide com identificador no corpus** | MEDIDO: 4 hits, TODOS prosa de doc-comment; ZERO identificadores/paths. Reservar é seguro (D1). Fork CLEARED. |
| **R5 — a resolução `svc` precisa de uma pass de análise-de-programa nova** | NÃO (§GENUÍNO-1): re-ferramenta a pré-walk `build_di_registry` existente numa `build_service_registry`; o `type_svc` lê a tabela do `Env` (slot `di`→`svc`), como o típer já fazia. |

**Tensão de lei residual: NENHUMA que force HALT.** Teko-only respeitado (produto em `.tks`; `teko_rt.c/
.h` frozen intocados). Issue-100%: os crumbs entregam o §7 inteiro (keyword+lifetimes → `svc`/`has_svc`+
conflito+escape → remoção da DI-anotação), sem regressão. Bootstrap-safe: o seed anterior compila cada
gen1 (corpus não usa nem DI-anotação nem `service`).

---

## 10. Forks genuínos (stop-and-report) — TODOS CLEARED, sem HALT

1. **A resolução comp-time `svc` precisa de uma "tabela estática por análise" que não existe?** **NÃO.**
   A tabela é a re-ferramenta da pré-walk DI existente (`build_di_registry`→`build_service_registry`,
   `di.tks:79-122`), que já varre o programa e já detecta o conflito (duplicado). O `type_svc` lê-a do
   `Env` (o slot `di: DiRegistry` já existe, `scope.tks:72`). NÃO é pass nova; RIDE a pré-walk existente.
2. **A regra de escape exprime-se via `escape.tks` sem maquinaria nova?** **PARCIALMENTE — e é honesto
   dizê-lo.** `escape.tks` faz análise de RESIDÊNCIA (points-to, escaping-vars) — não uma checagem de
   "este valor é de tipo service". A regra de escape do §7 é uma PROPRIEDADE DE TIPO checada em 3 sinks
   sintáticos (store/arg/return), maquinaria NOVA mas MÍNIMA (um predicado `type_is_service` + 3 guardas
   no typer), usando a travessia de `escape.tks` só como MOLDE. Recomendo implementá-la no typer
   (type-directed), não forçá-la na análise de residência de `escape.tks`. Não é HALT — é um sítio novo
   pequeno, declarado.
3. **Remover a DI de `teko_rt.tks` quebra a self-suficiência?** **NÃO** (§5): `teko_rt.tks` não tem
   maquinaria DI; só NOTAS de comentário que citam as primitivas de arena que o `svc` REUSA. ZERO edição.
4. **`service` como keyword colide com identificador no corpus?** **NÃO** (R4): 4 hits, todos prosa.

**Sem fork genuíno. Sem HALT.** O plano é executável na íntegra.

---

*Fonte: `mudancas-superficie-0.3.1.md` §7. Backend do lifetime-de-arena: `arena-especificacao-unica-0.3.1.md`
(Doc 1) §8. Constraint: §9.2b (mínimo-agora, §3). Precedentes: §5 (`plano-secao5-marshall.md`, costura
`emit_ptr_wrap_guard`), §6 (`plano-secao6-aposentar-unsafe.md`, "preserva o mecanismo, remove a
superfície"). Read+design apenas — nenhum código de produto, nenhum reseed.*
