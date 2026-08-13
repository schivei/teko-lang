---
section: design
created: 2026-08-13
status: DELIBERAÇÃO-PREP — material para o dono argumentar e definir PARTE A PARTE. NÃO é plano de
        crumbs, NÃO houve ratificação, NENHUM build/`teko test` foi executado. Fase reordenada em
        `mudancas-superficie-0.3.1.md` §11 Sequência: "expansão da stdlib — ANTES da §11".
source: §11 Sequência (reorder), §7 (service/DI), §10 (journal/threads/Intent), §9.2b (constraints),
        docs/memory/0.3.1-item13-monomorph-leak-investigation.md (o ângulo de memória)
scope: mapear a stdlib atual, abrir o espaço de SEPARAÇÃO (self-link), propor a EXPANSÃO como forks,
       e povoar o `exp`/`pub` que a §11 formaliza. READ-ONLY sobre código de produto.
---

# Stdlib — expansão + separação (self-link) e o corte de RSS do item 13

> **Papel desta fase** (dono, §11 Sequência): (1) **expandir** a stdlib — definir os novos itens; (2)
> decidir a **separação** que permite **self-link com o programa final** — como a stdlib se modulariza e o
> que cada módulo **expõe (`exp`)** vs **mantém interno (`pub`)**; (3) o bônus de memória — uma stdlib bem
> separada **corta parte do RSS super-linear do item 13** (o `monomorphize` arrasta menos código genérico/
> stdlib pra dentro de cada programa). Esta fase **povoa** o `exp`/`pub` que a §11 depois **formaliza** e
> **enforce**; a §12 (libc-direct/`#if`/`#os`) opera **sobre** a superfície resultante.
>
> Deliberação-prep: cada parte abaixo abre **forks + recomendação** (a recomendada primeiro), para o dono
> martelar parte a parte. Nada aqui está selado.

---

## 0. TL;DR das recomendações (para o dono confirmar/rejeitar por parte)

| # | Decisão | Recomendação (fork detalhado adiante) |
|---|---------|----------------------------------------|
| P1 | Fronteira de separação | **`exp` = ABI da stdlib no `.tkh`; `pub` = interno inter-namespace da stdlib; private = ficheiro.** É o modelo que a legislação Visibility já descreve (`ast.tks:513`). |
| P2 | Granularidade de link | **Pacote stdlib único, item-table POR NAMESPACE carregada sob `use` (híbrido A+B).** Não-genérico `exp` → arquivo pré-compilado; genérico `exp` → template `.tkb` monomorfizado no uso. |
| P3 | Corte do item 13 | **A separação encolhe os DOIS amplificadores** — o volume clonado da PHASE-1 (§3a) e o `items` do scan O(instâncias×métodos×items) (§3b). Não é fix do monomorph, é **redução de entrada**. |
| P4 | Expansão — o que entra agora | **Só o monomórfico-buildável-hoje + os tipos forward-compatible do §10** (Intent/threads). Coleções genéricas (Set/Deque/BTree) ficam **atrás do #254**. |
| P5 | Relação com §11/§12 | Esta fase **escreve** os marcadores `exp`/`pub` (sem enforcement); a §11 **liga** o enforcement; a §12 usa a superfície `exp` para decidir o que `#os`/libc-direct pode tocar. |

**Bloqueios honestos (design-ahead):** o *enforcement* de `exp`/`pub` é a §11 (penúltima) — hoje `pub` e
`exp` são **ambos** cross-namespace (`check_modules.tks:143`), então a separação é **escrita agora, ativada
na §11**. As **classes-coleção genéricas** dependem do **#254** (métodos em tipo genérico, OPEN). O **arquivo
self-link** de código não-genérico depende do backend nativo emitir objeto por-unidade (existe a via `.tkl`/
`.tkb`, §4). Tudo que **não** depende disso está desenhado abaixo.

---

## 1. Mapa do estado atual — a stdlib de hoje

### 1.1 Como namespace e link funcionam hoje (o ponto de partida da separação)

- **Um projeto só.** `teko.tkp` declara `name = "teko"`, `source = "src"`. **A stdlib mora DENTRO da árvore
  de fontes do compilador** — `teko::list`, `teko::fmt`, `teko::math` são compilados JUNTO com `teko::checker`,
  `teko::lir`, etc. **Não existe artefato de stdlib separado** hoje. O namespace de cada símbolo é o **caminho
  do ficheiro relativo a `src/`** (`src/io/io.tks` → `teko::io`; `src/encoding/json/json.tks` →
  `teko::encoding::json`). O comentário `// (namespace 'teko::io')` é informativo; a verdade é o caminho.
- **`use` é só alias** (`language-guide.md:223`): nunca muda visibilidade, nunca declara dependência, sem
  wildcard, sem re-export. `check_modules.tks` faz o enforcement de namespace (ref bare cross-ns = erro; ref
  qualificada a private = erro).
- **Visibilidade (legislação, `ast.tks:513`):** `Private` (default, sem keyword) = só o próprio namespace;
  `Pub` = **através dos namespaces do projeto, NÃO no header do artefato**; `Exp` = **exportado no `.tkh`
  (a ABI pública real do artefato)**, `pub` por definição. Ordem crescente de alcance: `Private < Pub < Exp`.
  **Esta é a fronteira de separação já legislada** — é ela que a fase povoa.
- **Empacotamento de dep já existe** (`project.tks:136`): um dep é um **`.tkl`** (ZIP) contendo um **`.tkb`**
  (itens tipados serializados), carregado e *prepended* antes do type-check. É o veículo pronto para a stdlib
  virar pacote.
- **`teko::list` e `teko::str` são BUILTINS injetados** — `teko::list::{empty,push,…}` é free-fn builtin
  (`type_list_builtin`), `teko::str::{concat,ends_with,…}` resolvido por último-segmento em `scope.tks:1054`.
  Não têm ficheiro-classe. `str`/`byte`/`error` são **tipos predefinidos injetados** (`core.tks`).

### 1.2 Inventário — namespaces da stdlib (user-facing) e seus ficheiros

| Namespace | Ficheiros | ~Linhas | `exp` hoje | Genérico? | Natureza |
|---|---|---:|---:|---|---|
| `teko::list` | builtin (`src/list/list.tks` = shim `grow`) | 19 | 0 | **sim** (builtin `<T>`) | free-fn builtin sobre a intrínseca de alocação |
| `teko::collections` | `collections.tks`, `list.tks` (`List<T>`), `map.tks` (`Map<V>`) | 313 | 0 | **sim** | classes-coleção — **bloqueado #254** |
| `teko::runtime` | `teko_rt.tks` (+ `.c`/`.h` MANTIDOS) | 789 | 47 | não | seam de runtime; C twin congelado exceto rt |
| `teko::assert` | `assert.tks` (+ `.c`/`.h` seed MANTIDO) | 375 | 24 | não | seed de assert |
| `teko::journal` | `journal.tks`, `summary.tks` | 1995 | 0 | parcial | journaling §10.4 (sink = service singleton) |
| `teko::env` | `env.tks` | 49 | 0 | não | FFI host (args/vars) |
| `teko::fmt` | `fmt.tks` | 878 | 0 | não | formatter DT0 |
| `teko::text` | `text.tks` | 106 | 0 | não | utilidades de `str`/UTF-8 |
| `teko::iter` | `iter.tks`, `int_iter/int_terminals/byte_iter/str_iter` | 837 | 0 | parcial | protocolo de iteração (quer `Iter<T>`) |
| `teko::io` | `io.tks` + 3 | 938 | 0 | parcial | FFI host + streams; Reader/Writer quer round-3 |
| `teko::fs` | `fs.tks` | 34 | 0 | não | FFI host de ficheiros |
| `teko::math` | `math.tks`, `checked.tks` | 2523 | 0 | parcial (`min/max/clamp`) | M0/M1 |
| `teko::sort` | 2 ficheiros | 345 | 0 | parcial | ordenação |
| `teko::time` | `time.tks` | 684 | 0 | não | data/hora (drop-128, campos ≤64b) |
| `teko::process` | `process.tks` | 600 | 0 | não | FFI host fork/exec |
| `teko::regex` | `regex.tks` | 585 | 0 | não | regex sobre `str`/`[]byte` |
| `teko::crypto` | 3 ficheiros | 699 | 0 | não | hash/CSPRNG sobre `[]byte` |
| `teko::compress` | 5 ficheiros | 1514 | 0 | não | DEFLATE/gzip/zlib sobre `[]byte` |
| `teko::encoding::{base64,csv,json,url}` | 4 ficheiros | — | 0 | não | codecs sobre `str`/`[]byte` |
| `teko::numeric::{bigint,dec}` | 2 ficheiros | — | 0 | não | aritmética de struct concreta |
| `teko::casting` | `casting.tks` | 182 | 0 | não | conversões |
| `teko::names` | `names.tks` | 237 | 0 | não | utilidades de identificador |
| `teko::coverage` | `coverage.tks` | 1138 | 19 | não | cobertura (ferramenta) |
| `teko::test` | `test.tks` | 109 | 0 | não | scaffolding de teste |
| `teko::str`, `teko::char` | **builtin** (`scope.tks`) | — | n/a | não | superfície de string injetada |

**Total user-facing ≈ 15 k linhas** de `.tks` (sem contar o compilador: `lexer/parser/checker/lir/codegen/
backend/emit/build/lsp` — ~100 ficheiros — que **NÃO** vão para o programa do usuário).

### 1.3 Observação-chave que motiva a fase

Hoje **quase tudo é `pub`** (1622 `pub` vs 206 `exp` vs 55 `intern`), e os únicos `exp` estão em
`runtime`/`assert`/`coverage`. Isso porque, sendo **um projeto só**, `pub` **basta** (cross-namespace intra-
projeto). **A distinção `exp` vs `pub` só passa a valer quando a stdlib vira um ARTEFATO** com `.tkh` — que é
exatamente o self-link desta fase. **Logo: a fase é, na prática, decidir para CADA item da stdlib se ele é
`exp` (ABI para o programa do usuário) ou `pub` (interno da stdlib).**

---

## 2. O espaço de SEPARAÇÃO — self-link com o programa final

**Problema.** Hoje, compilar um programa do usuário arrastaria a stdlib inteira como fonte, e o
`monomorphize` clonaria/carimbaria cada corpo dela na região raiz (item 13, §3). **Objetivo:** que a stdlib
seja uma **unidade separadamente compilável** cuja **superfície `exp`** o programa final consome, **sem
re-clonar/re-monomorfizar** o que não usa.

### 2.1 Fork P1 — a FRONTEIRA (o que é `exp` vs `pub`)

| Opção | `exp` | `pub` | private | Veredito |
|---|---|---|---|---|
| **A — ABI-no-`.tkh` (recomendada)** | superfície user-facing, entra no `.tkh` e no item-table do programa | inter-namespace **dentro** da stdlib, **fora** do `.tkh` — some do programa | ficheiro | **É a legislação já escrita** (`ast.tks:513`). Zero construto novo. |
| B — tudo `exp` (sem fronteira) | todo `pub` vira `exp` | — | ficheiro | rejeitar: não corta memória (tudo entra no programa); é o status quo com outro nome |
| C — visibilidade por pacote (3º eixo) | novo marcador `pkg` | `pub` | private | rejeitar: **cria exceção** (o dono: "já temos muitas") — o par `exp`/`pub` já cobre |

**Recomendação: A.** A stdlib expõe uma **API `exp` curada**; todo o resto vira `pub` (helper compartilhado
entre módulos da stdlib, ex. um buffer de bytes que `crypto` e `encoding` partilham) ou private (ficheiro).
**Nenhum construto novo** — só **reescrever `pub`→`exp`** onde é API pública, e **manter `pub`** onde é
interno. É o ato central da fase.

### 2.2 Fork P2 — a GRANULARIDADE de link

| Opção | Forma | Efeito em memória (item 13) | Custo |
|---|---|---|---|
| **Híbrido A+B (recomendada)** | **um** pacote stdlib, mas **item-table por namespace**, carregada só sob `use teko::X`; não-genérico `exp` → arquivo (`.a`) pré-compilado; genérico `exp` → template `.tkb` monomorfizado no uso | **máximo prático**: só os namespaces usados entram no `items`; não-genérico não é re-clonado | pré-compilar por namespace |
| A puro — pacote único monolítico | um `.tkl`/`.tkb` com **todos** os itens `exp` | corta os `pub`/private, mas **todo** `exp` entra no `items` mesmo sem uso | mais simples |
| B puro — um pacote por namespace | `teko-math-*.tkl`, `teko-crypto-*.tkl`, … | igual ao híbrido, granularidade fina | N pacotes, N resoluções de dep |
| D — status quo (source-merged) | nenhuma separação | **é o amplificador do item 13** | — (rejeitar) |

**Recomendação: híbrido A+B** — **distribuição única** (um pacote, ergonomia de B evitada), mas o *loader*
(`load_dep_package`, `project.tks:136`) traz **só a item-table dos namespaces que o programa `use`a**. Assim o
`items` que o `monomorphize` varre e clona é **∝ ao que o programa usa**, não à stdlib inteira.

### 2.3 Fork P3 — POR QUE isso corta o RSS do item 13 (o mecanismo, não a promessa)

O item 13 (`monomorph.tks`) tem dois amplificadores estruturais; a separação ataca a **ENTRADA** de ambos
(sem tocar o pass — o fix do pass é AL5/achatamento-#2, ortogonal):

- **§3a — PHASE-1 clona cada corpo não-genérico na raiz.** Hoje isso incluiria **todo** corpo `pub` da stdlib
  arrastado. Com separação, o não-genérico `exp` é **objeto pré-compilado** (linkado, não re-clonado) e o
  `pub`/private **nem entra no `items`**. → o volume clonado da PHASE-1 cai para "só o código do usuário +
  templates genéricos que ele instancia".
- **§3b — `stamp_all_instance_methods` é O(instâncias × métodos × `items`), SEM teto.** O custo é linear no
  tamanho de `items` (a varredura `find_template_method`). **Encolher `items`** (só namespaces usados, sem
  `pub`/private) **encolhe multiplicativamente** esse hot-spot. → menos `items`, menos scan, menos corpos
  carimbados parados na raiz.
- **Genéricos ainda monomorfizam no uso** (não dá pra pré-compilar um `List<T>` sem `T`), **mas** uma
  superfície `exp` **enxuta** limita **quais** templates são alcançáveis, e o carregamento por-namespace
  limita **quantos** `table_generic_instances` a stdlib injeta.

**Honestidade:** isto **não conserta** o monomorph (o teto/região-por-fase é AL5, outra onda). É **redução de
entrada** — encolhe o `n` de um pass super-linear. O ganho é real e mensurável (RSS-vs-namespaces-usados), mas
**complementar** ao AL5, não substituto. *(Medição segura pertence a VM isolada — NUNCA `teko test`, NUNCA
este container; ver o §6a do doc do item 13.)*

### 2.4 A tensão central da separação (registrar para o dono)

**Genérico não se pré-compila.** Um `exp` genérico (`List<T>`, `Iter<T>`, `svc<T>`, `chan<T>`) é **template**:
o `.tkb` carrega o corpo tipado, mas a monomorfização acontece **no programa do usuário**, por `T` concreto.
Então o corte de memória da separação é **assimétrico**:
- **Não-genérico `exp`** (math sobre `f64`, crypto/compress/encoding sobre `[]byte`, io/fs/env/process FFI):
  corte **total** — vira objeto linkado.
- **Genérico `exp`** (coleções, iter, DI, canais): corte **parcial** — sai o `pub`/private e o não-usado, mas
  o template instanciado ainda monomorfiza no programa.

Isso **sugere a política de expansão P4**: o que é **não-genérico** é o alvo prioritário da separação (maior
ganho), e o **genérico** deve ter superfície `exp` deliberadamente **mínima** (expor a classe, esconder os
helpers de rebuild em `pub` — como `collections.tks` já faz com as free-fns de replace-at/drop-at).

---

## 3. A EXPANSÃO — candidatos a NOVOS itens (forks)

Regra de sequência (P4): **entra agora** o que é **monomórfico-buildável-hoje** ou **forward-compatible** (os
tipos do §10 que já têm grafia `exp`/`pub` definida); **fica atrás do #254** o que precisa de método em tipo
genérico. Cada bloco é um fork para o dono martelar.

### 3.1 Fork E1 — completar `teko::collections` (BLOQUEADO #254)

| Candidato | Forma | Superfície `exp`/`pub` proposta | Entra? |
|---|---|---|---|
| `Set<T>` | class sobre `Map<T,unit>`/hash | `exp class Set<T>`; helpers de bucket `pub` | após #254 |
| `Deque<T>` | class ring-buffer | `exp class Deque<T>`; `pub` o rebuild | após #254 |
| `BTreeMap<K,V>` | class ordenada (precisa `IOrd`/`__lt`, §9.4) | `exp class`; nós `pub` | após #254 **e** interface-operador |
| `LinkedList<T>` | class encadeada | `exp class`; nó `pub` | após #254 |

**Recomendação:** **não antecipar** — todos dependem de #254 (métodos em genérico). Desenhar **só a fronteira
`exp`/`pub`** agora (classe `exp`, internos `pub`), implementar quando #254 fechar. `List<T>`/`Map<V>` já
existem e servem de precedente da fronteira.

### 3.2 Fork E2 — tipos de concorrência do §10 (FORWARD-COMPATIBLE, entram cedo)

O §10.3/§10.4 **já definem** a grafia `exp`/`pub` de `Intent`/`Intent<T>` (backing `_x` private, `exp get`,
`pub set`, `pub static fn new`). Esta fase **cataloga-os como itens de stdlib** e fixa o namespace:

| Candidato | Namespace | Superfície | Depende de |
|---|---|---|---|
| `Intent` / `Intent<T>` | `teko::threads` | `exp type` + `exp get`/`pub set` (§10.3, verbatim) | item 14 (value-struct mut), §9 (props) |
| `Ctx`, `Rx<T>`, `Tx<T>` | `teko::threads` | handles: `exp` métodos `send`/`pop`/`add`/`done`/`close` | DI por chave (§7), #254 |
| `IChannelKind<T>`, `OsChan<T>`, `MemChan<T>` | `teko::threads` | `exp interface` + built-ins; sink concreto `pub` | conformidade estática de interface |
| `Roll`, `Record`, `IJournalKind`, `FileJournal` | `teko::journal` | `exp` a interface/enum/Record; `FileJournal` internals `pub` | §10.4 (já grafado) |

**Recomendação:** **catalogar e fixar namespace `teko::threads`** agora (o §10.5 já reserva o nome), escrever
os `exp`/`pub` **exatamente como o §10** já grafou (forward-compatible, sem enforcement até §11). É pura
população da superfície — **entra**.

### 3.3 Fork E3 — consolidação de string (`teko::str` builtin → módulo?)

Hoje `teko::str::*` é **builtin injetado** (`scope.tks`) e `teko::text` é um módulo separado (106 linhas).
Fork:

| Opção | Forma | Veredito |
|---|---|---|
| **manter split (recomendada)** | `str` = builtins mínimos (`concat`/`ends_with`/…); `teko::text` = utilidades ricas `exp` | menor churn; o builtin é seam de codegen |
| fundir em `teko::str` módulo | mover text para `teko::str`, aposentar o builtin | alto churn no corpus (`teko::str::concat` está em toda parte); risco de reseed |

**Recomendação:** **manter o split**, mas **decidir a fronteira `exp` de `teko::text`** (o que é API vs
helper). Não mexer no builtin `str` nesta fase (é seam de codegen, não stdlib pura).

### 3.4 Fork E4 — itens monomórficos prontos (do drain fase-3, agora com fronteira `exp`)

Estes são **monomórfico-buildável-hoje** (o drain doc os lista sobre `f64`/`str`/`[]byte`) — a fase os
**admite** e lhes **atribui a fronteira `exp`/`pub`**:

- `teko::math::real` (libm FFI sobre `f64`) — `-lm` já linkado; `exp` as funções, `extern` `pub`.
- `teko::log` (facade + writers) — `exp` a facade, writers `pub`.
- `teko::config` (camadas sobre TOML) — `exp` a leitura, parser `pub`.
- completar `teko::encoding` (TOML/XML/YAML/binários) e `teko::numeric` (complex/rational/stats).

**Recomendação:** **admitir como candidatos**, mas cada um é **issue própria** (o drain doc já os ordena) — a
fase **não os implementa**, só **fixa que namespace e que `exp`** terão, para não retrabalhar na §11.
**Reportar ao integrador** (não abrir issues) que E4 herda a ordenação do `drain-fase3-stdlib-order.md`.

---

## 4. Interação com §11 (`exp`/`pub`) e §12

### 4.1 Como esta fase POVOA a §11

A §11 (penúltima) **liga o enforcement** de `exp`/`pub`: hoje `check_modules.tks:143` trata `pub` e `exp` como
**iguais** (ambos cross-namespace). Quando a §11 entrar, `pub` deixa de ser visível fora da stdlib e só `exp`
atravessa para o programa do usuário. **Esta fase escreve os marcadores que a §11 vai enforce** — ou seja:

1. Para **cada item `pub` da stdlib**, esta fase decide: **continua `pub`** (interno) **ou vira `exp`** (API).
   É o trabalho de maior volume (1622 `pub` a triar).
2. A grafia é **forward-compatible** (o §10.3 já a usa na Intent): hoje tudo lê como visível; a §11
   **auto-corrige** sem refactor. Logo esta fase pode ser escrita **sem esperar** a §11.
3. **Regra de ouro para triar (recomendação):** um item é `exp` **sse e só se** um programa de usuário o chama
   diretamente. Helper inter-módulo da stdlib = `pub`. Detalhe de ficheiro = private. **Na dúvida, `pub`** (o
   custo de esquecer um `exp` é um erro honesto na §11; o custo de um `exp` a mais é ABI inflada + memória).

### 4.2 Como a §12 usa a superfície resultante

A §12 (última — libc-direct/`#if`/`#os`/macro) **opera sobre a §11 já formada**: ela decide **o que**
`#os`/libc-direct pode substituir, e isso só é seguro se a **fronteira `exp` já está fixa** (uma substituição
`#os` de um corpo `pub` interno não vaza ABI; de um `exp` sim). Logo a ordem do dono (`exp/pub` penúltima,
libc-direct última) é **necessária**: a §12 precisa saber o que é ABI (`exp`) antes de mexer nos corpos.

### 4.3 O corte de memória como pré-condição, não consequência

O §11 Sequência coloca o bônus de memória explícito: a separação **"já reduz parte dos problemas de
memória"**. Registrar que o corte (§2.3) **não** depende da §11 estar ligada — o *loading por-namespace* e o
*arquivo pré-compilado* cortam `items`/PHASE-1 **assim que existirem**, mesmo antes do enforcement. A §11 só
**tranca** a fronteira que a separação já usa.

---

## 5. Riscos e tensões de lei (com resolução recomendada)

| Risco / tensão | Lei em jogo | Resolução recomendada |
|---|---|---|
| Genérico `exp` não pré-compila → corte assimétrico | item 13 / AL5 | **Aceitar**: separação corta não-genérico total, genérico parcial; complementa AL5, não o substitui (§2.3–2.4) |
| Reseed: pacote stdlib usa feature ausente do seed | bootstrap seed | **Sequenciar**: o formato `.tkl`/`.tkb` já existe (`project.tks:136`); nenhum construto novo — só reescrever `pub`→`exp`. Seguro no seed atual |
| Sweep silencioso de `.tkt`/`.tkr` ao mexer em visibilidade | owner-profile (sweep obrigatório) | Qualquer reescrita `pub`→`exp` que **restrinja** alcance pode quebrar `.tkt` calados — a §11 é onde isso aparece; **esta fase só escreve**, não enforce, então não quebra hoje |
| `exp` a mais infla ABI e memória | §2.3 | Regra de ouro §4.1: na dúvida `pub`; `exp` só o que o usuário chama |
| `teko::str`/`list` builtins não são stdlib pura | core.tks (injetado/reservado) | **Não** modularizar builtins nesta fase (são seam de codegen) — só stdlib `.tks` |
| Coleções genéricas parecem "prontas" mas travam no link nativo | #254 + report do #163 (`collections.tks`) | **Não antecipar** E1; a própria `collections.tks` documenta o link-fail de método-irmão genérico |

**Nenhuma tensão genuína de lei exige HALT.** A fronteira `exp`/`pub` já é lei (`ast.tks:513`); o veículo de
pacote já existe; a grafia é forward-compatible. O que falta é **decisão de conteúdo** (triar 1622 `pub`), que
é deliberação do dono — daí as perguntas abertas.

---

## 6. PERGUNTAS ABERTAS — para o dono argumentar e definir (parte a parte)

1. **P1 (fronteira).** Confirma `exp` = ABI-no-`.tkh` / `pub` = interno-stdlib / private = ficheiro (opção A)?
   Ou quer um 3º eixo de "visível-no-pacote-mas-não-ABI" (rejeitado como exceção nova)?
2. **P2 (granularidade).** Pacote stdlib **único com item-table por-namespace** (híbrido, recomendado), ou **N
   pacotes por namespace** (B puro), ou monolito `exp` (A puro)?
3. **P4 / E2.** Fixar **`teko::threads`** como namespace dos tipos de concorrência (Intent/Ctx/Rx/Tx/chan) já
   nesta fase, escrevendo `exp`/`pub` como o §10 grafou? Ou esperar a onda de concorrência?
4. **E1.** As coleções novas (Set/Deque/BTree/LinkedList) — **só desenhar a fronteira `exp`/`pub` agora** e
   implementar após #254, confirmado? Alguma delas é prioridade que justifique antecipar o desenho completo?
5. **E3.** `teko::str` builtin + `teko::text` — **manter o split** (recomendado) ou consolidar num
   `teko::str` módulo (alto churn no corpus)?
6. **Regra de ouro (§4.1).** Aceita "**na dúvida, `pub`**; `exp` só o que o usuário chama diretamente" como o
   critério de triagem dos 1622 `pub`? É o que define quanto da stdlib some do programa do usuário.
7. **Escopo da fase.** Esta fase **escreve os marcadores** (`pub`→`exp` triados) **e** desenha o
   loader-por-namespace, mas **não liga** enforcement (isso é §11) — confirma esse recorte? O corte de memória
   vem do loader/arquivo, não do enforcement (§4.3).
8. **Ordem de triagem.** Começar a triagem `exp`/`pub` pelos módulos **não-genéricos** (maior ganho de
   memória: math/crypto/compress/encoding/io/fs/env/process/time/regex), deixando os genéricos
   (collections/iter/threads) por último — de acordo?

---

## 7. Confirmação de segurança

`teko test` **NÃO** foi executado em forma alguma durante esta preparação — nem `teko test .`, nem subconjunto,
nem guardado. Nenhum build foi rodado, nenhum seed construído. O trabalho foi **leitura estática + raciocínio**
sobre `teko.tkp`, `src/core.tks`, `src/parser/ast.tks`, `src/checker/{check_modules,collect,scope}.tks`,
`src/build/project.tks`, os cabeçalhos dos módulos da stdlib, `docs/design/mudancas-superficie-0.3.1.md`,
`docs/design/drain-fase3-stdlib-order.md`, `docs/canonical/product/language-guide.md`,
`docs/memory/0.3.1-item13-monomorph-leak-investigation.md` e `docs/memory/owner-profile.md`. Nenhuma edição de
código de produto — apenas este documento.
