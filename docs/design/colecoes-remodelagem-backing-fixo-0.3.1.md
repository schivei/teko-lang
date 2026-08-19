---
section: design
created: 2026-08-19
updated: 2026-08-19
status: DESIGN — no product line. R9 (arena remount, origin/fix/retirement @ f380e593): remodel EVERY
        growable collection onto FIXED-ARRAY backing. **REWORK (owner 2026-08-19):** the earlier
        VALUE-COPY design (grow = copy values into a new fixed backing + drop the old) LEAKS — the dead
        value-array is stranded in the object BUMP arena with no individual free, so a full dead copy
        accumulates on EVERY grow. Owner (verbatim): *"esse design do arquiteto sem levar em conta que
        temos ponteiros faz a memória vazar."* This revision SUPERSEDES it with the POINTER-INDEX model
        (owner's fix) and adds a THIRD, array-free (node-linked) alternative + a side-by-side comparison
        with a per-collection recommendation. F1 is NOT up for relaxation. This doc IS the deliverable —
        no product `.tks` edited, no build, no reseed, no `teko test`.
source: arena-escopada-stream-expurgo-0.3.1.md (§5, §5.3, R9 — the removed primitives + the four
        conversion natures; §2.3/§2.4 dedicated per-object region F2, `:169-213`),
        arena-especificacao-unica-0.3.1.md (§7 concurrency, is_unique_at, §7.8 per-entry free-list
        `:542-548`; §2.3/§2.4 dedicated per-object region; §6 escape rule "dúvida → escapa"),
        plano-collections-genericas-e-concorrentes-0.3.1.md (the growable-collection census + the
        concurrent model B), CLAUDE.md (NO PUSHES, ZERO CRESCIMENTO + purge-imediato, the fixed-array
        ZERO-FILL/`count` form).
frozen: bootstrap/teko.c + the C twins are OUTPUT/FROZEN; the slice-grow machine is REMOVED, not patched.
        New product work is `.tks` only. Drains DIRECT into fix/retirement (no PR).
---

# R9 — remodelagem das coleções growable sobre backing FIXO (0.3.1)

Architect, 2026-08-19 (rework). Base: `origin/fix/retirement` @ `f380e593`. Documento de DESIGN — sem
linha de produto. Responde à ordem do dono: olhar CADA coleção e PROPOR a remodelagem sobre backing fixo.
**Nenhuma proposta relaxa F1** (arrays continuam de tamanho fixo). Esta é a REVISÃO que o dono mandou
fazer: o desenho de cópia-de-valor da primeira versão VAZA (§0.5), e é substituído pelo **modelo
ponteiro-índice** (§2), acompanhado de uma **terceira alternativa sem arrays** (§9, coleções ligadas por
nós) e uma **comparação lado a lado com recomendação por coleção** (§10).

---

## 0. A lei inviolável e a forma-âncora (o que TODA remodelagem obedece)

**F1 (arena remount, §5/§5.3/R9 de `arena-escopada-stream-expurgo-0.3.1.md`):** um `[]T` é FIXO. NÃO há
`push`/`grow_inplace`/`with_cap`/mutação de comprimento in-place, e NENHUM método growable sobrevive.
Para "crescer" aloca-se um NOVO array fixo (tamanho exato conhecido NAQUELA alocação) e DROPA-se o velho.
"Capacidade" mora SÓ no WRAPPER da coleção (um watermark `count ≤ backing.len` sobre um backing fixo).
**O que a REVISÃO muda:** o array fixo que cresce por new+free-old é o **ÍNDICE de ponteiros**, não um
array de valores — os valores nunca são recopiados (§2). Isto NÃO relaxa F1: o índice é 100% array fixo,
crescido por novo-array-fixo+dropa-velho, jamais resize in-place.

**A forma-âncora do array fixo (CLAUDE.md "FORMA DO ARRAY FIXO — ZERO-FILL, `count` UNIVERSAL"):** header
`{ptr, len}` (sem `cap`, sem tag). `of_len<T>(n): []T` = `memset`-zero, uma passada. Presença = watermark
`count`. Índice-assign `xs[i] = v` é PREFERIDO.

**A dependência declarada.** A maquinaria `of_len<T>(n)` + índice-assign + o idioma `count` é o passo-1 do
expurgo (`arena-escopada-stream-expurgo-0.3.1.md:501-505`) — hoje AUSENTE da árvore (o typer só conhece
`empty`/`push`/`with_cap`/`grow_inplace`, `typer.tks:805-835`). Esta remodelagem CONTRATA contra a forma
DECLARADA de `of_len`. O modelo ponteiro-índice e a alternativa por nós exigem **duas primitivas de
superfície a mais**, declaradas em §2.6 (arena-place, deref-por-cópia, free-de-slot-individual). Tudo o
mais aqui — o desenho, os contratos de tipo, as fixtures, os skeletons — está desbloqueado HOJE.

**As quatro naturezas de conversão (CLAUDE.md, lei permanente):** 1. **MAP** → `of_len(fonte.len)` +
índice; 2. **PARSE/SCAN** → conta `n`, depois `of_len(n)`; 3. **FILTRO** → alarga, grava `count`, corta;
4. **BUFFER DE SAÍDA** → literais + interpolação. As coleções genéricas instanciam sobretudo MAP.

**A lei do purge imediato (CLAUDE.md "ZERO CRESCIMENTO DINÂMICO + PURGE IMEDIATO NA REATRIBUIÇÃO"):** ao
`self.<campo> = <novo>`, o valor ANTERIOR é purgado IMEDIATAMENTE — o gatilho é a reatribuição, a liberação
é eager. No modelo ponteiro-índice há UM ponto de reatribuição (o `self.index = grow_index(...)`), e é lá
que o purge do índice velho ocorre — **e é seguro fazê-lo eager porque o índice velho NÃO carrega dados**
(carrega só ponteiros para valores que continuam vivos na arena do objeto). É essa propriedade que fecha o
vazamento.

---

## 0.5 O VAZAMENTO — por que o design cópia-de-valor foi SUPERSEDED (a razão da rework)

A primeira versão deste doc (o §2 "wrapper watermark + `ensure_cap`" de cópia de valor) crescia assim:
`ensure_cap` alocava um NOVO backing fixo de valores, **copiava os valores** `[0..count)` para dentro dele,
e devolvia — o chamador reatribuía `self.backing`, dropando o backing velho.

**O furo (achado do dono).** O backing velho de VALORES fica preso na arena BUMP dedicada do objeto (F2,
§2.3), que **não tem free individual** — só bulk-drop na morte do objeto. Logo cada `push` que cruza a
capacidade deixa para trás **uma cópia MORTA e COMPLETA do array de valores** que não dá para liberar
enquanto a coleção viver. Sobre N appends, o total de valores-mortos empilhados é 4+8+…+N/2 ≈ N — ou seja,
**a memória de valores vazada cresce linear com o tamanho vivo**, dobrando de fato o pico e nunca
reclamável. Owner, verbatim: *"esse design do arquiteto sem levar em conta que temos ponteiros faz a
memória vazar."* A série geométrica "≤2× e reclama na morte do objeto" que a v1 alegava é justamente o
vazamento: enquanto a coleção viver, o 2× fica retido.

**Por que o §7.8 free-list não salvava a v1 sozinho.** Poder-se-ia individualizar o free do backing velho
de valores via §7.8 — mas isso liberaria o array INTEIRO de valores (dados vivos misturados com slots
mortos), e os dados vivos ainda teriam de ser COPIADOS para o novo antes do free. O custo é a cópia de
VALORES a cada grow (caro para elementos grandes) e a fragmentação de blocos grandes no free-list. O
modelo certo (abaixo) não copia valor NENHUM.

**A correção (regra do dono): ponteiros.** *"os valores já estão na arena da coleção… enquanto a coleção
viver o dado vive."* O que cresce passa a ser um **índice de ponteiros** — os valores são escritos UMA vez
na arena do objeto e NUNCA se movem; o grow copia só os ponteiros (baratos, largura uniforme) e libera o
índice velho na hora (ele não segura dado). Zero cópia de valor, zero array-morto retido. §2 detalha.

**O §2 antigo (cópia de valor) está SUPERSEDED e foi substituído pelo §2 abaixo.** O RECON (§1), as
fixtures (§6) e a sequência de crumbs (§7) permanecem (a enumeração dos métodos growable não muda); o que
muda é o MECANISMO de crescimento.

---

## 1. RECON — enumeração EXAUSTIVA de todo método growable (file:line, na árvore correta)

Duas populações: as coleções JÁ EMBARCADAS em `src/collections/` (+ `src/list/`) e as coleções de DESIGN
do plano (`plano-collections-genericas-e-concorrentes-0.3.1.md`). Ambas remodeladas aqui.

### 1.1 Coleções embarcadas (`src/collections/`, `src/list/`)

| coleção / método | file:line | primitivo removido em que anda |
|---|---|---|
| `List<T>::push` | `src/collections/list.tks:15` | `teko::list::push` (value copy-grow) |
| `List<T>::set`/`pop`/`remove_at` | `list.tks:21,24,27` | `arr_replace_at`/`arr_drop_last`/`arr_drop_at` (que andam em `push`) |
| `Map<V>::insert` | `src/collections/map.tks:31-33` | 3× `teko::list::push` (keys/hashes/vals) |
| `Map<V>::remove` | `map.tks:47-49` | 3× `arr_drop_at` |
| `Dictionary<K,V>::insert` | `src/collections/dictionary.tks:31-33` | 3× `teko::list::push` |
| `Dictionary<K,V>::remove` | `dictionary.tks:47-49` | `arr_drop_at`/`arr_drop_u64_at`/`arr_drop_at` |
| `HashSet<T>::add` | `src/collections/hashset.tks:21-22` | 2× `teko::list::push` (hashes/items) |
| `HashSet<T>::remove` | `hashset.tks:35-36` | 2× `arr_drop_*` |
| `SortedSet<T>::add` | `src/collections/sorted_set.tks:16` | `sorted_insert` → `arr_insert_at` (push) |
| `SortedDictionary<K,V>::insert` | `src/collections/sorted_dictionary.tks:30-31` | 2× `arr_insert_at` (push) |
| `PriorityQueue<T>::enqueue` | `src/collections/priority_queue.tks:16-17` | `teko::list::push` + `heap_sift_up` |
| `PriorityQueue<T>::dequeue` | `priority_queue.tks:24-28` | `heap_pop_min` → `arr_drop_last`/`arr_replace_at`/`arr_swap` |
| combinadores `arr_replace_at`/`arr_drop_u64_at`/`arr_drop_at`/`arr_drop_last`/`arr_insert_at`/`arr_swap`/`arr_reverse`/`arr_slice` | `src/collections/collections.tks:2-92` | TODOS abrem `teko::list::empty()` + acumulam por `teko::list::push` |
| helpers `sorted_insert`/`heap_sift_up`/`heap_pop_min` | `collections.tks:94-145` | `arr_insert_at`/`arr_swap`/`arr_drop_last`/`arr_replace_at` |
| `teko::list::grow<T>(ref x, v)` | `src/list/list.tks:1-3` | `teko::list::push` (o choke-point folha) |

### 1.2 Coleções de DESIGN (plano; ainda sem `.tks`) — remodeladas na mesma varredura

| coleção / método | file:line no plano | primitivo |
|---|---|---|
| `Dictionary<K,V>::insert` (snippet) | `plano-collections-…:233-235` | 3× `teko::list::push` |
| `SortedSet<T>::add` (snippet) | `plano-…:302` | `sorted_insert<T>` |
| `PriorityQueue<T>::enqueue` (snippet) | `plano-…:333` | `teko::list::push` + `heap_sift_up<T>` |
| `Deque<T>`/`Queue<T>`/`Stack<T>` (FASE 1a) | `plano-…:176-178,757` | `List<T>` interno / `arr_insert_at` nas duas pontas |
| `LinkedList<T>` (free-list) | `plano-…:178,350-352` | arrays paralelos `next`/`prev`/`items` (push) |
| `ConcurrentStack<T>::push` | `plano-…:616` | `teko::list::push` sob CAS (o caso duro) |
| `ConcurrentDictionary<K,V>::insert` | `plano-…:555-557` | 3× `teko::list::push` sob lock striped |
| `ConcurrentQueue<T>`/`ConcurrentBag<T>`/`BlockingCollection<T>` | `plano-…:494-498,765` | nós por índice / `List<T>` por tarefa / ring bounded |

`ls src/collections/` = collections, dictionary, hashset, list, map, priority_queue, sorted_dictionary,
sorted_set. A enumeração está completa.

---

## 2. ALTERNATIVA 1 — O MODELO PONTEIRO-ÍNDICE (a correção do dono; SUBSTITUI o §2 de cópia-de-valor)

O backing deixa de guardar VALORES inline e passa a guardar **PONTEIROS** — um **índice** de largura
uniforme (uma palavra por slot). Os valores vivem na **arena dedicada do objeto** (F2, §2.3), escritos UMA
vez e **nunca recopiados**; o índice aponta para eles. Enquanto a coleção viver, o dado vive.

### 2.1 Os DOIS regimes de memória (o ponto que fecha o vazamento — declarar explícito)

O modelo tem DOIS regimes distintos, e é a separação deles que elimina o leak:

- **(a) VALORES = sub-regiões reparentáveis aninhadas na arena dedicada da coleção (§2.3), escritas UMA
  vez.** Um valor entra na coleção → é colocado por `arena_place` (§2.6) na SUA PRÓPRIA sub-região
  reparentável, aninhada na arena da coleção, e ganha um ponteiro estável; NUNCA é movido nem recopiado por
  um grow. A arena da coleção segura APENAS os elementos VIVOS: um elemento removido **MIGRA para fora** por
  reparent (§2.4a), então não há acúmulo nem high-water; os elementos que nunca saem caem no bulk-drop da
  morte da coleção. É arena-nativo (regiões + reparent/drop), correto, sem vazamento — porque nada aloca um
  "novo array de valores" a cada grow, e removidos não ficam presos.
- **(b) O ÍNDICE = alocado/liberado INDIVIDUALMENTE por grow.** O índice É um array fixo de ponteiros;
  crescer aloca um novo índice e **libera o velho na hora**. Para o velho ser liberável eager, o índice
  **NÃO pode morar na bump-arena do objeto** (que só faz bulk-drop) — tem de ser um **alloc/free direto,
  ou o seu próprio slot droppable / sub-região** (§2.6). **Se o índice morar em bump, é o ÍNDICE que
  vaza** (o mesmo erro da v1, transposto para o índice). Esta é a exigência dura do modelo.

A v1 tinha só o regime (a) mal-usado: alocava um array de VALORES novo a cada grow e o velho ficava no
bump sem free → leak. A revisão põe os VALORES em (a) escritos-uma-vez (nunca realoca array de valor) e o
ÍNDICE em (b) individualmente liberável → **zero grow-leak por construção**.

**Elemento OBJETO NÃO usa o regime (a):** um objeto NÃO reside na arena da coleção — vive na sua própria
arena por-objeto (F2), e só o seu PONTEIRO ocupa um slot do índice (§2.2b). Logo o regime (a) só governa
elementos VALOR (escalar-inline e value-struct grande); objetos são referência pura, geridos por
residência/LUB, fora da arena da coleção.

### 2.2 Política de armazenamento do elemento (default aprovado pelo dono)

O slot do índice tem largura uniforme (uma palavra). O que ele carrega depende da CLASSE DE
ARMAZENAMENTO de `T`, decidida na **monomorfização** (o compilador conhece `sizeof(T)` no ponto de
instanciação):

- **(a) escalar / valor que CABE numa palavra** (número, byte, char, bool, ponteiro) → **INLINE no slot do
  índice**. Não há armazenamento separado nem indireção; o índice É o armazenamento. Remover = baixar o
  watermark (pop é só uma cópia-pra-fora; nada a reclamar). Grow copia as palavras (que são os próprios
  valores) e libera o índice velho. **Sem vazamento, sem indireção, sem sub-região** — e note que aqui não
  existe "array de valores à parte" para ficar preso, logo o leak da v1 nem se forma neste caso. É a
  degeneração eficiente do modelo.
- **(b) elemento OBJETO → REFERÊNCIA C#-like (ruling do dono; NÃO é possuído pela coleção, NÃO escapa
  para a arena da coleção).** O objeto vive na **SUA PRÓPRIA arena dedicada por-objeto (F2, §2.3)** — a mesma
  onde nasceu. A coleção guarda SÓ um **PONTEIRO (referência)** para essa região no slot do índice — como em
  C#. Dono, verbatim: *"como os objetos têm arena própria o que se copia é o ponteiro desta região (um novo
  ponteiro para a mesma região)… a cópia é do ponteiro e não dos valores; se quer deep copy, aí a estratégia
  é outra."* Portanto: **`push` = copia o PONTEIRO** (novo ponteiro para a mesma região, zero cópia de
  valor); grow copia os ponteiros, libera o índice velho — o objeto nunca se move e nunca é colocado na arena
  da coleção. **`get`/`pop` = copiam o PONTEIRO** (nova referência à mesma região). **`remove` = SOLTA o
  ponteiro** (baixa o watermark / desloca ponteiros). **NUNCA há transferência-de-região do objeto, NUNCA a
  arena do executor recebe o storage do objeto.** A **vida do objeto = RESIDÊNCIA = o LUB de TODAS as
  referências vivas** (a coleção, o binding original, qualquer resultado de `get`) — o `pt_join`/escape-check
  já existente: a arena do objeto só cai quando a ÚLTIMA referência sai de escopo → **zero UAF**, e múltiplas
  referências ao mesmo objeto é comportamento CORRETO (mutar via uma reflete nas outras — é o mesmo objeto),
  não bug. **Deep copy é operação EXPLÍCITA e separada (um clone), jamais o default de `get`/`pop`.**
- **(c) value-struct GRANDE** (não cabe numa palavra, guardado por VALOR — não é objeto) → o slot guarda um
  ponteiro para a cópia do value-struct colocada UMA vez na **sua própria sub-região reparentável** na arena
  da coleção por `arena_place`. Grow copia os ponteiros, libera o índice velho; o value-struct não se move.
  Por ser VALOR (não referência): **remover = REPARENT** dessa sub-região para o executor (ou region_drop se
  descartado); **set-overwrite = region_drop** da antiga. É o padrão arena-nativo de VALOR — NÃO há free-list
  por-entrada nem high-water no remove/pop comum. (O §7.8 fica reservado ao caso imortal-F2 concorrente, §4 —
  nunca ao remove/pop ordinário.)

### 2.3 O GROW — novo índice, copia PONTEIROS, zero-fill do velho, FREE do velho (os valores não se movem)

```teko
/**
 * grow_index — the SINGLE growth choke point of the pointer-index model. Values never move: this only
 * grows the INDEX (a fixed pointer array whose `len` is the capacity). Allocates a NEW fixed index of
 * `grow_cap`-sized capacity, copies the live `[0..count)` POINTERS by index, zero-fills the old index,
 * and frees the old index immediately (it holds no data — only pointers into the object arena whose
 * targets stay alive). The caller reassigns its field; the reassign law's purge is satisfied by the
 * eager free here. Arrays stay fixed: a brand-new fixed pointer array replaces the old; never a resize.
 *
 * @param index  the current fixed pointer index (capacity == index.len)
 * @param count  the live watermark (count <= index.len)
 * @param want   the minimum capacity the caller needs after this call
 * @return       a fixed pointer index with capacity >= want, holding the same live pointer prefix
 * @since 0.3.1
 */
fn grow_index<T>(index: []*T, count: u64, want: u64): []*T {
    if want <= index.len { return index }
    var new_cap = grow_cap(index.len)
    loop { if new_cap >= want { break } new_cap = grow_cap(new_cap) }
    var next: []*T = of_len_ptr<T>(new_cap)
    var i: u64 = 0
    loop { if i >= count { break } next[i] = index[i]; i++ }
    zero_fill_ptr<T>(index)
    free_index<T>(index)
    next
}

/**
 * grow_cap — the geometric capacity policy in ONE place: given the current INDEX capacity, returns the
 * next fixed pointer-index size. Doubling (factor 2) from a small floor (4) is the default; because the
 * index holds only pointers (uniform width, no value copies), the waste of a doubling policy is at most a
 * few spare pointer slots — cheap. This is NOT a growable array: it only names the size of the NEXT fixed
 * pointer allocation.
 *
 * @param old_cap  the current index capacity (0 for a fresh collection)
 * @return         the capacity of the next fixed index (strictly greater than old_cap)
 * @since 0.3.1
 */
fn grow_cap(old_cap: u64): u64 {
    if old_cap == 0 { return 4 }
    old_cap * 2
}
```

**A amortização.** `grow_index` só aloca quando `count == index.len`, e dobra. Sobre N appends há
O(log N) crescimentos, copiando 4+8+…+N ≈ 2N **ponteiros** (não valores) → O(1) amortizado por append. O
pico transitório extra é `factor × count × sizeof(ptr)` — só ponteiros, e o índice velho é **liberado na
hora**, então nem esse pico persiste. **Zero valor copiado; zero array-morto retido.**

### 2.4 READ/WRITE por CÓPIA (regra do dono: *"Se eu buscar uma informação, ou mandar gravar, deve ser feito por cópia"*)

"Cópia" tem DOIS sentidos, decididos pela classe de `T` (monomorfização): **VALOR** = cópia do valor;
**OBJETO** = cópia do PONTEIRO (referência C#-like, §2.2b) — nunca deep copy no default.

- **`get(i)`**: **VALOR** → deref de `index[i]` e copia o valor para fora (`copy_out<T>`); **OBJETO** →
  copia o PONTEIRO `index[i]` (nova referência à mesma região). Em ambos, nada do storage interno da
  coleção escapa como alias mutável de VALOR.
- **`set(i, x)`**: **VALOR** → escalar-inline `index[i] = x`; value-struct → `region_drop` do antigo +
  `arena_place` do novo. **OBJETO** → **sobrescreve o slot com o novo PONTEIRO** (solta a referência antiga;
  a região do objeto antigo cai quando a sua última referência sair — residência/LUB). Sem acúmulo.
- **`push(x)`**: **VALOR** → `arena_place` de `x` (uma vez, na sua sub-região na arena da coleção). **OBJETO**
  → grava o PONTEIRO de `x` em `index[count]` (copia o ponteiro, zero cópia de valor; o objeto fica na sua
  arena). `count++`; cresce o índice antes se cheio. O índice nunca copia valor de novo.

**A REGRA DE DESLOCAMENTO — PARTIDA LIMPA por classe (ruling do dono).** pop/remove/set têm DUAS semânticas
completamente distintas, escolhidas pela classe de `T`:

- **(a) Elemento VALOR (mora NA arena da coleção) = TRANSFERÊNCIA DE REGIÃO** (o move-on-return / DPS,
  §5 canonical de `arena-especificacao-unica-0.3.1.md`):
  - **`x[0] = 5` num array de ESCALARES** → OVERWRITE LIMPO de memória, sem transferência, nada a reclamar.
  - **`pop()`/`remove(i)`/`replace` de VALUE-STRUCT** → a sub-região do elemento deslocado é TRANSFERIDA
    (reparent) para a arena de QUEM EXECUTOU; o executor passa a cuidar do tempo de vida (morre na saída do
    escopo do executor). Resultado NÃO ligado (ex. `remove(0)` sem receber) → morre de IMEDIATO, um
    `region_drop` deliberado. É a lei do purge-na-reatribuição: o purgado vai para a arena do executor.
- **(b) Elemento OBJETO (mora na SUA PRÓPRIA arena) = CÓPIA/SOLTURA DE PONTEIRO — NADA de transferência de
  região, NADA de arena do executor recebendo o storage do objeto:**
  - **`get`/`pop`** → copiam o PONTEIRO (nova referência à mesma região do objeto).
  - **`remove`** → SOLTA o ponteiro (baixa o watermark / desloca ponteiros); a região do objeto NÃO se move.
  - **Vida por RESIDÊNCIA/LUB** (`pt_join`/escape-check): a arena do objeto cai só quando a ÚLTIMA
    referência viva (coleção, binding original, resultado de `get`) sair de escopo → zero UAF. Múltiplas
    referências ao mesmo objeto = comportamento correto.

Consequência: a arena da coleção segura APENAS os VALORES vivos (objetos nem estão nela); o churn/high-water
de valor é NULO (reparent/drop), e o objeto é referência normal (sem churn de arena). **Por que é sempre
seguro:** VALOR — ler/gravar por cópia ⇒ nenhum alias vivo do valor fora da coleção ⇒ dono único ⇒
transferir a região não dá dangling; OBJETO — a residência/LUB garante que a região vive enquanto QUALQUER
referência viver ⇒ soltar uma referência nunca pendura as outras.

### 2.5 O modelo de OBJETO — RULED pelo dono (Q3 fechado): arena-própria + referência + LUB

O dono cravou (não é mais martelo em aberto): um elemento **OBJETO não é possuído pela coleção e não escapa
para a arena dela**. Fica registrado o modelo e por que ele DISPENSA a antiga dúvida "escape vs borrow".

**O modelo RULED (C#-like), verbatim do dono:** *"como os objetos têm arena própria o que se copia é o
ponteiro desta região (um novo ponteiro para a mesma região), como em C#… a cópia é do ponteiro e não dos
valores; se quer deep copy, aí a estratégia é outra."*

- **Posse:** o objeto NÃO é possuído pela coleção; vive na SUA PRÓPRIA arena por-objeto (F2, onde nasceu).
  A coleção guarda só um PONTEIRO (referência) no slot do índice.
- **`push`/`get`/`pop`:** copiam o PONTEIRO (nova referência à mesma região) — zero cópia de valor, zero
  `arena_place` na arena da coleção. `remove` solta o ponteiro. O objeto NUNCA se move.
- **Vida = RESIDÊNCIA = LUB de todas as referências vivas** (coleção + binding original + resultados de
  `get`), via o `pt_join`/escape-check já existente. A arena do objeto cai só quando a ÚLTIMA referência sai
  de escopo. **Zero UAF por construção**, e múltiplas referências ao mesmo objeto é o comportamento CORRETO
  (mutar por uma reflete nas outras — mesmo objeto), não bug.
- **Deep copy** (clone independente) é operação EXPLÍCITA e separada — jamais o default de `get`/`pop`.

**Por que isto DISPENSA a antiga dúvida "escape vs borrow" (que estava ERRADA):** não há escape do objeto
para a coleção (a coleção nunca vira dona) NEM borrow-com-outlives-a-provar (não é empréstimo temporário; é
referência de 1ª classe cuja região vive pelo LUB). É exatamente a semântica de referência de `class` que a
linguagem já tem — a coleção é só mais um detentor de referência, como qualquer variável. Nada de
transferência-de-região, nada de cópia de valor, nada de escape-analysis nova no caminho quente do `push`.
**O único caso que ainda transfere região é o elemento VALOR** (§2.4a-a: escalar-inline overwrite,
value-struct reparent/drop) — porque VALOR mora na arena da coleção; OBJETO não.

### 2.6 Doubling+watermark vs exact-grow NO ÍNDICE (fork agora BAIXO-RISCO)

Com o índice sendo ponteiros baratos de largura uniforme e SEM cópia de valor no grow, o fork perde peso:

- **(1) Doubling + watermark** (o `grow_cap` acima): amortizado O(1) por append; desperdício = alguns slots
  de PONTEIRO sobressalentes (≤ `count` ponteiros, i.e. ≤ `count × 8 B`) — barato, e some quando o índice
  velho é liberado.
- **(2) Exact-grow** (índice cresce exatamente para `count+1` a cada append): zero desperdício de slot, mas
  **cada** append realoca o índice e recopia todos os ponteiros → O(n) por append, O(n²) total. Só faz
  sentido quando a coleção é praticamente imutável após construção (build-once).

**RECOMENDAÇÃO: doubling+watermark como default** (o custo do desperdício é ponteiros, não valores — a
razão que tornava exact-grow atraente na v1, evitar copiar valores caros, DESAPARECEU no modelo
ponteiro-índice). Exact-grow fica como modo opcional `build_exact` para coleções sabidamente build-once
(ex.: um snapshot literal). Um `grow_cap` de fator 1.5 (`old + old/2`) é o swap de uma linha se o pico de
ponteiros importar mais que a contagem de grows.

### 2.7 As primitivas de superfície que o modelo declara (design-ahead; contrato)

O modelo ponteiro-índice precisa de três capacidades além do `of_len` já declarado. Contrato aqui;
implementação resume quando semearem:

*VALOR (regime a):*
- **`arena_place<T>(v: T): *T`** — escreve o VALOR `v` UMA vez na SUA PRÓPRIA sub-região reparentável,
  aninhada na arena da coleção (§2.3), e devolve um ponteiro estável. **Só para elemento VALOR** (value-struct
  grande); escalar-inline não a usa; OBJETO **NUNCA** a usa (o objeto não entra na arena da coleção).
- **`copy_out<T>(p: *T): T`** — deref + cópia do VALOR para fora (o read-por-cópia de §2.4).
- **`write_in<T>(p: *T, x: T)`** — o `set(i,x)` de VALOR: inline escreve o slot; value-struct faz
  `region_drop` do alvo antigo e `arena_place` do novo, reapontando (§2.4).
- **`region_reparent(elem_region, dest_region)`** — TRANSFERE a sub-região de um VALOR removido para a arena
  do executor (o reparent O(1) do pop/remove de VALOR, §2.4a-a). Arena-nativo (move-on-return, §5 canonical),
  NÃO um free-list. **Não se aplica a objeto.**
- **`region_drop(elem_region)`** — descarta a sub-região de um VALOR (O(1) deliberado) no set-overwrite ou
  no remove-descartado.

*OBJETO (referência C#-like — nenhuma primitiva de arena da coleção; usa a maquinaria de referência que já
existe):*
- **cópia de ponteiro** — `push`/`get`/`pop` copiam o ponteiro do slot (nova referência à mesma região). É a
  cópia de referência de `class` que a monomorfização JÁ produz — nada novo a semear.
- **soltura de ponteiro** — `remove`/`set` sobrescreve/limpa o slot; a vida do objeto segue por
  **RESIDÊNCIA/LUB** (`pt_join`/escape-check já existente). Nenhum `arena_place`/`reparent`/`drop` da coleção.

*ÍNDICE (regime b):*
- **`of_len_ptr<T>(n): []*T`** (índice fixo de ponteiros zero-fill), **`free_index<T>(index)`** (free eager
  do índice velho), **`zero_fill_ptr`**. Duas realizações: (i) alloc/free direto (fora do bump do objeto) ou
  (ii) sub-região droppable própria com `region_drop`. NÃO pode ser bump do objeto (senão o índice vaza).

Nenhuma é o free-list §7.8 (reservado ao imortal-F2 concorrente, §4). Nenhuma toca `teko_rt.c` (arena 100%
Teko). Onde a árvore ainda não expõe `*T` cru, o implementer usa o `[]T`-de-referência que a monomorfização
já produz para OBJETO (uma `class` já é referência — este caso NEM PRECISA de primitiva nova) e o `of_len`
para o índice; escalar-inline e value-struct-grande (VALOR) são as extensões que `arena_place`/`copy_out`/
`write_in`/`region_*` nomeiam. **Bloqueado até `of_len`+índice-assign+`arena_place` (para VALOR) semearem; o
caminho OBJETO compila hoje (é referência pura).**

---

## 3. Remodelagem por coleção (modelo ponteiro-índice)

### 3.1 `List<T>` — a sequência base

- **Atual.** `intern items: []T` (`list.tks:3`); `push` = `self.items = teko::list::push(...)` (`:15`,
  value copy-grow — o leak).
- **Remodelado (ponteiro-índice).** Campos `{ index: []*T, count: u32 }`. `push(x)`: **VALOR** `arena_place(x)`
  / **OBJETO** copia o ponteiro de `x` (referência à sua arena, §2.5); `self.index =
  grow_index(self.index, self.count, self.count+1)` (só se cheio), `self.index[self.count] = <ptr>`,
  `count++`. `get(i)` = **VALOR** `copy_out(self.index[i])` / **OBJETO** copia o ponteiro. `set(i,x)` =
  **VALOR** region_drop do alvo antigo + `arena_place` do novo (inline: `index[i]=x`) / **OBJETO** sobrescreve
  o slot com o novo ponteiro (solta a referência antiga). `pop()`/`remove_at(i)`: **VALOR** reparenta/dropa a
  sub-região do elemento para o executor (inline: cópia-pra-fora) / **OBJETO** SOLTA o ponteiro (a região do
  objeto segue por residência/LUB — nunca transfere); `count--`. **`push`/`pop` no FIM = O(1)** (só o
  watermark). `remove_at(i)` também faz **shift-left compactando os PONTEIROS `[i+1..count)`**. **CUSTO EXPLÍCITO
  (eixo de 1ª classe, §10): o shift preservando ordem é O(n) por remove/insert no MEIO ou na FRENTE → O(n²)
  sob churn frontal/meio.** Move ponteiros, não valores, mas é O(n) mesmo assim. Um índice-array NUNCA é
  O(1) no meio arbitrário — só nas pontas (e O(1) nas DUAS pontas exige um ring/deque com wrap, §3.7). Para
  workload de remove/insert frontal/meio pesado, o vencedor é alt-3 node-linked (O(1) dado o nó, zero shift).
  `to_array()` = snapshot: `of_len(count)` + `copy_out` de cada slot (cópia independente — nunca partilha o
  índice; senão a view penduraria quando a lista crescesse). Escalar-inline (§2.2a) degenera para
  `index: []T` inline sem `arena_place`.
- **Grow/drop:** §2.3 — índice geométrico; índice velho liberado eager; valores intactos na arena.

### 3.2 `Map<V>` / `Dictionary<K,V>` / `HashSet<T>` — as tabelas por hash

- **Atual (o que a árvore REALMENTE faz).** Varredura LINEAR sobre arrays paralelos, NÃO open-addressing:
  `Dictionary` guarda `keys`/`hashes`/`vals` (`dictionary.tks:3-5`), acha por `dict_find_index` loop linear
  (`:26,38`); `insert` faz 3× `teko::list::push` (`:31-33`); `remove` 3× `arr_drop_at`. `Map`/`HashSet`
  idem.
- **Remodelado.** Cada array paralelo vira um ÍNDICE de ponteiros (ou inline, se `K`/`V` couberem numa
  palavra — chaves `u64`/hash são inline por natureza). Um watermark `count` serve os três; um
  `dict_grow_index` cresce os três índices ao mesmo alvo numa chamada, copiando ponteiros e liberando os
  três índices velhos:

  ```teko
  /**
   * dict_grow_index — grow the three parallel pointer indices of a Dictionary/Map/HashSet in lockstep to
   * hold one more entry, when the shared watermark reached capacity. Each becomes a NEW fixed pointer
   * index (geometric); the live pointer prefixes are copied and the three old indices freed. Values do
   * not move — only the pointer indices grow. Growing together keeps keys[i]/hashes[i]/vals[i] aligned.
   *
   * @param keys    the key pointer index
   * @param hashes  the cached-hash index (u64, inline)
   * @param vals    the value pointer index
   * @param count   the shared live watermark
   * @return        the three grown indices, aligned, capacity >= count + 1
   * @since 0.3.1
   */
  fn dict_grow_index<K, V>(keys: []*K, hashes: []u64, vals: []*V, count: u64): (keys: []*K, hashes: []u64, vals: []*V)
  ```

  `insert(k,v)`: acha `at`; se presente, atualiza o valor em `at` (VALOR: sobrescreve por cópia; OBJETO:
  troca o ponteiro, solta a referência antiga — count intacto); senão cresce se cheio, grava chave/valor em
  `[count]` (VALOR `arena_place`; OBJETO copia ponteiro), `count++`. `remove`: reclama `keys[at]`/`vals[at]`
  conforme a classe — **VALOR reparenta/dropa a sub-região; OBJETO SOLTA o ponteiro** (a região do objeto
  segue por residência/LUB) — + **swap-remove** dos ponteiros (troca com o último, `count--`) — **O(1), sem
  shift, sem realocar, MAS QUEBRA A ORDEM** → válido só porque uma tabela por hash é NÃO-ordenada (bag/set).
  Se `keys()` for contratualmente "insertion order" (`dictionary.tks:53`), o swap-remove NÃO serve: cai no
  **shift-left dos ponteiros, O(n)** (→ O(n²) sob churn) — o preço de preservar ordem num índice-array.
  `hashes` é `u64` inline (não precisa de `arena_place`).
- **A forma bucket open-addressing** (a "rehash com load-factor" do escopo R9): é a evolução recomendada e
  casa perfeitamente com o modelo — o backing de buckets é um índice fixo `[]*Slot` (ou `[]Slot` inline se
  `Slot` couber numa palavra); quando `count/cap > 0.75`, `bucket_rehash` aloca um NOVO índice de buckets,
  re-insere os ponteiros vivos por hash, libera o velho. Rebuild-para-índice-novo, jamais resize. Entregar
  a forma linear-remodelada JÁ (fiel, mínima); a bucket como follow-up aditivo (ADJACENTE, reportado).

### 3.3 `SortedSet<T>` / `SortedDictionary<K,V>` — inserção ordenada por shift

- **Atual.** `SortedSet` guarda `items` ordenado (`sorted_set.tks:3`), `add` = `sorted_insert` (`:16` →
  `collections.tks:94-106`, busca binária + `arr_insert_at`). `SortedDictionary` idem em `keys`/`vals`
  (`sorted_dictionary.tks:30-31`).
- **Remodelado.** `{ index de ponteiros, count }`. `add(x)`: `at = lower_bound` sobre os alvos (deref para
  comparar); se presente, no-op; senão cresce se cheio, `arena_place(x)`, **shift-right dos PONTEIROS**
  `[at..count)` (abre o slot, movendo só ponteiros), `index[at] = &x_placed`, `count++`. O shift é trabalho
  de PONTEIRO in-bounds sobre o índice fixo — sem mover valor, sem mudar `len`. `SortedDictionary` shifta os
  dois índices no mesmo `at`. `remove` = reclama o elemento por classe (VALOR reparenta/dropa a sub-região;
  OBJETO solta o ponteiro, região por residência/LUB) + shift-left dos ponteiros + `count--`. `contains`/`get`
  = busca binária deref-comparando `index[0..count]`.
- **Nota honesta:** o shift ainda é O(n) em ponteiros por inserção. Aqui a alternativa 3 (BST/skip-list,
  §9) é estritamente melhor (O(log n) sem shift) — ver a recomendação de §10.

### 3.4 `PriorityQueue<T>` — o heap binário sobre índice de ponteiros

- **Atual.** `heap: []T` (`priority_queue.tks:3`); `enqueue` = `heap_sift_up(teko::list::push(...))`
  (`:16-17`); `dequeue` = `heap_pop_min(&h)` (`:24-28` → `collections.tks:122-145`).
- **Remodelado.** `{ heap: []*T índice, count }`. `enqueue(x)`: cresce se cheio, `arena_place(x)`,
  `heap[count] = &x_placed; count++`, **sift-up trocando PONTEIROS** `heap[i]`↔`heap[parent]` (duas
  escritas de ponteiro, sem mover valor). `dequeue()`: reclama `heap[0]` por classe (**VALOR** reparenta a
  sub-região para o executor / inline copia-pra-fora; **OBJETO** copia o ponteiro e solta o slot — região por
  residência/LUB); `heap[0] = heap[count-1]; count--`; **sift-down de ponteiros**; devolve o mínimo. `peek()`
  = `copy_out(heap[0])` (VALOR) / copia o ponteiro (OBJETO). Todo o sift vira
  `heap_sift_*_at` operando sobre `(index, count)` por ponteiro — os `arr_swap`/`arr_drop_last` que copiavam
  array somem.

### 3.5 Os combinadores `arr_*` e helpers heap/sorted — presize exato, zero push

Estes RODAM em `push`/`empty` e são a fundação. Remodelam-se pela natureza TAMANHO-EXATO-CONHECIDO (MAP):

| combinador | file:line | remodelagem |
|---|---|---|
| `arr_replace_at<T>` | `collections.tks:2-11` | `of_len(xs.len)` + copiar + `out[at]=v` (ou índice-assign in-place se o chamador possui) |
| `arr_drop_u64_at`/`arr_drop_at<T>` | `:13-35` | `of_len(xs.len-1)` + copiar pulando `at` |
| `arr_drop_last<T>` | `:37-41` | `of_len(xs.len-1)` + copiar prefixo |
| `arr_insert_at<T>` | `:43-59` | `of_len(xs.len+1)` + copiar com um slot aberto em `at` |
| `arr_swap<T>` | `:61-66` | `of_len(xs.len)` + copiar + trocar dois índices |
| `arr_reverse<T>` | `:68-78` | `of_len(xs.len)` + `out[i] = xs[len-1-i]` |
| `arr_slice<T>` | `:80-92` | `of_len(hi-from)` + copiar a janela |
| `sorted_insert<T>` | `:94-106` | subsumido pelo shift-de-ponteiros do §3.3 |
| `heap_sift_up<T>`/`heap_pop_min<T>` | `:108-145` | subsumidos pelo `heap_sift_*_at` do §3.4 |

Todos têm tamanho final KNOWN na entrada (`xs.len ± 1`) → `of_len(n)` + preenchimento por índice. Natureza
MAP pura. (Estes operam sobre `[]T` cru, não índices — são utilitários de array, não coleções.)

### 3.6 `teko::list::grow<T>(ref x, v)` — REMOVIDO

`src/list/list.tks:1-3` = `x.value = teko::list::push(x.value, v)` — choke-point folha do push por `ref`.
Owner F1 (`arena-escopada-…:481,514`): *"não é pra manter o método."* REMOVIDO inteiro — `ref []T` é só
ponteiro-de-posição. Chamadores usam `List<T>::push` (§3.1) ou uma das quatro naturezas in-line.

### 3.7 `Deque<T>` / `Queue<T>` / `Stack<T>` — o RING de índice (O(1) nas DUAS pontas, sem shift)

Estas são de DESIGN (plano FASE-1a; `plano-…:176-178`). O ponto-chave é evitar o shift O(n): um índice-array
plano só é O(1) numa ponta (append/pop na cauda). Para O(1) nas DUAS pontas SEM shift, o índice é um **RING**
(head/tail com wrap-around) sobre um índice fixo de ponteiros:

- **`Stack<T>`** = índice-array simples; push/pop na cauda O(1) (só watermark). Não precisa de ring.
- **`Queue<T>` / `Deque<T>`** = **ring de índice**: `{ index: []*T, head: u64, tail: u64, count: u64 }`.
  `push_back`/`push_front`/`pop_back`/`pop_front` = escrever/ler o slot em `head`/`tail` (mod `index.len`) e
  mover o cursor — **O(1) nas DUAS pontas, ZERO shift**. Crescer (quando `count == index.len`) = `grow_index`
  para um novo ring maior, RE-LINEARIZANDO os ponteiros a partir de `head` (copia só ponteiros, valores
  intactos, índice velho liberado). Remove/pop de PONTA reparenta a sub-região para o executor (§2.4a).
- **Remove no MEIO de um ring** volta a ser O(n) (compactar) — se o workload exige remove/insert no meio, a
  recomendação é alt-3 node-linked (§9), não o ring.
- **BOUNDED** (tamanho de bound conhecido) = ring fixo do tamanho do bound, NÃO cresce nunca — encaixe
  perfeito de backing fixo.

---

## 4. As coleções CONCORRENTES — reconciliação T-2 (valores estáveis + índice sob concorrência)

As concorrentes são DESIGN no plano (`plano-…:488-625`), BLOQUEADAS em F1(thread)+F2. O modelo
ponteiro-índice **melhora** a história de concorrência, mas não a resolve toda — declarar o split:

### 4.1 O que o ponteiro-índice RESOLVE e o que AINDA precisa da disciplina de retain/segment

- **RESOLVIDO por construção: o VALUE-UAF.** No modelo antigo, um grow que dropasse o backing de valores
  enquanto outra tarefa o indexava = UAF de valor cross-thread. Agora os **valores vivem estáveis na arena
  do objeto, escritos uma vez e nunca movidos** — outra tarefa que deref um ponteiro do índice SEMPRE
  encontra o valor vivo. O grow não toca valor nenhum → **o VALUE-UAF desaparece**.
- **AINDA aberto: a realocação do ÍNDICE sob concorrência.** O `grow_index` libera o índice velho; se outra
  tarefa estiver a ler `index[i]` no exato instante do free, é UAF **do índice** (não do valor). O portão
  `is_unique_at` (`spine.tks:720`, §7 canonical) é FALSE sob concorrência → não se pode liberar o índice
  velho enquanto leitores concorrentes existirem. Aqui a disciplina de **retain/segment** que já
  desenhamos permanece necessária — MAS agora só para o índice (largura uniforme de ponteiros), não para
  valores.

### 4.2 A resolução (sem relaxar F1): índice como segment-list de ponteiros imortais em F2

Os arrays continuam FIXOS; o índice cross-thread NÃO é liberado por grow — é **retido em F2** e cresce como
segment-list:
- um **segment** é um `[]*Node` (ou `[]Node`) FIXO de tamanho `SEG` (ex. 1024);
- um **directory** fixo de ponteiros-de-segment (dimensionado na construção; se esgotar, encadeia
  segment-de-segments — nunca realoca o existente);
- crescer = alocar um NOVO segment fixo em F2 e instalá-lo por CAS no próximo slot livre do directory. **Os
  segments e o directory existentes NUNCA se movem nem se dropam** (F2 imortal) → um índice lido por outra
  tarefa é SEMPRE válido → zero UAF de índice cross-thread. Nós/slots liberados vão para o free-list §7.8.

**Não relaxa F1** (todo array é fixo; crescer aloca um novo array fixo). A diferença face ao sequencial é
que o índice velho é RETIDO (imortal) em vez de liberado — exigência de segurança cross-thread. **Risco
sinalizado (não bloqueia):** slab de índice retida até o free-por-entrada; directory com cap de construção.
Agora o custo é só de PONTEIROS retidos (o índice), não de valores — a memória retida cai face à v1.

### 4.3 Por coleção concorrente (inalterado no essencial; valores agora estáveis)

- **`ConcurrentStack<T>` (Treiber).** `push` aloca um nó do slab §4.2 (bump atómico dá o slot num segment
  fixo imortal), `node.next = head`, `tk_atomic_cas(&head, old, idx)`. `pop` = CAS + devolve o nó ao §7.8.
  O VALOR do nó vive estável — nenhum drop de backing.
- **`ConcurrentDictionary<K,V>` (striped).** Cada stripe é um `Dictionary` sequencial-remodelado (§3.2)
  atrás do SEU lock. Sob o lock exclusivo do stripe, `is_unique_at` VALE → o `grow_index` sequencial
  (libera o índice velho) é SEGURO dentro do stripe. Leitura lock-free futura precisaria do retain-de-índice
  — ADJACENTE (`plano-…:573`).
- **`ConcurrentQueue<T>` (Michael-Scott).** Nós no slab §4.2; head/tail por CAS. Crescer = novo segment
  imortal, jamais drop.
- **`ConcurrentBag<T>`.** Um `List<T>` sequencial-remodelado por tarefa (thread-local), crescido só pelo
  dono → `is_unique_at` vale → o `grow_index` sequencial é seguro; steal toma lock fino.
- **`BlockingCollection<T>` (bounded).** Ring fixo do tamanho do bound, alocado na construção, NÃO cresce.
  Encaixe perfeito.

**Adianta-se AGORA:** skeletons com honest-stop (`panic("blocked on F1+F2 shared-region + thread mode")`),
Javadoc, assinaturas `slab_alloc_node`/`slab_free_node` contra a forma §7.8. Resume quando F1 + as
primitivas C `tk_atomic_*`/`tk_mutex_*` aterrarem.

---

## 5. As formas de tipo dos wrappers (W15 doc-comment, o implementer copia)

O corpo usa três wrappers **despachados por classe de `T` na monomorfização** — `place`/`read`/`write` — que
resolvem para: **VALOR value-struct** → `arena_place`/`copy_out`/`write_in` (§2.7); **escalar** → `[]T` inline
(`index[i]=x` direto, §2.2a); **OBJETO** → cópia/soltura de PONTEIRO (referência à arena própria do objeto,
zero cópia de valor, vida por residência/LUB — §2.2b/§2.5), sem tocar a arena da coleção. O `pop` de VALOR
reparenta a região ao executor; o de OBJETO só solta o ponteiro.

```teko
/**
 * List<T> — a growable sequence over a FIXED POINTER INDEX (R9 pointer-index model). `index` is a fixed
 * `[]*T` whose `len` is the CAPACITY; `count` is the live watermark. For a VALUE element the value is
 * placed once in the collection's arena and never moved; for an OBJECT element the slot holds a REFERENCE
 * to the object's own arena (C#-like: push/get copy the pointer, not the value; remove releases it;
 * lifetime is the LUB of live references). To append past capacity, `grow_index` builds a NEW fixed
 * pointer index and frees the old one immediately (it holds no data). No value array is ever recopied on
 * grow, so no dead value-array can strand — the v1 grow-leak is closed by construction.
 *
 * @since 0.3.1
 */
exp type List<T> = class {
    /** The fixed pointer index; `index.len` is the capacity, never a live count. */
    intern index: []*T
    /** The live-element watermark: `count <= index.len`; slots `[count, index.len)` are zero (null). */
    intern count: u32

    /**
     * Build an empty `List<T>` — a zero-length pointer index and a zero watermark.
     *
     * @return a fresh, empty list at the caller's concrete `T`
     */
    pub static fn make(): List<T> { .{ index = of_len_ptr<T>(0); count = 0 } }

    /** The number of live elements (the watermark), NOT the index capacity. */
    pub fn len(): u64 { self.count }

    /** True iff `count == 0`. */
    pub fn is_empty(): bool { self.count == 0 }

    /**
     * Append `x`. For a VALUE element `place` writes it once into the collection's arena; for an OBJECT
     * element `place` copies the reference (pointer to the object's own arena, no value copy). Grows the
     * fixed pointer index geometrically (a NEW fixed index, old freed) only when full, then writes the
     * pointer. Amortized O(1); no value is ever recopied on grow.
     *
     * @param x  the element to append (value copied once, or object referenced)
     */
    pub fn push(x: T) {
        var p: *T = place<T>(x)
        self.index = grow_index<T>(self.index, self.count, self.count + 1)
        self.index[self.count] = p
        self.count = self.count + 1
    }

    /**
     * Read the element at `i`; `i` must be in `[0, len())`. For a VALUE element returns an independent copy
     * (the caller never holds a pointer into the collection); for an OBJECT element returns the reference
     * (a new pointer to the same object — C#-like), never a deep copy.
     *
     * @param i  the index to read
     * @return   a value copy, or an object reference
     */
    pub fn get(i: u64): T { read<T>(self.index[i]) }

    /**
     * Overwrite the element at `i` (O(1)); a no-op if `i >= len()`. VALUE: drop the old value's region and
     * place the new one. OBJECT: overwrite the slot with the new reference (release the old reference).
     *
     * @param i  the index to overwrite
     * @param x  the new value (copied) or object (referenced)
     */
    pub fn set(i: u64, x: T) { if i < self.count { write<T>(self.index[i], x) } }

    /**
     * Remove the last element by lowering the watermark; a no-op on an empty list. O(1), no realloc. VALUE:
     * the element's region reparents to the caller (or drops if unbound). OBJECT: the pointer is released;
     * the object's arena lives on by the LUB of its remaining references.
     */
    pub fn pop() { if self.count > 0 { self.count = self.count - 1 } }

    /**
     * A fresh `[]T` snapshot of `[0, len())` — a new array, never a view over the index (a view would
     * dangle when the list later grows and frees the old index). VALUE elements are copied out; OBJECT
     * elements are copied as references (new pointers to the same objects — C#-like), not deep-cloned.
     *
     * @return a fresh array of the live elements (values copied, objects referenced)
     */
    pub fn to_array(): []T {
        var out: []T = of_len<T>(self.count)
        var i: u64 = 0
        loop { if i >= self.count { break } out[i] = read<T>(self.index[i]); i++ }
        out
    }
}
```

`Dictionary<K,V>`/`Map<V>`/`HashSet<T>` seguem o molde `{ índices paralelos de ponteiros, count comum }`
com `dict_grow_index` (§3.2); `SortedSet`/`SortedDictionary` o shift-de-ponteiros (§3.3); `PriorityQueue`
o sift-de-ponteiros (§3.4). `dict_find_index` ganha `count` (varre `[0..count)`, deref-comparando).

---

## 6. Fixtures de regressão (`.tkr` ISOLADO, exit code nativo — spec, NÃO rodar aqui)

Cada fixture roda ISOLADO (nunca `teko test .` — o leak do monomorph crasha o container). O `exit`/token
codifica QUAL ramo correu.

| fixture | prova | exit esperado |
|---|---|---|
| `list_grow_amortized` | `List<i64>`: push N=100000; `len()==N`; checksum `Σi` bate | 0 |
| `list_no_value_recopy` | (o leak-guard) push N cruzando muitos grows; um contador de `arena_place` == N (valores colocados UMA vez, nunca recopiados); token = N | 0 |
| `list_index_freed_on_grow` | força K grows; o índice velho é liberado eager (o valor de `free_index` chamado == K); sem acúmulo | 0 |
| `list_reassign_purge` | grow força `grow_index`; a view antiga do índice não é lida após reatribuir; roda M ciclos | 0 |
| `list_to_array_snapshot` | `to_array()` guardado; a lista cresce e libera o índice velho; a cópia intacta | 0 |
| `list_read_write_copy` | (VALOR) `get` devolve cópia; mutar a cópia não altera a coleção; `set` grava por cópia | 0 |
| `list_obj_get_is_reference` | (OBJETO) `get` devolve a REFERÊNCIA (mesmo objeto): mutar via o retorno reflete no elemento da lista; NÃO é deep copy | 0 |
| `list_pop_watermark` | push 8, pop 3, `len()==5`, `get(4)` correto; slots acima do watermark invisíveis | 0 |
| `list_value_pop_reparent` | (VALOR: region-transfer) `List<BigStruct>` pop de N; a região do valor reparenta pro executor; `region_reparent`==N; valor retornado vivo no escopo do executor | 0 |
| `list_obj_pop_release` | (OBJETO: referência, NÃO reparenta) `List<Obj>` pop; `get`/`pop` copiam o PONTEIRO; ZERO `region_reparent`/`arena_place` na arena da coleção; o objeto segue vivo pelo binding original após o pop (LUB); mutar via a referência retornada reflete no objeto | 0 |
| `list_obj_lub_lifetime` | objeto ainda referenciado pelo binding original sobrevive ao `remove` da lista; objeto SÓ referenciado pela lista cai quando a lista morre; sem UAF | 0 |
| `list_set_drop_old` | (VALOR) `set(i, y)`: a sub-região antiga é region_dropped (contador == nº de sets); (OBJETO) `set` troca o ponteiro, solta a referência antiga; sem acúmulo | 0 |
| `dict_grow_lockstep` | `Dictionary<StrKey,i64>`: insert N chaves distintas; todos `get` batem após vários grows | 0 |
| `dict_update_not_grow` | insert repetido da mesma chave: `len()` constante, valor atualizado (count intacto) | 0 |
| `hashset_add_dup` | `HashSet<i64>`: add duplicado = no-op; `len()` correto após grows | 0 |
| `sortedset_shift_order` | `SortedSet<i64>`: insere fora de ordem além da capacidade; `to_array()` ascendente | 0 |
| `sorteddict_shift_pair` | `SortedDictionary<StrKey,i64>`: keys+vals shiftam alinhados; `get` bate | 0 |
| `pq_heap_fixed` | `PriorityQueue<i64>`: enqueue além da capacidade; dequeue devolve mínimo crescente | 0 |
| `arr_combinators_exact` | `arr_insert_at`/`arr_drop_at`/`arr_reverse`/`arr_slice`: tamanho e conteúdo exatos, zero push | 0 |
| (ALT-3) `linked_list_no_backing` | `LinkedList<i64>`: push/remove-anywhere; nenhum array de backing alocado; `len` e ordem batem | 0 |
| (ALT-3) `skiplist_sorted_order` | `SortedSet` node-linked: insert fora de ordem; iteração ascendente; O(log n) sem shift | 0 |
| (ALT-3) `linked_remove_reparent` | `LinkedList<i64>`: remove de nó reparenta a região do nó pro chamador; a coleção encolhe; sem free-list §7.8 | 0 |
| (FASE-2) `concurrent_stack_cas` | `ConcurrentStack<i64>` sob N tarefas: soma/contagem finais batem (Treiber) | 0 |
| (FASE-2) `concurrent_dict_striped` | `ConcurrentDictionary<StrKey,i64>`: insert em buckets disjuntos + join + soma dos `get` | 0 |

Os dois primeiros novos (`list_no_value_recopy`, `list_index_freed_on_grow`) são os **leak-guards** da
rework: provam que valor não é recopiado e que o índice velho é liberado — exatamente a patologia que o dono
achou.

---

## 7. Sequência de crumbs + pontos de ritual

Menor passo independentemente gate-able. **[dry]** = compila + fixpoint trivial; **[RITUAL]** = gate cheio
(build gen2 `TEKO_BACKEND=native`, regressão isolada verde, FIXPOINT gen2==gen3 byte-idêntico). Reseed só
num [RITUAL]. Nenhum crumb ensina idioma que o seed não tenha.

**R9.0 — (dependência) `of_len`+índice-assign+`count`+`arena_place`/`copy_out`/`free_index` semeados**
(passo-1 do expurgo + as primitivas §2.7). PRÉ-REQUISITO; não é crumb desta carga.

**R9.1 — combinadores `arr_*` presize exato** (§3.5). Reescreve `collections.tks:2-92`; remove
`sorted_insert`/`heap_sift_up`/`heap_pop_min` do caminho. **[dry]**. Ritual: NÃO.

**R9.2 — `List<T>` sobre `{index, count}` ponteiro** (§3.1) + `grow_cap`/`grow_index` (§2.3). Remove
`src/list/list.tks` `grow` (§3.6). **[dry]** (aditivo-inerte; o corpus do compilador não instancia `List`).

**R9.3 — `Map`/`Dictionary`/`HashSet` sobre índices paralelos+`count`** (§3.2) + `dict_grow_index`/
`dict_find_index` com `count`. **[RITUAL]** — `Map` É consumido por `teko::env` no compiler-core
(`plano-…:762`); build gen2 native, fixtures `dict_*` verdes, FIXPOINT gen2==gen3. Ritual: SIM.

**R9.4 — `SortedSet`/`SortedDictionary` shift-de-ponteiros** (§3.3) + `PriorityQueue` sift-de-ponteiros
(§3.4). Fixtures `sortedset_*`/`sorteddict_*`/`pq_*`. **[dry]** se nenhum é consumido pelo core; senão
**[RITUAL]** (na dúvida, RITUAL).

**R9.5 — (ALT-3, aditivo) família ligada por nós** (§9): `LinkedList<T>`, e as variantes node-linked de
Sorted/PQ/Deque. **[dry]** (aditivo; ninguém no core as instancia ainda). Ritual: NÃO.

**R9.6 — (FASE-2, bloqueado em F1+F2) skeletons concorrentes** (§4). Tipos-handle value + honest-stops +
Javadoc + assinaturas de slab. **[dry]**. Resume quando F1 + primitivas C aterrarem.

**Pontos de ritual:** R9.3 (o `Map`/core toca o self-build) e o fecho de R9.4 se algum sorted/PQ for
consumido pelo core. R9.1/R9.2/R9.5/R9.6 são [dry].

---

## 8. Riscos + tensões de lei (law-first) — SEM HALT (Q3 agora RULED pelo dono)

- **T-1 — "rehash bucket com load-factor" (escopo R9) vs. impl LINEAR embarcada.** RESOLVIDO (§3.2). As
  Dict/Map/HashSet são varredura linear sobre arrays paralelos → não há rehash a fazer; a remodelagem fiel é
  `dict_grow_index` + `count`. A forma bucket open-addressing (onde o rehash-por-load-factor É a remodelagem
  exata) é a evolução recomendada, ADJACENTE. **Não é HALT.**
- **T-2 — F1 (free-old) vs. §7 `is_unique_at` sob concorrência.** RESOLVIDO/RECONCILIADO (§4). O
  ponteiro-índice REMOVE o VALUE-UAF (valores estáveis, nunca movidos). Resta o UAF do ÍNDICE na realocação
  cross-thread — fechado pelo segment-list de índices imortais em F2 + free-list §7.8 (o índice velho é
  RETIDO, não liberado, sob concorrência; liberado eager só no caso sequencial/sob-lock). Arrays continuam
  fixos → F1 intacto. Risco sinalizado: slab de ÍNDICE retida (só ponteiros, muito menor que a v1). **Não
  é HALT.**
- **T-3 — o índice individualmente liberável vs. a bump-arena do objeto.** RESOLVIDO (§2.1/§2.7). O índice
  NÃO pode morar no bump do objeto (senão o ÍNDICE vaza — o mesmo erro da v1 transposto). Vai para
  alloc/free direto OU sub-região droppable própria (region_drop O(1)). Os VALORES moram em sub-regiões
  reparentáveis (escritas uma vez): removidos MIGRAM para o chamador por reparent (§2.4a), os que ficam caem
  no bulk-drop da morte — a arena da coleção segura só os vivos, sem high-water. **Não é HALT.**
- **T-3b — DESLOCAMENTO, partida limpa por CLASSE (ruling do dono).** RESOLVIDO (§2.4a). **VALOR** (mora na
  arena da coleção): pop/remove = TRANSFERÊNCIA DE REGIÃO (reparent ao executor, move-on-return/§5), O(1);
  set-overwrite/remove-descartado = region_drop O(1); escalar-inline = cópia-pra-fora / overwrite limpo.
  **OBJETO** (mora na sua arena própria): get/pop = copia o PONTEIRO; remove = SOLTA o ponteiro; vida por
  residência/LUB; NADA de transferência de região, NADA de arena do executor recebendo o storage do objeto.
  NENHUM churn/high-water, NENHUM free-list por-entrada no caminho comum (o §7.8 fica só para o slab
  imortal-F2 concorrente, §4). Vale igual no índice-de-ponteiros e na alt-3 (nós). **Não é HALT.**
- **T-4 — `to_array()` / `get` partilhavam o backing.** RESOLVIDO (§2.4, §5). VALOR → `get`/`to_array`
  copiam o valor para fora; OBJETO → devolvem a REFERÊNCIA (novo ponteiro à mesma região, C#-like), nunca um
  ponteiro para dentro do ÍNDICE da coleção (que dangling ao grow). Fecha o UAF de view. **Não é HALT.**
- **T-5 — `remove` swap-vs-shift (ordem de inserção).** As tabelas por hash fazem swap-remove O(1) de
  ponteiros (perde ordem) ou shift-left O(n) de ponteiros (preserva). `keys()` documenta "insertion order"
  (`dictionary.tks:53`) → recomendo shift para preservar o contrato. **Não é HALT.**
- **T-6 — modelo de OBJETO: self-owned + referência + LUB (RULED pelo dono, CORRIGE a v-anterior).** A versão
  anterior deste doc recomendava ERRADO "ESCAPE, a coleção é dona" (Q3-A). O dono cravou o modelo C#-like
  (§2.5): o objeto NÃO é possuído pela coleção e NÃO escapa para a arena dela — vive na sua arena própria
  por-objeto (F2), a coleção guarda só um PONTEIRO; get/pop copiam o ponteiro, remove solta; a vida é o LUB
  de todas as referências vivas (`pt_join`/escape-check). **O objeto NÃO tem o problema de transfer/UAF que o
  enquadramento anterior sugeria — é referência de 1ª classe normal.** Zero cópia de valor no push, zero
  escape-analysis nova no caminho quente, múltiplas referências ao mesmo objeto = correto. Deep copy é
  operação explícita à parte. **A antiga dúvida "escape vs borrow" está FECHADA (nenhuma das duas: é
  referência, não posse-da-coleção nem empréstimo-com-outlives). Não é HALT, não é martelo pendente.**

**Nenhuma barreira, nenhum item pendente do dono: o modelo de objeto foi RULED (T-6) e propagado por todas
as coleções; cada uma — inclusive a concorrente — tem proposta concreta sob todas as alternativas.**

---

## 9. ALTERNATIVA 3 — coleções ligadas por NÓS (SEM arrays) — o pedido do dono

Owner: *"quero uma terceira alternativa que não trabalha com arrays."* Aqui NÃO há backing contíguo nenhum
(nem de valores, nem de ponteiros-índice). Cada elemento é um **NÓ** alocado individualmente na arena do
objeto, ligado por ponteiros. **O problema de grow-copy/leak DESAPARECE POR CONSTRUÇÃO:** não existe "array
velho" para ficar preso, porque não existe array.

### 9.1 O mecanismo universal

- **Nó** = `Node<T> = { val: T, next: *Node<T>, prev: *Node<T> }` (prev só onde a estrutura pede). Cada nó
  mora na **sua própria sub-região reparentável** na arena da coleção. **VALOR**: `val` guarda o valor
  (escrito uma vez). **OBJETO**: `val` guarda a REFERÊNCIA (ponteiro à arena própria do objeto — o objeto
  NÃO está no nó nem na arena da coleção, §2.2b).
- **GROW** = `arena_place` de UM nó e liga-o (ajusta 1–2 ponteiros). **Sem backing, sem grow-copy, sem
  realocação, sem "array velho".** É O(1) estrutural.
- **RECLAIM por classe.** `pop`/`remove` de um nó: **VALOR** → REPARENTA a sub-região do nó para a arena do
  executor (§2.4a, O(1), move-on-return) — o valor morre no escopo do executor; ou region_drop deliberado
  se descartado. **OBJETO** → SOLTA a referência em `val` (a arena do objeto segue por residência/LUB) e
  region_drop do NÓ (que só segurava um ponteiro — barato). Os nós que nunca saem caem no bulk-drop da morte
  da coleção. Sem free-list §7.8 no caminho comum (§7.8 só para o slab imortal concorrente, §4).
- **Read/write por cópia/referência** (mesma regra do dono): `get` copia `node.val` para fora (VALOR) ou
  copia o ponteiro (OBJETO); `set` grava o valor (VALOR) ou troca a referência (OBJETO).

```teko
/**
 * Node<T> — one individually arena-allocated element of a node-linked collection. `val` is written once
 * into the collection's dedicated arena and lives as long as the node; `next`/`prev` link it. There is NO
 * backing array anywhere: a collection is a graph of these. Growing allocates one node and links it —
 * never a realloc, never a grow-copy, so no dead backing can strand.
 *
 * @since 0.3.1
 */
type Node<T> = class {
    /** The element value, written once, copied out on read. */
    intern val: T
    /** The successor node, or null at the tail. */
    intern next: *Node<T>
    /** The predecessor node, or null at the head (omitted in singly-linked variants). */
    intern prev: *Node<T>
}

/**
 * link_after — splice a freshly placed node after `at` in a doubly-linked chain, in O(1). Adjusts at
 * most four pointers; allocates no array and copies no value. This is the entire "grow" of a node-linked
 * collection.
 *
 * @param at    the node to insert after (null to insert at head)
 * @param fresh the newly arena-placed node to splice in
 * @since 0.3.1
 */
fn link_after<T>(at: *Node<T>, fresh: *Node<T>)
```

### 9.2 Mapeamento por coleção

- **`List<T>` / `LinkedList<T>`** → lista duplamente ligada. `push` = liga na cauda (O(1)); `remove_at`
  dado o nó = unlink O(1); `get(i)` = **traversal O(n)** (é o custo honesto de não ter array).
- **`Map`/`Dictionary`/`HashSet`** → duas formas: (i) **árvore balanceada** (AVL/rubro-negra) ordenada por
  hash-e-chave — 100% sem array, O(log n) get/insert/remove; (ii) **hash com node-chaining** — O(1)
  esperado, MAS precisa de um **array de buckets** (híbrido: o array-de-buckets é um índice fixo,
  crescido/rehash por §3.2, com correntes de nós). A forma (i) é a TRULY array-free; a (ii) é híbrida.
- **`SortedSet`/`SortedDictionary`** → **skip-list** ou **BST balanceada**. O(log n) insert/remove/lookup,
  iteração ordenada por traversal, **sem shift, sem grow-copy**. É onde alt-3 é estritamente melhor que o
  array (que shifta O(n)).
- **`PriorityQueue<T>`** → **pairing heap** ou **binomial/leftist heap** por nós. Insert O(1) (pairing),
  extract-min O(log n) amortizado, merge O(1) — sem grow-copy, sem heapify de array.
- **`Deque`/`Queue`/`Stack`** → ligados. O(1) nas duas pontas (deque duplamente ligada), sem grow-copy.
- **Concorrentes** → **JÁ eram node-based** (Treiber/MS-queue sobre o slab de segments, §4). Alt-3
  **unifica** com elas: a família sequencial ligada e a concorrente ligada partilham o `Node`/free-list. É o
  encaixe natural.

### 9.3 Tradeoffs HONESTOS vs. as abordagens de array

- **PERDE:** índice aleatório O(1) (List-by-index vira O(n) traversal); overhead de ponteiro por elemento
  (1–2 palavras `next`/`prev`); cache-unfriendly (nós espalhados na arena, não contíguos) → mais cache-miss
  em varredura sequencial.
- **GANHA:** insert/remove em QUALQUER posição O(1) dado o nó (sem shift); **zero grow-copy; zero desperdício
  de capacidade; SEM VAZAMENTO POR CONSTRUÇÃO** (não há array velho a estrangular); casa 1-para-1 com as
  concorrentes; a reclamação por-nó é o reparent/drop arena-nativo (§2.4a), fino e exato.
- **Neutro:** total de memória — o overhead de ponteiro por nó (16 B doubly) compete com o desperdício de
  capacidade do array doubling (≤ `count` ponteiros); para elementos grandes o array (índice de ponteiros)
  e o nó são comparáveis; para escalares o array-inline vence em bytes/cache.

Alt-3 tem design CONCRETO para toda coleção (nenhuma fica "blocked"); exige as MESMAS primitivas §2.7
(`arena_place`, ponteiros, `region_reparent`/`region_drop`) — nada além.

---

## 10. Comparação lado a lado + recomendação POR coleção

Legenda de score: ✔ bom / ~ médio / ✘ ruim. As alternativas: **(1)** ponteiro-índice doubling+watermark;
**(1r)** ponteiro-índice em RING (head/tail, §3.7); **(2)** ponteiro-índice exact-grow; **(3)** node-linked
sem array. **O eixo do SHIFT (O(n)/O(n²)) é de PRIMEIRA CLASSE** — está separado em "pontas" vs "meio".

| dimensão | (1) índice doubling | (1r) índice RING | (2) índice exact | (3) node-linked |
|---|---|---|---|---|
| memória (leak) | ✔ zero | ✔ zero | ✔ zero | ✔ zero POR CONSTRUÇÃO |
| memória (waste/high-water) | ~ ≤`count` ptrs | ~ ≤`count` ptrs | ✔ zero waste | ~ 1–2 palavras/nó |
| append/pop nas PONTAS | ✔ O(1) 1 ponta | ✔ **O(1) nas DUAS** | ✘ O(n) | ✔ O(1) nas duas |
| **remove/insert no MEIO/FRENTE (SHIFT)** | ✘ **O(n) → O(n²)** | ✘ **O(n) no meio** | ✘ O(n) | ✔ **O(1) dado o nó, ZERO shift** |
| índice aleatório `get(i)` | ✔ O(1) | ✔ O(1) (mod) | ✔ O(1) | ✘ O(n) traversal |
| swap-remove (perde ordem) | ✔ O(1) | ✔ O(1) | ✔ O(1) | — |
| cache | ✔ contíguo | ✔ contíguo | ✔ contíguo | ✘ nós espalhados |
| fit concorrência | ~ retain/segment do índice | ~ idem | ~ idem | ✔ nativo (Treiber/MS) |
| complexidade | ✔ simples | ~ wrap-around | ✔ simples | ~ mais ponteiros/casos |

**O trade-off do SHIFT, explícito (o eixo que o dono cobrou):** num índice-array, `remove(i)`/`insert(i)`
na FRENTE ou no MEIO PRESERVANDO ORDEM exige compactar `[i+1..count)` deslizando um slot → **O(n) por
operação → O(n²) sob churn frontal/meio**. `push`/`pop` no FIM = O(1). `swap-remove` = O(1) mas QUEBRA a
ordem (só Set/bag não-ordenado). O(1) nas DUAS pontas só via **ring/deque** (1r), e ainda assim NUNCA O(1)
no meio arbitrário. Só **alt-3 (3)** dá remove/insert O(1) no meio, zero shift.

**Recomendação por coleção** (divergem — e o shift é visível em cada uma):

- **`List<T>` append + `get(i)` aleatório, remove-meio RARO → (1) índice doubling.** O(1) index e append,
  cache-friendly; **assumindo O(n) no remove/insert-meio (O(n²) sob churn) — trade-off explícito.**
- **`List<T>` com remove/insert FRONTAL/MEIO pesado → (3) node-linked** (O(1) dado o nó, zero shift) — o
  vencedor decisivo quando o workload é churn no meio/frente. Se o churn é só nas PONTAS → **(1r) ring**.
- **`Map`/`Dictionary`/`HashSet` (NÃO-ordenado) → (1) índice paralelo com SWAP-REMOVE O(1)** (a ordem não é
  contratual numa tabela por hash); **evolução = bucket open-addressing** (rehash por load-factor). Se
  `keys()` exigir insertion-order, o remove vira shift O(n) — pesar. **Default: (1) linear+swap-remove agora,
  (1) bucket como follow-up.**
- **`SortedSet`/`SortedDictionary` → (3) node-linked (skip-list ou BST).** Aqui alt-3 VENCE claro: o índice-
  array shifta O(n)→O(n²) por inserção ordenada; a skip-list dá O(log n) insert/remove/lookup + iteração
  ordenada, ZERO shift. Só se build-once-then-read, (1) com busca binária serve. **Default: (3) insert-heavy;
  (1) build-once.**
- **`PriorityQueue<T>` → (1) índice heap doubling** (sift O(log n) por swap de ponteiros — NÃO é o shift
  linear; o heap não compacta). **Alt (3) pairing heap** para insert-heavy/merge. **Default: (1); (3) para
  merge.**
- **`Stack<T>` → (1) índice doubling** (push/pop O(1) numa ponta; sem shift, sem ring). **Qualquer serve.**
- **`Queue`/`Deque` → (1r) ring de índice** (O(1) nas DUAS pontas, sem shift, cache-friendly) como default;
  **(3) node-linked** se também houver remove no MEIO. BOUNDED → ring fixo (nem cresce).
- **Concorrentes → (3) node-linked** (Treiber/MS-queue), já era o caminho; o slab de segments imortais é a
  "arena" dos nós. **Default: (3).**

**Síntese.** **ponteiro-índice (1)** para append + index aleatório + hash (List-append, Map, PQ, Stack),
**assumido o O(n²)-shift no remove-meio**; **ring (1r)** para filas/deques (O(1) nas duas pontas);
**node-linked (3)** para ordenado-mutável, remove/insert-no-meio pesado, e as concorrentes (O(1) no meio,
zero shift); **exact-grow (2)** só build-once. **As alternativas fecham o vazamento da v1:** (1)/(1r)/(2)
porque o que cresce é um índice de ponteiros liberado individualmente (valores escritos uma vez, nunca
recopiados); (3) porque não existe array nenhum a estrangular. F1 permanece inviolável em todas — todo array
usado (o índice, os buckets, o ring) é fixo, crescido por novo-array-fixo+free-old, nunca resize in-place.
