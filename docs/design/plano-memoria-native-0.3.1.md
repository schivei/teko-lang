# Plano — campanha de memória do caminho de emissão native (0.3.1)

> **Papel:** arquiteto (SÓ design/levantamento; nenhuma linha de produto tocada). Base:
> `origin/feat/crumb10-strbytes-constbs` HEAD `adde10ff` (Crumbs 1-10). Branch
> `arch/native-mem-campaign`. Recon 100% ESTÁTICO (nenhum build-probe — verificador builda à parte).
>
> **Objetivo (D206 — eixo 2):** o native OOMa a **~11,5 GB** (VmHWM ~12,1 GB, exit 137) ao buildar
> `sys_exit_group`, contra a rota C ~1,08 GB. É a dívida NO-PUSHES do path native (copy-grow O(n²) +
> vazamento reclaim-0%) que NUNCA recebeu a campanha de memória da rota C. **Gate-alvo por crumb:
> `pico_native ≤ pico_C × 1.10` (~1,2 GB)** — análogo native do ratchet D68.

---

## 0. Achado-raiz que muda a cura — `List<T>.push` TAMBÉM é copy-grow aritmético

`src/collections/list.tks:15`:

```teko
pub fn push(x: T) { self.items = [..self.items, x] }
```

O `List<T>` de hoje **NÃO cresce in-place** — cada `push` reconstrói o backing inteiro
(`[..self.items, x]`), grow-by-1 (aritmético): copiar N vezes listas de tamanho 1,2,…,N = **O(n²)**.
Sob reclaim-0% do path native (a arena root nunca libera) cada cópia intermediária **VAZA** → O(n²)
de memória. Logo **"converter para List<> in-place" (D206) usando o `List<>` ATUAL NÃO cura nada** —
troca `[..x,y]` por outro `[..x,y]` embrulhado numa classe.

**A cura real (insight geométrico, resolve law-first — §7 Fork-A):** o que mata o O(n²) é
**crescimento GEOMÉTRICO** (dobra de capacidade). Com dobra, as cópias têm tamanho 1,2,4,…,N e a soma
é `2N` = **O(n)** — mesmo SEM reclaim (os intermediários vazados somam O(n), não O(n²)). É a diferença
entre 11,5 GB e trivial. Um `List<T>` genuinamente in-place = backing sobre-alocado (`items` de
CAPACIDADE) + `count` separado (watermark); `push` grava `items[count]=x; count++` e só realoca
(dobrando) quando `count==capacity`. **Isto É a lei 2026-08-29 ("List<> cresce IN-PLACE, sem `x=…`")
tomada à letra** e casa com a natureza FILTRO (sobre-aloca + `count`). O array-header segue `{ptr,len}`
SEM `cap` (a lei do header vale para o `[]T`, não para uma CLASSE container que carrega o próprio
`count`). Ver Fork-A (§7): é a decisão load-bearing da campanha.

**Corolário de sequência:** o pré-requisito de TODA conversão que use `List<>` é **tornar
`List<T>.push` geométrico** (Crumb 11a). Sem isso, nenhum `List<>` cura o O(n²).

---

## 1. Censo dos acumuladores copy-grow no path native (ordenado por impacto)

Legenda de cura: **(a)** duas-passadas / pré-aloca `[n]T` (n contável barato — MAP/PARSE); **(b)**
`List<T>` geométrico in-place (crescimento dinâmico genuíno, n irredutível/streaming); **(c)** reclaim
por-região D130 (vaza na root). Muitos = (b) resolve memória SEM (c) pelo insight geométrico.

Legenda de fase: **LOWER** (roda ao lowerizar todo programa, inclui o prelúdio → HOT no reprodutor);
**ISEL/RA** (por função); **EMIT** (encode+objfile, escala com tamanho de `.text/.rodata`); **BUILD**
(project.tks copy-to-region no emit).

### Top-10 por impacto

| # | Acumulador | arquivo:linha | Fase | Complexidade | Cura |
|---|---|---|---|---|---|
| 1 | `intern_rodata` — `[..ctx.rodata, entry]` por string internada (prelúdio inteiro) | `lir/lower.tks:5225` | LOWER | **O(strings²)** whole-program → **hog #1, 11,5 GB** | (b) `List<LRodata>` geométrico |
| 2 | `add_rodata`/`with_rodata` — merge rodata por-função no `LModule` | `lir/lir.tks:247`, `:254`; `lower.tks:6565,6984` | LOWER | O(funcs·rodata) acumula whole-program | (b) mesma `List<LRodata>` de #1 |
| 3 | `LEnv` bind — 7 arrays paralelos copiados por binding | `lir/lower.tks:20-24,33-36,42-45,51-54,60-63` | LOWER | **O(bindings²)** por função | (a)/(b) — ver §3.2 |
| 4 | `add_func` — `[..m.funcs, f]` por função | `lir/lir.tks:235`; `backend/minst.tks:465`, `minst_x86.tks:385` | LOWER/ISEL | O(funcs²) módulo | (b) `List<LFunc>` |
| 5 | `add_inst`/`add_block` — `[..b.insts,i]`, `[..f.blocks,b]` | `lir/lir.tks:207,265`; `minst.tks:414,432,436`; `minst_x86.tks:334,352,356` | LOWER/ISEL | O(insts²)/bloco, O(blocks²)/fn | (b) `List<>` |
| 6 | `copy_*_to_current_region` (rodata/relocs/encoded-func/bytes) + `commit_rodata_delta` | `build/project.tks:1706-1830,1974-2018` | BUILD | O(n²) sobre funcs/relocs/bytes no emit | (a) pré-aloca do `.len` conhecido |
| 7 | regalloc RPO/eventos/intervalos/pins/subst — `out=[..out,…]` | `backend/regalloc.tks:61,68,77,117,332,337,362,433,722,972,1005,1061`; `regalloc_x86.tks:*` | ISEL/RA | O(insts²)/O(vregs²) por função | (a) contável do stream |
| 8 | encode byte-emit — `push_byte`/`emit_u32_le` `[..buf, b]` | `backend/encode_x86_64.tks:36,845,908,961,986,998,1009`; `encode_arm64.tks:485,726,1204,1227,1260` | EMIT | **O(bytes²)** sobre `.text` (latente: HOT no self-emit) | (b) `List<byte>` OU pré-dimensiona |
| 9 | objfile byte-builders (ELF/Mach-O/COFF/DWARF/ar) — `[..bytes, b]`/`[..b, …]` | `backend/objfile_elf.tks:21,25,612,625`; `objfile_macho.tks:*`; `objfile_coff.tks:109-113`; `dwarf.tks:53-303,528-623`; `objfile_ar*.tks:*` | EMIT | O(bytes²) por seção (latente) | (b) `List<byte>` |
| 10 | `str_to_bytes` — `[..out, s[i]]` por char | `lir/lower.tks:5209-5217` | LOWER | O(len²) por string (curtas → ruído; Crumb 10 já mediu ~8 MB) | (a) `[s.len]byte` + índice |

### Cauda (mesma classe, impacto menor / por-caso)

- `lir/lower.tks`: `100` (arr-replace), `602/657/675/681` (region_stack frames — profundidade ≤64, pequeno), `1459` (ref-fn infos), `1727/2869-2874/5290/5301/5490-5494` (arg lists por chamada — n pequeno), `2216/3173/3191/3202/3213/3224/3457/3470/3515/3526/3538/3553` (env-snapshot loops — O(n²) por escopo, ver §3.2), `6519/6629/6694/6705/6722-6737/6907/6908/6944/7055/7068/7092` (layout/decl acc — por-tipo, contável ⇒ (a)).
- `lir/lower_const.tks`: `31/91/102/114/190/200/251/286/308/535/565/581/637` — const-image byte/field acc; (a) contável na maioria.
- `lir/lir_print.tks`: `11-35` — só `--emit-lir` debug, frio.
- `lir/frame_escape.tks:20,145` — por-função, pequeno.
- `backend/isel_*`: `isel_x86_64.tks:582,664`; `isel_arm64.tks:24-26,775` — uses/classes por-inst, pequeno.
- `backend/abi_*`, `objfile_ar*` insert-sorted — por-símbolo, contável ⇒ (a).
- `build/project.tks`: `170` (dep items), `529/550/703-951` (argv de link — n pequeno, frio), `1524-1639` (dwarf facts — por-função), `3180-3641` (cov/diff — fora do emit native).

**Leitura:** o pico de 11,5 GB no `sys_exit_group` é dominado por **#1 (+#2)** — o prelúdio de runtime
inteiro é lowerizado mesmo para um `main` mínimo (milhares de strings), e `ctx.rodata` cresce O(n²)
vazando. **#3/#4/#5/#7** são O(n²) por-função/módulo que também disparam no lowering do prelúdio.
**#8/#9** (byte-emit) são LATENTES no reprodutor (o `.o` do `sys_exit_group` é minúsculo) mas viram
HOT no **self-emit** (o `.text` do compilador é MB) — precisam ser curados antes do `gen2.o==gen3.o`.

---

## 2. Independência da cascata (o que dá pra fazer JÁ)

**TODA a campanha de memória é independente da cascata N2** (ensinar novos lowerings). São conversões
NO-PUSHES puras de código que JÁ existe e JÁ roda:

- **#1-#10 são todos exercitáveis HOJE** pelo reprodutor `sys_exit_group` native (D206: o programa
  mínimo chega ao emit, não está atrás de honest-stop). Logo medíveis e atacáveis AGORA.
- A cascata (ensinar união/nullable/fat/etc.) só muda QUANTO código o native lowerização — não muda a
  NATUREZA dos acumuladores. Curar #1 hoje cura o mesmo #1 depois da cascata.
- **Única dependência latente:** #8/#9 (byte-emit) só ficam MATERIAIS quando o `.text`/`.rodata`
  emitido cresce (self-emit). Não dependem da cascata em si, mas o ganho só é MEDÍVEL num objeto
  grande. Sequenciados por último (Crumbs 15-16), antes do fixpoint de objeto.

Conclusão: **Crumbs 11-14 rodam já, em paralelo à cascata.** Nada bloqueado.

---

## 3. Crumb 11 — `intern_rodata` (hog #1)

### 3.1 Crumb 11a (pré-requisito) — `List<T>.push` geométrico

**Onde:** `src/collections/list.tks`. Muda o backing de grow-by-1 para dobra de capacidade +
watermark. Afeta AS DUAS rotas (List é compartilhado) → ritual completo + reseed (compiler-core).

```teko
/**
 * A growable, reference-semantic sequence of `T`. Backing is geometric: `items` holds capacity,
 * `count` the logical length; `push` writes at the watermark and only reallocates (doubling) when
 * full, giving amortized O(1) push and O(n) total memory even under an append-only arena.
 */
exp type List<T> = class {
    intern items: []T
    intern count: u64

    /**
     * Builds an empty `List<T>` with zero capacity.
     *
     * @return an empty list
     */
    pub static fn make(): List<T> { .{ items = []; count = 0 } }

    /**
     * The number of elements currently held (the logical length, not the capacity).
     *
     * @return the element count
     */
    pub fn len(): u64 { self.count }

    /**
     * True iff `len() == 0`.
     *
     * @return whether the list is empty
     */
    pub fn is_empty(): bool { self.count == 0 }

    /**
     * Appends `x` at the watermark in place, doubling capacity when full (amortized O(1)).
     *
     * @param x the element to append
     */
    pub fn push(x: T) {
        if self.count == self.items.len { self.items = list_grown(self.items, self.count) }
        self.items[self.count] = x
        self.count = self.count + 1
    }

    /**
     * Reads the element at index `i`; `i` must be in `[0, len())`.
     *
     * @param i the index
     * @return the element at `i`
     */
    pub fn get(i: u64): T { self.items[i] }

    /**
     * Overwrites the element at index `i` with `x`, in place; a no-op if `i >= len()`.
     *
     * @param i the index
     * @param x the replacement element
     */
    pub fn set(i: u64, x: T) { if i < self.count { self.items[i] = x } }

    /**
     * Removes the last element, in place; a no-op on an empty list.
     */
    pub fn pop() { if self.count > 0 { self.count = self.count - 1 } }

    /**
     * A `[]T` snapshot of the list's current contents (length `len()`, capacity trimmed).
     *
     * @return a fresh `[]T` of exactly `len()` elements
     */
    pub fn to_array(): []T { list_prefix(self.items, self.count) }
}
```

Helpers privados (corpo real — D161, nada inline no backend):

```teko
/**
 * Returns a fresh backing of at least `count+1` capacity (doubling, min 4), copying the live prefix.
 *
 * @param items the current capacity-sized backing
 * @param count the live element count
 * @return a doubled backing holding the first `count` elements
 */
fn list_grown<T>(items: []T, count: u64): []T {
    var cap = if items.len == 0 { 4 } else { items.len * 2 }
    var out: [cap]T = []
    var i: u64 = 0
    loop { if i >= count { break }; out[i] = items[i]; i++ }
    out
}

/**
 * A fresh `[n]T` holding the first `n` elements of `items`.
 *
 * @param items the backing
 * @param n     the prefix length
 * @return the trimmed prefix
 */
fn list_prefix<T>(items: []T, n: u64): []T {
    var out: [n]T = []
    var i: u64 = 0
    loop { if i >= n { break }; out[i] = items[i]; i++ }
    out
}
```

**Nota de memória:** `list_grown` faz `self.items = …` (reatribuição) → sob reclaim-0% do native o
backing antigo vaza, mas a série geométrica soma O(n) → aceitável. Sob a rota-C (purge-na-reatribuição
D130 LANDADO) o antigo é liberado eager → O(n) estrito. Nas duas rotas o pico ≤ 3n instantâneo.

### 3.2 Crumb 11b — `LowerCtx.rodata: List<LRodata>`

**Onde:** `src/lir/lower.tks` (`LowerCtx` decl `:439-441`, `ctx_with_rodata:478`, `intern_rodata:5222`,
`rodata_symbol:5205`, `find_const_rodata:789`), `src/lir/lir.tks` (`add_rodata:246`, `with_rodata:254`,
`LModule.rodata`), e os pontos de merge (`lower.tks:6565,6984`, `LoweredFunction.rodata`).

**Forma:** `ctx.rodata` passa de `[]LRodata` para `List<LRodata>` (reference-semantic). `intern_rodata`
troca a reconstrução por append in-place:

```teko
/**
 * Interns `text` into the module's read-only data, returning its symbol. The rodata list grows in
 * place (geometric `List`), so interning the whole runtime prelude is O(n) memory, not O(n²).
 *
 * @param ctx  the lowering context carrying the shared rodata list
 * @param text the string to intern
 * @return the (unchanged-reference) ctx and the assigned `.Lstr<i>` symbol
 * @since 0.3.1
 */
fn intern_rodata(ctx: LowerCtx, text: str): InternedRodata {
    var sym = rodata_symbol(ctx.rodata.len())
    ctx.rodata.push(LRodata { symbol = sym; bytes = str_to_bytes(text); relocs = [] })
    InternedRodata { ctx = ctx; symbol = sym }
}
```

**Consideração de threading (design, não bloqueio):** `LowerCtx` é imutável/funcional (threaded). Com
`List` reference-semantic, várias versões de `ctx` compartilham a MESMA `List` de rodata — o que é
CORRETO: rodata é acumulada monotonicamente program-wide, nunca há rollback especulativo de rodata
(`find_const_rodata` só LÊ; lambdas só append). Os merges por-função (`with_rodata`/`add_rodata`,
`LoweredFunction.rodata`) deixam de copiar: o `LModule.rodata` passa a ser a MESMA `List` acumulada
(ou, transição mínima, `to_array()` UMA vez no fecho do módulo, não por-função). Recomendo colapsar o
seam por-função (a rodata é program-wide) — elimina #1 E #2 de uma vez.

**Fixture (reprodutor, native exit):** `sys_exit_group` com `TEKO_BACKEND=native` — o programa que
D206 já nomeou. Mede `teko: memory: peak` do build native (agora que chega ao emit). **Não** cria
`.tkr`/`.tkt` novo (a lei: o self-build exercita; o reprodutor é medição, não teste). Alvo do crumb:
derrubar de ~11,5 GB para a ordem de ~C+ (o rodata era o O(n²) dominante).

**Ritual:** gate completo (compiler-core: `list.tks`+`lower.tks`+`lir.tks`) — fixpoint gen0→gen1
byte-idêntico (rota C) + ASan+UBSan + 3 harnesses `scripts/*_test.sh` + grep zero-ref + MEM_PARANOID no
`.o` do reprodutor (D203). Reseed `bootstrap/teko.c`. Mede pico rota-C (ratchet D68, ±10 ruído) E pico
native (gate `≤C+10%`).

---

## 4. Sequência proposta de crumbs (bisectável, cada um mede o pico native no `sys_exit_group`)

| Crumb | Alvo | Censo | Cura | Fase que ataca | Gate |
|---|---|---|---|---|---|
| **11a** | `List<T>.push` geométrico | list.tks:15 | (b) | pré-requisito universal | `≤C+10%` + reseed |
| **11b** | `intern_rodata` + merge rodata | #1,#2,#10 | (b)/(a) | LOWER (hog) | `≤C+10%` — grande queda esperada |
| **12** | `LEnv` binds + env-snapshots | #3 + cauda 2216-3553 | (a)/(b) | LOWER (por-fn) | `≤C+10%` |
| **13** | IR containers: `add_func`/`add_block`/`add_inst` (LIR+MInst) | #4,#5 | (b) `List<>` | LOWER/ISEL | `≤C+10%` |
| **14** | regalloc out-lists | #7 | (a) contável | ISEL/RA | `≤C+10%` |
| **15** | `copy_*_to_current_region` + `commit_rodata_delta` | #6 | (a) pré-aloca `.len` | BUILD/emit | `≤C+10%` |
| **16** | encode + objfile byte-emit | #8,#9 | (b) `List<byte>` | EMIT (latente→self-emit) | `≤C+10%` + prep `gen2.o==gen3.o` |

**Estimativa até `≤C+10%`:** o hog é #1/#2 → **Crumb 11 sozinho deve derrubar a maior parte** dos
11,5 GB (o O(strings²) do prelúdio é o dominante). Crumbs 12-14 (O(n²) por-função no lowering do
prelúdio) devem fechar o gate no reprodutor. **Estimativa: `≤C+10%` alcançável em 4 crumbs (11-14)**;
15-16 são para o **self-emit** (objeto grande) e o fixpoint de objeto, não para o reprodutor mínimo.
Cada crumb MEDE o pico native no `sys_exit_group` e reporta a queda.

---

## 5. Regra PROIBIDO-C respeitada (D148)

Toda cura é Teko: `List<>` geométrico (`collections/list.tks`), pré-alocação `[n]T` + índice, corte.
Zero `teko_rt.c`/`tk_*` novo, zero libc, zero grow-primitive em C. O `list_grown`/`list_prefix` são
corpo Teko de superfície (D161), não inline-synth no backend.

---

## 6. Interação com o modelo por-escopo (D130) no native — NÃO é pré-requisito do gate

O path native NÃO tem o reclaim/purge-na-reatribuição por-região wired (o `open_native_region` dirige
elisão de arena via `scope_slot_count==0`, mas o reclaim geral ainda é `tk_region_*` extern; região=param
no emit NÃO existe). **A tese central deste plano:** o **crescimento GEOMÉTRICO sozinho dá O(n) de
memória mesmo sob reclaim-0%** (série 2n) — logo o gate `≤C+10%` é alcançável **SEM** wire o modelo
por-escopo no native, que é o **byte-mover de MAIOR RISCO** do D130 (região=param em toda assinatura/
chamada de emit). Isto é "adiantar o que der" (D154/D155): faz-se a cura barata e segura primeiro; só se
escala ao byte-mover de região se sobrar um O(n²) residual irredutível por geométrico. Ver Fork-B.

---

## 7. Forks (para o dono)

### Fork-A — `List<T>` pode carregar `count`/capacidade e crescer GEOMÉTRICO? (RESOLVIDO law-first; ponto de veto)

**Tensão:** NO-PUSHES baniu `tk_slice_push_r`/`grow_inplace` (amortized copy-grow atrás de arrays nus);
a lei do array-header diz `{ptr,len}` **SEM `cap`**. O Crumb 11a re-introduz capacidade+`count` DENTRO
da classe `List<T>` e dobra geométrica.

**Resolução law-first (passa-todas-as-leis, NÃO HALT):** a lei 2026-08-29 designa `List<>` como o LAR
sancionado do crescimento dinâmico genuíno e exige que ele cresça **"IN-PLACE, sem `x=…`"** — o
`List.push` atual (`[..self.items,x]`) VIOLA isso (é copy-grow, não in-place). Tornar `List` geométrico
com `count`/capacidade é a ÚNICA forma de satisfazer a lei existente. A lei "SEM cap" governa o
**array-header `[]T`** (que permanece `{ptr,len}`); uma CLASSE container carregar o próprio `count` é a
natureza FILTRO (sobre-aloca + watermark), já sancionada. Portanto: **não é permissão nova, é conformar
`List` à lei que já existe.** Recomendo ratificar. Registro como ponto de veto por ser sensível (o dono
baniu amortized copy-grow) e load-bearing (toda a campanha depende).

### Fork-B — o gate `≤C+10%` exige wire do modelo por-escopo (região=param) no emit native?

**Enunciado:** SE, após os Crumbs 11-14 (conversões geométricas), sobrar um O(n²) residual atribuível ao
threading funcional de `LowerCtx`/`LEnv` (versões antigas do ctx retêm snapshots de arrays que vazam na
root), o gate pode exigir wire do reclaim por-região D130 no path native — o **byte-mover de MAIOR
RISCO** (região=param em toda assinatura/chamada de emit; `TypeTable` no lowering).

**Recomendação (não-HALT):** tentar geométrico-só primeiro (Crumbs 11-14) e MEDIR. O insight geométrico
prediz O(n) sem reclaim → provavelmente NÃO precisa. Só se um residual O(n²) persistir é que vira
architect-deeper para o byte-mover de região no native. NÃO adiantar o byte-mover de risco especulativo.
Sinalizado para o dono decidir SE e QUANDO o residual aparecer — hoje não há tensão aberta.

---

## 8. Âncoras (verificadas em `adde10ff`)

| o quê | arquivo:linha |
|---|---|
| hog #1 rodata copy-grow | `src/lir/lower.tks:5225` (`intern_rodata`), `:5222`, `:5205` |
| `List.push` aritmético (a curar) | `src/collections/list.tks:15` |
| `LowerCtx.rodata` decl + threading | `src/lir/lower.tks:439-441,478` |
| merge rodata program-wide | `src/lir/lir.tks:246-256`; `lower.tks:6565,6984` |
| `LEnv` binds 7-arrays | `src/lir/lower.tks:20-24,33-63` |
| IR containers append | `src/lir/lir.tks:207,235,247,265`; `backend/minst.tks:414,432-436,465`; `minst_x86.tks:334,352-356,385` |
| regalloc out-lists | `src/backend/regalloc.tks:61-1061`; `regalloc_x86.tks:86-397` |
| copy-to-region emit | `src/build/project.tks:1706-1830,1974-2018` |
| encode/objfile byte-emit | `src/backend/encode_x86_64.tks:36,…`; `encode_arm64.tks:*`; `objfile_{elf,macho,coff}.tks:*`; `dwarf.tks:*` |
| reprodutor + gate | D206 (`sys_exit_group` native, `pico_native ≤ pico_C × 1.10`) |

*Grounding: `arquivo:linha` reais em `adde10ff`. Recon 100% estático — nenhum build-probe rodado
(verificador builda à parte). Nenhuma linha de produto tocada — só este doc.*
