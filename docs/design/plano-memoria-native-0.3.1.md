# Plano — campanha de memória do caminho de emissão native (0.3.1)

> **Papel:** arquiteto (SÓ design/levantamento; nenhuma linha de produto tocada). Base:
> `origin/feat/crumb10-strbytes-constbs` HEAD `adde10ff` (Crumbs 1-10). Branch
> `arch/native-mem-campaign`. Recon 100% ESTÁTICO (nenhum build-probe — verificador builda à parte).
>
> **Objetivo (D206 — eixo 2):** o native OOMa a **~11,5 GB** (VmHWM ~12,1 GB, exit 137) ao buildar
> `sys_exit_group`, contra a rota C ~1,08 GB. É a dívida NO-PUSHES do path native (copy-grow O(n²) +
> vazamento reclaim-0%) que NUNCA recebeu a campanha de memória da rota C. **Gate-alvo por crumb:
> `pico_native ≤ pico_C × 1.10` (~1,2 GB)** — análogo native do ratchet D68.
>
> **Mecanismo RULADO pelo dono (D207) — revisão desta versão:** o geométrico (capacity-doubling)
> proposto na v1 foi **VETADO** (recopia na dobra = o amortized copy-grow que o NO-PUSHES expurgou do
> C). O crescimento dinâmico é: **(ii) CHUNKED/segmentado (nunca recopia elemento)** para STREAMING;
> **(iii) DUAS-PASSADAS → array-fixo `[n]T`** onde `n` é contável barato ("se consegue contar, usa
> array fixo"). Geométrico: PROIBIDO.

---

## 0. Achado da auditoria — `List<T>` está MORTO na árvore; o residual é o `[..x,y]` direto

Grep tree-wide (excl. `ngen/` = superfície futura): **`List<T>` (a classe de `collections/list.tks`)
NÃO é usado em lugar nenhum** — zero `.push(`, zero instanciação fora da própria definição
(`list.tks:2,6`). `ParsedList<T>` (`parser/result.tks:2`) é um struct `{items,next}` SEPARADO, não a
classe `List`.

**Consequência para a observação "<50%" do dono:** a hipótese de que o `List<>.push` (copy-grow
`self.items=[..self.items,x]`, `list.tks:15`) escondeu copy-grow na rota C **NÃO se confirma na
árvore** — como ninguém chama `List<>`, não há residual dele exercitado por rota alguma. O `List<T>` é
superfície `exp` de stdlib **morta** (nunca exercitada pelo self-build). O buraco real do "<50%" é:
**(a) o path de emit NATIVE nunca foi varrido** (os ~10 acumuladores deste censo) — é o alvo deste
plano; **(b)** `[..x,y]` DIRETOS remanescentes na rota-C src que o sweep não pegou — auditoria
adjacente (reportada, §7), fora do D206 eixo 2.

`List<T>` em si (§3.4): deve ser **reescrito sobre o `Segmented<T>` chunked** (fecha a lei mesmo sendo
morto — é a superfície de crescimento dinâmico da stdlib), como crumb de higiene de stdlib de baixa
prioridade (fora do hot path native).

---

## 1. Censo re-classificado — STREAMING→chunked vs CONTÁVEL→array-fixo `[n]T`

Critério (D207): **dá pra contar `n` barato numa 1ª passada?** SIM → **array-fixo `[n]T`** (conta,
pré-aloca, grava por índice — MAP/PARSE/FILTRO). NÃO (`n` irredutível até o fim) → **CHUNKED** (§2).

Fase: **LOWER** (todo programa, inclui o prelúdio → HOT no reprodutor); **ISEL/RA** (por função);
**EMIT** (encode+objfile, escala com o tamanho de `.text/.rodata`); **BUILD** (project.tks copy-to-region).

### Top-10 por impacto

| # | Acumulador | arquivo:linha | Fase | `n` contável barato? | Cura |
|---|---|---|---|---|---|
| 1 | `intern_rodata` `[..ctx.rodata, entry]` (prelúdio inteiro) | `lir/lower.tks:5225` | LOWER | NÃO — n = strings internadas, só ao fim do lowering | **CHUNKED** `Segmented<LRodata>` |
| 2 | merge rodata `add_rodata`/`with_rodata` | `lir/lir.tks:247,254`; `lower.tks:6565,6984` | LOWER | NÃO — idem #1 (program-wide) | **CHUNKED** (unifica com #1) |
| 3 | `LEnv` binds (7 arrays paralelos) | `lir/lower.tks:20-24,33-63` | LOWER | Parcial — exigiria pré-scan+shadowing; cresce durante | **CHUNKED** (1 `Segmented<LBinding>`) |
| 4 | `add_func` (+ lifted lambdas/thunks) | `lir/lir.tks:235`; `minst.tks:465`; `minst_x86.tks:385` | LOWER/ISEL | NÃO — funcs LIFTADAS dinamicamente durante o lowering | **CHUNKED** `Segmented<LFunc>` |
| 5 | `add_inst`/`add_block` | `lir/lir.tks:207,265`; `minst.tks:414,432,436`; `minst_x86.tks:334,352,356` | LOWER/ISEL | NÃO — nº de insts por stmt é variável, só ao emitir | **CHUNKED** `Segmented<LInst>`/`<LBlock>` |
| 6 | `copy_*_to_current_region` + `commit_rodata_delta` | `build/project.tks:1706-1830,1974-2018` | BUILD | **SIM** — copia array EXISTENTE (`.len` conhecido) | **ARRAY-FIXO** `[src.len]T`+índice |
| 7 | regalloc RPO/eventos/intervalos/pins/subst | `backend/regalloc.tks:61-1061`; `regalloc_x86.tks:86-397` | ISEL/RA | **SIM** — derivados do stream de tamanho conhecido | **ARRAY-FIXO / FILTRO** (conta + watermark) |
| 8 | encode byte-emit `push_byte`/`emit_u32_le` | `backend/encode_x86_64.tks:36,845,908,961,986,998,1009`; `encode_arm64.tks:485,726,1204,1227,1260` | EMIT | NÃO — x86 é variable-length; tamanho só ao encodar | **CHUNKED** `Segmented<byte>` → flatten p/ seção |
| 9 | objfile byte-builders (ELF/Mach-O/COFF/DWARF/ar) | `objfile_elf.tks:21,25,612,625`; `objfile_macho.tks:*`; `objfile_coff.tks:109-113`; `dwarf.tks:53-303,528-623`; `objfile_ar*.tks:*` | EMIT | Misto — headers fixos (array-fixo); corpo de seção (chunked) | **CHUNKED** corpo + **ARRAY-FIXO** headers |
| 10 | `str_to_bytes` `[..out, s[i]]` | `lir/lower.tks:5209-5217` | LOWER | **SIM** — `n = s.len` | **ARRAY-FIXO** `[s.len]byte`+índice |

**Resumo:** CHUNKED (streaming, n irredutível): **#1,#2,#3,#4,#5,#8** e o corpo de seção de **#9**.
ARRAY-FIXO (contável): **#6,#7,#10** e os headers de **#9**.

### Cauda (mesma classe, menor / por-caso) — todos CONTÁVEIS ⇒ array-fixo salvo nota

- `lir/lower.tks`: `100` (arr-replace — array-fixo), `602/657/675/681` (region_stack — profundidade ≤64, array-fixo pequeno), `1459` (ref-fn infos — contável), `1727/2869-2874/5290/5301/5490-5494` (arg lists por chamada — n pequeno contável), `2216/3173/3191/3202/3213/3224/3457/3470/3515/3526/3538/3553` (env-snapshots — derivam do env, contável ⇒ array-fixo; ver §3.3), `6519/6629/6694/6705/6722-6737/6907/6908/6944/7055/7068/7092` (layout/decl por-tipo — contável).
- `lir/lower_const.tks`: `31/91/102/114/190/200/251/286/308/535/565/581/637` — const-image byte/field; contável ⇒ array-fixo.
- `lir/lir_print.tks:11-35` — só `--emit-lir` debug, frio (deixar).
- `lir/frame_escape.tks:20,145` — por-função pequeno, contável.
- `backend/isel_*`: `isel_x86_64.tks:582,664`; `isel_arm64.tks:24-26,775` — uses/classes por-inst, pequeno contável.
- `backend/abi_*`, `objfile_ar*` insert-sorted — por-símbolo contável.
- `build/project.tks`: `170` (dep items), `529/550/703-951` (argv de link — pequeno frio), `1524-1639` (dwarf facts por-função), `3180-3641` (cov/diff — fora do emit native).

**Leitura:** o pico de 11,5 GB no `sys_exit_group` é dominado por **#1(+#2)** — o prelúdio inteiro é
lowerizado mesmo p/ um `main` mínimo (milhares de strings), `ctx.rodata` cresce O(n²) vazando.
**#3/#4/#5/#7** são O(n²) por-função/módulo que também disparam no lowering do prelúdio. **#8/#9**
(byte-emit) são LATENTES no reprodutor (o `.o` é minúsculo) mas viram HOT no **self-emit** (o `.text`
do compilador é MB) — curar antes do `gen2.o==gen3.o`.

---

## 2. Container CHUNKED `Segmented<T>` (Teko puro — D148, zero C)

**Estrutura: lista LIGADA de chunks (zero recopy jamais).** Cada elemento é escrito UMA vez no chunk
corrente; quando o chunk enche, aloca-se um chunk novo e liga-se ao anterior — a espinha é uma lista
ligada (`next`), NUNCA recopiada (nem elemento nem ponteiro-de-chunk). O(n) puro, sem amortized-copy.

**Reconciliação com "SEM cap" (header `{ptr,len}`):** o backing do chunk é um `[]T` alocado UMA vez em
`SEG_CHUNK` elementos (header `{ptr,len}` normal, `len==SEG_CHUNK`, nunca realocado); o `used` é o
watermark do CONTAINER (natureza FILTRO/count universal), não um `cap` do array-header. Elementos
não-preenchidos = zero-fill (`[SEG_CHUNK]T = []`), lidos só abaixo de `used`.

```teko
/**
 * Fixed element count per chunk. Backing is a `[SEG_CHUNK]T` allocated once and never regrown; the
 * container's `used`/`total` watermarks track fill (FILTRO/count law), so no array-header carries a
 * capacity. A byte-heavy stream may instantiate with a larger chunk; 1024 balances node overhead
 * against wasted tail slots for struct payloads.
 */
const SEG_CHUNK: u64 = 1024

/**
 * A single segment: a once-allocated `[SEG_CHUNK]T` backing, a fill watermark, and a link to the
 * next segment. Reference-semantic (class) so the spine is a linked list, never recopied.
 */
type SegNode<T> = class {
    intern data: []T
    intern used: u64
    intern next: SegNode<T> | null
}

/**
 * A segmented (chunked) growable sequence of `T` that never recopies an element or the spine on
 * growth: append writes at the tail watermark and links a fresh node when the current chunk fills.
 * O(n) total memory even under an append-only arena (reclaim-0%). `flatten` materializes a
 * contiguous `[]T` in two passes only where a consumer needs one (e.g. writing an object section).
 */
exp type Segmented<T> = class {
    intern head: SegNode<T> | null
    intern tail: SegNode<T> | null
    intern total: u64

    /**
     * Builds an empty segmented sequence (no chunk allocated until the first append).
     *
     * @return an empty `Segmented<T>`
     */
    pub static fn make(): Segmented<T> { .{ head = null; tail = null; total = 0 } }

    /**
     * The number of elements appended so far.
     *
     * @return the element count
     */
    pub fn len(): u64 { self.total }

    /**
     * Appends `x` at the tail in place, linking a fresh once-allocated chunk when the current one is
     * full. Never recopies an existing element or chunk pointer (O(1), no amortized copy).
     *
     * @param x the element to append
     */
    pub fn append(x: T) {
        match self.tail {
            SegNode<T> as t => { if t.used < SEG_CHUNK { t.data[t.used] = x; t.used = t.used + 1; self.total = self.total + 1; return } }
            null => {}
        }
        var fresh: [SEG_CHUNK]T = []
        var node = SegNode<T> { data = fresh; used = 1; next = null }
        node.data[0] = x
        seg_link(self, node)
        self.total = self.total + 1
    }

    /**
     * Reads the element at index `i` in `[0, len())` by walking `i / SEG_CHUNK` chunks. `i` out of
     * range panics (internal — unchecked, the compiler drives only valid indices).
     *
     * @param i the index
     * @return the element at `i`
     */
    pub fn get(i: u64): T {
        var node = self.head
        var rem = i
        loop {
            match node { SegNode<T> as n => { if rem < n.used { return n.data[rem] }; rem = rem - SEG_CHUNK to u64; node = n.next }; null => { break } }
        }
        seg_oob<T>()
    }

    /**
     * Overwrites the element at index `i` in place; a no-op if `i >= len()`.
     *
     * @param i the index
     * @param x the replacement element
     */
    pub fn set(i: u64, x: T) {
        var node = self.head
        var rem = i
        loop {
            match node { SegNode<T> as n => { if rem < n.used { n.data[rem] = x; return }; rem = rem - SEG_CHUNK to u64; node = n.next }; null => { return } }
        }
    }

    /**
     * Materializes a contiguous `[]T` of exactly `len()` elements. Two passes over the chunks: the
     * total is already tracked, so one allocation `[total]T` + one index-copy pass. Zero element
     * recopy during growth; the single copy here happens only at a consumer boundary.
     *
     * @return a fresh contiguous `[]T`
     */
    pub fn flatten(): []T {
        var out: [self.total]T = []
        var i: u64 = 0
        var node = self.head
        loop {
            match node {
                SegNode<T> as n => { var j: u64 = 0; loop { if j >= n.used { break }; out[i] = n.data[j]; i = i + 1; j = j + 1 }; node = n.next }
                null => { break }
            }
        }
        out
    }
}
```

Helpers privados (corpo real — D161):

```teko
/**
 * Links `node` as the new tail, setting head on the first insertion. Panics never (internal).
 *
 * @param s    the segmented sequence
 * @param node the fresh tail node
 */
fn seg_link<T>(s: Segmented<T>, node: SegNode<T>) {
    match s.tail { SegNode<T> as t => { t.next = node }; null => { s.head = node } }
    s.tail = node
}

/**
 * Panics on an out-of-range segmented index (internal, unchecked path).
 *
 * @return never returns
 */
fn seg_oob<T>(): T { teko::runtime::panic("segmented index out of range") }
```

**Nota de projeto:** `data: []T` (não `[SEG_CHUNK]T` como TIPO de campo) evita depender de array-fixo
como campo de classe; o backing é criado com `var fresh: [SEG_CHUNK]T = []` (zero-fill, alocado uma vez)
e guardado como `[]T`. Iteração/`get` são O(i/SEG_CHUNK); para `LEnv` (lookup já linear por nome hoje —
`lenv_newest_index`) não piora o assintótico. `SEG_CHUNK` é tunável; byte-streams (#8/#9) podem usar um
valor maior (menos nós). Se generics-com-const-param existir, parametrizar o chunk; senão, uma const de
módulo + (se preciso) uma variante byte dedicada.

**Verificação de superfície necessária (scout do Crumb 11a):** confirmar que (1) classe genérica
recursiva com campo `next: SegNode<T> | null` compila (precedente: `xml::XmlNode | null` em campos —
`crypto/xmldsig/*`); (2) `var x: [N]T = []` como expressão de init de campo via variável local
funciona (precedente: `[count]byte = []` em `runtime/rtio.tks:195`, `numfmt.tks`). Ambos têm precedente
— risco baixo, mas o scout valida antes do implementer.

---

## 3. Crumb 11 — `intern_rodata` (hog #1) com o container chunked

### 3.1 Crumb 11a — introduzir `Segmented<T>` (aditivo, zero mudança de comportamento)

**Onde:** novo módulo `src/collections/segmented.tks` (§2). Puro-aditivo: nenhum consumidor ainda →
não muda o `teko.c` emitido semanticamente além da nova superfície. Scout valida a superfície (acima).
**Ritual:** compila + fixpoint-C (a nova decl entra no `.tkh`/`teko.c`) + ASan/UBSan + 3 harnesses.
Reseed.

### 3.2 Crumb 11b — `LowerCtx.rodata: []LRodata → Segmented<LRodata>`

**Onde:** `src/lir/lower.tks` (`LowerCtx` decl `:439-441`, `ctx_with_rodata:478`, `intern_rodata:5222`,
`rodata_symbol:5205`, `find_const_rodata:789`), `src/lir/lir.tks` (`add_rodata:246`, `with_rodata:254`,
`LModule.rodata`), pontos de merge (`lower.tks:6565,6984`, `LoweredFunction.rodata`), e o
`commit_rodata_delta`/`copy_lrodata_to_current_region` de `project.tks` (que consome a lista — passa a
consumir `.flatten()` UMA vez ao emitir).

```teko
/**
 * Interns `text` into the module's read-only data, returning its symbol. The rodata is a chunked
 * `Segmented`, so interning the whole runtime prelude never recopies an entry: O(n) memory, not
 * O(n²). The reference-semantic sequence lets the functional `ctx` thread the same rodata without a
 * per-append rebuild.
 *
 * @param ctx  the lowering context carrying the shared rodata sequence
 * @param text the string to intern
 * @return the (unchanged-reference) ctx and the assigned `.Lstr<i>` symbol
 * @since 0.3.1
 */
fn intern_rodata(ctx: LowerCtx, text: str): InternedRodata {
    var sym = rodata_symbol(ctx.rodata.len())
    ctx.rodata.append(LRodata { symbol = sym; bytes = str_to_bytes(text); relocs = [] })
    InternedRodata { ctx = ctx; symbol = sym }
}
```

- `rodata_symbol(ctx.rodata.len())` — `len()` do `Segmented`, igual semântica.
- `find_const_rodata` (`:789`) itera lendo → passa a iterar o `Segmented` (walk de chunks) ou consulta
  via `get`; só LÊ, sem recopy.
- **Threading:** `LowerCtx` é funcional/threaded; com `Segmented` reference-semantic, versões de `ctx`
  compartilham a MESMA sequência — CORRETO (rodata é monotônica program-wide, sem rollback especulativo;
  `find_const_rodata` só lê, lambdas só apendam). **Colapsar o seam por-função** (`with_rodata`/
  `add_rodata`/`LoweredFunction.rodata`): o `LModule.rodata` passa a ser a MESMA sequência acumulada;
  `flatten()` UMA vez no fecho do módulo (quando o emit precisa de `[]LRodata` contíguo). Elimina #1 E #2.
- **`str_to_bytes` (#10) dobra aqui** (barato): `var out: [s.len]byte = []` + índice (array-fixo), some
  o `[..out, s[i]]`.

**Fixture (reprodutor, native):** `sys_exit_group` com `TEKO_BACKEND=native` (o programa que D206 já
nomeou). Mede `teko: memory: peak` do build native (agora chega ao emit). **Não** cria `.tkr`/`.tkt`
novo (self-build exercita; o reprodutor é medição). Alvo: derrubar de ~11,5 GB para a ordem de ~C+ (o
rodata era o O(n²) dominante).

**Ritual:** gate completo (compiler-core: `lower.tks`+`lir.tks`+`project.tks`+`segmented.tks`) —
fixpoint gen0→gen1 byte-idêntico (rota C) + ASan+UBSan + 3 harnesses `scripts/*_test.sh` + grep
zero-ref + MEM_PARANOID no `.o` do reprodutor (D203). Reseed. Mede pico rota-C (ratchet D68, ±10 ruído)
E pico native (gate `≤C+10%`).

### 3.3 Nota sobre os env-snapshots (cauda 2216-3553)

Vários loops copiam sub-conjuntos do `LEnv` (prefixos, filtros por escopo) — todos derivam de um
`env`/lista de tamanho CONHECIDO → **array-fixo** (conta o alvo, pré-aloca `[n]T`, grava por índice /
FILTRO com watermark). Não precisam de chunked. Ficam no Crumb 12 junto do `LEnv`.

### 3.4 `List<T>` (stdlib morto) → reescrever sobre `Segmented<T>` (higiene, baixa prioridade)

`List<T>` (`collections/list.tks`) é morto (§0) mas é superfície `exp`. Para fechar a lei (o `push`
atual é copy-grow), reescrever `List` como fachada fina sobre `Segmented<T>` (`push`→`append`,
`get`/`set`→idem, `to_array`→`flatten`; `pop`/`remove_at` seguem O(n) como já eram). **Fora do hot
path native** (List não é exercitado) → crumb de higiene separado, não bloqueia a campanha; pode
dobrar no Crumb 11a (mesmo módulo/PR do `Segmented`) já que é trivial e prova o container.

---

## 4. Sequência proposta de crumbs (bisectável; cada um mede o pico native no `sys_exit_group`)

| Crumb | Alvo | Censo | Mecanismo | Fase | Gate |
|---|---|---|---|---|---|
| **11a** | `Segmented<T>` chunked (+ `List<T>` fachada) | novo `segmented.tks`; `list.tks` | CHUNKED | aditivo | fixpoint-C + reseed |
| **11b** | `intern_rodata` + merge rodata + `str_to_bytes` | #1,#2,#10 | CHUNKED + array-fixo | LOWER (hog) | `≤C+10%` — grande queda |
| **12** | `LEnv` binds + env-snapshots | #3 + cauda 2216-3553 | CHUNKED (`LBinding`) + array-fixo | LOWER (por-fn) | `≤C+10%` |
| **13** | IR containers: `add_func`/`add_block`/`add_inst` (LIR+MInst) | #4,#5 | CHUNKED | LOWER/ISEL | `≤C+10%` |
| **14** | regalloc out-lists | #7 | ARRAY-FIXO/FILTRO | ISEL/RA | `≤C+10%` |
| **15** | `copy_*_to_current_region` + `commit_rodata_delta` | #6 | ARRAY-FIXO `[src.len]T` | BUILD/emit | `≤C+10%` |
| **16** | encode + objfile byte-emit | #8,#9 | CHUNKED corpo + array-fixo headers | EMIT (latente→self-emit) | `≤C+10%` + prep `gen2.o==gen3.o` |

**Estimativa até `≤C+10%`:** o hog é #1/#2 → **Crumb 11 sozinho deve derrubar a maior parte** dos
11,5 GB. Crumbs 12-14 (O(n²) por-função no lowering do prelúdio) fecham o gate no reprodutor.
**Estimativa: `≤C+10%` alcançável em 4 crumbs (11-14)**; 15-16 são para o **self-emit** (objeto grande)
e o fixpoint de objeto, não para o reprodutor mínimo. Cada crumb MEDE o pico native no `sys_exit_group`
e reporta a queda.

---

## 5. Regra PROIBIDO-C respeitada (D148) + independência da cascata

- **Zero C:** toda cura é Teko — `Segmented<T>` (`collections/segmented.tks`), pré-alocação `[n]T` +
  índice, corte. Nenhum `teko_rt.c`/`tk_*` novo, nenhum grow-primitive, nenhum amortized/geométrico.
  `seg_link`/`seg_oob`/`flatten` são corpo de superfície (D161), não inline-synth no backend.
- **Independente da cascata N2:** #1-#10 são todos exercitáveis HOJE pelo reprodutor `sys_exit_group`
  (D206: chega ao emit, não está atrás de honest-stop). Curar #1 hoje cura o mesmo #1 depois da
  cascata. Única latência: #8/#9 (byte-emit) só ficam MATERIAIS num `.o` grande (self-emit) — o ganho
  só é medível num objeto grande, por isso 15-16 por último. **Crumbs 11-14 rodam já, em paralelo à
  cascata. Nada bloqueado.**

---

## 6. Modelo por-escopo (D130) no native — NÃO é pré-requisito do gate

O path native NÃO tem reclaim/purge-na-reatribuição por-região wired (o `open_native_region` dirige só
a elisão de arena via `scope_slot_count==0`; região=param no emit NÃO existe). **Tese central:** o
CHUNKED (lista ligada, zero recopy) dá **O(n) puro** de memória mesmo sob reclaim-0% (cada elemento e
cada nó alocados uma vez, nunca movidos; total = n + n/SEG_CHUNK nós). Logo o gate `≤C+10%` é alcançável
**SEM** wire o modelo por-escopo no native — o **byte-mover de MAIOR RISCO** do D130 (região=param em
toda assinatura/chamada de emit). "Adiantar o que der" (D154/D155): cura barata e segura primeiro; só
escalar ao byte-mover de região se sobrar O(n²) residual irredutível. Ver Fork-B.

---

## 7. Forks + achados adjacentes

### Fork-A — RESOLVIDO pelo dono (D207). Sem tensão aberta.
O mecanismo de crescimento está rulado: CHUNKED (streaming) + array-fixo `[n]T` (contável); geométrico
PROIBIDO. O `Segmented<T>` (§2) implementa (ii) sem recopy. Nada a decidir.

### Fork-B — o gate `≤C+10%` exige região=param no emit native? (deferido, sem tensão hoje)
SE, após 11-14, sobrar O(n²) residual do threading funcional de `LowerCtx`/`LEnv` (versões antigas
retendo snapshots que vazam na root), o gate pode exigir wire do reclaim por-região D130 no native — o
byte-mover de maior risco. **Recomendação (não-HALT):** chunked-só primeiro + MEDIR; o chunked prediz
O(n) sem reclaim → provavelmente não precisa. Só vira architect-deeper se o residual persistir. Não
adiantar risco especulativo.

### Achado adjacente (REPORTADO, não vira issue minha) — residual `[..x,y]` na rota-C src
O "<50%" do dono, fora do path native, aponta para `[..x,y]` DIRETOS remanescentes na rota-C src
(checker/codegen/etc.) que o sweep NO-PUSHES original não pegou. NÃO é `List<>` (morto, §0) e NÃO é
D206 eixo 2 (native). Um censo `[..x,y]` da rota-C src seria um eixo separado — reporto ao coordenador,
não abro escopo.

---

## 8. Âncoras (verificadas em `adde10ff`)

| o quê | arquivo:linha |
|---|---|
| hog #1 rodata copy-grow | `src/lir/lower.tks:5225` (`intern_rodata`), `:5222`, `:5205` |
| `List<T>` MORTO (0 usos tree-wide) | `src/collections/list.tks:2,6,15` (só a própria def) |
| `LowerCtx.rodata` decl + threading | `src/lir/lower.tks:439-441,478` |
| merge rodata program-wide | `src/lir/lir.tks:246-256`; `lower.tks:6565,6984` |
| `LEnv` binds 7-arrays | `src/lir/lower.tks:20-24,33-63` |
| IR containers append | `src/lir/lir.tks:207,235,247,265`; `backend/minst.tks:414,432-436,465`; `minst_x86.tks:334,352-356,385` |
| regalloc out-lists | `src/backend/regalloc.tks:61-1061`; `regalloc_x86.tks:86-397` |
| copy-to-region emit | `src/build/project.tks:1706-1830,1974-2018` |
| encode/objfile byte-emit | `src/backend/encode_x86_64.tks:36,…`; `encode_arm64.tks:*`; `objfile_{elf,macho,coff}.tks:*`; `dwarf.tks:*` |
| precedente nullable self-field / `[n]T=[]` | `src/crypto/xmldsig/*` (`XmlNode|null`); `src/runtime/rtio.tks:195`, `numfmt.tks` |
| reprodutor + gate | D206/D207 (`sys_exit_group` native, `pico_native ≤ pico_C × 1.10`, chunked) |

*Grounding: `arquivo:linha` reais em `adde10ff`. Recon 100% estático — nenhum build-probe rodado
(verificador builda à parte). Nenhuma linha de produto tocada — só este doc.*
