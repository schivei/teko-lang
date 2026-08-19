---
section: design
created: 2026-08-19
status: DESIGN — no product line. R9 (arena remount, origin/fix/retirement @ 001c7400): remodel EVERY
        growable collection onto FIXED-ARRAY backing. Owner ruling (verbatim): the architect must look at
        EACH collection and PROPOSE the remodeling; "não criar barreira, aliviar array não é opção, arrays
        continuarão fixos." F1 is NOT up for relaxation. This doc IS the deliverable — no product `.tks`
        edited, no build, no reseed, no `teko test`.
source: arena-escopada-stream-expurgo-0.3.1.md (§5, §5.3, R9 — the removed primitives + the four
        conversion natures), arena-especificacao-unica-0.3.1.md (§7 concurrency, is_unique_at, §7.8
        per-entry free-list; §2.3/§2.4 dedicated per-object region), plano-collections-genericas-e-
        concorrentes-0.3.1.md (the growable-collection census + the concurrent model B), CLAUDE.md
        (NO PUSHES, ZERO CRESCIMENTO + purge-imediato, the fixed-array ZERO-FILL/`count` form).
frozen: bootstrap/teko.c + the C twins are OUTPUT/FROZEN; the slice-grow machine is REMOVED, not patched.
        New product work is `.tks` only. Drains DIRECT into fix/retirement (no PR).
---

# R9 — remodelagem das coleções growable sobre backing FIXO (0.3.1)

Architect, 2026-08-19. Base: `origin/fix/retirement` @ `001c7400` (o tip que carrega o plano de arena +
R9). Documento de DESIGN — sem linha de produto. É a resposta à ordem do dono: olhar CADA coleção e
PROPOR a remodelagem sobre backing fixo. **Nenhuma proposta relaxa F1** (arrays continuam de tamanho
fixo); onde uma remodelagem colide com lei selada (concorrência, os portões do §7), contra-argumento,
proponho a resolução, não silencio nem crio barreira.

---

## 0. A lei inviolável e a forma-âncora (o que TODA remodelagem obedece)

**F1 (arena remount, §5/§5.3/R9 de `arena-escopada-stream-expurgo-0.3.1.md`):** um `[]T` é FIXO. NÃO há
`push`/`grow_inplace`/`with_cap`/mutação de comprimento in-place, e NENHUM método growable sobrevive.
Para "crescer" aloca-se um NOVO array fixo (tamanho exato conhecido NAQUELA alocação) e DROPA-se o velho;
o velho dropado morre na arena do braço/objeto. "Capacidade" mora SÓ no WRAPPER da coleção (um watermark
`count ≤ backing.len` sobre um backing fixo); crescer o wrapper = construir um NOVO backing fixo + copiar +
retornar, dropando o velho.

**A forma-âncora do array fixo (CLAUDE.md "FORMA DO ARRAY FIXO — ZERO-FILL, `count` UNIVERSAL"):** header
`{ptr, len}` (sem `cap`, sem tag). `of_len<T>(n): []T` = `memset`-zero, uma passada. Presença = watermark
`count`: preenche contíguo `[0..count)`, corta `slice[0..count]`. Índice-assign `xs[i] = v` é PREFERIDO.

**A dependência declarada.** A maquinaria `of_len<T>(n)` + índice-assign + o idioma `count` é o passo-1 do
expurgo (`arena-escopada-stream-expurgo-0.3.1.md:501-505`) — hoje AUSENTE da árvore (o typer só conhece
`empty`/`push`/`with_cap`/`grow_inplace`, `typer.tks:805-835`). Esta remodelagem CONTRATA contra a forma
DECLARADA de `of_len` e resume em minutos quando o passo-1 semear. Tudo o mais aqui — o desenho, os
contratos de tipo, as fixtures, os skeletons — está desbloqueado HOJE.

**As quatro naturezas de conversão (CLAUDE.md, lei permanente) que esta remodelagem instancia:**
1. **MAP** (um item por elemento da fonte) → `of_len(fonte.len)` + `loop i { xs[i] = f(fonte[i]) }`.
2. **PARSE/SCAN** (`n` sai de varrer) → duas passadas: conta `n`, depois `of_len(n)` + grava por índice.
3. **FILTRO** (subconjunto) → alarga ao limite superior, grava num `count`, corta `slice[0..count]`.
4. **BUFFER DE SAÍDA** → literais + interpolação (não se aplica às coleções genéricas).

**A lei do purge imediato (CLAUDE.md "ZERO CRESCIMENTO DINÂMICO + PURGE IMEDIATO NA REATRIBUIÇÃO"):** ao
`self.backing = <novo backing>`, o backing ANTERIOR é purgado IMEDIATAMENTE — o gatilho é a reatribuição,
a liberação é eager. Toda remodelagem abaixo tem UM ponto de reatribuição de backing (o `ensure_cap`), e é
lá que o purge do velho ocorre.

---

## 1. RECON — enumeração EXAUSTIVA de todo método growable (file:line, na árvore correta)

Duas populações: as coleções JÁ EMBARCADAS em `src/collections/` (+ `src/list/`) e as coleções de DESIGN
do plano (`plano-collections-genericas-e-concorrentes-0.3.1.md`) que ainda não têm `.tks` mas montam sobre
os mesmos primitivos removidos. Ambas são remodeladas aqui — nenhuma fica de fora.

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

Nada de `deque`/`queue`/`hashset`-extra/`multimap` além disto existe na árvore (`ls src/collections/` =
collections, dictionary, hashset, list, map, priority_queue, sorted_dictionary, sorted_set). A enumeração
está completa.

---

## 2. A remodelagem UNIVERSAL — o wrapper watermark + o único ponto de crescimento

Toda coleção sequencial troca `intern items: []T` (onde `items.len` ERA a contagem) por **dois campos**: um
backing FIXO cuja `len` é a CAPACIDADE, e um watermark `count` da contagem viva.

```teko
/**
 * grow_cap — the geometric capacity policy in ONE place: given the current capacity, returns the next
 * fixed-backing size so append stays amortized O(1) while every backing remains a brand-new FIXED array.
 * Doubling (factor 2) from a small floor (4) is the default; the memory-lean 1.5× variant (`old + old/2`)
 * is a one-line swap here when peak transient matters more than grow count. This is NOT a growable array:
 * it only names the size of the NEXT fixed allocation.
 *
 * @param old_cap  the current backing capacity (0 for a fresh collection)
 * @return         the capacity of the next fixed backing (strictly greater than old_cap)
 * @since 0.3.1
 */
fn grow_cap(old_cap: u64): u64 {
    if old_cap == 0 { return 4 }
    old_cap * 2
}

/**
 * ensure_cap — the SINGLE growth choke point for a fixed-backing sequence. When `count` already fits
 * `want` slots, returns `backing` unchanged (no allocation). Otherwise builds a NEW fixed backing of
 * `grow_cap`-sized capacity via `of_len<T>` (zero-fill), copies the live `[0..count)` prefix by index,
 * and returns it — the caller reassigns its field, which PURGES the old backing eagerly (the reassign
 * law). Arrays stay fixed: this allocates a new exact-sized fixed array and drops the old; it never
 * resizes in place.
 *
 * @param backing  the current fixed backing (capacity == backing.len)
 * @param count    the live watermark (count <= backing.len)
 * @param want     the minimum capacity the caller needs after this call
 * @return         a fixed backing with capacity >= want, holding the same live prefix
 * @since 0.3.1
 */
fn ensure_cap<T>(backing: []T, count: u64, want: u64): []T {
    if want <= backing.len { return backing }
    var new_cap = grow_cap(backing.len)
    loop { if new_cap >= want { break } new_cap = grow_cap(new_cap) }
    var next: []T = of_len<T>(new_cap)
    var i: u64 = 0
    loop { if i >= count { break } next[i] = backing[i]; i++ }
    next
}
```

**A amortização, explícita.** `ensure_cap` só aloca quando `count == backing.len` (capacidade cheia), e
quando aloca dobra a capacidade. Sobre N appends há O(log N) crescimentos, copiando 4+8+…+N ≈ 2N elementos
no total → **O(1) amortizado por append, SEM array growable** — cada backing é um array fixo novo, o velho
reclamado. O pico transitório é limitado a `factor × count` (2× no default), NÃO os ~1.8 GB do vazamento
raiz (que era root-leak não-limitado por todo o programa); aqui é limitado por-objeto E reclamado.

**Onde o backing DROPADO aterra (F2/§2.3 de `arena-especificacao-unica`/`arena-escopada`).** O wrapper é
uma `class` (objeto), logo o backing vive na **região DEDICADA do objeto** (§2.3, owner F2:
`arena-escopada-stream-expurgo-0.3.1.md:169-190`). Ao `self.backing = ensure_cap(...)`, o backing anterior
é o valor que o campo segurava na região do objeto. Duas rotas, law-first:
- **Eager (a lei do purge imediato, CLAUDE.md).** A região dedicada do objeto ganha o **free-list por-entrada
  do §7.8** (`arena-especificacao-unica-0.3.1.md:542-548`) para o slot de backing — a MESMA capacidade já
  sancionada para F2 — de modo que a reatribuição libera o backing velho NA HORA. Recomendado, honra a lei
  do purge exatamente. Alternativa equivalente: o backing mora numa **sub-região própria droppable** e o
  crescimento faz `region_drop` da sub-região velha (O(1), eager, exato).
- **Bulk (o fallback sem capacidade nova).** Os backings velhos acumulam na região dedicada do objeto e são
  reclamados no `region_drop` da morte do objeto. A série geométrica limita o total a ≤2× o tamanho vivo —
  limitado, amortizado, jamais a patologia root-leak. Correto quando o §7.8 free-list não estiver disponível
  para regiões de objeto.

**A leitura e o snapshot.** `len()` retorna `count` (NÃO `backing.len`). `get(i)`/loops leem `backing[i]`
para `i < count` (os slots `[count, cap)` são zero/null de `of_len`). **`to_array()` DEVE copiar** — retorna
`of_len(count)` + cópia por índice, um snapshot independente. Retornar `backing[0..count]` cru partilharia o
backing; se a coleção depois crescer e purgar o backing velho, a view do chamador vira UAF. O snapshot-cópia
fecha isso (o único ponto onde a remodelagem MUDA a semântica de `to_array`, e é uma correção).

---

## 3. Remodelagem por coleção

### 3.1 `List<T>` — a sequência base

- **Mecanismo atual.** `intern items: []T` (`list.tks:3`); `push` = `self.items = teko::list::push(...)`
  (`list.tks:15`, copy-grow root-leaking); `set`/`pop`/`remove_at` andam nos combinadores `arr_*`.
- **Remodelado.** Campos `{ backing: []T, count: u32 }`. `push(x)` = `self.backing =
  ensure_cap(self.backing, self.count, self.count + 1); self.backing[self.count] = x; self.count = self.count
  + 1` — índice-assign, um único ponto de crescimento. `pop()` = `self.count = self.count - 1` (in-bounds,
  sem realocar; o slot fica lixo até ser sobrescrito, invisível abaixo do watermark). `set(i, x)` =
  `self.backing[i] = x` (índice-assign direto, in-bounds; hoje faz `arr_replace_at` que copia o array todo —
  a remodelagem também o torna O(1)). `remove_at(i)` = shift-left `[i+1..count)` por índice + `count--`
  (in-bounds, sem mudar `len`). `to_array()` = snapshot-cópia `of_len(count)`.
- **Amortização/drop:** §2 — geométrico; velho reclamado na região do objeto (free-list §7.8 eager, ou bulk).

### 3.2 `Map<V>` / `Dictionary<K,V>` / `HashSet<T>` — as tabelas por hash

- **Mecanismo atual (o que a árvore REALMENTE faz — contra-argumento à moldura "rehash bucket").** As três
  são **varredura LINEAR sobre arrays paralelos**, NÃO tabelas de bucket com open-addressing: `Dictionary`
  guarda `keys`/`hashes`/`vals` (`dictionary.tks:3-5`) e acha por `dict_find_index` loop linear
  (`dictionary.tks:57-65`); `Map` idem com `map_find_index` (`map.tks:69-77`); `HashSet` idem
  (`hashset.tks:3-5,20`). `insert`/`add` fazem `teko::list::push` em cada array paralelo
  (`dictionary.tks:31-33`, `map.tks:31-33`, `hashset.tks:21-22`); `remove` faz `arr_drop_at` em cada
  (`dictionary.tks:47-49`).
- **Remodelado (a forma fiel à impl atual).** Cada array paralelo vira `{ backing fixo, count comum }`. Como
  os três arrays crescem em lockstep, UM watermark `count` serve os três, e um `dict_ensure_cap` cresce os
  três ao mesmo alvo de capacidade numa chamada:

  ```teko
  /**
   * dict_grow — grow the three parallel fixed backings of a Dictionary/Map/HashSet in lockstep to hold
   * one more entry, when the shared watermark has reached capacity. Each becomes a NEW fixed backing
   * (geometric via ensure_cap) with the live prefix copied; the caller reassigns all three, purging the
   * old backings. Growing them together keeps keys[i]/hashes[i]/vals[i] index-aligned.
   *
   * @param keys    the key backing
   * @param hashes  the cached-hash backing
   * @param vals    the value backing
   * @param count   the shared live watermark
   * @return        the three grown backings, index-aligned, capacity >= count + 1
   * @since 0.3.1
   */
  fn dict_grow<K, V>(keys: []K, hashes: []u64, vals: []V, count: u64): (keys: []K, hashes: []u64, vals: []V)
  ```

  `insert(k,v)`: acha `at`; se presente, `self.vals[at] = v` (índice-assign, count intacto); senão
  `dict_grow` se `count == backing.len`, depois `self.keys[count] = k; self.hashes[count] = h;
  self.vals[count] = v; count++`. `remove`: swap-remove (move o último para `at`, `count--`) — O(1),
  in-bounds, sem shift, sem realocar (a ordem de inserção deixa de ser preservada; o `keys()` snapshot já é
  documentado "insertion order" — a remodelagem troca para "sem ordem garantida", uma mudança a documentar,
  ou usa shift-left se a ordem for contratual). `len()` = `count`.

- **A moldura "rehash com load-factor" do escopo R9 — onde se aplica e onde NÃO.** O escopo R9 pede
  "rehash into a NEW fixed bucket backing on grow; load-factor threshold triggers a rebuild." Isso é a forma
  de uma tabela de **open-addressing real**, que a impl atual NÃO é (é varredura linear). **Contra-argumento
  e resolução:** para a impl embarcada (linear), NÃO há buckets, logo NÃO há rehash — o crescimento é o
  `dict_grow` geométrico dos arrays paralelos + `count`, e nada de load-factor. **A forma bucket é a
  EVOLUÇÃO recomendada** (troca O(n) find por O(1)), e nela a remodelagem F1 é exatamente o pedido do R9:
  o backing de buckets é um `[]Slot` FIXO (`Slot = { hash: u64, key: K, val: V, used: bool }`, zero-fill →
  `used=false` natural); quando `count / cap > 0.75` (load-factor), `bucket_rehash` aloca um NOVO backing de
  buckets fixo de `grow_cap(cap)`, re-insere todos os slots vivos por hash, e retorna — o velho dropado. É
  um rebuild-para-backing-novo-fixo, jamais um resize in-place. Recomendo entregar a forma linear-remodelada
  JÁ (fiel, mínima) e a forma bucket como follow-up aditivo (ADJACENTE — reportado, não aberto como issue).

### 3.3 `SortedSet<T>` / `SortedDictionary<K,V>` — inserção ordenada por shift

- **Mecanismo atual.** `SortedSet` guarda `items` ordenado (`sorted_set.tks:3`); `add` = `sorted_insert`
  (`sorted_set.tks:16` → `collections.tks:94-106`), que faz busca binária e `arr_insert_at` (push).
  `SortedDictionary` guarda `keys`/`vals` ordenados (`sorted_dictionary.tks:3-4`); `insert` =
  `sorted_key_lower_bound` + `arr_insert_at` em ambos (`sorted_dictionary.tks:30-31`).
- **Remodelado.** `{ backing/keys+vals fixos, count }`. `add(x)`: `at = lower_bound(backing, count, x)`; se
  presente, no-op; senão `ensure_cap` se cheio, depois **shift-right** `[at..count)` por índice para abrir o
  slot (`i` de `count` decrescendo até `at+1`: `backing[i] = backing[i-1]`), `backing[at] = x`, `count++`.
  O shift é trabalho de índice IN-BOUNDS sobre o backing fixo (há sempre `cap > count` após `ensure_cap`),
  **sem mudança de comprimento** — o `count` é o watermark, o backing não muda de tamanho no shift.
  `SortedDictionary::insert` faz o MESMO shift em `keys` e `vals` no mesmo `at`. `remove`/drop = shift-left +
  `count--`. `contains`/`get` = busca binária sobre `backing[0..count]` (inalterada).
- **Estratégia de ordenação/shift sem mutar `len`:** a ordenação é preservada porque o shift abre o slot
  exato de `lower_bound`; o `len` do backing nunca muda (só o `count`). O `ensure_cap` geométrico dá o
  amortizado; o backing velho reclamado como §2.

### 3.4 `PriorityQueue<T>` — o heap binário sobre backing fixo

- **Mecanismo atual.** `heap: []T` (`priority_queue.tks:3`); `enqueue` = `heap_sift_up(teko::list::push(...))`
  (`priority_queue.tks:16-17`, push + sift); `dequeue` = `heap_pop_min(&h)` (`priority_queue.tks:24-28` →
  `collections.tks:122-145`, que faz `arr_drop_last`/`arr_replace_at`/`arr_swap`).
- **Remodelado.** `{ heap: []T fixo, count }`. `enqueue(x)`: `ensure_cap` se `count == heap.len`, depois
  `heap[count] = x; count++`, e **sift-up** por índice (troca `heap[i]`↔`heap[parent]` = duas escritas de
  índice, sem realocar). `dequeue()`: guarda `heap[0]`; `heap[0] = heap[count-1]; count--`; **sift-down** por
  índice; retorna o mínimo. `peek()` = `heap[0]`. **Enqueue além da capacidade = novo backing fixo + copiar +
  sift** (o `ensure_cap` faz o novo-backing; o sift é índice in-bounds). **Dequeue é trabalho de índice
  in-bounds (nada cresce)** — exatamente como o escopo R9 pede. Todo o sift vira `heap_sift_up_at`/
  `heap_sift_down_at` operando sobre `(backing, count)` por índice, sem os `arr_swap`/`arr_drop_last` que
  copiavam o array.

### 3.5 Os combinadores `arr_*` e os helpers heap/sorted — presize exato, zero push

Estes NÃO são coleções, mas RODAM em `push`/`empty` e são a fundação das coleções — remodelam-se pela
natureza de TAMANHO EXATO CONHECIDO (natureza MAP/known-size, não precisa de watermark de crescimento):

| combinador | file:line | remodelagem (tamanho exato, `of_len` + índice) |
|---|---|---|
| `arr_replace_at<T>` | `collections.tks:2-11` | `of_len(xs.len)` + copiar + `out[at]=v` (ou índice-assign in-place se o chamador possui) |
| `arr_drop_u64_at`/`arr_drop_at<T>` | `collections.tks:13-35` | `of_len(xs.len-1)` + copiar pulando `at` |
| `arr_drop_last<T>` | `collections.tks:37-41` | `of_len(xs.len-1)` + copiar prefixo |
| `arr_insert_at<T>` | `collections.tks:43-59` | `of_len(xs.len+1)` + copiar com um slot aberto em `at` |
| `arr_swap<T>` | `collections.tks:61-66` | `of_len(xs.len)` + copiar + trocar dois índices |
| `arr_reverse<T>` | `collections.tks:68-78` | `of_len(xs.len)` + `out[i] = xs[len-1-i]` |
| `arr_slice<T>` | `collections.tks:80-92` | `of_len(hi-from)` + copiar a janela por índice |
| `sorted_insert<T>` | `collections.tks:94-106` | subsumido pelo shift-in-place do §3.3 (o combinador some do caminho da coleção) |
| `heap_sift_up<T>`/`heap_pop_min<T>` | `collections.tks:108-145` | subsumidos pelo `heap_sift_*_at` por índice do §3.4 |

Todos têm o tamanho final KNOWN na entrada (`xs.len ± 1`), logo são `of_len(n)` + preenchimento por índice —
zero `push`, zero `empty`, zero crescimento. São a natureza MAP pura.

### 3.6 `teko::list::grow<T>(ref x, v)` — REMOVIDO

`src/list/list.tks:1-3` é `x.value = teko::list::push(x.value, v)` — o choke-point folha do push mutável por
`ref`. Owner F1 (`arena-escopada-stream-expurgo-0.3.1.md:481,514`): *"não é pra manter o método."* **REMOVIDO
inteiro** — `ref []T` é só ponteiro-de-posição (não cresce, não reatribui o array todo, CLAUDE.md). Os
chamadores que queriam crescer usam o wrapper de coleção (§3.1 `List<T>::push`) ou uma das quatro naturezas
in-line. Não é remodelado, é deletado; o compilador enumera os sobreviventes na remoção (metodologia expurgo).

---

## 4. As coleções CONCORRENTES — o caso duro (FASE-2, design-ahead; proposta concreta, sem barreira)

As concorrentes são DESIGN no plano (`plano-…:488-625`), sem `.tks` ainda, BLOQUEADAS em F1(thread)+F2
(região partilhada — F2 já semeada, `tk_region_program`, `teko_rt.c:2305`). Os skeletons do plano montam
sobre `teko::list::push` (`plano-…:555-557,616`) — o mesmo primitivo removido. Aqui está o que muda, e por
que o "novo-backing + dropa-velho" de F1 **não pode ser aplicado cru** sob concorrência.

### 4.1 A tensão de lei (contra-argumento) e a resolução

F1 assume UM dono: "aloca novo backing, dropa o velho." O drop-do-velho só é seguro se NENHUMA referência
viva ao velho sobrevive — é precisamente o que o portão `is_unique_at` (`spine.tks:720`, o gate cross-thread
do §7 canonical, `arena-escopada-…:326,383`) exige. **Sob concorrência `is_unique_at` é FALSE:** outra
tarefa pode estar a indexar o backing velho no exato instante do drop → dropar o backing = **UAF
cross-thread**. Logo aplicar F1 cru a uma estrutura partilhada lock-free viola o §7.

**A resolução (sem relaxar F1):** os arrays continuam FIXOS; o que muda é que o backing velho NÃO é dropado —
é **retido imortalmente em F2** e os slots livres reciclam pelo **free-list por-entrada do §7.8**
(`arena-especificacao-unica-0.3.1.md:542-548`). Ou seja, o crescimento de uma coleção concorrente é um
**segment-list de backings fixos**:
- um **segment** é um `[]Node` FIXO de tamanho `SEG` (ex. 1024), `Node = { val: T, next: u64 }`;
- um **directory** fixo de ponteiros-de-segment (dimensionado na construção, ex. 4096 entradas → 4M nós; se
  esgotar, encadeia um segment-de-segments — nunca realoca o existente);
- crescer = alocar um NOVO segment fixo em F2 e instalá-lo por CAS no próximo slot livre do directory. **Os
  segments e o directory existentes NUNCA se movem nem se dropam** (F2 imortal), logo um índice lido por
  outra tarefa é SEMPRE válido → zero UAF cross-thread. Nós liberados vão para o free-list do §7.8 e reciclam.

Isto **não relaxa F1**: todo array (segment, directory) é fixo; crescer aloca um NOVO array fixo (o segment);
não há resize in-place nem array growable. A ÚNICA diferença face ao caso sequencial é que o backing velho é
RETIDO (imortal) em vez de dropado — o que é EXIGIDO pela segurança cross-thread e é exatamente a propriedade
imortal-F2 + o free-list §7.8 já sancionados. **Risco sinalizado (não bloqueia):** a memória de slab é retida
até o free-por-entrada; o directory tem cap de construção (ou encadeia). É o custo consciente da segurança
lock-free; nenhuma outra estrutura o evita sem GC/epoch.

### 4.2 Por coleção concorrente

- **`ConcurrentStack<T>` (Treiber, `plano-…:591-624`).** `push` NÃO faz `teko::list::push` num array
  growable; aloca um nó do slab de segments (§4.1) — um `bump` atómico dá o índice do slot, num segment fixo
  imortal — aponta `node.next = head`, e `tk_atomic_cas(&head, old, idx)` publica (retry no loop clássico).
  `pop` = CAS na cabeça + devolve o nó ao free-list §7.8. Nenhum backing dropado, nenhum array cresce
  in-place. É o caminho lock-free puro do dono sobre backing 100% fixo.
- **`ConcurrentDictionary<K,V>` (striped, `plano-…:519-575`).** Cada stripe é um **`Dictionary<K,V>`
  sequencial-remodelado (§3.2) próprio, atrás do SEU lock fino**. O `insert` toma só `stripes[hash % n]`
  (`plano-…:551`) e cresce APENAS o backing daquele stripe — **e aí o F1 novo-backing+dropa-velho é SEGURO**,
  porque ocorre sob o lock exclusivo do stripe: nenhum leitor daquele stripe toca o backing fora do lock
  (`get` também toma o lock, `plano-…:569`), logo `is_unique_at` VALE dentro do stripe. O concurrent dict =
  N dicts sequenciais fixos, cada um crescido por §3.2 sob o seu lock. Sem backing partilhado lock-free → o
  drop-old sequencial aplica-se limpo. (Uma leitura lock-free futura precisaria do retain-de-segment/epoch —
  ADJACENTE, `plano-…:573`.)
- **`ConcurrentQueue<T>` (Michael-Scott / dois locks, `plano-…:495`).** Nós no slab de segments §4.1 (mesmo
  mecanismo do stack); head/tail por CAS ou dois locks. Crescer = novo segment imortal, jamais drop.
- **`ConcurrentBag<T>` (`plano-…:497`).** Um `List<T>` sequencial-remodelado (§3.1) **por tarefa** (thread-
  local), crescido só pelo dono → `is_unique_at` vale no buffer local → o novo-backing+dropa-velho sequencial
  é seguro; o roubo (steal) toma um lock fino. Sem backing partilhado growable.
- **`BlockingCollection<T>` (bounded, `plano-…:498`).** LIMITADA → o backing é um **ring fixo do tamanho do
  bound**, alocado na construção, **NÃO cresce nunca**. Encaixe perfeito de backing fixo — zero crescimento,
  mutex fino + condvar/futex para cheio/vazio.

**Adianta-se AGORA (compila sem F1):** os skeletons com honest-stop (`panic("blocked on F1+F2 shared-region +
thread mode")`), os contratos Javadoc, e as assinaturas dos helpers de slab (`slab_alloc_node`/
`slab_free_node`) contra a forma declarada do §7.8. Quando F1 aterrar + as primitivas C
`tk_atomic_*`/`tk_mutex_*` (`plano-…:415-427`), o implementer troca os honest-stops pelas chamadas — a
representação (segment-slab em F2, striping, cabeça atómica) já está desenhada aqui.

---

## 5. As formas de tipo dos wrappers (W15 doc-comment, o implementer copia)

```teko
/**
 * List<T> — a growable, reference-semantic sequence on FIXED backing (R9). `backing` is a fixed `[]T`
 * whose `len` is the CAPACITY; `count` is the live watermark (count <= backing.len). To append past
 * capacity, `ensure_cap` builds a NEW fixed backing (geometric) and the old is purged on reassignment —
 * no growable array ever exists. Reads index `backing[0..count)`; `to_array` copies out an independent
 * snapshot.
 *
 * @since 0.3.1
 */
exp type List<T> = class {
    /** The fixed backing; `backing.len` is the capacity, never a live count. */
    intern backing: []T
    /** The live-element watermark: `count <= backing.len`; slots `[count, backing.len)` are zero-fill. */
    intern count: u32

    /**
     * Build an empty `List<T>` — a zero-length fixed backing and a zero watermark.
     *
     * @return a fresh, empty list at the caller's concrete `T`
     */
    pub static fn make(): List<T> { .{ backing = of_len<T>(0); count = 0 } }

    /** The number of live elements (the watermark), NOT the backing capacity. */
    pub fn len(): u64 { self.count }

    /** True iff `count == 0`. */
    pub fn is_empty(): bool { self.count == 0 }

    /**
     * Append `x`. Grows the fixed backing geometrically (a NEW fixed array, old purged) only when the
     * watermark has reached capacity; otherwise an in-bounds index-assign. Amortized O(1).
     *
     * @param x  the element to append
     */
    pub fn push(x: T) {
        self.backing = ensure_cap<T>(self.backing, self.count, self.count + 1)
        self.backing[self.count] = x
        self.count = self.count + 1
    }

    /**
     * Read the element at `i`; `i` must be in `[0, len())`.
     *
     * @param i  the index to read
     * @return   the element at `i`
     */
    pub fn get(i: u64): T { self.backing[i] }

    /**
     * Overwrite the element at `i` (in-bounds index-assign, O(1)); a no-op if `i >= len()`.
     *
     * @param i  the index to overwrite
     * @param x  the new value
     */
    pub fn set(i: u64, x: T) { if i < self.count { self.backing[i] = x } }

    /**
     * Remove the last element by lowering the watermark; a no-op on an empty list. O(1), no realloc.
     */
    pub fn pop() { if self.count > 0 { self.count = self.count - 1 } }

    /**
     * An independent `[]T` snapshot of `[0, len())` — a fresh fixed array, never a view over the backing
     * (a view would dangle when the list later grows and purges the old backing).
     *
     * @return a fresh copy of the live elements
     */
    pub fn to_array(): []T {
        var out: []T = of_len<T>(self.count)
        var i: u64 = 0
        loop { if i >= self.count { break } out[i] = self.backing[i]; i++ }
        out
    }
}
```

```teko
/**
 * Dictionary<K, V> — a growable, reference-semantic map on FIXED parallel backings (R9). `keys`/`hashes`/
 * `vals` are fixed arrays grown in lockstep by `dict_grow`; one `count` watermark serves all three. A new
 * entry index-assigns at `count` and bumps it; an existing key overwrites in place (count unchanged).
 * Growth allocates NEW fixed backings and purges the old — no growable array. (Linear-scan today; the
 * open-addressed bucket form, whose load-factor rebuild is the R9 rehash, is the additive follow-up.)
 *
 * @since 0.3.1
 */
exp type Dictionary<K: IEq & IHash, V> = class {
    /** The keys, index-aligned with `hashes`/`vals`; a fixed backing, `keys.len` is the capacity. */
    intern keys: []K
    /** Each live key's cached `hash()`, compared before the full key for cheap collision rejection. */
    intern hashes: []u64
    /** The values, index-aligned with `keys`. */
    intern vals: []V
    /** The shared live-entry watermark across the three parallel backings. */
    intern count: u32

    /**
     * Build an empty `Dictionary<K, V>` — three zero-length fixed backings and a zero watermark.
     *
     * @return a fresh, empty dictionary at the caller's concrete `K`/`V`
     */
    pub static fn make(): Dictionary<K, V> {
        .{ keys = of_len<K>(0); hashes = of_len<u64>(0); vals = of_len<V>(0); count = 0 }
    }

    /**
     * Insert or update the value for `k`. An existing key overwrites its value in place (count unchanged);
     * otherwise the three fixed backings grow in lockstep only when full, then the entry index-assigns.
     *
     * @param k  the key to insert or update
     * @param v  the value to associate with `k`
     */
    pub fn insert(k: K, v: V) {
        var h = k.hash()
        var at = dict_find_index<K>(self.keys, self.hashes, self.count, h, k)
        if at < self.count { self.vals[at] = v; return }
        var g = dict_grow<K, V>(self.keys, self.hashes, self.vals, self.count)
        self.keys = g.keys
        self.hashes = g.hashes
        self.vals = g.vals
        self.keys[self.count] = k
        self.hashes[self.count] = h
        self.vals[self.count] = v
        self.count = self.count + 1
    }
}
```

`HashSet<T>`, `Map<V>`, `SortedSet<T>`, `SortedDictionary<K,V>`, `PriorityQueue<T>` seguem o mesmo molde
`{ backing(s) fixo(s), count }` com o corpo por-coleção do §3.2/§3.3/§3.4. `dict_find_index` ganha o
parâmetro `count` (varre `[0..count)` em vez de `keys.len`).

---

## 6. Fixtures de regressão (`.tkr` ISOLADO, exit code nativo — spec, NÃO rodar aqui)

Cada fixture roda ISOLADO (nunca `teko test .` — o leak do monomorph crasha o container). O `exit`/token
codifica QUAL ramo correu (axis-law: testa-se o valor, nunca um efeito incidental).

| fixture | prova | exit esperado |
|---|---|---|
| `list_grow_amortized` | `List<i64>`: push de N=100000; `len()==N`; checksum `Σi` bate (corrupção = valor errado) | 0 |
| `list_reassign_purge` | grow força `ensure_cap`; a view antiga não é lida após reatribuir; sem UAF (roda M ciclos) | 0 |
| `list_to_array_snapshot` | `to_array()` guardado; a lista cresce e purga o backing velho; a cópia guardada intacta | 0 |
| `list_pop_watermark` | push 8, pop 3, `len()==5`, `get(4)` correto; slots acima do watermark invisíveis | 0 |
| `dict_grow_lockstep` | `Dictionary<StrKey,i64>`: insert de N chaves distintas; todos `get` batem após vários grows | 0 |
| `dict_update_not_grow` | insert repetido da mesma chave: `len()` constante, valor atualizado (count intacto) | 0 |
| `hashset_add_dup` | `HashSet<i64>`: add duplicado = no-op; `len()` correto após grows | 0 |
| `sortedset_shift_order` | `SortedSet<i64>`: insere fora de ordem além da capacidade; `to_array()` sai ascendente | 0 |
| `sorteddict_shift_pair` | `SortedDictionary<StrKey,i64>`: keys+vals shiftam alinhados; `get` bate | 0 |
| `pq_heap_fixed` | `PriorityQueue<i64>`: enqueue além da capacidade; dequeue devolve mínimo em ordem crescente | 0 |
| `arr_combinators_exact` | `arr_insert_at`/`arr_drop_at`/`arr_reverse`/`arr_slice`: tamanho e conteúdo exatos, zero push | 0 |
| (FASE-2) `concurrent_stack_cas` | `ConcurrentStack<i64>` sob N tarefas: soma/contagem finais batem (Treiber, sem perda/dup) | 0 |
| (FASE-2) `concurrent_dict_striped` | `ConcurrentDictionary<StrKey,i64>`: insert em buckets disjuntos + join + soma dos `get` | 0 |

---

## 7. Sequência de crumbs + pontos de ritual

O menor passo independentemente gate-able cada. Gate: **[dry]** = compila + fixpoint trivial (sem
consumidores de emit); **[RITUAL]** = gate cheio (build gen2 `TEKO_BACKEND=native`, regressão isolada verde,
FIXPOINT gen2==gen3 byte-idêntico). Reseed só num [RITUAL]. Bootstrap-safe: nenhum crumb ensina um idioma
que o seed não tenha — a remodelagem DEPENDE do passo-1 do expurgo (`of_len`+índice-assign) já semeado.

**R9.0 — (dependência) `of_len<T>`+índice-assign+`count` semeados** (passo-1 do expurgo,
`arena-escopada-…:501-505`). PRÉ-REQUISITO; não é crumb desta carga. Sem ele os crumbs abaixo não compilam.

**R9.1 — os combinadores `arr_*` para presize exato** (§3.5). Reescreve `collections.tks:2-92` em
`of_len(n)`+índice; remove `sorted_insert`/`heap_sift_up`/`heap_pop_min` do caminho (subsumidos). Base de
tudo. **[dry]** — só biblioteca, sem instanciar. Ritual: NÃO.

**R9.2 — `List<T>` sobre `{backing, count}`** (§3.1) + `grow_cap`/`ensure_cap` (§2). Remove `src/list/list.tks`
`grow` (§3.6). **[dry]**. Ritual: NÃO (aditivo-inerte, o corpus do compilador não instancia `List`).

**R9.3 — `Map<V>`/`Dictionary<K,V>`/`HashSet<T>` sobre paralelas+`count`** (§3.2) + `dict_grow`/`dict_find_index`
com `count`. **[RITUAL]** — `Map` É consumido por `teko::env` no compiler-core (`plano-…:762`), logo a
remodelagem toca um caminho auto-compilado: build gen2 native, fixtures `dict_*` isoladas verdes, FIXPOINT
gen2==gen3. Ritual: SIM.

**R9.4 — `SortedSet`/`SortedDictionary` shift-in-place** (§3.3) + `PriorityQueue` heap-in-place (§3.4).
Fixtures `sortedset_shift_order`/`sorteddict_shift_pair`/`pq_heap_fixed`. **[dry]** se nenhum é consumido pelo
core; senão **[RITUAL]**. Ritual: condicional ao uso no core (verificar; na dúvida, RITUAL).

**R9.5 — (FASE-2, bloqueado em F1+F2) skeletons concorrentes** (§4). Tipos-handle value + honest-stops +
Javadoc + assinaturas de slab contra a forma §7.8. **[dry]** — os honest-stops compilam hoje. Ritual: NÃO
(nada emite). Resume quando F1 + as primitivas C aterrarem.

**Pontos de ritual (gate cheio obrigatório):** R9.3 (o `Map`/core toca o self-build) e o fecho de R9.4 se
qualquer sorted/PQ for consumido pelo compiler-core. R9.1/R9.2/R9.5 são [dry]. O reseed iterativo
(ensinar→seed→sweep→seed) segue a metodologia expurgo: o compilador ENUMERA os sobreviventes quando `push`/
`empty` saírem do typer (§5.3 do doc de arena), e cada erro é um sítio a converter para uma das quatro
naturezas.

---

## 8. Riscos + tensões de lei (law-first) — SEM HALT

- **T-1 — a moldura "rehash bucket com load-factor" (escopo R9) vs. a impl LINEAR embarcada.** RESOLVIDO
  (§3.2). As Dict/Map/HashSet embarcadas são varredura linear sobre arrays paralelos (`dictionary.tks:57-65`),
  NÃO tabelas de bucket — logo não há rehash a fazer; a remodelagem fiel é `dict_grow` geométrico + `count`.
  A forma bucket open-addressing (onde o rehash-por-load-factor É a remodelagem F1 exata do R9) é a evolução
  recomendada, ADJACENTE. Contra-argumentado, não silenciado. **Não é HALT.**
- **T-2 — F1 (dropa-velho) vs. o §7 `is_unique_at` sob concorrência.** RESOLVIDO (§4.1). Dropar um backing
  que outra tarefa indexa é UAF cross-thread (`is_unique_at` FALSE). Resolução sem relaxar F1: arrays
  continuam fixos, o crescimento é segment-list de backings fixos imortais em F2, os slots livres reciclam
  pelo free-list §7.8; o velho é RETIDO (imortal), não dropado — exigência de segurança, não relaxamento de
  F1. Risco sinalizado: slab retido até free-por-entrada + directory com cap de construção. **Não é HALT.**
- **T-3 — a lei do purge imediato vs. a região bump do objeto.** RESOLVIDO (§2). O purge eager do backing
  velho numa região bump precisa do free-list por-entrada do §7.8 na região DEDICADA do objeto (capacidade já
  sancionada), OU o backing mora em sub-região droppable (region_drop O(1)). Fallback bulk: os velhos
  acumulam limitados a ≤2× (série geométrica) e caem na morte do objeto — limitado, jamais root-leak.
  Recomendo o free-list/sub-região para honrar o purge exatamente. **Não é HALT.**
- **T-4 — `to_array()` partilhava o backing.** RESOLVIDO (§2, §5). Retornar `backing[0..count]` cru penduraria
  a view quando a coleção crescesse e purgasse o backing velho; `to_array` passa a copiar (`of_len(count)`),
  um snapshot independente. É uma correção de semântica, documentada. **Não é HALT.**
- **T-5 — `remove` swap-vs-shift (ordem de inserção).** As tabelas por hash podem fazer swap-remove O(1)
  (perde ordem de inserção) ou shift-left O(n) (preserva). O `keys()` embarcado documenta "insertion order"
  (`dictionary.tks:53`); se contratual, usa shift; senão swap. RESOLVIDO por documentação — recomendo shift
  para preservar o contrato atual. **Não é HALT.**

**SEM HALT.** Toda tensão resolve law-first sem relaxar F1: as sequenciais viram `{backing fixo, count}` com
crescimento geométrico novo-backing+purga-velho; as por-hash crescem as paralelas em lockstep; sorted/PQ
shiftam/siftam por índice in-bounds sobre backing fixo; as concorrentes retêm segments fixos imortais em F2 +
free-list §7.8 (a única forma cross-thread-segura, e ainda 100% arrays fixos). Nenhuma barreira: cada coleção
— inclusive a dura (concorrente) — tem proposta concreta.
