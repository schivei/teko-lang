# Plano — emit native com saída-direta-em-disco (streaming-com-passada-de-montagem) (0.3.1)

> **Papel:** arquiteto (SÓ design/levantamento; nenhuma linha de produto tocada). Base:
> `origin/fix/retirement` HEAD `90bcc4f1` (D208 landado). Branch `arch/emit-native-streaming`.
> Recon 100% ESTÁTICO (nenhum build-probe — o verificador builda à parte).
>
> **Reframe (D208, dono 2026-09-01):** o OOM native (~11,5 GB, VmHWM ~12,1 GB, exit 137) NÃO é
> problema de container. É que o **emit native ACUMULA as seções do `.o` inteiras em RAM** pra montar
> o binário no fim — nunca recebeu a campanha de I/O-STREAMING (D 2026-08-19), a mesma que fez a rota C
> parar de montar o `teko.c` de 22 MB em RAM e **gravar direto em disco** por chunks ≤1024 B. O fix é
> **NÃO SEGURAR A MONTANHA — gravar conforme produz.** O container (Segmented/Paged/chunked/geométrico —
> D207) fica só como referência residual (§4-M2).
>
> **Vocabulário (correção do dono):** "streaming" AQUI = saída-direta-em-disco por chunk (D 2026-08-19),
> NÃO "acumulador de tamanho desconhecido".
>
> **Gate (D206):** `pico_native ≤ pico_C × 1.10`. Alvo/marco da campanha `< 2 GB` (D203). Fixpoint native
> = `gen2.o == gen3.o` byte-idêntico (D203/D205).

---

## 1. O que a rota C já faz (o modelo a espelhar) — e o que o native faz de errado

**Rota C (CERTA — streaming):** `codegen.tks` recebe `buf: teko::io::FileStream` aberto sobre o path de
saída (`tk_emit_c_file(prog, out_path, meta)` — `project.tks:2458`) e emite tudo com `buf.write_byte(…)`/
`buf.write(…)` DIRETO no stream (`cb_i64`, `cb_cstr_escaped`, `emit_type`, … — `codegen.tks:60-410`+).
Nada acumula: o `teko.c` de 22 MB nunca existe inteiro em RAM. `FileStream` já corta em chunks ≤`CHUNK`
(1024 B) sobre syscalls, ZERO `teko_rt` (`io/file_stream.tks`).

**Rota native (ERRADA — acumula):** três camadas montam o `.o` inteiro em RAM antes do único write:

1. **LOWER — `intern_rodata` (`lir/lower.tks:5223`)** guarda cada string interna num `LRodata{bytes}` e
   faz `[..ctx.rodata, entry]` (copy-grow O(n²): cada append recopia a lista TODA **incluindo os bytes**,
   e cada cópia intermediária VAZA sob reclaim-0%). É o hog #1 — o prelúdio inteiro é lowerizado até pra
   um `main` mínimo (milhares de strings) → **os ~11,5 GB são a recópia O(n²) dos bytes**, não o tamanho
   final do rodata.
2. **ENCODE — `text: []byte`** montado por `append_bytes` (`[..buf,b]`) inst-a-inst, func-a-func, módulo
   (`encode_x86_64.tks:35,835,868,+`; `encode_arm64.tks:57,+`). Latente no reprodutor (`.o` minúsculo),
   HOT no self-emit (o `.text` do compilador é MB).
3. **EMIT/MONTAGEM — `emit_elf_object` (`objfile_elf.tks:698`)** constrói o `.o` inteiro num único
   `var b: []byte = []` por `append_bytes`/`emit_*` copy-grow, e `finish_native_object`
   (`project.tks:2199`) faz `write_file_bytes(objp, obj)` de UM buffer materializado inteiro.

**Achado que habilita o fix (barato):** `emit_elf_object` **já roda `compute_elf_layout` PRIMEIRO**
(`objfile_elf.tks:711`) — todos os offsets/tamanhos das seções são conhecidos ANTES de escrever o corpo.
O layout up-front já existe → a montagem já é "calcula tudo, depois escreve em ordem", que é exatamente a
forma de uma **passada de montagem streaming**. E o `elf_partition_data_rel_ro` (`:686`) opera sobre
**metadado** (`symbols` + offsets de reloc); só `elf_build_partition_blob` (`:628`) toca bytes de rodata —
via `elf_append_byte_range(start,end)`, que sob streaming vira **seek-read** do buffer-de-seção.

---

## 2. O modelo: streaming-com-passada-de-montagem

Duas mecânicas, aplicadas por natureza do dado (§4 classifica cada acumulador):

- **M1 — SAÍDA-DIRETA-EM-DISCO** para o **conteúdo de seção do `.o`** (bytes de `.text`/`.rodata`/DWARF) e
  para o **`.o` final**. Grava conforme produz; em RAM fica só **contador de tamanho** por seção.
- **M2 — NÃO-COPY-GROW EM RAM** para o **IR e metadado que fases posteriores CONSOMEM** (não dá pra
  jogar em disco e descartar: LIR `LFunc/LInst/LBlock`, `LEnv` binds, tabela de símbolos, relocs, strtab).
  Mecanismo D207 (o container que D208 preserva como residual): **array-fixo `[n]T`** onde `n` é contável
  numa 1ª passada barata (a maioria, no tempo de montagem os counts JÁ são conhecidos); **chunked
  `Segmented<T>`** (nunca recopia elemento) só no residual genuinamente streaming (`n` irredutível até o
  fim). **PROIBIDO geométrico** (D207).

### 2.1 O buffer-de-seção-em-disco (scaffold M1)

Um wrapper fino sobre `FileStream` (reuso puro do D 2026-08-19 — ZERO C, D148) que representa UMA seção
do `.o` como um arquivo temporário append-only com seek-read para a passada de montagem. Contrato:

```teko
/**
 * A single object-file section streamed to a temp file on disk instead of accumulated in RAM: emit
 * appends section content as produced; the assembly pass seek-reads byte ranges back. Only the byte
 * count is kept live; the payload never resides in memory. Reuses the D-2026-08-19 I/O-stream
 * machinery (`FileStream`, chunks <=1024 B, over syscalls, zero `teko_rt`).
 */
exp type SectionBuf = class {
    intern stream: teko::io::FileStream
    intern path: str
    intern size: u64

    /**
     * Opens a fresh empty section buffer backed by a deterministic-free temp path derived from
     * `stem` and `tag` (e.g. `<stem>.<tag>.sect`); the temp path never leaks into the emitted `.o`.
     *
     * @param stem the object stem (for a unique temp path)
     * @param tag  the section tag (`text`/`rodata`/`drr`/`dwinfo`/…)
     * @return an open, empty section buffer, or an I/O error
     */
    pub static fn open(stem: str, tag: str): SectionBuf | error

    /**
     * Appends `data` at the tail (streamed in chunks); advances the running size. No payload copy.
     *
     * @param data the bytes to append, in emission order
     * @return the new section size, or a write error
     */
    pub fn append(data: []byte): u64 | error

    /**
     * Appends the first `n` bytes of a reusable fixed buffer (partly filled), for per-instruction
     * emit that never allocates a growing array.
     *
     * @param data the reusable buffer
     * @param n    the count of leading bytes to append
     * @return the new section size, or a write error
     */
    pub fn append_prefix(data: []byte, n: u64): u64 | error

    /**
     * The section byte length produced so far (the only live-RAM datum).
     *
     * @return the running size in bytes
     */
    pub fn len(): u64 { self.size }

    /**
     * Copies the byte range `[start, end)` of this section into `out` (the assembly-pass sink),
     * chunked <=1024 B via seek-read; used by rodata partitioning to reorder spans without holding
     * the section in RAM. Zero net allocation beyond one reusable <=1024 B scratch.
     *
     * @param out   the destination stream (the final `.o`)
     * @param start the inclusive start offset within this section
     * @param end   the exclusive end offset
     * @return the bytes copied, or an I/O error
     */
    pub fn copy_range_into(out: teko::io::FileStream, start: u64, end: u64): u64 | error

    /**
     * Streams the whole section into `out` in order (the common case: no reordering), chunked.
     *
     * @param out the destination stream (the final `.o`)
     * @return the bytes copied, or an I/O error
     */
    pub fn copy_into(out: teko::io::FileStream): u64 | error

    /**
     * Closes and removes the temp file; called once the `.o` is fully assembled.
     *
     * @return null on success, or an I/O error
     */
    pub fn dispose(): error | null
}
```

Todas as primitivas já existem: `open_write`/`open_append` + `stream_write`/`stream_write_prefix` +
`stream_seek(Whence)` + `stream_size` + `stream_read` + `stream_close` (`io/file_stream.tks:272-520`),
mais `gensym` pra path único (`file_stream.tks:124`). `copy_range_into` = `stream_seek(start)` no arquivo
da seção + laço de `stream_read(scratch≤1024)`→`stream_write(out)` até `end`. O `scratch` é UM buffer fixo
reusável (natureza BUFFER-DE-SAÍDA, 4-naturezas).

### 2.2 As três fases

1. **PRODUZIR (1ª passada, streaming):** `lower → isel → regalloc → encode`. Conforme cada byte de
   `.rodata`/`.text` nasce, `SectionBuf.append` grava no arquivo-de-seção. Em RAM sobra: **contadores**
   de tamanho por seção; **tabela de símbolos** (nome→offset+len+sect — metadado, O(#símbolos)); **relocs**
   (O(#referências)); **índice do rodata** (símbolo→offset — pra o partition/symtab).
2. **LAYOUT (metadado puro):** `compute_elf_layout` a partir dos contadores de seção (já conhecidos) —
   **inalterado**, é aritmética de offsets. Idem `elf_partition_data_rel_ro` (opera sobre símbolos+relocs).
3. **MONTAR (2ª passada, streaming):** abre o `.o` como `FileStream` (`open_write`); escreve o header ELF
   (do layout, buffer fixo pequeno); `text_secbuf.copy_into(o)`; pad; para rodata, se houver partition,
   `rodata_secbuf.copy_range_into(o, e.start, e.end)` por entrada NA ORDEM da partição (senão `copy_into`);
   pad; escreve symtab/strtab/shstrtab/relas/shdrs (metadado de RAM, pequeno). `dispose()` nos secbufs.

O `.o` sai **byte-idêntico** ao que `emit_elf_object` produz hoje — porque o algoritmo de layout, a ordem
de seções e o conteúdo são os mesmos; muda só ONDE os bytes moram (disco vs RAM) e QUANDO são escritos.
Isso é o que garante o `gen2.o==gen3.o` de graça (§6).

---

## 3. Por que M1 não é one-pass puro (a passada de montagem)

As referências-cruzadas do header ELF/Mach-O/COFF (offsets de seção em `shdr`, `sh_offset`/`sh_size`,
índices de símbolo nas relas) precisam dos **tamanhos finais de todas as seções** antes de escrever o
header. Como os tamanhos só fecham ao fim da 1ª passada, é **streaming-COM-passada-de-montagem**: 1ª
passada grava conteúdo (streaming) + coleta contadores/metadado; 2ª passada escreve header+tabelas lendo
os buffers-de-seção-em-disco. Não se recorre a `stream_seek` de patch-para-trás no `.o` final (o layout
up-front torna a escrita em-ordem suficiente); o `seek` só é usado para **ler** spans dos secbufs no
reordenamento do rodata-partition.

---

## 4. Censo dos acumuladores do emit native — classificado M1/M2 + plano por-um

Reaproveita o censo de `plano-memoria-native-0.3.1.md` (SUPERSEDIDO no enquadramento; o censo de sítios
segue válido). Coluna "Mec." = M1 (disco) / M2-fix (array-fixo) / M2-seg (chunked).

| # | Acumulador | arquivo:linha | Fase | Mec. | Plano por-um |
|---|---|---|---|---|---|
| 1 | `intern_rodata` `[..ctx.rodata, entry]` (+`str_to_bytes` bytes) | `lir/lower.tks:5223-5227` | LOWER | **M1** | bytes → `rodata` SectionBuf conforme intern; `LRodata` perde `bytes`, ganha `offset`/`len`. HOG #1 (11,5 GB). |
| 2 | índice de rodata (a lista de metadado que sobra do #1) | idem #1 | LOWER | **M2-seg** | `n` de strings irredutível até o fim → `Segmented<LRodataMeta>` (nunca recopia); ou índice-em-disco. Residual pequeno que D208 preserva. |
| 3 | merge rodata `add_rodata`/`with_rodata` | `lir/lir.tks:247,254`; `lower.tks:6565,6984` | LOWER | **M1** | unifica com #1 (mesmo SectionBuf + índice). |
| 4 | `add_func` (+ lambdas/thunks liftados) | `lir/lir.tks:235`; `minst.tks:465`; `minst_x86.tks:385` | LOWER/ISEL | **M2-seg** | consumido por isel → fica em RAM; funcs liftadas dinâmicas, `n` irredutível → `Segmented<LFunc>`. |
| 5 | `add_inst`/`add_block` | `lir/lir.tks:207,265`; `minst.tks:414,432,436`; `minst_x86.tks:334,352,356` | LOWER/ISEL | **M2-seg** | idem #4 (`Segmented<LInst>`/`<LBlock>`). |
| 6 | `LEnv` binds (7 arrays paralelos) | `lir/lower.tks:20-24,33-63` | LOWER | **M2-seg** | 1 `Segmented<LBinding>` no lugar dos 7 `[..]`. |
| 7 | encode byte-emit `text`/`push_byte`/`emit_u32_le` | `encode_x86_64.tks:35,835,868,+`; `encode_arm64.tks:57,+` | ENCODE | **M1** | por-função: buffer FIXO reusável (patch de branch in-place) → `append_prefix` no `text` SectionBuf + `text_off` corrente + relocs rebased. |
| 8 | `emit_elf_object` `var b: []byte = []` (o `.o` inteiro) | `objfile_elf.tks:698-734` | MONTAGEM | **M1** | vira passada de montagem: escreve header + `copy_into` das seções + metadado direto no `.o` FileStream. Remove `write_file_bytes(obj)`. |
| 9 | corpo de seção DWARF (`emit_elf_dwarf_body`) | `objfile_elf.tks:736-747` | MONTAGEM | **M1** | abbrev/info/line → SectionBufs (streamados no encode/lower do DWARF); montagem faz `copy_into`. |
| 10 | headers/tabelas ELF (`syms`/`relas`/`strtab`/`shdrs`) | `objfile_elf.tks:118,232,462,473,+` | MONTAGEM | **M2-fix** | counts conhecidos na montagem → `[n]T`+índice (hoje `[..out,x]`). |
| 11 | partition `elf_collect_const_entries` `[..starts,e]` | `objfile_elf.tks:559-565` | MONTAGEM | **M2-fix** | conta símbolos-rodata numa passada, `[n]ElfConstEntry`+índice. |
| 12 | regalloc RPO/eventos/intervalos/pins/subst | `backend/regalloc.tks:61-1061`; `regalloc_x86.tks:86-397` | RA | **M2-fix** | derivados de stream de tamanho conhecido → array-fixo/FILTRO+watermark. |
| 13 | objfile macho/coff/ar (mesmo `var b:[]byte`) | `objfile_macho.tks:492`; `objfile_coff.tks:350`; `objfile_ar.tks:130,161` | MONTAGEM | **M1**+**M2-fix** | espelha #8/#10 (monólito cross-emit — as 4 pernas). |
| 14 | `copy_*_to_current_region`/`commit_rodata_delta` | `build/project.tks:1706-1830,1974-2018` | BUILD | **M2-fix** | copia array EXISTENTE (`.len` conhecido) → `[src.len]T`+índice. |
| 15 | `str_to_bytes` `[..out, s[i]]` (já ARRAY-FIXO no tip) | `lir/lower.tks:5209-5217` | LOWER | — | JÁ convertido (`[s.len]byte`+índice, `lower.tks:5209`); verify-only. |

**Cauda contável (todos M2-fix, array-fixo — do censo anterior §1):** `lower.tks:100,602,657,675,681,1459,
1727,2216,2869-2874,3173-3553,5290,5301,5490-5494,6519-7092`; `lower_const.tks:31-637`; `frame_escape.tks:20,
145`; `isel_x86_64.tks:582,664`; `isel_arm64.tks:24-26,775`; `abi_*`/`objfile_ar*` insert-sorted;
`project.tks:170,529,550,703-951,1524-1639`. `lir_print.tks:11-35` = debug `--emit-lir`, frio, deixar.

**Dominância:** o gate mede o build seco de `sys_exit_group` native (dominado pelo LOWER do prelúdio) →
**#1(+#3)** movem a agulha do gate; **#7/#8/#9** são latentes no reprodutor mas HOT no self-emit → curar
antes do `gen2.o==gen3.o`.

---

## 5. Crumb-sequence ordenada (bisectável; cada crumb baixa o ratchet; começa por `intern_rodata`)

Cada crumb: **Where** (sítios), **Assinatura** (shape Teko a adicionar/tocar), **Fazer**, **Fixture**,
**Ritual** (onde o gate completo roda). Gate por crumb, SEPARADO POR ROTA (D203):
- **Rota C:** reseed + fixpoint gen0→gen1 (`teko.c` re-emitido byte-idêntico) + ASan+UBSan (D166) + 3
  harnesses `scripts/*_test.sh` (D185).
- **Rota native:** `*San` NÃO se aplica (D203); prova = `.o` do fixture reproduz byte-idêntico 2× + roda
  (exit code) sob **MEM_PARANOID**; pico do build seco native `TEKO_BACKEND=native` medido, gate `≤C+10%`.
- **Varredura de árvore inteira** (D191): `src/`+`cases/`+`examples/`+`tklib/`+`tooling/`+raiz quando um
  símbolo/assinatura muda.

### C0 — scaffold `SectionBuf` (compila hoje, não-fiado)
- **Where:** novo `src/backend/secbuf.tks` (§2.1). Só reusa `teko::io` (D148, zero C).
- **Assinatura:** `exp type SectionBuf` (o contrato §2.1).
- **Fazer:** implementar sobre `open_write`/`stream_write`/`stream_write_prefix`/`stream_seek`/
  `stream_read`/`stream_close` + `gensym`. `copy_range_into` = seek+laço de read≤1024→write.
- **Fixture:** nenhuma nova (self-build não exercita ainda — não escrever teste tautológico, lei de
  testes). Módulo-folha novo, sem consumidor → NÃO exige reseed.
- **Ritual:** build seco compila (rota C verde). Ratchet: flat (sem consumidor).

### C1 — `intern_rodata` grava bytes no `rodata` SectionBuf (HOG #1, D208)
- **Where:** `lir/lower.tks:5209-5227` (`intern_rodata`, `str_to_bytes`, `InternedRodata`, `LRodata`);
  `encode_*.tks` `encode_rodata` (`encode_arm64.tks:1277`, `encode_x86_64.tks` equiv.); `objfile_elf.tks`
  partition (`:628,686`) que lê bytes de rodata.
- **Assinatura (shape novo do metadado):**
```teko
/**
 * A rodata entry as metadata only: its symbol and its byte span within the streamed rodata section.
 * The bytes live on disk (the `rodata` SectionBuf), never in this record — killing the O(n^2) byte
 * recopy that the old `LRodata{bytes}` list incurred on every append.
 *
 * @see SectionBuf
 */
exp type LRodataMeta = struct {
    symbol: str
    offset: u64
    len: u64
    relocs: []teko::lir::LReloc
}
```
- **Fazer:** `intern_rodata` faz `rodata_secbuf.append(str_to_bytes(text))` e guarda `LRodataMeta{symbol,
  offset=size_antes, len=text.len, relocs}` — SEM `[..ctx.rodata, entry]` de bytes. `encode_rodata` e o
  partition passam a ler spans via `SectionBuf.copy_range_into`/offset+len em vez de `LRodata.bytes`. O
  índice de metadado ainda pode crescer no C1 (vira M2-seg no C2); o ganho de C1 é matar a recópia de
  BYTES (a montanha).
- **Fixture:** `sys_exit_group` native (já existe, D206/Crumb 9) — o `.o` emitido reproduz byte-idêntico
  2× (nível-fixture, D203). Adicionar `sys_write_hello` se ainda não estiver (native, exit + stdout).
- **Ritual:** GATE COMPLETO (rota C: reseed+fixpoint+ASan+3 harnesses; native: `.o` byte-idêntico 2× +
  build seco native mede pico, gate `≤C+10%`). **Este é o crumb que derruba os ~11,5 GB.** Ratchet: queda
  massiva medida no build native de `sys_exit_group`.

### C2 — índice de rodata → `Segmented` (dissolve o `[..]` residual, "sem lista crescente" D208)
- **Where:** o `ctx.rodata` de `lower.tks` (agora `[]LRodataMeta`); `add_rodata`/`with_rodata`
  (`lir.tks:247,254`; `lower.tks:6565,6984`); `ctx_with_rodata`.
- **Assinatura:** `intern rodata: teko::backend::Segmented<LRodataMeta>` no `LowerCtx`/`LModule` (o
  chunked D207, blessed como residual por D208; `Segmented` já esboçado em `plano-memoria-native-0.3.1.md`
  §2 — reusar, ZERO geométrico).
- **Fazer:** trocar `[..ctx.rodata, entry]` por `ctx.rodata.append(entry)` in-place; consumidores leem por
  `get(i)`/`flatten()` só na fronteira (encode). Se `Segmented` ainda não existir na árvore, este crumb o
  introduz (módulo `backend/segmented.tks`, D148).
- **Fixture:** reusa C1 (`sys_exit_group`/`sys_write_hello`).
- **Ritual:** GATE COMPLETO. Ratchet: queda estrita (mata o O(n²) de metadado remanescente).

### C3 — LIR `add_func`/`add_inst`/`add_block` → `Segmented` (M2-seg)
- **Where:** `lir.tks:207,235,265`; `minst.tks:414,432,436,465`; `minst_x86.tks:334,352,356,385`.
- **Assinatura:** campos `funcs`/`insts`/`blocks` viram `Segmented<LFunc>`/`<LInst>`/`<LBlock>`;
  `add_*` = `.append(x)` in-place.
- **Fazer:** trocar copy-grow por append chunked; isel/regalloc/encode iteram por `len()`+`get(i)`.
- **Fixture:** C1.
- **Ritual:** GATE COMPLETO. Ratchet: queda (O(n²) do LOWER do prelúdio).

### C4 — `LEnv` 7 arrays paralelos → 1 `Segmented<LBinding>` (M2-seg)
- **Where:** `lower.tks:20-24,33-63`.
- **Assinatura:** `type LBinding = struct { … }` (agrega os 7 campos hoje paralelos); `LEnv` guarda
  `Segmented<LBinding>` (+ shadowing por índice, como hoje).
- **Fazer:** unifica os 7 `[..]` num append de struct; lookups por varredura/índice como hoje.
- **Fixture:** C1.
- **Ritual:** GATE COMPLETO. Ratchet: queda.

### C5 — ENCODE `.text` → `text` SectionBuf (M1, #7)
- **Where:** `encode_x86_64.tks` (`FuncEmitX86`/`encode_func_x86:930+`, `append_enc_inst_x86`,
  `emit_block_x86`, `ModuleTextX86`); `encode_arm64.tks` equiv.
- **Assinatura:** o encode de função escreve num buffer FIXO reusável (natureza BUFFER-DE-SAÍDA; patch de
  branch in-place DENTRO da função, `patch_branches_x86` inalterado); depois `text_secbuf.append_prefix
  (fnbuf, fnlen)`; `ModuleTextX86` perde `text: []byte`, ganha `text_len: u64` + `text: SectionBuf`.
- **Fazer:** por-função (buffer limitado ao tamanho da maior função — patch local), streama pro secbuf;
  `text_off` corrente + relocs rebased (`rebase_relocs_x86`). Bytes do módulo nunca acumulam.
- **Fixture:** C1 (o `.o` reproduz byte-idêntico — prova que o stream = o `[]byte` anterior).
- **Ritual:** GATE COMPLETO. Ratchet: latente no fixture, mas prova byte-idêntico; medir no self-emit
  quando C6 fechar.

### C6 — `emit_elf_object` vira passada de montagem streaming (M1, #8/#9)
- **Where:** `objfile_elf.tks:698-747` (`emit_elf_object`, `emit_elf_dwarf_body`); `project.tks:2199`
  (`finish_native_object`), `:2290` (`finish_static_archive`).
- **Assinatura:** `pub fn write_elf_object(o: teko::io::FileStream, obj: ElfObject): error | null`
  (escreve direto no `.o` em vez de retornar `[]byte`); `ElfObject.text`/`.rodata` viram `SectionBuf`.
  `finish_native_object` abre `open_write(objp)` e passa o stream (remove `write_file_bytes(obj)`).
- **Fazer:** header (buffer fixo pequeno, do layout já computado) → `stream_write`; `text.copy_into(o)`;
  pad; rodata na ordem do partition via `copy_range_into`; pad; symtab/strtab/shstrtab/relas/shdrs
  (metadado de RAM) → `stream_write`; DWARF idem. `dispose()` nos secbufs.
- **Fixture:** C1 + o `.o` do compilador-inteiro quando o subset native fechar.
- **Ritual:** GATE COMPLETO. Ratchet: queda (remove o `.o` inteiro-em-RAM). **Determinismo:** §6.

### C7 — tabelas/headers de montagem → array-fixo (M2-fix, #10/#11/#12)
- **Where:** `objfile_elf.tks:76-118,232-262,462-486,558-608`; `regalloc*.tks`.
- **Assinatura:** `elf_build_symbols`/`elf_build_relas`/`build_elf_strtab`/`elf_collect_const_entries`
  passam a contar (1ª passada) + `[n]T`+índice (hoje `[..out,x]`).
- **Fazer:** counts conhecidos na montagem → duas-passadas/FILTRO+watermark (D207 iii).
- **Fixture:** C1.
- **Ritual:** GATE COMPLETO. Ratchet: queda.

### C8 — macho/coff/ar espelham C5/C6/C7 (monólito cross-emit — as 4 pernas)
- **Where:** `objfile_macho.tks:492`; `objfile_coff.tks:350`; `objfile_ar.tks:66,130,161`;
  `encode_arm64.tks` (arm64 text stream).
- **Assinatura:** `write_macho_object`/`write_coff_object`/`write_ar_archive` sobre FileStream, análogos a
  `write_elf_object`.
- **Fazer:** mesma passada de montagem streaming; o `ar` archive streama membro-a-membro.
- **Fixture:** fixtures native arm64/macOS/coff quando o subset fechar; enquanto isso, byte-idêntico do
  `.o` per-leg emitido pra os fixtures existentes.
- **Ritual:** GATE COMPLETO por perna. Ratchet: queda (as pernas cross).

### C9 — cauda contável restante (M2-fix)
- **Where:** a lista de cauda do §4 (lower/lower_const/isel/abi/project).
- **Fazer:** array-fixo por natureza (MAP/PARSE/FILTRO). Baratos, agrupáveis em 1-2 crumbs.
- **Ritual:** GATE COMPLETO. Ratchet: queda residual. Depois: `gen2.o==gen3.o` do compilador-inteiro
  quando o subset native self-hospedar (o gate FINAL, §6).

---

## 6. Determinismo do `.o` sob streaming (`gen2.o==gen3.o`)

O streaming é um refactor **byte-preservante** — o `.o` sai idêntico ao do `emit_elf_object` atual:

1. **Layout idêntico:** `compute_elf_layout` roda sobre os mesmos tamanhos de seção (agora contadores em
   vez de `.len` de `[]byte`), mesma aritmética → mesmos offsets.
2. **Ordem de seções fixa:** a 2ª passada escreve header→text→rodata→(drr)→symtab→strtab→shstrtab→relas→
   (dwarf)→shdrs, a MESMA ordem do código atual (`objfile_elf.tks:713-733`).
3. **Ordem de símbolos/relocs estável:** vem de chave ordenada / ordem de definição (inalterado); o
   array-fixo do C7 preserva a ordem de inserção da 1ª passada.
4. **Sem timestamp / sem path absoluto:** já auditado (D203); os arquivos-de-seção são TEMP (paths
   `gensym`) e NUNCA entram no `.o` — só o conteúdo é copiado byte-a-byte. `objfile_ar` `mtime`/`mode`
   já zerados (D203).
5. **Partition determinista:** `elf_assign_partition_offsets` opera sobre metadado ordenado; o
   `copy_range_into` lê spans na mesma ordem que `elf_build_partition_blob` monta hoje.

**Encenação do fixpoint (D203/D205):** por crumb, nível-fixture (`sys_exit_group`/`sys_write_hello`: o
`.o` reproduz byte-idêntico 2× + roda sob MEM_PARANOID) + fixpoint-C transitório (reseed, `teko.c`
byte-idêntico — o compilador-como-programa-C íntegro). O `gen2.o==gen3.o` do compilador-inteiro é o gate
FINAL, quando o subset native self-hospeda (ladder gen0/gen1 rota-C, gen2/gen3 native — D203).

---

## 7. Critério de gate (resumo)

- **Por crumb (durante a campanha):** rota C verde (reseed + fixpoint gen0→gen1 + ASan+UBSan + 3
  harnesses); native `.o` do fixture byte-idêntico 2× + roda (MEM_PARANOID); **pico do build seco native
  `≤ pico_C × 1.10`** (D206) e trajetória rumo a `< 2 GB` (D203); ratchet estrito (cada crumb BAIXA o
  pico native, D68 análogo).
- **Final:** `gen2.o == gen3.o` byte-idêntico (compilador-inteiro emitido native), quando o subset
  N1/N2 fechar e as pernas native do CI ficarem verdes.

---

## 8. Riscos + tensões de lei (resolução recomendada)

1. **R1 — I/O de temp-file no meio do LOWER (C1):** o `rodata` SectionBuf abre um arquivo temp durante o
   lowering (fase que hoje é pura-memória). Risco: overhead de syscall por-append. **Resolução:** o
   `append` já corta em ≤1024 B e o volume total de rodata é modesto (KB-MB); o custo de I/O é trocado
   pela ELIMINAÇÃO de GB de recópia+vazamento → net-win garantido. Um buffer de coalescência FIXO (≤1024)
   por SectionBuf amortiza os appends pequenos (natureza BUFFER-DE-SAÍDA), sem acumular.
2. **R2 — `Segmented` reintroduz "container" que D208 declarou moot:** tensão aparente. **Resolução
   (law-first, D208 explícito):** D208 preserva o container "se restar algum acumulador pequeno genuíno";
   o IR consumido downstream (#4/#5/#6) e o índice de rodata (#2) SÃO esse residual — não dá pra jogar em
   disco (fases posteriores leem). A ESPINHA da campanha é M1 (disco); M2-seg é o residual blessed, ZERO
   geométrico (D207). Sem HALT.
3. **R3 — `Map`/`Dictionary` são copy-grow** (`collections/map.tks:31-33` `self.keys=[..self.keys,k]`;
   idem `dictionary`/`hashset`). Se o dedup-map do rodata fosse um `Map`, reintroduziria O(n²).
   **Resolução:** o `intern_rodata` de HOJE **NÃO dedupa** (sempre cria `.LstrN` novo, `lower.tks:5224`)
   → não precisa de `Map`; o índice é sequencial (`Segmented`/offset corrente). **Achado adjacente
   REPORTADO (não vira issue nova, lei):** `Map`/`Dictionary`/`Hashset` copy-grow são dívida NO-PUSHES da
   rota C que "passou" a campanha (o sweep não os pegou) — fora do escopo D206/D208, reportar pro dono.
4. **R4 — partition data_rel_ro reordena rodata (C1/C6):** o `.o` reordena spans de rodata que contêm
   relocs pra uma seção `.data.rel.ro`. **Resolução:** a decisão de partição é metadado puro
   (`elf_collect_const_entries`/`elf_assign_partition_offsets` sobre símbolos+relocs); a 2ª passada faz
   `copy_range_into` por span NA ORDEM nova — o seek-read cobre exatamente o `elf_append_byte_range`
   atual. Byte-preservante.
5. **R5 — patch de branch precisa da função inteira em buffer (C5):** `patch_branches_x86` reescreve
   deslocamentos após conhecer offsets de bloco. **Resolução:** o buffer é POR-FUNÇÃO (limitado à maior
   função — não ao módulo), reusável entre funções; patch in-place ANTES de `append_prefix` no secbuf.
   Não acumula módulo. (Idem arm64.)
6. **R6 — determinismo do path temp:** se o path do temp-file vazasse pro `.o` (ex.: DWARF file-table),
   quebraria `gen2.o==gen3.o`. **Resolução:** o SectionBuf temp é puramente conteúdo-copiado; o DWARF
   file-table usa o path FONTE (não o temp). Auditar no C6/C9 que nenhum path temp entra no DWARF.

**Nenhuma tensão genuinamente aberta → sem HALT.** (Protocolo de fork checado: DECISION_LOG D208/D207/
D206/D203, CLAUDE.md I/O-streaming — tudo deliberado; mais-recente D208 vence e é o que este plano segue.)
