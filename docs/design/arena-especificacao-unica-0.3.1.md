---
section: design
created: 2026-08-10
status: DESENHO — especificação única e consolidada do modelo de arena (0.3.1). Fonte da verdade.
        Consolida e SUPERSEDE, no que houver conflito, os docs de arena anteriores
        (ast-computed-arena-assessment, arena-por-escopo, modelo-de-memoria-por-escopo,
        transicao-move-on-return, backend-memoria-por-funcao, o-profiler, port-memoria,
        arena-em-teko). Onde um anterior detalha um mecanismo aqui só nomeado, ele permanece
        vigente COMO REFERÊNCIA; onde diverge desta, esta vence.
source: ruling do dono 2026-08-10 ("um documento único que especifica 100% de como a arena deve se
        comportar, incluindo multi-threading") + ast-computed-arena-assessment-0.3.1.md (as três
        ideias ratificadas) + paralelizacao-0.3.1-eixo2 + concorrencia-isolate-spawn-chan-0.3.1.md
---

# Arena — especificação única e completa (0.3.1)

> **Este documento é a fonte única da verdade do modelo de memória do teko.** Ele diz, em um só
> lugar, como uma arena nasce, cresce, morre, o que ela garante, o que ela NÃO garante (e quem
> garante no lugar dela), como um valor que escapa é colocado, e como tudo isso se comporta sob
> múltiplas threads. Os exemplos usam a superfície 0.3.1 (`var`, retorno com `:`) definida no Doc 2.

---

## 0. O princípio governante e o escopo native-only

### 0.1 Segurança de memória — onde ela mora (regra do dono)

> *"A segurança não está no tipo da variável, está na capacidade das arenas."* — dono

A segurança de memória do teko é a soma de **três eixos ortogonais**, e **nenhum deles é uma keyword
de variável** (`let`/`mut`/`var` não são invariantes de segurança — são intenção):

| hazard | quem garante | nível |
|---|---|---|
| **Use-after-free (tempo de vida)** | a arena: vida da arena ⊇ escopo do uso | arena (tempo de vida) |
| **Overflow (capacidade)** | a arena: piso pré-calculado + lista de chunks que nunca estoura | arena (capacidade) |
| **Aliasing (duas escritas vivas na mesma célula)** | exclusividade de fluxo (F1, `borrow.tks`) — análise de fluxo, independente de keyword | controle de fluxo |

A frase precisa: **segurança = tempo-de-vida + capacidade da arena (UAF, overflow) + exclusividade F1
por fluxo (aliasing)**. Remover `let`/`mut` não remove nenhuma dessas garantias, porque nenhuma delas
vinha da keyword (verificação completa: `ast-computed-arena-assessment-0.3.1.md` §4.8).

### 0.2 O destino: native-only, sem emitir nem depender de C

O teko **não emite C nem depende de um compilador C**. A rota C (emitir `teko.c`, invocar `cc`) é
**andaime de bootstrap**, não destino — sai quando o backend nativo se auto-hospeda. O único resíduo
de C admitido é:

- **FFI nativo com link dinâmico** para bibliotecas de sistema em **Apple e Windows** (libSystem,
  `kernel32`, etc.) — chamada nativa por símbolo dinâmico, **nunca** a emissão de um arquivo `.c`
  local que exija um compilador C.

Consequência para este documento: **o modelo de arena aqui é o modelo do backend NATIVO.** A
"preservação de bytes para a rota C" que dominou o período de bootstrap era uma restrição de
transição; a invariante permanente é a **auto-consistência do backend nativo** — o fixpoint
`gen2 == gen3` (o compilador nativo se reconstrói ao mesmo byte).

---

## 1. A região — a mecânica física

### 1.1 Uma região é uma LISTA de chunks, não um buffer que realoca

`tk_region_alloc` (`teko_rt.c:2034`) faz **bump** dentro do chunk-cabeça; quando o pedido não cabe, ele
**PREPENDA um chunk novo** de `max(pedido, TK_REGION_DEFAULT_CHUNK)` (`teko_rt.c:2061`;
`TK_REGION_DEFAULT_CHUNK = 64 KiB`, `teko_rt.h:155`). **Crescer uma região NUNCA copia os bytes dela** —
apenas encadeia outro chunk. Logo:

- Não há "realocação" no nível da região. A capacidade cresce por encadeamento, O(1) amortizado,
  sem cópia.
- Um chunk nunca estoura: se o pedido não cabe, encadeia-se outro. **Overflow no nível da região é
  estruturalmente impossível.**

### 1.2 O copy-grow O(n²) vive na SLICE, não na região

A cópia que custa vive uma camada abaixo, na slice: `tk_slice_push_r` (`teko_rt.c:1272`) aloca um buffer
NOVO maior, `memcpy` do velho→novo, e **abandona o velho dentro da região**. Com reclaim 0%, cada
metade-abandonada da duplicação fica residente até a região dropar — é a origem do pico ~1.8 GB
(`al1-proof-report.md`: 1138 MB de copy-grow medidos).

A cura in-place já existe como primitiva: `tk_slice_grow_inplace` (`teko_rt.h:1287`, o append "Modelo A"
/ AL3) **acrescenta sem abandonar quando há capacidade**. É o `push(ref x, v)` ref-push. Presente, mas
ainda não é o caminho de emissão pervasivo.

**Distinção decisiva:** o piso da AST (§3) mira a demanda de chunk da região; o 1.8 GB mira a slice, e é
domínio do AL3 `grow_inplace`, não do piso.

### 1.3 O retrato de retenção (medido)

| quantidade | valor | onde |
|---|---|---|
| root (nunca liberado) | **1926 MB** | raiz da task, dropada só na saída do processo |
| razão de reclaim | **0.0 %** | nada dropado no meio do build |
| scoped (liberado no drop) | **0.0 MB** | escopos abrem região, não liberam nada útil |

**A parede é retenção, não iteração.** Os três mecanismos do modelo (piso, elisão, DPS) atacam
retenção e throughput; o DPS ataca também **correção** (fronteira de retorno).

---

## 2. Ciclo de vida — a árvore de regiões

### 2.1 Abrir, entrar, sair, dropar

A disciplina de região é uma árvore. Cada nó:

```
child = region_new(parent)   // filha com chunks SEPARADOS do pai
region_enter(child)          // todo bump-alloc subsequente cai nesta região
... trabalho ...             // aloca livremente
region_leave()               // volta o topo da pilha ao pai
region_drop(child)           // larga TODOS os chunks da filha de uma vez — O(1)
```

- **`region_drop` é atômico e total:** larga todo o conteúdo da filha numa tacada. Não há free
  por-objeto; a granularidade de liberação é a região.
- **Escopo léxico = região.** Um bloco abre uma região ao entrar e a dropa ao sair. A vida de tudo que
  ele alocou é exatamente a vida do escopo.
- **A raiz da task** (`tk_region_root`) é dropada só na saída do processo — é onde a retenção-root mora.

### 2.2 A pré-condição de segurança do drop total

`region_drop` só é seguro porque **nada vivo fora da região aponta para dentro dela**. Toda a disciplina
de escape (§6) existe para manter essa invariante: um valor que precisa sobreviver ao drop **não pode
nascer na região que vai dropar** — tem de nascer numa região que o cobre. É exatamente aqui que o
`arena-por-escopo` (a passada falsificada) quebrou: fez bulk-drop de uma região enquanto um alias vivo
ainda a referenciava → UAF. **A lição, gravada:** nunca dropar região com referência viva pendurada; a
colocação correta (§3, §5, §6) é o que garante isso por construção.

---

## 3. O piso da arena — pré-cálculo estático a partir da AST (Ideia 1)

> *"cada arena tem seu piso pré-calculado em tempo de montagem da AST… reduzindo realocação."* — dono

### 3.1 O que é

A AST prova um **limite inferior** da demanda do primeiro chunk de uma região: a soma das alocações de
largura fixa que os statements de um escopo provadamente fazem (literais de struct de layout conhecido,
literais de array de contagem conhecida, sítios de box). Esse piso semeia o presize:

- Ponto de encaixe: `open_frame_region`/`open_native_region` passam um piso calculado a
  `tk_region_new_sized_u(parent, piso)` em vez de deixar a primeira alocação puxar 64 KiB default.
- **Não é análise standalone.** É o **seed do caso `Confidence::Thin`** do presize do profiler
  (`#arena_size`, `codegen.tks:9832`), onde a amostra dinâmica está ausente. O caminho dinâmico
  (profiler p99.9) continua sizeando melhor as coleções dinâmicas; o piso estático é o complemento para
  quando não há amostra.

### 3.2 O que ele garante e o que não

- **Piso é limite INFERIOR.** A região cresce além dele normalmente (§1.1). Nunca há UAF por sub-piso —
  não há como faltar espaço, só encadeia chunk.
- **Sobre-piso é leak-safe** (reservado-não-usado), a mesma assimetria que o custo newsvendor do
  profiler modela (`c_sobra = 1 byte/byte`).
- **O piso NÃO entrega o 1.8 GB.** Esse número vive na slice-cap (§1.2) e é do AL3. O piso entrega
  dezenas de MB (tail-waste, ~26 MB / 21k chunks) + throughput no `posix_memalign`.

### 3.3 Prioridade

Mais baixa das três ideias. **Dobrar no presize do profiler**, não construir como análise separada.

---

## 4. Elisão de arena — não abrir região onde não se aloca (Ideia 2)

> *"pode chegar à conclusão que não há necessidade de uma arena em determinada região, no caso da
> expressão não tocar a arena."* — dono

### 4.1 O que é

Um predicado `scope_touches_arena(body) -> bool` — verdadeiro sse e só se o escopo contém ao menos um
sítio de alocação routável (`push`/`box`/init-de-struct/lit-de-array/concat-de-str/`tk_alloc`). Quando
falso, `open_native_region`/`open_frame_region` **pulam** `tk_region_new_u`/`enter_u` (e o `drop_u`
pareado) — exatamente como o skip de `bracket_depth > 0` já faz hoje (`lower.tks:1637`), um padrão
provado na mesma função. A pilha de regiões continua balanceada pela mesma simetria.

### 4.2 O que ganha

Cada região USADA custa **64 KiB mínimo** (o piso default). O profiler mediu o caso brutal: um bloco que
guarda 200 bytes custa 64 KiB — perda de 300×. Elisão remove esse piso para cada escopo-folha sem
alocação (`if x { return a }`, um braço de comparação, um bloco-guarda). Economia = 64 KiB × regiões
elididas simultaneamente vivas (o custo é por região **simultaneamente viva**, limitado pela
profundidade, não pela contagem total).

### 4.3 A regra de ouro: conservador

**Dúvida → NÃO elide.** O predicado é conservador (mesma postura de `escape.tks`: "dúvida → escapa").
Um sítio routável não detectado roteia sua alocação para a região **externa** (leak-safe), **nunca**
UAF — desde que a elisão só remova o wrapper da região, nunca redirecione uma alocação existente.
Compõe limpo com o DPS: um escopo sem alocação não conveia nada, então os brackets de conveyance nunca
disparam dentro dele.

---

## 5. DPS — retorno virtual na arena de quem chama (Ideia 3) — A CHAVE

> *"tratar return sempre como alocação do valor diretamente na arena de quem chamou a função… a função
> chamada teria a referência da arena do chamador e nosso return seria virtual — seta na arena do
> caller e sai."* — dono

### 5.1 O modelo

O callee recebe uma **referência ao destino na arena do caller** e o `return` escreve o valor **ali** e
sai — não há slot de frame local no callee para copiar. Isso é **destination-passing style (DPS)** para
retornos de agregado.

- O destino é alocado pelo caller na sua **região CORRENTE** (`alloc_call_dest`, via
  `region_current_vreg`) — **nunca** uma filha nova que poderia ser dropada por baixo do valor
  retornado (isso reabriria o UAF do arena-por-escopo). É exatamente a região onde o caller hoje
  alocaria o resultado depois de uma cópia.
- `return` vira **virtual**: constrói no destino + emite o `ret` nu. Sem cópia-out.
- **Tail merges que alimentam o retorno** (um `if`/`match` em posição de cauda) escrevem cada braço
  **no MESMO `ret_dest`** — fechando a fronteira tail-merge-into-return numa tacada.

### 5.2 O que ele retira e o que ele preserva

- **Retira `own_returned_value`** (`lower.tks:11715`) — a box post-hoc que hoje copia o agregado para
  storage que sobrevive ao frame, DEPOIS do fato. O DPS constrói-no-lugar-desde-o-início. O
  `own_returned_value` era um remendo retrofitado; o DPS é o modelo.
- **`frame_escape_guard`** (`frame_escape.tks:56`) fica **satisfeito por construção** para funções DPS
  (não há slot de frame para escapar) e **permanece** como rede de inversão para os casos residuais
  (retorno de registrador/closure).
- **`ret_dest = null` = caminho de hoje, byte-idêntico.** O DPS entra SÓ quando um destino é passado.
  Função de valor-registrador (escalar/enum/ponteiro) retorna em registrador, sem destino — DPS nunca se
  aplica.

### 5.3 As assinaturas (copiar verbatim de `ast-computed-arena-assessment-0.3.1.md` §4.2)

```teko
fn fn_returns_aggregate(f: checker::TFunction, ctx: LowerCtx) -> bool
fn with_ret_dest(ctx: LowerCtx, dest: u32 | null) -> LowerCtx
fn lower_return_into_dest(ctx: LowerCtx, r: checker::TReturn) -> LowerStmtOut | error
fn alloc_call_dest(ctx: LowerCtx, callee: checker::TFunction) -> Lowered
```

Funções existentes tocadas: `lower_return`/`lower_return_fat` (`lower.tks:7245`/`:7278`), `lower_call`
(`:1740`), `lower_block_value`/`lower_match` tail (`:10798`), retirar `own_returned_value` (`:11715`) no
caminho DPS.

### 5.4 DPS subsume `-> ref T`

O retorno por referência (`-> ref T`) é vestigial — **zero funções de produção o usam** (só uma probe e
duas regressões de rejeição). Seu único uso real era o *identity pass-down* (retornar um `ref` PARÂMETRO
inalterado). Sob DPS, todo retorno de agregado já aterrissa na storage do caller, então um retorno
tipado-referência é redundante: o caller já tem o valor onde queria. **DPS subsume todos os casos de
`-> ref T`** → o form é removido (Doc 2). Depois disso, `ref` sobrevive SÓ em **parâmetros**
(caller→callee, borrow write-through, ex. `grow_inplace(ref []T)`), nunca como escape callee→caller. Dois
mecanismos ortogonais: escape por DPS, borrow por F1.

---

## 6. Escape e as quatro fronteiras — onde um valor "sai" da sua região

O deep-root da corrupção nativa é: *agregados são o endereço de um slot de frame por-instrução, e não são
copiados-out de forma confiável nas fronteiras de conveyance.* Há **quatro fronteiras**, e cada uma tem
um dono:

| fronteira | exemplo | dono da cura |
|---|---|---|
| **retorno passando o slot de frame** | `return agregado` | **DPS** (§5) — nasce na arena do caller |
| **tail-merge alimentando retorno** | `return if…`, `match` em cauda | **DPS** (§5) — cada braço no mesmo `ret_dest` |
| **self-append / push em loop** | `push(ref buf, v)` em laço | **AL3** `grow_inplace` (§1.2) — não DPS |
| **acumulador by-address / merge não-cauda** | `ref acc: []str` escrito em loop | **região que sobrevive** (piso/AL3) — colocação, não drop |

**A regra única:** um valor que escapa **nasce na região cuja vida o cobre**. O piso da AST computa a
demanda; o DPS coloca o retorno na arena do caller; o AL3 mantém o append in-place sem abandonar. A
elisão nunca redireciona uma alocação existente (só remove wrapper vazio). **Nenhuma cura envolve dropar
mais cedo** — a falha do arena-por-escopo foi exatamente dropar cedo com alias vivo.

---

## 7. Multi-threading — a arena sob paralelismo

O paralelismo do teko é do **BACKEND pós-lowering**, e o modelo de arena sob threads é uma extensão
direta e provada da disciplina de região por-função.

### 7.1 Escopo: paralelizar o backend, NUNCA o lowering

Paraleliza-se `select`/`regalloc`/`encode` **por função**. O `lower_program` fica **SERIAL** — e isso é
o que desarma a ameaça mais perigosa: os **nomes ordinais** (`.Lstr<n>`, `.Lclofn<n>`) são atribuídos
DURANTE o lowering, na ordem de visita. Paralelizar o lowering renomearia símbolos e mataria o fixpoint.
Quando o fluxo chega ao encode, o `LModule` já está todo baixado com cada nome fixado em ordem serial —
o backend paralelo só CONSOME nomes já fixos.

### 7.2 Região-por-thread (por-lane)

Cada worker corre numa `tk_task` própria (`tk_task_current()` é `_Thread_local`) com root próprio.
Dentro do worker, a disciplina de região reusa-se tal-e-qual:

```
// a lane_region é criada pelo PAI (filha de tk_region_program(), task-agnóstica,
// sobrevive ao tk_task_end do worker) e passada ao worker; o worker só a ENTRA.
scratch = region_new(lane_region)             // filha da região-de-raia
region_enter(scratch)
ef = encode_lfunc_in_region_x86(funcs[i])     // isel/regalloc/encode bump-alocam no scratch
region_leave()
encoded[i] = clone_encoded_into(lane_region, ef)   // COPIA p/ a raia antes de dropar (sobrevive à barreira)
region_drop(scratch)                          // larga o scratch da função
```

- **Dois workers nunca bump-alocam a mesma região** → sem corrida de alocação, sem lock no caminho
  quente. `tk_g_region_gen` é atômico e só afeta a validade do `push_cache`, **nunca os bytes**.
- **Cópia-antes-do-drop:** `clone_encoded_into` copia o resultado para a `lane_region` (parent-visível,
  sobrevive à barreira) antes de largar o scratch. Os `str` de NOME (`Symbol.name`/`RelocX86.sym`)
  **não são copiados** — originam em `prog` (Grupo B, imutável, partilhado, sobrevive a tudo).

### 7.3 MAP paralelo → REDUCE serial ordenado

```
// FASE MAP (paralela): função i → worker (i % lanes); escrita no slot DISJUNTO encoded[i]
parallel for i in 0..funcs.len:
    encoded[i] = encode_lfunc_in_region_x86(funcs[i])
// BARREIRA (join) — o ÚNICO ponto de sincronização
// FASE REDUCE (serial, no pai, em ordem de ÍNDICE, nunca de conclusão):
var mt = empty_module_text_x86()
for i in 0..funcs.len:
    mt = fold_encoded_func_x86(mt, encoded[i])   // absolutiza offsets .text AQUI, serial e ordenado
finish_encoded_module_x86(mt, rodata, globals)
```

### 7.4 A prova de fixpoint sob paralelismo (byte-idêntico)

O objeto emitido é byte-idêntico para qualquer `lanes ≥ 1`. Duas invariantes:

- **(I1) Cada `e[i]` tem o mesmo valor em série e em paralelo.** `encode_lfunc_in_region_x86(funcs[i])` é
  função **pura** de `funcs[i]`: inputs = a `LFunc` + ABI constante; toca só memória per-task; outputs
  (bytes, syms com offsets relativos ao próprio `.text`, relocs) não dependem de outro `funcs[j]` nem de
  estado global mutável (sem interning no backend, `region_gen` irrelevante para bytes, rodata/globals
  lidos só no `finish`).
- **(I2) O fold é aplicado na mesma ordem `0,1,…,n-1`.** O REDUCE itera por ÍNDICE, lê `encoded[i]`,
  absolutiza offsets só aqui. A ordem de CONCLUSÃO dos workers afeta só QUANDO `encoded[i]` é preenchido,
  nunca a ORDEM em que é consumido.

Logo o output é byte-idêntico. **`gen2 == gen3` é preservado E é o próprio detector:** se um byte mudar,
uma das invariantes foi violada (escape para região errada, ou fold em ordem de conclusão) — PARAR e
reexaminar. `lanes = 1` reproduz o serial de hoje byte-a-byte (modo de bisecção).

### 7.5 `nproc` — default concedido pelo SO, cap no máximo do SO

`lanes` default = o paralelismo que o SO CONCEDE ao processo (`hardware_parallelism()`:
`sched_getaffinity` popcount no Linux, máscara de afinidade no Windows, `hw.logicalcpu` no macOS),
sempre `≥ 1`. Override explícito (`TEKO_BUILD_LANES`) clampado a `[1, os_max]`. Nunca sobre-subscreve.
Todo o fork-join fica dentro de UMA fase `phase_begin`/`phase_end` (codegen) → `dark` continua 0; o
overlap encolhe o `wall` da fase.

### 7.6 Isolate, task e a região imortal do programa (F2)

O isolamento de memória tem **duas camadas**:

1. **Fronteira de região (raiz própria) — JÁ EXISTE:** `#arena_size`/`tk_region_new(NULL)` — pai `NULL`,
   raiz de árvore independente, *"como se fosse outro programa"*. Suporta uma função sozinha na própria
   raiz, sem paralelismo real.
2. **Fronteira de task (raiz própria + thread de SO concorrente) — o pré-requisito bloqueante:**
   `tk_task`/`tk_task_current()`, as globais colapsadas por-task, `tk_arena_push`/`pop` sobre a raiz da
   task chamadora. Necessária para qualquer `spawn` que rode código Teko concorrente.

**A região do programa (F2):** depois que F1 parte a raiz única em N raízes de task, **não sobra raiz de
processo** para um singleton morar — cada task morre e sua raiz esvazia. Um `chan`, criado UMA vez pela
`main` e que sobrevive a todas as tasks, precisa de uma região **imortal, separada de qualquer raiz de
task**. F2 é essa região, e é **pré-requisito de F1**, não adicional.

### 7.7 IDs, não ponteiros — a regra do dono para tudo que cruza fronteira de task

> *"a main abre o canal e passa para a thread do orquestrador um id pra ele buscar a ref do canal
> somente leitura e para os handlers passa o id pra eles buscarem a ref de escrita."* — dono

A razão é memória: **um ponteiro para a arena de OUTRA task pendura no instante em que essa task rebobina
seu `arena_pop`.** Um `u64` não é um ponteiro — é um **NOME**, resolvido por consulta a um registro
processo-inteiro a cada uso, **nunca cacheado**. Um handle carrega o id e **nada mais**; todo predicado é
uma CHAMADA que consulta o registro pelo id, nunca um campo lido do handle (uma cópia do handle é
inofensiva por construção só se copiar um nome não envelhecer o nome).

```teko
pub type Isolate = struct { handle: u64 }   // só o id, zero estado observável em cache
pub type ChanId  = u64                       // o canal que a main abre e distribui por nome
```

### 7.8 `chan<T>` — MPSC (fan-in), a primitiva de dados

Fan-in: N escritores, 1 leitor. `Tx` (copiável — os N escritores são a metade que pode ser múltipla),
`Rx` (um só; um segundo `pop` de outra task é erro de runtime nomeado, nunca corrida silenciosa).

```teko
pub fn chan_bounded(cap: u64) -> u64      // canal LIMITADO com contrapressão — a lei
pub fn chan_writer(id: u64)   -> Tx | error
pub fn chan_reader(id: u64)   -> Rx | error
pub fn chan_is_open(id: u64)  -> bool     // consulta ao registro, NUNCA cacheado
```

`join` é a **única barreira de memória** do modelo v1: nenhuma leitura do que uma raia escreveu é
legítima antes dele. Canais são **limitados com contrapressão** por lei — um canal sem limite tem a
memória como único travão (ver ponto aberto §11).

---

## 8. DI — tempos de vida SÃO tempos de vida de arena

A injeção de dependência mapeia diretamente na árvore de regiões (detalhe pleno no Doc 2 / §7 do master
de superfície):

| lifetime | região | resolução |
|---|---|---|
| **singleton** | raiz (root) — once-guard, uma instância | slot na raiz |
| **scoped** | caminhada de ancestralidade de arena — a instância da arena-escopo do call-site | walk até a arena-escopo |
| **transient** | região corrente — instância nova a cada resolução | alloc na região atual |

- `svc<T>()` é um **intrínseco de comp-time** (sem ABI; o compilador substitui o call-site inline por
  código por-lifetime). Não é uma chamada de runtime.
- **Regra de escape:** um valor de serviço **nunca** é armazenado em campo, passado como argumento, ou
  retornado em código de usuário (`a.b = svc<S>()`, `fun(svc<S>())`, `return svc<S>()` são rejeitados). O
  backend é isento dessa proibição, mas os valores que ele segura permanecem **arena-bounded** (vivem na
  arena, dropam com ela) — é o caso especial das funções de background do backend sobre a arena.

---

## 9. Invariantes de segurança e casos de borda (checklist)

1. **UAF:** vida da arena ⊇ escopo ⊇ todo uso. Um valor que escapa nasce na região que o cobre (DPS pro
   retorno; piso/AL3 pros acumuladores). Nunca dropar região com referência viva pendurada.
2. **Overflow:** piso pré-calculado + crescimento por chunk-list (§1.1) — chunk nunca estoura, encadeia.
3. **Aliasing:** exclusividade F1 por fluxo (`borrow.tks`), independente de keyword. O destino DPS é
   **single-writer-por-construção do controle de fluxo** (o caller sintetiza UM destino, passa a UM
   callee, não lê até o retorno; o callee escreve uma vez por caminho de retorno) — não vem de `let`.
4. **Conservadorismo:** dúvida → escapa / não elide / roteia para a região externa (leak-safe, nunca
   UAF).
5. **Sem drop prematuro:** o DPS não dropa nada sob o callee; ele redireciona ONDE o callee escreve. A
   falha do arena-por-escopo (bulk-drop com alias vivo) não recorre.
6. **Threads:** cada worker na sua região; resultado copiado para a lane-region antes do drop; nomes em
   `prog` imutável; fold em ordem de índice. IDs entre tasks, nunca ponteiros.

---

## 10. Exemplos (superfície 0.3.1)

**(a) Retorno virtual — o valor nasce na arena de quem chama:**
```teko
type Point = struct { x: i64, y: i64 }

fn make(cond: bool): Point {          // retorno de agregado → recebe destino do caller (DPS)
    return if cond { Point { x: 1, y: 2 } } else { Point { x: 3, y: 4 } }
    // cada braço do `if` em cauda escreve DIRETO no ret_dest do caller — sem slot de frame, sem cópia
}

fn use_it() {
    var p = make(true)                // `p` já é a storage do destino; nada é copiado para cá
    print(p.x)                        // lê da arena de `use_it`, que vive ⊇ este uso
}
```

**(b) Elisão — braço sem alocação não abre região:**
```teko
fn classify(n: i64): i64 {
    if n < 0 { return -1 }            // braço-folha sem alocação → NENHUMA região aberta (elidida)
    var buf = [n, n * 2]              // este escopo aloca → região aberta normalmente
    return buf[1]
}
```

**(c) Append in-place — AL3, não DPS:**
```teko
fn collect(xs: []i64): []i64 {
    var out = []                      // slice
    for x in xs {
        push(ref out, x * 2)          // grow_inplace: acrescenta sem abandonar quando há cap (AL3)
    }
    return out                        // o retorno do agregado ainda é DPS (nasce na arena do caller)
}
```

**(d) Threads — canal por id, nunca ponteiro:**
```teko
fn pipeline() {
    var c = chan_bounded(1024)        // main abre o canal (vive na região F2, imortal)
    var w = chan_writer(c)            // handlers recebem o ID `c`, buscam a ref de escrita por nome
    var r = chan_reader(c)            // o orquestrador recebe o ID `c`, busca a ref de leitura
    // ... spawn passa `c` (um u64), nunca &c: um ponteiro penduraria quando a task rebobinasse a arena
}
```

---

## 11. Pontos em aberto (marcados para tua validação)

1. **% de escopos-folha elidíveis** — estimativa 20–45%, **não medido**. A caminhada estática dá o
   denominador; o profiler dá quais eram quentes. (Não bloqueia; afeta só a magnitude do ganho.)
2. **MB exato que o DPS reclama** — bounded pelo volume de box do `own_returned_value`, **não medido** até
   instrumentar (o campo `copy_bytes` do profiler carrega isso).
3. **`chan_unbounded`** — pedido na superfície, mas reabre o risco de OOM que a lei "limitado, com
   contrapressão" existe para fechar. **Decisão do dono:** mantém como superfície com aviso, ou remove?
4. **DI `scoped` sob threads** — a caminhada de ancestralidade de arena para `scoped` interage com as
   lane-regions (§7.2): uma dependência `scoped` resolvida dentro de um worker resolve contra a
   lane-region ou contra a arena-escopo lógica do trabalho? **Novo — precisa da tua ruling** (não estava
   nos docs de origem; nomeio para não passar por decidido).
5. **`spawn <call-expr>`** (açúcar `spawn orquestrar(c)` vs. a assinatura `spawn(entry, ctx, lane)`) —
   registrado como açúcar FUTURO, não fechado aqui. Confirmar que fica fora do escopo desta onda.
