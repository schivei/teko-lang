# Redução do pico de memória da build — de +6 GB para ≤1,5 GB

Base de leitura: `feat/expurgo-arrays` @ `df63f88c`. Autor: arquiteto (só design; não
toca código de produto). Este documento é o **crumb sequence** que o implementer de
continuação executa, escrevendo **apenas Teko**.

Meta dura: pico da build de ~6,2 GB (perto do guard `ulimit -v 6815744` = 6,5 GiB) para
**≤1,5 GB**. O guard é INVIOLÁVEL: estouro = achar causa-raiz do consumo, NUNCA levantar
o teto.

---

## 1. Diagnóstico — as duas fontes do pico

O profiler `tk_obs` já dividiu o pico:

| Fonte | Peso | Natureza |
|---|---|---|
| `tk_slice_push_r` (copy-grow amortizado) | **4980 MB (93%)** | 20,3 M cópias-crescimento que **vazam na arena `root`** (nunca liberada) |
| Construção da AST + scratch residual | ~1,2 GB (7%) | dados **vivos** (não vazados) + scratch por-arquivo que também cai em `root` |

A conta que governa o design: cada `push` num loop é copy-grow **amortizado** — a cada
dobra, o buffer anterior é abandonado na arena `root`, que nunca é liberada até o fim do
processo. Um loop que empurra `n` elementos deixa para trás `O(n)` bytes de buffers
mortos além do buffer final. Multiplicado por 20,3 M crescimentos → 4980 MB de lixo vivo.

**Duas alavancas independentes, ambas construíveis 100% em Teko:**

- **A — matar o crescimento** (as 4 naturezas): converter cada `push` para pré-alocação
  de tamanho exato + escrita por índice. Elimina a cópia-crescimento na origem. É o
  golpe que derruba os 93%.
- **B — reclamar o scratch** (arena-por-escopo, já pronta em `src/runtime/arena.tks`):
  o que ainda se aloca transitório é reclamado em massa na saída de escopo, em vez de
  vazar em `root`. Derruba a cauda de 7% e o resíduo do que A não pega.

---

## 2. A fundação já pronta em `df63f88c` (NÃO reconstruir)

O expurgo já construiu a maquinária aditiva. O implementer HERDA e USA:

| Peça | Estado | Sítio |
|---|---|---|
| `[n]T = []` (array fixo runtime, zero-fill) | pronto | `emit_slice_of_len` `src/codegen/codegen.tks:3167`; `TSliceOfLen` no checker |
| literal byte-string `b"abc"` → `[]byte` | pronto | commit `54097f6f` |
| cast implícito `str`↔`[]byte` (reinterpret, sem cópia) | pronto | commit `dc1cf767` |
| `ref []T` = escrita-por-posição `dst[i] = v` | pronto | commit `1bf3ec6c` |
| guard null-deref de slot zerado | pronto | commit `df63f88c` |
| **arena Teko completa** (mmap/syscall, regiões, chunks, free-list, marcas) | pronto | `src/runtime/arena.tks` |

A arena Teko (`src/runtime/arena.tks`) já expõe, em Teko puro sobre `mmap`/`munmap` via
syscall (Linux) / `VirtualAlloc` (Windows) / `mmap` FFI (macOS):

```
pub fn region_alloc(r: ptr, n: u64): ptr
pub fn region_new(parent: ptr): ptr
pub fn region_drop(r: ptr)
pub fn region_drop_subtree(r: ptr)
pub fn region_root(): ptr
pub fn region_current(): ptr
pub fn region_enter(child: ptr)
pub fn region_leave()
pub fn alloc(n: u64): ptr
pub fn arena_push()
pub fn arena_pop()
pub fn arena_commit()
```

O codegen já roteia todas essas por `CgArenaSym` (`codegen.tks:101`) e escolhe símbolo
Teko (`cg_arena_teko_sym`) ou C (`cg_arena_c_sym`). O `emit_slice_of_len` já aloca `[n]T`
via `RegionAlloc` na **região do escopo corrente** (`cg_enclosing_region_expr(regions)`).
**A arena-por-escopo já está fisicamente ligada ao codegen.** O que falta é (1) matar os
`push`, (2) fechar o loop de escopo (enter/leave nas fronteiras), e (3) expor o free
targetado para o purge-na-reatribuição.

Único resíduo em C: 3 shims `from "teko_rt"` (`tk_arena_control_get`/`_set`/`_paranoid`)
— o slot mutável de processo (P2 de `arena-em-teko.md`). Fora do caminho crítico de
memória; transcrição registrada no crumb 9 (não bloqueia a meta).

---

## 3. `arena_doc1` — o que a Doc-1 de arena delibera

`docs/design/arena-em-teko.md` (carga `cargo/20-arena-teko`) é a Doc-1. Ela projeta e
**já entregou** (em `src/runtime/arena.tks`) o arena em Teko: árvore de regiões
(`parent`), registro plano de regiões vivas (`reg_next`), chunks de 64 KB por bump,
free-list com bins (ceil no take / floor no park), e — decisivo aqui — o **checkpoint
mark/rewind** (`arena_push`/`arena_pop`/`arena_commit`) e o **enter/leave de região
corrente** (`region_enter`/`region_leave`).

A deliberação que resolve o free deste projeto: **arena-por-escopo com drop em massa
dispensa free individual** para o transitório. `region_drop_subtree` libera uma subárvore
inteira; `arena_pop` rebobina o bump da `root` até a marca. Para o scratch que nasce e
morre dentro de um escopo (a esmagadora maioria dos arrays do compilador), NÃO é preciso
liberar bloco a bloco — entra-se numa região/marca na entrada do escopo e derruba-se tudo
na saída. O free targetado (crumb 8) é reservado para o caso patológico: acumulador que é
reatribuído muitas vezes DENTRO de um escopo longo, onde esperar o drop-de-escopo ainda
piquearia.

---

## 4. `mem_model` — estimativa de queda por etapa

Baseline: **~6,2 GB**. `push` = 4980 MB; não-push ≈ 1,2 GB.

| Etapa (crumbs) | O que derruba | Pico após |
|---|---|---|
| Fundação (já em `df63f88c`) | — | ~6,2 GB |
| **C3 — buffer do codegen** (`cb`/`append_fo` → duas-passadas/spread) | o maior consumidor único em runtime; -3,0 a -3,5 GB | ~2,8 GB |
| **C4/C5 — checker+lir+build+parser** (4 naturezas) | resto das 20,3 M cópias; -1,4 a -1,8 GB | ~1,2–1,3 GB |
| **C6 — arena-por-escopo** (enter/leave + push/pop nas fronteiras) | reclama scratch que caía em `root`; -0,2 a -0,4 GB | ~0,9–1,1 GB |
| **C7 — literal de array via arena** (tirar `malloc` cru do `emit_array_lit`) | fecha vazamento de `malloc` nunca-liberado; -0,1 a -0,2 GB | ~0,9 GB |
| **C8 — purge-na-reatribuição** (free targetado eager) | fecha o pico do acumulador reatribuído em loop longo; robustez | ≤1,0 GB |

O golpe principal são **C3+C4/C5** (matar o `push`). C6–C8 consolidam e blindam contra
regressão. Meta ≤1,5 GB é atingida já em C4/C5; C6–C8 dão margem confortável.

Redução da AST: a AST é **viva** durante todo o compile (não vaza), então seu peso é o
resident set — não some com free. O ganho vem de (a) rotear parse-scratch por uma região
por-arquivo derrubada após o lowering daquele arquivo (C6), e (b) evitar array-de-array
inflado no build (C4). Estimado dentro dos números acima.

---

## 5. `Como se constrói em Teko` (seção concreta — SÓ Teko, sem `teko_rt.c`)

### 5.1 Free targetado — o que a arena precisa expor

`ar_free_block(control, block, bytes)` já existe interno em `arena.tks` (park na
free-list com ceil/floor + modo paranoico). Falta **expô-lo**. Crumb 8 adiciona:

```teko
/**
 * Devolve um backing de array à free-list da arena Teko, para reuso imediato.
 *
 * @param p ponteiro-base do backing a liberar (0 = no-op)
 * @param bytes tamanho do backing em bytes (len * sizeof(elem))
 */
exp fn region_free(p: ptr, bytes: u64) {
    ar_free_block(ar_control(), teko::sys::ptr_word(p) to u64, bytes)
}
```

Não usa `teko_rt.c`: `ar_free_block` opera por `load`/`store` sobre memória crua vinda de
`mmap`. É a mesma família de alocador dos chunks. O purge-na-reatribuição (5.2) chama
ESTA função, nunca `tk_slice_*`.

### 5.2 Purge-imediato-na-reatribuição (`a = <novo>`) — o ponto de codegen

Regra do dono: ao reatribuir a variável-array (`a = <novo>`), o backing ANTERIOR é
purgado IMEDIATAMENTE (a variável-array POSSUI o backing; UAF é responsabilidade do dev
no design de uso, não do backend).

Ponto de emissão: `emit_assign` em `src/codegen/codegen.tks:6020`. Já existe o esqueleto
`checker::assign_frees_old(fn_body, a)` (hoje só para valor `TCall`/`list::push`, ramo
`@fo`). O crumb 8 **generaliza** o predicado do checker para "reatribuição cujo valor é um
backing novo E cuja variável possui o backing anterior" e o codegen passa a emitir, ANTES
do store:

```
{ ptr _old = <lvalue>.ptr; uint64_t _oldn = <lvalue>.len * sizeof(<elem>);
  <lvalue> = <novo backing>;
  teko::mem::region_free(_old, _oldn); }
```

O `_old` é capturado antes do overwrite (sequência correta). A chamada emitida é o
símbolo Teko `region_free` via novo `CgArenaSym::RegionFree` (roteado por
`cg_arena_teko_sym`) — **zero** referência a `tk_slice_push_*`.

**Qual reatribuição qualifica (predicado do checker, load-bearing):** só quando o backing
anterior é (i) uma alocação de arena fresca possuída pela variável (não um literal, não um
sub-slice `s[a..b]`, não um reinterpret `str`↔`[]byte`, não um parâmetro emprestado). Um
free de sub-slice ou de string reinterpretada corromperia o vizinho. Ver risco R2.

### 5.3 Arena-por-escopo — a rota que DISPENSA free individual

Para o transitório (a maioria): NÃO liberar bloco a bloco. Na fronteira de escopo:

- **Por-função** (scratch de codegen/lir por função): `region_enter(region_new(region_current()))`
  no prólogo emitido, `region_leave()` + `region_drop_subtree(<essa região>)` no epílogo.
  Todo `region_alloc` da função (o `emit_slice_of_len` já roteia para a região corrente)
  cai nessa região e é derrubado em massa no retorno.
- **Por-arquivo** (parse/lower scratch): `arena_push()` antes de compilar um arquivo,
  `arena_pop()` depois — rebobina o bump da `root` até a marca, devolvendo todo o scratch
  daquele arquivo. `arena_commit()` onde o resultado precisa sobreviver (a AST que segue
  para o próximo passo).

Isso já é fisicamente possível: `RegionFrame` + `cg_enclosing_region_expr` existem. O
crumb 6 fecha o loop emitindo enter/leave nas fronteiras certas.

### 5.4 Remoção das raízes C (código morto)

Após C3–C5 (nenhum `src/` chama mais o crescimento), REMOVER:

- de `src/runtime/teko_rt.c`: `tk_slice_push_r`, `tk_slice_push`, `tk_slice_push_fo`,
  `tk_slice_with_cap_r`, `grow_inplace`, `tk_push_cache*`, `tk_g_push_ra`;
- de `src/checker/scope.tks`: os builtins `append_fo`/`push_fo`/`with_cap`/`grow_inplace`;
- de `src/lir/lower.tks`: `is_append_fo_call`/`lower_append_fo_call`/`slice_push_symbol`/
  `lower_list_push` (paths `tk_slice_push*`);
- de `src/list/list.tks` e `src/collections/list.tks`: `push`/`empty`.

Metodologia "compilador enumera a limpeza": remover a RAIZ → o self-compile ERRA em cada
sítio remanescente → cada erro é um sítio a converter. NÃO caçar 4917 à mão.

### 5.5 O que precisa ser TRANSCRITO de C para Teko

Quase nada: `[n]T=[]` já emite `memset` inline; a cópia de array é loop por índice (ou
`mem::copy` em Teko). Os 3 shims de controle da arena (`tk_arena_control_get/set/paranoid`)
são o único resíduo `from "teko_rt"` — transcritos no crumb 9 (slot `.bss` via
`LGlobalAddr` ou `mmap MAP_FIXED`, o P2 de `arena-em-teko.md`). Fora do caminho de
memória; não bloqueia a meta. NENHUM novo `from "teko_rt"`.

---

## 6. `crumbs` — sequência ordenada (cada um gate-ável isolado)

Ordem obrigatória: **construir/converter ANTES de remover a raiz** (metodologia do dono).
Fixpoint `gen2==gen3` é a guarda a cada harvest; guard 6,5 GiB inviolável.

### C1 — Reconhecimento e shadow do buffer do codegen
Medir, com o `tk_obs`, a fração do pico atribuída ao `cb`/`append_fo` (o buffer de emissão
de C). Montar um `.tkr` shadow isolado (fora do OOM do `teko test .`) que emite um bloco
grande de texto pelas duas vias (append_fo velho vs. duas-passadas novo) e compara pico.
Baseline registrada. Nenhuma mudança em `src/` ainda.

### C2 — `mem::copy` e o idioma de junção por índice (aditivo)
Garantir em Teko o primitivo de cópia por índice `exp fn copy(dst: ref []byte, at: u64,
src: []byte)` (loop `dst[at+i]=src[i]`; sem crescimento) e o idioma "conta-total →
`[total]byte=[]` → copia por índice". Não remove nada. Seed.

### C3 — Converter o buffer do codegen (`cb`/`append_fo`) — o de 93%
Reescrever a emissão de C do codegen do idioma `out = cb(out, ...)` (append_fo encadeado)
para **peça = spread-literal** `b"…"` nos literais + `..str` nos dinâmicos, acumulando
`total` e materializando `var final: [total]byte = []` por índice. Alvos densos:
`emit_*` de `codegen.tks`. `cb`/`cb_str`/`cb_byte` viram finos sobre o idioma novo até
sumirem. Fixpoint a cada arquivo convertido. **Maior queda isolada.**

### C4 — Converter checker + build (4 naturezas)
`src/checker/*` (1615 sítios) e `src/build/*` (652): classificar cada `push` em MAP /
PARSE / FILTRO / BUFFER e converter (MAP→`of_len(fonte.len)`+índice; PARSE→duas passadas;
FILTRO→aloca o maior+`count`+corte `s[0..count]`). Redesenhar posse dos agregados de
aliasing (`Env` em `scope.tks`). Fixpoint por módulo.

### C5 — Converter lir + backend + parser + codegen residual
`src/lir/*` (877, incl. os 6 arrays paralelos de `LEnv`/`LowerCtx`), `src/backend/*` (633),
`src/parser/*` (293), resíduo de `src/codegen` (163). Mesmas 4 naturezas. Fixpoint por
módulo. Ao fim: nenhum `src/` chama mais o crescimento.

### C6 — Arena-por-escopo nas fronteiras (reclamar scratch)
Emitir `region_enter`/`region_leave`+`region_drop_subtree` por função e
`arena_push`/`arena_pop`/`arena_commit` por arquivo (5.3). Reclama o transitório que caía
em `root`. Fixpoint.

### C7 — Literal de array via arena (tirar `malloc` cru)
`emit_array_lit` (`codegen.tks`, ramo não-spread) hoje usa `malloc(...)` direto — vazamento
nunca-liberado. Rotear para `region_alloc` na região do escopo (como `emit_slice_of_len`).
Fixpoint.

### C8 — Free targetado + purge-na-reatribuição
Expor `exp fn region_free(p, bytes)` (5.1); generalizar `assign_frees_old` para
reatribuição-que-possui-backing (5.2); adicionar `CgArenaSym::RegionFree` e emitir o
free eager em `emit_assign`. Fixpoint.

### C9 — Remover raízes C + transcrever o slot de controle
Remover o código morto de 5.4 (o self-compile enumera qualquer resíduo). Transcrever os 3
shims de controle da arena para Teko (slot `.bss`/`MAP_FIXED`), zerando `from "teko_rt"`
no caminho de array. Passe de mensagens unificado + reseed ITERATIVO final. Fixpoint
`gen2==gen3`.

---

## 7. Disciplina de fixpoint e guard (a cada crumb)

- Validação local = **compilação** (`--no-verify --release`, `TEKO_BACKEND=c`,
  `TEKO_CC=clang`, `ulimit -v 6815744`) + fixpoint (tc2==tc3) + cross-check offline.
- A conversão é **preservante** (mesmo layout `{ptr,len}`, zero-fill puro, sem tag) →
  transição escalonada-verde: o idioma novo coexiste com o velho durante a migração.
- Reseed ITERATIVO (ensinar→seed→sweep→seed) quando a mudança altera o C emitido.
  Módulos-folha não exigem reseed.
- Guard 6,5 GiB INVIOLÁVEL: se um crumb estoura, achar a causa-raiz do consumo daquele
  crumb e corrigir — nunca levantar o teto.

---

## 8. `riscos` e decisões abertas

**R1 — Free targetado vs. arena-por-escopo (rota preferida = escopo).** A rota LIMPA para
o transitório é arena-por-escopo com drop em massa (C6), que **dispensa** free individual;
`region_free` (C8) é o backstop para o acumulador reatribuído em loop longo. Decisão de
design: preferir escopo; só emitir purge-na-reatribuição onde o checker prova posse do
backing. Não transformar TODO `a = <novo>` em free — ver R2.

**R2 — Qualificar a reatribuição (soundness do purge).** Free eager de um backing que é
sub-slice (`s[a..b]`), reinterpret (`str`↔`[]byte`, mesma memória), literal, ou parâmetro
emprestado CORROMPE o vizinho/aliás. O predicado `assign_frees_old` generalizado (C8) DEVE
liberar apenas quando o backing anterior é alocação de arena fresca possuída pela variável.
Conservador: na dúvida, NÃO liberar (o drop-de-escopo recolhe depois). Este é o ponto de
maior cuidado do plano.

**R3 — O slot de controle da arena ainda é C (3 shims `from "teko_rt"`).** Fora do caminho
de memória, mas viola "TUDO em Teko" enquanto não transcrito (C9). Rota: slot `.bss` via
`LGlobalAddr` (P2 honesto de `arena-em-teko.md`) ou `mmap MAP_FIXED` (plano B). Não bloqueia
a meta de memória; registrado para fechar o expurgo do C.
