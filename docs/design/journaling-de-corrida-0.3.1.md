---
section: design
created: 2026-07-30
source: ruling do dono ("sem meio termos, sem faz isso e depois completa"), ruling do dono ("journaling"), ADENDO do dono 2026-07-30 ("colocar um sumario ... que aponta todos os erros, skips e sucessos, bem como a cobertura medida" — §13), docs/design/concorrencia-adiantada-s8.md §3.2 (fork_join — atribuicao estatica de raia), src/build/regression.tks (run_pool / ProcSpec / o terceiro canal), src/runtime/teko_rt.c:1214 (o despejo periodico que ja sobrevive a SIGKILL), cargo/0.3.1.0-testes-paralelos-canais (teko::test::scoped + a guarda textual)
status: DESENHO — nenhuma linha de produto escrita nesta carga; C1–C9 executaveis hoje, nenhum crumb depende de capacidade inexistente
branch: cargo/0.3.1.0-arq-concorrencia (de remodel/0.3.1.0-linux-native-2 @ 44e39eb)
---

# Journaling de corrida — o controlo de concorrencia que faltou ao paralelismo

> *"Entao, mas atender apenas o caso 1 nao resolve a sobreposicao de arquivo por concorrencia."* — dono
>
> *"Ja nomearia esse 'encerramento elegante' com um nome que o liga muito bem: `journaling`."* — dono

O paralelismo de testes esta medido (4,22x; 20,674 s -> 4,904 s) e o `.tkcov` foi isolado. Este
documento fecha o resto, e fecha-o **com um mecanismo so**. Nao ha fase 2: os dez crumbs abaixo sao
todos executaveis sobre a arvore de hoje, e nenhum espera capacidade que nao exista.

**§13 e o adendo do dono (2026-07-30):** o sumario final sobre as duas fases — erros, skips e
sucessos nomeados, mais a cobertura medida. Ele nao e um enfeite pregado no fim: e a leitura humana
do mesmo `fold`, e por isso sobrevive a morte da corrida que o produziu.

---

## 1. O modelo, numa frase, e porque ha um so

**Toda a escrita de uma corrida vai para um *journal*: um registo append-only, carimbado com a
identidade da corrida, segmentado por escritor. A sumarizacao nao junta nada no fim — ela *rele*.**

Tres propriedades, e cada uma mata um defeito:

| propriedade | mata |
|---|---|
| **segmento por escritor** (ninguem partilha destino) | a sobreposicao — sem lock, porque nao ha escritor partilhado |
| **carimbo de corrida** (o segmento nomeia a corrida a que pertence) | o lixo de corrida anterior com ar de valido |
| **append-only + sumarizacao por releitura** | a perda por morte abrupta — nao ha nada por fundir que se perca, ja estava escrito |

### 1.1 Porque isto colapsa os dois substratos do dono

O dono descreveu dois mecanismos: um **acumulador com thread de sincronizacao** para threads, e um
**tunel com consumidor** ou **ficheiro por processo** para processos. Eles nao sao dois mecanismos:
sao o mesmo, com dois armazens de segmento.

A lei que os une ja esta ratificada nesta casa. `docs/design/concorrencia-adiantada-s8.md` §3.2 fixa
para `fork_join`: *"a atribuicao de indices a raias e ESTATICA (item `i` pertence a raia `i % lanes`),
portanto reprodutivel; cada raia escreve APENAS nas casas dos seus proprios indices, portanto sem
escrita compartilhada; ... Quem le o resultado e o chamador, depois da barreira."*

Palavra por palavra, isso e um journal. "As casas dos seus proprios indices" e o segmento. "Depois
da barreira" e a releitura. O sharding de processos de hoje ja e o mesmo desenho com o segmento em
disco em vez de em memoria. **Portanto nao ha duas mecanicas a propor: ha uma, e ela ja foi
ratificada para threads antes de threads existirem.**

O unico ponto do desenho do dono que **recuso**, e digo-o com a razao em vez de o contornar: a
*"thread de sincronizacao"*. Uma thread que recolhe as entradas dos outros e, por definicao, um
destino partilhado — precisa de lock, serializa exactamente o que foi paralelizado, e (o pior) deixa
o estado a viver **dentro dela**, logo um `SIGKILL` leva-o. Com um segmento por escritor, a
recolha e uma *leitura* apos a barreira, e uma leitura nao coordena com ninguem. O acumulador do dono
existe; o que nao existe e um escritor central para ele.

### 1.2 O que muda de forma quando as threads chegarem: nada

`Journal` carrega um `seg: u64` **opaco** — hoje um descritor de ficheiro, amanha um indice de laje
por raia. O formato do registo, a funcao `append`, a funcao `fold` e o consumidor sao os mesmos. A
troca de substrato e **uma funcao**: `journal_open_rt`. Isto nao e uma fase adiada — threads nao
existem em Teko hoje (todos os acertos de "thread" na arvore sao a palavra *threaded* em
doc-comments do parser), logo implementar um sumidouro de raia e impossivel, nao adiado. O que este
desenho garante e que a chegada delas **nao muda uma linha** do que os crumbs C1–C9 escrevem.

### 1.3 O precedente que ja esta em casa, e onde ele nao serve

`src/runtime/teko_rt.c:1214` ja diz, por escrito, a ideia inteira:

> *"Dumped periodically (every 512 MB — survives a SIGKILL under memory pressure) and at process exit"*

**Serve como precedente da CADENCIA** — escrever durante o voo, e nao no fim, e a unica coisa que
sobrevive ao matador mal-educado — e o desenho herda-a. **Nao serve como mecanismo**, por tres
defeitos medidos que o journal corrige:

1. `tk_obs_dump` faz `fopen(tk_obs_path, "w")` (`teko_rt.c:1294`) — **reescrita total**, nao append.
   Uma morte no meio da reescrita deixa um ficheiro truncado que **parece** valido.
2. `tk_obs_path` e `"/tmp/teko_arena_obs.txt"` (`teko_rt.c:1256`) — **fixo e sem carimbo**. Dois
   processos concorrentes do mesmo host atropelam-se, e o de ontem passa por o de hoje. E o defeito
   deste documento, dentro do proprio precedente.
3. O despejo final vive em `tk_regions_free_all` (`teko_rt.c:1499`), que o **manipulador de crash
   nao chama** — `tk_rt_crash_handler` faz `_Exit(128+sig)` directo (`teko_rt.c:119`). Um SIGSEGV ja
   perde o mapa final hoje.

Reuso a cadencia, reuso o lugar (o mesmo choke point), e substituo `fopen("w")` por append carimbado.

---

## 2. A superficie, em Teko real

### 2.1 O que muda no `.tkt` de quem escreve testes hoje: **nada**

`teko::test::scoped("...")` — que a lane `cargo/0.3.1.0-testes-paralelos-canais` ja fez aterrar —
mantem a grafia e mantem o sentido. O que muda e **onde** o caminho aterra: passa de um sufixo de
identidade para um caminho enraizado na corrida **e** na identidade. Zero chamadas mudam de forma;
a garantia sobe de "dois testes nao colidem" para "dois testes nao colidem, e nenhuma corrida ve o
lixo de outra".

### 2.2 `teko::journal` — o modulo novo

```teko
/**
 * Journal — o segmento append-only que UM escritor possui e mais ninguem.
 *
 * O `seg` e opaco de proposito: hoje e um descritor de ficheiro aberto em `O_APPEND`, e quando as
 * raias de `teko::isolate` chegarem sera o indice de uma laje por raia. O formato do registo, o
 * `append` e o `fold` nao mudam com a troca — e essa invariancia e o motivo de o campo nao ter tipo
 * concreto na superficie.
 *
 * @since 0.3.1
 */
pub type Journal = struct {
    /** o identificador da corrida a que este segmento pertence (`run_id`). */
    run: str
    /** a identidade do escritor — `s<i>` para uma shard do gate, `p<i>` para um filho do pool. */
    writer: str
    /** o sumidouro aberto: descritor hoje, laje quando houver raias. */
    seg: u64
}

/**
 * Record — um registo lido de volta pelo sumarizador.
 *
 * @since 0.3.1
 */
pub type Record = struct {
    /** a corrida que o escreveu; um registo de outra corrida e DESCARTADO pelo `fold`. */
    run: str
    /** quem o escreveu. */
    writer: str
    /** a especie do registo (`begin`, `ok`, `fail`, `cov`, `end`, `stop`, `crash`). */
    kind: str
    /** o corpo do registo, sem quebras de linha (o emissor escapa-as). */
    payload: str
}

/**
 * run_id — o identificador desta corrida, herdado do orquestrador ou criado agora.
 *
 * NOMEAR A CORRIDA E O QUE MATA O LIXO CALADO. Um segmento orfao de ontem vive noutra raiz e carrega
 * outro carimbo, logo e identificavel em vez de silenciosamente fundido — o modo de falha que ja nos
 * mordeu, e o unico da lista que nao dava sinal nenhum.
 *
 * @return o identificador da corrida (`<ns monotonico>-<pid>`), estavel dentro do processo
 * @since 0.3.1
 */
pub fn run_id() -> str

/**
 * run_root — o directorio que esta corrida possui, e o UNICO que ela pode criar.
 *
 * @return `bin/.tkrun/<run_id()>`
 * @since 0.3.1
 */
pub fn run_root() -> str

/**
 * open — abrir o segmento de `writer` nesta corrida.
 *
 * @param writer  a identidade do escritor; dois escritores vivos nunca a repetem
 * @return        o segmento aberto
 * @throws        quando o segmento nao pode ser criado (permissao, disco cheio)
 * @since 0.3.1
 */
pub fn open(writer: str) -> Journal | error

/**
 * append — acrescentar UM registo ao segmento de `j`.
 *
 * O PONTO DE DURABILIDADE E ESTE, e esta nomeado: uma unica chamada `write(2)` sobre um descritor
 * aberto em `O_APPEND`, sem buffer de espaco de utilizador. O registo esta no kernel quando `append`
 * retorna, logo um `SIGKILL` do processo nao o perde — so uma queda da maquina o perderia, e essa
 * fronteira esta em §5.
 *
 * @param j        o segmento a acrescentar
 * @param kind     a especie do registo
 * @param payload  o corpo, sem quebras de linha
 * @throws         quando a escrita falha (ENOSPC, EIO) — e a falha e PEGAJOSA (§4, modo 5)
 * @since 0.3.1
 */
pub fn append(j: Journal, kind: str, payload: str) -> null | error

/**
 * fold — reler TODOS os segmentos de `run` sob `root` e devolver os registos em ordem de segmento.
 *
 * TRES REGRAS DE HIGIENE, e cada uma corresponde a um modo de falha nomeado em §4:
 *
 * 1. um registo cuja corrida nao e `run` e descartado (lixo de outra corrida);
 * 2. a ultima linha de um segmento que nao termina em `\n` e descartada (escrita rasgada);
 * 3. um segmento sem registo `end` e devolvido com um registo `end` sintetico de especie
 *    `incomplete` — o escritor morreu, e a corrida TEM de o saber em vez de o arredondar.
 *
 * @param root  a raiz da corrida (`run_root()`, ou uma raiz antiga numa releitura)
 * @param run   o carimbo que um registo tem de trazer para contar
 * @return      os registos, ordenados por nome de segmento e depois por ordem de escrita
 * @since 0.3.1
 */
pub fn fold(root: str, run: str) -> []Record

/**
 * scratch — o caminho que `base` tem NESTA corrida e para ESTE escritor.
 *
 * ESTE E O COMPOSITOR UNICO. "Quais caminhos estao isolados" responde-se lendo os chamadores desta
 * funcao, nao greando literais — que foi exactamente como os caminhos fixos passaram despercebidos.
 *
 * @param base  o caminho que se escreveria a mao
 * @return      `<run_root()>/<writer>/<escopo>/<base>`, com os directorios ja criados
 * @since 0.3.1
 */
pub fn scratch(base: str) -> str

/**
 * sweep — remover as raizes de corrida sob `bin/.tkrun/` que nao sejam `keep`.
 *
 * A LIMPEZA E DA CORRIDA SEGUINTE, NUNCA DA PROPRIA. Uma corrida que limpasse ao sair perderia a
 * limpeza no unico caso em que ela importa (morte abrupta), e perderia com ela a prova de que morreu.
 * Varrer na abertura tem a propriedade oposta: um `SIGKILL` deixa a raiz *de proposito*, e a corrida
 * seguinte varre-a depois de a poder relatar.
 *
 * @param keep  a raiz a preservar (a da corrida corrente)
 * @return      quantas raizes foram removidas
 * @since 0.3.1
 */
pub fn sweep(keep: str) -> u64
```

### 2.3 Os fundos de runtime (C mantido — `src/runtime/teko_rt.{c,h}`)

```c
// tk_journal_open — abrir/criar `path` em O_WRONLY|O_APPEND|O_CREAT e devolver o descritor cru.
// SEM stdio: um FILE* traz um buffer de espaco de utilizador, e um buffer de espaco de utilizador
// morre com o processo — que e o unico momento em que este ficheiro tinha valor.
int64_t tk_journal_open(tk_str path);
// tk_journal_append — escrever `rec` inteiro no descritor `seg`, repetindo em escrita curta.
// Devolve 0, ou o errno. O registo e limitado a TK_JOURNAL_REC_MAX bytes.
int32_t tk_journal_append(int64_t seg, tk_str rec);
// tk_journal_note — a versao ASYNC-SIGNAL-SAFE, para uso DENTRO de um manipulador de sinal:
// escreve um buffer pre-formatado no descritor ja aberto, sem alocar e sem formatar.
void tk_journal_note(int sig);
// tk_rt_rename — renomear atomicamente dentro do mesmo directorio (rename(2) / MoveFileExW com
// MOVEFILE_REPLACE_EXISTING). O unico primitivo que falta para publicar um ficheiro INTEIRO ou
// nenhum; usado pelos artefactos que nao sao append (o `.tkcov` de uma shard, o `cobertura.xml`).
int32_t tk_rt_rename(tk_str from, tk_str to);
```

`tk_rt_rename` e a segunda metade da disciplina: **o que e append vai por append; o que e um
artefacto inteiro escreve-se num temporario do proprio escritor e publica-se por `rename`.** Nenhum
leitor ve nunca um ficheiro meio-escrito. Sao dois verbos, nao dois modelos: ambos dizem "o escritor
so publica o que esta completo".

### 2.4 Como um teste emite, e como o orquestrador agrega

Do lado de quem escreve testes — **nada muda**:

```teko
#test
/**
 * a_test_that_writes_scratch — um teste que precisa de um ficheiro so seu.
 *
 * A GRAFIA E A MESMA DE HOJE. `teko::test::scoped` continua a ser a chamada; o que ela devolve passa
 * a estar enraizado na corrida, e por isso duas corridas tambem deixam de se ver.
 */
fn a_test_that_writes_scratch() {
    let p = teko::test::scoped("probe")
    match teko::io::write_file(p, "payload") { error => { }; null => { } }
    teko::assert::eq_str(match teko::io::read_file(p) { str as s => s; error => "" }, "payload")
}
```

Do lado do orquestrador, `run_gate_sharded` deixa de "fundir no fim" e passa a **reler**:

```teko
/**
 * gate_summary — o veredicto do gate, RELIDO do journal em vez de acumulado em memoria.
 *
 * PORQUE E UMA RELEITURA E NAO UMA FUSAO. Uma fusao vive na memoria do orquestrador e morre com ele;
 * este `fold` e uma consulta pura sobre bytes que ja estao em disco, logo o mesmo resultado sai de um
 * `teko test --replay <run>` depois de o orquestrador ter sido morto por OOM. Foi o que aconteceu
 * duas vezes hoje (`rc=137`), e nas duas o agregado teria sido perdido em silencio.
 *
 * @param root  a raiz desta corrida
 * @param run   o carimbo desta corrida
 * @param jobs  quantas shards deviam ter escrito um segmento
 * @return      o veredicto agregado, com as shards incompletas nomeadas
 */
fn gate_summary(root: str, run: str, jobs: u64) -> GateVerdict {
    let recs = teko::journal::fold(root, run)
    let v = gate_fold_records(recs)
    if v.segments < jobs { return gate_verdict_missing(v, jobs) }
    v
}
```

E a fusao de cobertura passa a ser **falivel**, que hoje nao e:

```teko
/**
 * merge_shard_coverage — fundir o despejo de cobertura de cada shard, e PARAR quando um falta.
 *
 * O DEFEITO QUE ISTO FECHA E MEDIDO NA ARVORE DE HOJE. `coverage::cov_merge` devolve um `bool` que a
 * versao actual descarta, e nada remove `<base><i>.tkcov` entre corridas — logo uma shard que morra
 * antes de despejar deixa no lugar o ficheiro da corrida ANTERIOR, e as fasquias sao calculadas sobre
 * ele. Um ficheiro em falta e um facto sobre a corrida, nao um zero.
 *
 * @param base  o prefixo de rascunho das shards
 * @param jobs  quantas shards correram
 * @throws      quando alguma shard nao deixou despejo — a corrida para, com a shard nomeada
 */
fn merge_shard_coverage(base: str, jobs: u64) -> null | error {
    mut i: u64 = 0
    loop {
        if i >= jobs { break }
        let p = teko::str::concat(base, $"{i}", ".tkcov")
        if !coverage::cov_merge(p) { return error { message = $"teko: shard {i} left no coverage dump ({p})" } }
        i = i + 1
    }
    null
}
```

---

## 3. O inventario completo dos caminhos fixos, e a regra unica

Contei-os eu, com o comando abaixo, e o total esta dito. **A tabela do briefing conta *literais*; esta
conta *escritas*, e as duas nao coincidem.**

```
# literais de caminho no corpus .tkt
grep -rhoE '"(bin|out)/[^"]*|"\.[A-Za-z][^"]*|"/tmp/[^"]*' --include='*.tkt' src | sort | uniq -c | sort -rn
# quem escreve, no corpus inteiro
grep -rn 'teko::io::write_file\|teko::fs::mkdir\|spawn_redirected\|mkdir_p\|rm_rf' --include='*.tks' --include='*.tkt' src
```

### 3.1 A descoberta que reordena a lista: `bin/teko` **nao e escrito por nenhum teste**

As nove utilizacoes de `"bin/teko"` estao todas em `src/build/project_binary_path_test.tkt`, e todas
sao **comparacoes** — `bin_path_for(NativeTarget::X8664Linux) == "bin/teko"`,
`artifact_path_for_target("bin/teko", "x86_64-windows") == ...`. O teste afirma o que o compositor de
caminhos devolve; nao toca no disco. O mesmo vale para toda a familia:

| literal | usos | escritas | onde |
|---|---:|---:|---|
| `bin/teko` | 9 | **0** | `project_binary_path_test.tkt:49,50,51,99,100,101,102,132,133` |
| `bin/teko.wasm` | 3 | **0** | idem :67,68,69 |
| `bin/teko.o` | 3 | **0** | idem :115,116,117 |
| `bin/teko.exe` | 1 | **0** | idem :34 |
| `out/x` | 2 | **0** | `project_test.tkt:1293,1295` |
| `out/x.o` | 1 | **0** | idem :1294 |
| `bin/x` | 2 | **0** | `regression_test.tkt:335,341` (elemento de argv, comparado) |
| `out/` | 1 | **0** | `project_test.tkt:1293` |
| **subtotal** | **22** | **0** | |

**Resposta directa a pergunta do dono ("o que acontece ao `bin/teko` que nove testes escrevem"):
nada, porque nenhum o escreve.** Sob a regra unica esses literais permanecem intocados, e a guarda
G1 tem de os deixar passar — o que a inversao dela ja afirma
(`sg_line_violates("teko::assert::eq_str(bin_path_for(t), \"bin/teko\")")` tem de dar `false`).

O `bin/teko` **e** escrito — pela *build*, como artefacto (`<od>/<stem>`). Isso e §11.

### 3.2 Classe A — literais que um `.tkt` entrega a um escritor (14 familias, 19 usos)

| # | caminho | usos | quem escreve | testemunha |
|---:|---|---:|---|---|
| 1 | `bin/pt-probe` | 2 | `spawn_redirected` (`project.tks:1502`) | `project_test.tkt:1169,1173` |
| 2 | `bin/pt-probe.err` | (derivado) | idem :1502 | idem |
| 3 | `bin/pt-probe-sh` | 2 | idem | `project_test.tkt:1226,1227` |
| 4 | `bin/pt-probe-sh.err` | (derivado) | idem | idem |
| 5 | `bin/.regr-work/c4_test_probe` | 1 | `tkr_compile_fail_artifact` | `regression_test.tkt:424` |
| 6 | `bin/.regr-work/c4_test_probe2` | 1 | idem | `regression_test.tkt:441` |
| 7 | `.regr-pool-test` | 1 | `spawn_spec` (+`.in/.out/.err/.chan` por filho) | `regression_test.tkt:967` |
| 8 | `.regr-pool-order-test` | 1 | idem | :994 |
| 9 | `.regr-pool-crash-test` | 1 | idem | :1043 |
| 10 | `.regr-pool-determinism-test` | 1 | idem | :1066 |
| 11 | `.regr-pool-verdict-test` | 1 | idem | :1096 |
| 12 | `.regr-pool-verdict-crash-test` | 1 | idem | :1114 |
| 13 | `/tmp/.regr-verdict-path-test.chan` | 2 | `set_var` + `verdict_emit` | `process_test.tkt:9,10` |
| 14 | `/tmp/.regr-verdict-emit-test.chan` | 1 | `verdict_emit` | `process_test.tkt:22` |

### 3.3 Classe B — compostos pelo produto durante a corrida (14 familias, **0 literais em `.tkt`**)

Um `grep` no corpus `.tkt` encontra **zero** destes. Foi por isso que a auditoria por literais nao
podia estar completa — e e por isso que a guarda G2 (§7.2) existe.

| # | caminho | onde |
|---:|---|---|
| 15 | `bin/.regr-work` (`REGR_WORK_DIR`) | `regression.tks:1996` |
| 16 | `bin/.regr-work/.toolquery` + `.in/.out/.err/.chan` | `regression.tks:474` |
| 17 | `<prefix>.proj/` + `teko.tkp`, `main.tks`, `src/regr_decls.tks`, `bin/<nome>` | `regression.tks:1408,1431` |
| 18 | `<prefix>.compile` + `.in/.out/.err/.chan` | `regression.tks:1420` |
| 19 | `<prefix>.run` `.dep_compile` `.depout` `.consumer` `.dep` `.cwd` `.wellformed` `.wasmvalid` | `regression.tks:2545,1563,1557,2350,2352,2424,974,998` |
| 20 | `<prefixo absoluto>.tkcov` | `regression.tks:2017` |
| 21 | `<binary>.ccprobe.c` | `project.tks:557` |
| 22 | `<binary>.ccmachine` | `project.tks:1472` |
| 23 | `<binary>.Info.plist` | `project.tks:1460` |
| 24 | `.teko-per-commit.diff` | `project.tks:5172` |
| 25 | `<od>/.<stem>-shard/<i>` + `.in/.out/.err/.chan/.tkcov` | `run_gate_sharded` (lane dos canais) |
| 26 | o `covfile` do gate (`TEKO_TKCOV`) | `project.tks:3554` |
| 27 | **`/tmp/teko_arena_obs.txt`** | `teko_rt.c:1256` — fixo, sem carimbo, partilhado por TODO o host |
| 28 | `<out_dir>/cobertura.xml` | `cobertura_path_of` |

**TOTAL: 28 familias de caminho escritas durante uma corrida** (14 de classe A + 14 de classe B),
mais 22 usos de literais que nao escrevem nada. O item 27 e o pior de todos e nenhuma auditoria de
`.tkt` o encontraria: dois `teko test` no mesmo host, em worktrees diferentes, atropelam-se nele.

### 3.4 A regra unica

> **Todo o caminho que uma corrida escreve e `teko::journal::scratch(base)`. Sem excepcao por
> recurso, sem lista.** Um caminho que nao passe por ai ou nao e escrito (§3.1) ou e um artefacto
> declarado da build (§11).

As 28 familias migram por substituicao mecanica do compositor. As 22 comparacoes nao mudam.

---

## 4. Os modos de falha, por caminho de saida

O dono pediu a tabela por **caminho de saida**, porque a casa ja tem a nocao de que caminhos saltam
que limpeza (`tk_panic` chama `tk_regions_free_all()` a mao porque `abort()` salta o `atexit`,
`teko_rt.c:1660`). Aqui esta, e a ultima coluna e o argumento inteiro:

| caminho de saida | `atexit` corre? | `tk_regions_free_all`? | o journal perde |
|---|:--:|:--:|---|
| retorno normal do `main` | sim | sim (atexit tardio) | **nada** |
| `exit(n)` / `tk_exit` | sim | sim (explicito) | **nada** |
| `panic` / `abort` | **nao** | sim (explicito, W9.3b) | **nada** |
| sinal de crash (SEGV/BUS/ILL/FPE) | **nao** (`_Exit`) | **NAO** (buraco existente) | **nada** |
| sinal educado (INT/TERM/HUP/QUIT) | hoje: nao ha manipulador | nao | **nada** (+ um registo `stop`, C5) |
| `SIGKILL` (OOM killer) | nao | nao | **nada** |

A coluna e uniforme porque o journal **nunca depende de um passo final**. E disso que vem o nome.

E os modos de falha nomeados no briefing, cada um com o que acontece:

| modo | o que acontece |
|---|---|
| **shard que morre a meio** | o segmento tem tudo ate a morte; falta o registo `end`; o `fold` sintetiza `incomplete` e a corrida **falha nomeando a shard**. Hoje: funde silenciosamente o `.tkcov` da corrida anterior. |
| **escrita parcial** | a ultima linha nao termina em `\n` e o `fold` descarta-a. Um `write` curto e repetido em ciclo; registos sao limitados a 4 KiB e `O_APPEND` torna o deslocamento atomico, logo dois escritores nunca se entrelacam no mesmo segmento (e alias nunca partilham segmento). |
| **`SIGKILL` do orquestrador** | nada por fundir se perde. A raiz da corrida fica em disco com o carimbo; `teko test --replay <run>` reconstroi o veredicto inteiro (C9). |
| **`SIGKILL` de um shard** | igual a "shard que morre a meio". |
| **disco cheio** | `append` devolve `error` com o `errno`; a falha e **pegajosa** — o primeiro `append` falhado marca o journal e o processo sai nao-zero com `teko: journal write failed`. Um journal que nao pode ser escrito **nao pode ser saltado em silencio**: e a lei da guarda aplicada ao proprio instrumento. |
| **lixo de corrida anterior com ar de valido** | estruturalmente inalcancavel: outra raiz, outro carimbo. O `fold` nunca le fora de `run_root(run)` **e** descarta registo cujo `run` difere — duas barreiras, porque esta e a que mordeu. |
| **cancelamento de CI / Ctrl-C / timeout** | C5: `stop <sig>` no journal, depois `_Exit(128+sig)`. O pai le "cancelada", nunca "passou". |

---

## 5. A durabilidade: o ponto nomeado, e quanto custa

**O ponto de durabilidade e a chamada `write(2)` em `tk_journal_append`, sobre um descritor
`O_APPEND` sem stdio.** Quando `append` retorna, o registo esta no kernel.

Isso significa uma fronteira exacta, e digo-a em vez de a insinuar:

* **sobrevive**: `SIGKILL`, `abort`, `_Exit`, OOM killer, sinal de crash, qualquer morte do processo;
* **nao sobrevive**: queda da maquina ou falta de energia antes de o kernel esvaziar.

A fronteira certa e essa, porque **o OOM killer mata o processo, nao o kernel**. Foi o OOM killer que
nos matou duas vezes hoje.

### 5.1 O custo, com numeros

Base: ~1167 `#test` na arvore (`grep -rc '^#test' --include='*.tkt' src`), ~3 registos por teste
(`begin`, veredicto, e o `cov`/`end` de shard) => **~3500 registos por corrida**.

| opcao | custo por registo | custo por corrida | % do gate paralelo (4,904 s) |
|---|---:|---:|---:|
| `write(2)` sem `fsync` (**escolhida**) | ~2 us | **~7 ms** | **0,14 %** |
| `fsync` por registo (`TEKO_JOURNAL_FSYNC=1`) | ~500 us (ext4 tipico) | ~1,75 s | ~36 % |
| o que ha hoje: `verdict_emit` (le tudo + escreve tudo) | O(n) por registo | ~490 MB de I/O em O(n^2) | nao medido, e nao trivial |

A terceira linha nao e uma estimativa da forma nova — e a **forma actual**:
`verdict_emit` (`process.tks:215-220`) faz `read_file` do canal inteiro e `write_file` do canal
inteiro **por registo**. Com 3500 registos de ~80 bytes isso da ~490 MB de trafego. Trocar para
append nao e so mais seguro; e a unica das tres que nao cresce em quadrado.

**Decisao: `write(2)` sempre, `fsync` nunca por omissao, com o botao `TEKO_JOURNAL_FSYNC=1` para
quem quiser durabilidade de queda de maquina.** O botao tem de trazer a sua medicao real no crumb
que o aterra (o numero acima e um tipico de ext4, nao uma medicao desta arvore) — e se a medicao
desmentir a estimativa, e a medicao que fica.

---

## 6. Os sinais educados — o braco que falta

Medido: `tk_rt_install_crash_handler` (`teko_rt.c:120-127`) instala `SIGSEGV`, `SIGBUS`, `SIGILL`,
`SIGFPE` — os de *"o programa partiu-se"*. **Nenhum dos de "alguem pediu para parares"** esta
tratado: `SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT`.

```c
// tk_rt_stop_handler — o braco EDUCADO: alguem pediu para parar (Ctrl-C, timeout de CI,
// cancelamento). Escreve UM registo `stop <sig>` no segmento ja aberto e sai com 128+sig.
//
// ASYNC-SIGNAL-SAFE POR CONSTRUCAO, e e por isso que o descritor e mantido aberto: dentro de um
// manipulador nao se pode alocar nem formatar, so `write(2)` sobre um descritor que ja existe e um
// buffer que ja esta pronto. Este e o motivo tecnico de tk_journal_open ser chamado na abertura da
// corrida e nao no primeiro registo.
static void tk_rt_stop_handler(int sig) {
    tk_journal_note(sig);
    _Exit(128 + sig);
}
```

O mesmo `tk_journal_note` entra em `tk_rt_crash_handler`, o que **fecha de passagem o buraco
existente** do despejo de arena (§1.3, defeito 3).

**Fronteira, dita e nao insinuada:** `SIGKILL` e `SIGSTOP` nao sao capturaveis (POSIX). O
encerramento elegante **nao** salva do OOM killer. Os dois mecanismos sao complementares e ambos sao
obrigatorios: **sinais para os matadores educados, journal para o mal-educado.** Fazer so um e o
meio-termo proibido.

Windows: `signal(SIGINT/SIGTERM)` mais `SetConsoleCtrlHandler` para `CTRL_CLOSE_EVENT`, sob o mesmo
`#ifdef _WIN32` que `tk_win32_spawnvp` ja usa.

---

## 7. A guarda, e como se inverte

Sao duas, porque ha duas coisas verdadeiras a manter, e **cada uma tem de poder falhar**.

### 7.1 G1 — textual: todo o literal de rascunho passa pelo compositor

Ja aterrou na lane `cargo/0.3.1.0-testes-paralelos-canais`
(`src/test/scratch_guard_test.tkt`), com inversao. Este desenho **reusa-a** e alarga-a em dois
pontos:

1. o varrimento passa de `.tkt` para `.tkt` **e** `.tks` — porque as 14 familias de classe B nao tem
   literal em `.tkt` nenhum e a guarda actual e cega para elas;
2. o compositor que ela exige passa a ser `teko::journal::scratch(` (e `teko::test::scoped(`, que
   delega nele).

Inversao: a que ja existe, mais duas linhas plantadas de classe B.

### 7.2 G2 — observacional: a raiz da corrida e a UNICA coisa que a corrida cria

G1 e uma guarda de **grafia**. Um caminho composto em tempo de execucao — de um `.tkr`, de uma
variavel de ambiente, de uma concatenacao — escapa-lhe por construcao. G2 nao le codigo, olha o disco:

```teko
#test
/**
 * jg_a_run_creates_nothing_outside_its_root — A GUARDA QUE NAO SE DEIXA ENGANAR PELA GRAFIA.
 *
 * Tira uma fotografia das raizes escrivies (`bin/`, `out/`, `/tmp`), corre uma corrida de journal
 * completa, e tira outra. Todo o caminho novo tem de estar dentro de `run_root()`. Um caminho
 * composto em tempo de execucao — que e como as 14 familias de classe B nascem — cai aqui e nao cai
 * em G1.
 *
 * NOMEIA O CAMINHO, NUNCA UM NUMERO. Um numero nao se pode agir.
 *
 * @throws quando a corrida criou algo fora da sua raiz
 */
fn jg_a_run_creates_nothing_outside_its_root() {
    let before = jg_snapshot()
    teko::assert::gt_i64(before.len to i64, 0)
    let root = jg_run_a_probe_run()
    let stray = jg_first_outside(jg_snapshot(), before, root)
    teko::assert::is_absent(teko::str::concat("stray path (", stray, ")"), stray.len > 0)
}

#test
/**
 * jg_the_guard_rejects_a_planted_stray — A INVERSAO, EM TRES BRACOS, E NENHUM E OPCIONAL.
 *
 * Tres vezes hoje uma guarda deu ZERO numa arvore que continuava partida, porque era CEGA em vez de
 * tolerante. Por isso a inversao nao afirma so "apanha o plantado": afirma tambem que o INSTRUMENTO
 * esta vivo. Uma fotografia vazia nunca podia diferir de nada, e uma guarda que nao pode falhar e
 * decoracao.
 *
 * 1. um caminho plantado FORA da raiz e nomeado;
 * 2. um caminho novo DENTRO da raiz nao e nomeado (a guarda e tolerante, nao paranoica);
 * 3. a fotografia de base nao e vazia (a guarda esta viva).
 */
fn jg_the_guard_rejects_a_planted_stray() {
    let base = jg_snapshot_of(jg_fixture_before())
    teko::assert::gt_i64(base.len to i64, 0)
    teko::assert::eq_str(jg_first_outside(jg_fixture_after_stray(), base, "bin/.tkrun/r1"), "bin/.planted-stray")
    teko::assert::eq_str(jg_first_outside(jg_fixture_after_clean(), base, "bin/.tkrun/r1"), "")
}
```

**Como se inverte a minha guarda, em uma frase:** planta-se `bin/.planted-stray`, e G2 tem de o
nomear; remove-se a fotografia de base, e o braco 3 tem de falhar. As duas direccoes estao afirmadas
por testes, o que e a diferenca entre uma guarda e um enfeite.

---

## 8. A prova por colisao forcada

Nao provo "corrigi" com 1155 testes verdes — verde numa particao diz que **naquela** particao nao
colidiram. Provo com uma colisao **forcada e deterministica**, no mesmo padrao da prova por reversao
que fechou o degrau 29.

O material ja esta todo na arvore: `run_pool` corre filhos concorrentes, `sh_argv` monta-os
(`regression_test.tkt:967` ja o faz), e o rendezvous e feito com ficheiros marcadores.

```teko
#test
/**
 * jc_the_same_base_in_two_processes_loses_a_write — O ANTES: dois escritores, um caminho, e uma
 * escrita que desaparece. Deterministico, nao provavel.
 *
 * O RENDEZVOUS E O QUE O TORNA DETERMINISTICO. Sem ele a colisao e uma corrida que pode nao
 * acontecer, e uma prova que pode nao acontecer nao e prova. Com ele os dois filhos ficam
 * garantidamente dentro da janela ao mesmo tempo: cada um anuncia-se, espera pelo anuncio do outro,
 * e so entao escreve. O ultimo a escrever fica com o ficheiro inteiro, e o primeiro le de volta bytes
 * que nao sao os seus.
 *
 * ISTO E O DEFEITO QUE O DONO NOMEOU, reproduzido em vez de argumentado — e fica no corpus para
 * sempre, porque uma prova que so se corre uma vez nao guarda nada.
 *
 * @throws quando os dois escritores partilham um caminho e AINDA ASSIM sobrevivem os dois (o que
 *         significaria que esta prova deixou de provar o que diz)
 */
fn jc_the_same_base_in_two_processes_loses_a_write() {
    let w = teko::test::scoped("collide")
    let caps = run_pool(jc_two_children_sharing(w), 2)
    teko::assert::eq_u64(caps.len, 2)
    teko::assert::is_true(jc_one_of_them_read_back_foreign_bytes(caps))
}

#test
/**
 * jc_journal_scratch_keeps_both_writes — O DEPOIS, contra o mesmo rendezvous e a mesma janela: os
 * dois filhos derivam o caminho da sua IDENTIDADE, e os dois sobrevivem inteiros.
 *
 * @throws quando algum dos dois nao le de volta exactamente o que escreveu
 */
fn jc_journal_scratch_keeps_both_writes() {
    let w = teko::test::scoped("noncollide")
    let caps = run_pool(jc_two_children_scoped(w), 2)
    teko::assert::eq_u64(caps.len, 2)
    teko::assert::str_contains(caps[0].chan_text, "intact")
    teko::assert::str_contains(caps[1].chan_text, "intact")
}

#test
/**
 * jc_round_robin_puts_neighbours_in_different_shards — o agravante, afirmado como PROPRIEDADE em vez
 * de suposto.
 *
 * O split e round-robin por ordinal (escolha certa: o custo por teste e desigual), logo dois `#test`
 * ADJACENTES caem em shards diferentes — e adjacentes sao exactamente os que partilham caminho,
 * porque vivem no mesmo ficheiro e na mesma familia. Sem esta linha, a prova acima seria sobre dois
 * processos quaisquer; com ela, e sobre o par que o corpus real produz.
 */
fn jc_round_robin_puts_neighbours_in_different_shards() {
    teko::assert::ne_i64(shard_of_ordinal(7, 4) to i64, shard_of_ordinal(8, 4) to i64)
}
```

O par `jc_the_same_base_*` / `jc_journal_scratch_*` e o **ANTES e o DEPOIS na mesma corrida**. Nao
exige desligar o mecanismo por variavel de ambiente (um botao global desses seria uma maneira nova de
partir a arvore); o braco "antes" simplesmente escreve o literal partilhado directamente nos filhos
`sh`, que e o que o corpus fazia.

---

## 9. `run_regression_sources` — o que isto destranca, e o que nao

**Destranca, e digo porque.** `run_regression_sources` (`regression.tks:2988-3020`) e serial por um
motivo estrutural, nao por preguica: todos os `.tkr` partilham `REGR_WORK_DIR`, e
`merge_regression_coverage` varre essa **unica** raiz. Enraizar o rascunho por escritor remove
exactamente esse acoplamento: cada filho tem a sua sub-arvore e o seu `.tkcov`, e a cobertura da fase
passa a ser o `fold` dos segmentos. A partir dai, repartir os ficheiros `.tkr` por `run_pool` e um
crumb pequeno — `run_pool`, o carimbo de shard e o sumarizador ja existem (C9).

**E o que NAO destranca, dito com a mesma clareza.** Os 620 s de `own_native` sao **um** `.tkr`.
Reparticao por FICHEIRO nao o divide. Reparticao por LINHA dividi-lo-ia, mas as linhas partilham o
artefacto construido (cache de build por processo), logo cada shard reconstruiria. O ganho depende
de quanto dos 620 s e build e quanto e linhas — e **esse numero eu nao tenho**. C9 mede-o primeiro e
aborta se a build dominar; medir antes de paralelizar e a mesma disciplina que o cabecalho do
`regressor.tkr` ja impoe ao numero de builds (*"numero de build se MEDE na lane fail-closed, nao se
deriva por adicao"*). A perna Windows de 15-20 min tem a mesma forma e a mesma medicao pendente.

---

## 10. O custo, em crumbs

Cada crumb e independentemente fechavel e cada um entrega algo sozinho.

| # | crumb | toca | ganho |
|---:|---|---|---|
| **C1** | `teko::journal` (modulo) + fundos `tk_journal_open/append/note` + `tk_rt_rename`; `teko::process::verdict_emit` passa a delegar em `append` | `src/journal/journal.tks` (novo), `src/runtime/teko_rt.{c,h}`, `src/process/process.tks`, `src/checker/scope.tks` (injeccao do namespace) | o sumidouro duravel existe; `verdict_emit` deixa de ser O(n^2) (~490 MB -> ~280 KB de I/O) |
| **C2** | identidade da corrida: `run_id`/`run_root`/`scratch`/`sweep`, `TEKO_RUN` herdado por `spawn_spec`; `teko::test::scoped` re-apontada; ferrolho da raiz | `src/journal/journal.tks`, `src/test/test.tks`, `src/build/regression.tks` | lixo de corrida anterior fica estruturalmente inalcancavel; duas corridas no mesmo worktree deixam de partilhar rascunho |
| **C3** | migrar as **28 familias** (§3.2, §3.3) para `scratch`, incluindo `/tmp/teko_arena_obs.txt` | `regression.tks`, `project.tks`, `teko_rt.c`, 4 `.tkt` | a regra passa a ser total; a auditoria passa a ser "ler os chamadores de um compositor" |
| **C4** | `fold` + `summarize` + `render_summary` (§13); `merge_shard_coverage` falivel; shard sem `end` = falha nomeada | `project.tks`, `src/journal/summary.tks` (novo) | **fecha o defeito medido**: um `.tkcov` em falta deixa de flutuar sobre o da corrida anterior — e passa a ser VISTO, no sumario |
| **C4b** | o arnes journaliza: `emit_test_main`/`emit_test_call` emitem `plan`/`begin`/`ok` alem das linhas que ja imprimem | `src/codegen/codegen.tks` | **a fase unitaria passa a saber contar** — hoje nao ha sumario nenhum e um `panic` mata a suite sem nomear os que nunca correram (§13.2) |
| **C5** | braco dos sinais educados (INT/TERM/HUP/QUIT) + `tk_journal_note` no manipulador de crash | `teko_rt.c` | cancelamento de CI e Ctrl-C deixam de ser mudos; fecha o buraco existente do despejo de arena no crash |
| **C6** | G1 alargada a `.tks`, compositor unico, inversao estendida | `src/test/scratch_guard_test.tkt` | reincidencia por grafia fica bloqueada nas 28 familias, nao nas 14 visiveis |
| **C7** | G2 observacional + inversao de tres bracos (incl. vivacidade do instrumento) | `src/journal/journal_guard_test.tkt` (novo) | reincidencia por caminho composto em execucao fica bloqueada; a guarda pode falhar, e a inversao prova-o |
| **C8** | a prova por colisao forcada (3 `#test` + 2 filhos `sh` com rendezvous) | `src/journal/journal_collision_test.tkt` (novo) | "corrigi" vira "provei", com o ANTES na mesma corrida |
| **C9** | `teko test --replay <run>` (o mesmo sumario, depois do facto) + o aviso de corrida nao-sumarizada no `sweep` + fase de regressao repartida por `run_pool`, com a medicao build-vs-linhas de §9 | `project.tks`, `regression.tks` | um OOM deixa de perder o veredicto — a corrida seguinte **sumariza a morta** (§13.5); a fase de regressao entra no mesmo mecanismo |

**Pontos de ritual (gate completo obrigatorio):** depois de **C2** (muda caminho em toda a arvore),
depois de **C4b** (muda o arnes emitido, logo muda TODA a fase unitaria), depois de **C8** (a prova
tem de correr verde e o braco "antes" tem de continuar a falhar quando o mecanismo e removido),
depois de **C9**.

**O custo do adendo, dito em separado para ser auditavel:** o sumario **nao** cabe inteiro no C4.
`summarize`/`render_summary` sao um `fold` puro e cabem (C4 cresce ~1 unidade); a releitura cabe no
C9 (que ja era o comando `--replay`). O que **nao** cabia e o crumb novo **C4b**: hoje a fase
unitaria nao emite nada alem de `test <label> ... ` / `ok` para stdout, e um sumario nao pode ser
lido do proprio stdout sem parsing fragil nem distinguir "morreu" de "nunca correu" (§13.2).
**Total: 9 -> 10 crumbs, um novo, dois crescidos.**

**Ordem e semente:** nenhum crumb usa funcionalidade de linguagem ausente da semente. Os fundos novos
sao `extern fn` sobre `teko_rt.{c,h}` — C mantido, excepcao explicita ao congelamento. O unico custo
de infra-estrutura e a injeccao do namespace `teko::journal` em `src/checker/scope.tks`, com
precedente directo e recentissimo: a lane dos canais fez o mesmo para `teko::test` (+106 linhas).

---

## 11. O que esta solucao **NAO** cobre

Dito na primeira pessoa, com a razao, e nao como alarme:

1. **Queda da maquina ou falta de energia.** O ponto de durabilidade e o kernel, nao o prato. O botao
   `TEKO_JOURNAL_FSYNC=1` existe e o seu custo esta estimado em §5.1; por omissao esta desligado
   porque paga 36 % do gate por um risco que nao e o nosso (o nosso e o OOM killer).
2. **`SIGKILL` e `SIGSTOP` nao sao capturaveis.** Nenhum encerramento elegante os apanha; e por isso
   que o journal existe **alem** do braco de sinais, e nao em vez dele.
3. **O artefacto `bin/teko`.** Ele e escrito — pela build, como saida declarada (`<od>/<stem>`), nao
   como rascunho. Duas builds simultaneas para o mesmo `-o` atropelam-se, e isso continua verdade.
   C2 mitiga-o com um ferrolho na raiz da corrida que **recusa** a segunda com uma mensagem nomeada,
   mas nao o torna concorrente: dois artefactos no mesmo caminho sao um erro de invocacao, nao uma
   corrida a resolver.
4. **Sistemas de ficheiros de rede.** A atomicidade de deslocamento do `O_APPEND` nao vale em NFS.
   Nenhuma perna nossa constroi sobre NFS; se alguma vier a construir, a garantia degrada e tem de
   ser re-afirmada nesse contexto.
5. **O sumidouro de raia.** A superficie esta desenhada e o `Journal.seg` ja e opaco para o acomodar,
   mas nao ha threads em Teko hoje — isto e capacidade inexistente, nao fase adiada. Quando
   `teko::isolate` (S8/C4, raiz por tarefa) aterrar, muda **uma** funcao: `journal_open_rt`.

---

## 12. Riscos e tensoes de lei

| risco | resolucao |
|---|---|
| C3 toca 28 familias e a lane dos canais esta em merge forward | C3 e mecanico e o compositor e unico; a lane ja aterrou `teko::test::scoped` e este desenho **delega nela** em vez de a substituir — o trabalho dela nao se perde, ganha uma raiz |
| o descritor aberto para o manipulador de sinal e estado global | e a **unica** forma de o manipulador ser async-signal-safe; sem descritor pre-aberto nao ha `write(2)` legal dentro do manipulador, e sem `write(2)` nao ha braco educado |
| `sweep` na abertura apaga a raiz de uma corrida que ainda corre noutro processo | o ferrolho de C2 nomeia a raiz viva; `sweep` salta qualquer raiz com ferrolho valido |
| a estimativa de `fsync` (~500 us) e tipica, nao medida nesta arvore | C5 traz a medicao; se ela desmentir a estimativa, e a medicao que fica — nunca o contrario |

**Tensao de lei resolvida em primeiro lugar:** o meu briefing pede fixtures de regressao expressas
como *"inputs -> expected native exit codes"*. O ruling do dono de hoje proibe-o: *"um teste afirma o
que deve FAZER, nao o numero com que sai"*. Passa o ruling. Toda a fixture de regressao deste desenho
afirma-se por `Then stdout pattern = "scenario <nome>: ok"`, na forma que
`examples/regressions/own_native/src/scenario.tks` ja canonizou, e vive num canal **existente** —
nenhum canal novo, porque um canal novo custa uma build e o alvo do dono e dez.

**Nenhuma tensao por resolver. Nao ha HALT.**

---

## 13. O sumario final — adendo do dono, 2026-07-30

> *"Colocar um sumario (no fim de todos os testes, unitarios e regressivos) que aponta todos os
> erros, skips e sucessos, bem como a cobertura medida."*

O desenho acima nao o resolvia. Resolvia o `gate_summary` da fase **unitaria** (§2.4) e deixava a
fase de regressao a reportar-se sozinha, que e exactamente a separacao que o adendo fecha. Esta
seccao acrescenta-o, e acrescenta-o **como consequencia do journal e nao ao lado dele**.

### 13.1 Porque isto e uma releitura e nao um acumulador

As duas fases escrevem na **mesma raiz de corrida**. Logo nao ha nada por juntar: o sumario das duas
e um `fold` sobre uma raiz, e o `fold` ja existe (C4). A fase de cada registo deriva do prefixo do
escritor (`s<i>` = shard unitaria, `r<i>` = filho de regressao, `m` = orquestrador) — **zero
canalizacao nova**.

E dai vem a propriedade que o adendo torna valiosa em vez de bonita: **um sumario que so existe se a
corrida chegar ao fim e o sumario que falta exactamente quando e preciso.** Este existe depois do
facto (§13.5).

### 13.2 O buraco que o adendo desenterra, e e maior do que parecia

Fui ver o que a fase unitaria reporta hoje. **Nao reporta nada.** `emit_test_main`
(`src/codegen/codegen.tks:11990-12017`) chama cada `#test`, cada um imprime `test <label> ... ` e
`ok`, e o `main` faz `return 0`. Nao ha linha de total, nao ha contagem, nao ha coluna de skip.

E e **fail-fast**: uma assercao falhada faz `panic` -> `abort`, o processo morre, e **todos os testes
seguintes nunca correm, nem sao contados nem nomeados**. O leitor ve um `test X ... ` pendurado sem
`ok` e mais nada. Nao ha como saber quantos ficaram por correr.

Por isso o C4b existe e por isso ele **nao cabia** no C4: sem um registo `plan` (quantos testes esta
shard ia correr) e um `begin` por teste, o sumario nao pode dizer `never-ran`, e `never-ran` e
precisamente o numero que hoje esta invisivel. Sob sharding a coisa melhora sozinha — as outras tres
shards acabam — mas so o journal permite dizer **qual** shard morreu, **em que teste**, e **quantos**
ficaram por correr.

> Nao parseio o meu proprio stdout para isto. Um sumario derivado das linhas `test ... ok` seria
> fragil (o teste imprime o que quiser no meio) e, sobretudo, **nao distingue "morreu" de "nunca
> correu"** — que e a unica distincao que o dono precisa de ler.

### 13.3 A superficie

```teko
/**
 * Finding — UM achado nomeado do sumario: uma falha, um skip, ou um escritor que morreu.
 *
 * NOMEADO, NUNCA CONTADO. Um numero nao se pode agir: "1 skipped" nao diz qual fila nem porque, e a
 * lei desta casa e que um skip no CI do proprio compilador E falha (`scripts/no_skips_gate.sh`). Uma
 * lei sem instrumento e uma lei que ninguem aplica.
 *
 * @since 0.3.1
 */
pub type Finding = struct {
    /** `unit` ou `regression`, derivado do prefixo do escritor. */
    phase: str
    /** que escritor o produziu (`s2`, `r0`, `m`). */
    writer: str
    /** o que ele nomeia: o `#test`, ou `<ficheiro>.tkr[<cenario> linha <n>]`. */
    label: str
    /** porque — a razao honesta que o produtor ja escreve hoje, transportada em vez de resumida. */
    reason: str
}

/**
 * RunSummary — o veredicto INTEIRO de uma corrida: as duas fases, os achados nomeados, a cobertura.
 *
 * @since 0.3.1
 */
pub type RunSummary = struct {
    /** o carimbo da corrida que este sumario descreve. */
    run: str
    /** testes/linhas que passaram, por fase e no total (`unit`, `regression`, `total`). */
    passed: PhaseTally
    /** os que falharam. */
    failed: PhaseTally
    /** os que foram saltados por capacidade ausente. */
    skipped: PhaseTally
    /** os que NUNCA correram porque o escritor morreu antes de lá chegar (`plan` menos o resto). */
    never_ran: PhaseTally
    /** cada falha, nomeada. */
    fails: []Finding
    /** cada skip, nomeado COM a razao. */
    skips: []Finding
    /** cada escritor sem registo `end`, com o ultimo teste que chegou a comecar. */
    dead: []Finding
    /** funcoes/linhas/ramos medidos, do fold dos despejos desta corrida. */
    cov: CovTriple
    /** quantos despejos de cobertura eram esperados e nao apareceram (§2.4). */
    cov_missing: u64
}

/**
 * summarize — o sumario das DUAS fases, como funcao pura sobre os registos.
 *
 * PURA DE PROPOSITO: e o que permite testa-la sobre registos sinteticos (§13.6) sem correr uma
 * suite, e e o que permite ao `--replay` produzir byte-a-byte o mesmo bloco depois do facto.
 *
 * @param recs  os registos que `fold` devolveu
 * @return      o veredicto agregado
 * @since 0.3.1
 */
pub fn summarize(recs: []Record) -> RunSummary

/**
 * render_summary — o bloco que o humano le, em texto estavel.
 *
 * ESTAVEL PORQUE E LIDO POR UM SCRIPT TAMBEM. `scripts/no_skips_gate.sh` reconstitui hoje os skips
 * por tres padroes de `grep` diferentes, um dos quais (`regression ok ... 2 scenario row(s)
 * skipped`) existe so porque um skip pode esconder-se atras de um `ok`. Com uma seccao `SKIPPED`
 * nomeada, um skip nunca se esconde e o script colapsa para UM padrao.
 *
 * @param s  o veredicto a renderizar
 * @return   o bloco, com quebras de linha, terminado pela linha de fecho
 * @since 0.3.1
 */
pub fn render_summary(s: RunSummary) -> str
```

### 13.4 O bloco, como o dono o vai ler

```
teko: ===== summary (run 1753891200123456789-48213) =====
teko:   unit          1167 passed     0 failed    0 skipped    0 never-ran
teko:   regression       8 passed     1 failed    2 skipped    0 never-ran
teko:   TOTAL         1175 passed     1 failed    2 skipped    0 never-ran
teko:
teko:   FAILED (1):
teko:     regression  r0  own_native.tkr[own_defer_arm_write_propagates]: exit 1, expected stdout pattern absent
teko:
teko:   SKIPPED (2) — SKIPPED IS FAILURE IN THIS PROJECT'S OWN CI:
teko:     regression  r0  own_native.tkr[wasm32-wasi row 3]: wasmtime not found on PATH for target wasm32-wasi
teko:     regression  r0  own_native.tkr[wasm32-wasi row 7]: wasmtime not found on PATH for target wasm32-wasi
teko:
teko:   COVERAGE (4 shard dumps + 63 regression dumps merged, 0 missing):
teko:     functions   92% (4811/5219)    floor 90   ok
teko:     lines       88% (61233/69582)  floor 85   ok
teko:     branches    74% (18220/24486)  floor 70   ok
teko: ===== 1 failed, 2 skipped — run FAILED =====
```

Quatro coisas que este bloco faz e que hoje ninguem faz:

1. **Soma as duas fases.** A linha `TOTAL` nao existe em lado nenhum da arvore.
2. **Nomeia cada skip com a sua razao.** O `wasmtime not found on PATH for target wasm32-wasi` que
   viaja pelas pernas do CI ja e produzido — `regression.tks:611` — e ja e impresso em linha
   (`regr_row_skip_line`, `regression.tks:2593`). O que faltava era ele **aparecer no fim**, junto,
   em vez de estar espalhado a 4000 linhas de transcricao do sitio onde o leitor olha.
3. **Mostra a cobertura medida e as fasquias lado a lado**, incluindo `missing` — o numero do defeito
   que C4 fecha passa a ser visto, em vez de calculado em silencio.
4. **Tem uma linha de fecho.** "O sumario chegou a ser impresso?" passa a ser respondivel; e um `grep`
   por `===== .* run ` que hoje nao tem alvo.

**O que este sumario NAO muda: a politica.** Nomear um skip nao o transforma em falha. A separacao ja
esta decidida e esta certa (`scripts/no_skips_gate.sh`: *"a skipped row is a legitimate outcome of
the LANGUAGE ... o que NAO lhes pertence e o NOSSO CI reportar verde sobre linhas que nunca
executou"*), e `REGRESSION_REQUIRE_TOOLS=1` continua a ser o botao fail-closed. O sumario da a lei o
**instrumento**; quem decide continua a ser a lane.

### 13.5 A corrida morta tambem e sumarizada

Duas mortes por OOM hoje (`rc=137`), em duas maquinas. Nesses dois casos o orquestrador nunca chegou
a imprimir bloco nenhum. Com o journal:

* os segmentos estao em disco, carimbados;
* `teko test --replay <run>` imprime **o mesmo bloco**, com `dead: s2 (morreu em <ultimo teste>)` e
  `never-ran: 291`;
* e o `sweep` da corrida SEGUINTE (C2), antes de varrer, ve uma raiz sem sumario e diz uma linha:

```
teko: warning: run 1753891200123456789-48213 ended without a summary (2 failures, 1 writer dead) — `teko test --replay 1753891200123456789-48213`
```

**Uma corrida morta passa a ser sumarizada pela seguinte.** E isso nao e um extra do adendo: e a
razao pela qual `sweep` varre na abertura e nao no fecho, que ja estava fixada no `@return` de
`teko::journal::sweep` (§2.2) antes de o adendo existir.

### 13.6 A guarda do sumario — e ela tambem tem de poder falhar

Um sumario e um instrumento, e um instrumento cego e a patologia desta lane (tres vezes hoje). Por
isso o `summarize` puro tem inversao de tres bracos, e o terceiro e o que importa:

```teko
#test
/**
 * js_the_summary_names_every_finding — a inversao: alimentado com uma falha, dois skips e um
 * escritor morto, o sumario tem de nomear os quatro. Um sumario que os contasse sem os nomear
 * passaria neste teste se ele afirmasse numeros; por isso afirma NOMES.
 *
 * @throws quando algum achado plantado nao aparece no bloco renderizado
 */
fn js_the_summary_names_every_finding() {
    let block = teko::journal::render_summary(teko::journal::summarize(js_planted_records()))
    teko::assert::str_contains(block, "own_native.tkr[wasm32-wasi row 3]")
    teko::assert::str_contains(block, "wasmtime not found on PATH")
    teko::assert::str_contains(block, "js_planted_failure")
    teko::assert::str_contains(block, "never-ran")
}

#test
/**
 * js_an_empty_run_is_a_FAILURE_not_a_green — O BRACO DE VIVACIDADE, e e o unico indispensavel.
 *
 * Zero registos significa que a corrida nao produziu nada, e um sumario que renderize "0 failed" com
 * ar de verde sobre zero registos e exactamente a guarda cega que deu ZERO tres vezes hoje numa
 * arvore que continuava a parar. Um instrumento que nao pode falhar e decoracao — inclusive este.
 *
 * @throws quando um sumario vazio nao se declara falhado
 */
fn js_an_empty_run_is_a_FAILURE_not_a_green() {
    let block = teko::journal::render_summary(teko::journal::summarize(teko::list::empty()))
    teko::assert::str_contains(block, "no records — run produced nothing")
    teko::assert::str_absent(block, "run PASSED")
}

#test
/**
 * js_a_dead_writer_cannot_read_as_passed — a shard que morre a meio nao pode arredondar para verde,
 * e a contagem de `never-ran` tem de ser a diferenca para o `plan` que ela declarou.
 *
 * @throws quando um `plan` de 300 com 9 vereditos nao rende 291 nunca-corridos
 */
fn js_a_dead_writer_cannot_read_as_passed() {
    let s = teko::journal::summarize(js_records_of_a_dead_shard(300, 9))
    teko::assert::eq_u64(s.never_ran.total, 291)
    teko::assert::eq_u64(s.dead.len, 1)
    teko::assert::str_absent(teko::journal::render_summary(s), "run PASSED")
}
```

### 13.7 Fixture de regressao

Um cenario no canal **existente** (`own_native`) — nenhum canal novo, nenhuma build nova:

```
  Scenario: journal_summary_names_a_planted_skip
    Given args = ["--replay-fixture"]
    When built and run
    Then stdout pattern = "scenario journal_summary_names_a_planted_skip: ok"
```

O caso monta registos sinteticos com uma falha e um skip, renderiza, e afirma pelo NOME (nunca pelo
codigo de saida — ruling do dono de hoje). A prova de que o bloco sobrevive a morte da corrida e o
`#test` `js_a_dead_writer_cannot_read_as_passed` acima, que nao precisa de matar processo nenhum
porque `summarize` e pura.

### 13.8 O que o sumario NAO cobre

1. **Nao muda a politica de skip** (§13.4) — nomeia, nao decide.
2. **Nao torna a fase unitaria tolerante a falhas.** O `panic` continua a matar a shard; o que muda e
   que a morte passa a ser nomeada e os nao-corridos contados. Tornar cada `#test` isolado e
   `teko::isolate` (S8), que nao existe — e §11.5 ja o diz.
3. **Nao inclui a cobertura de uma shard cujo despejo falta.** Ela e reportada como `missing`, e a
   corrida falha (C4). Estimar o que falta seria inventar o numero que a fasquia julga.
