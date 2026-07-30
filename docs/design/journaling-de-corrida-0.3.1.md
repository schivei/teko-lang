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
| 19 | `<prefix>.run` `.dep_compile` `.depout` `.consumer` `.dep` `.cwd` `.wellformed` | `regression.tks:2482,1505,1499,2292,2294,2347,941` |
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

> **REORDENADO POR §14 (ruling do dono, 2026-07-30).** Em **modo teste** as quatro primeiras linhas
> desta tabela **deixam de ocorrer**: `panic` e `exit` sao CAPTURADOS e nao emitem syscall de saida,
> logo nao ha `atexit` saltado, nao ha shard morta por sua propria mao, e a tabela reduz-se a **duas
> classes** — o programa termina (sempre elegante) ou algo de FORA o mata. A tabela abaixo fica como
> esta porque continua a valer **fora** do modo teste (modo `Program`, o compilador em uso real) e
> para a classe externa. Le-a com §14 ao lado.

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

### 5.2 O `.tkcov` NAO tem essa forma — e onde ele tem, e noutro sitio

**Dito explicitamente para ninguem abrir o C1 a procura de um append de cobertura que nao existe, e
"consertar" o que ja esta certo.** O `O(n^2)` de §5.1 e do **`verdict_emit`**, nao do `.tkcov`. O
ficheiro de cobertura **acumula em memoria e despeja UMA vez**: `tk_cov_dump` e um
`fopen(path, "wb")` que percorre as tabelas e escreve tres seccoes (`teko_rt.c:2698-2717`), e o
`fread` esta do lado do **agregador** (`tk_cov_merge`, `:2731`) — exactamente onde deve estar. A
conclusao do dono mantem-se e ja e o comportamento da cobertura: **ler so no fim, para agregar,
sumarizar e gerar o XML.** O que o C1 traz de novo e pôr o CANAL DE VEREDICTO nessa mesma forma.

Os tres defeitos que partiam a cobertura sao de outra natureza, e dois ja estao fechados hoje: shards
a partilhar o caminho (identidade do escritor, C2/C3); uma shard que panica nunca chegar ao despejo
(**C0**, a captura); e o ficheiro da corrida anterior a flutuar (**C4**).

**MAS — e fui medir os TRES sumidouros e nao so o despejo — a intuicao do dono acerta noutro lugar, e
acerta em cheio.** *"Ao invés de apenas apendar, está lendo para saber o que fazer"* descreve
literalmente **dois dos tres sumidouros**, uma camada abaixo do ficheiro: eles releem o **vector em
memoria** para decidir se inserem.

| sumidouro | dedup por acerto | custo | fonte |
|---|---|---|---|
| **linhas** | conjunto de endereçamento aberto (`tk_line_insert_packed`) | **O(1)** amortizado | `teko_rt.c:2646-2652` |
| **funcoes** | **varrimento linear do vector inteiro** (`tk_cov_mark`) | **O(distintas)** por acerto | `teko_rt.c:2541-2554` |
| **ramos** | **varrimento linear do vector inteiro** (`tk_covb_add`) | **O(distintos)** por acerto | `teko_rt.c:2590-2599` |

E a frequencia e o pior da historia: `tk_cov_mark(cov_idx)` e um **prologo de entrada de funcao**
(`emit_function_cov`, `codegen.tks:9601`) e `tk_cov_branch_at` corre em **cada execucao de ramo**
(`codegen.tks:8303`) — nao uma vez por linha distinta, mas uma vez por *passagem*. O lado da consulta
tem a mesma forma: `tk_cov_branch_hit` (`teko_rt.c:2613`) tambem varre linearmente, e a caminhada das
fasquias chama-o por ramo.

**A remodelacao que o dono pede e real, e UMA so forma de funcao, e ela ja existe tres linhas acima no
mesmo ficheiro** — `tk_line_insert_packed`. Funcoes e ramos passam a usar o mesmo conjunto de
endereçamento aberto que as linhas ja usam; o despejo, o formato do ficheiro e o agregador **nao mudam
uma linha**.

**O que eu NAO medi, e digo-o em vez de estimar:** quantos acertos dinamicos uma corrida instrumentada
faz. Sem esse numero o custo real e uma forma conhecida com magnitude desconhecida, e eu nao penduro
um numero inventado num documento que o dono compara com o que ja tem. **A medicao e o primeiro passo
do trabalho**, e se ela desmentir a forma, e a medicao que fica.

**Achado adjacente: REPORTADO, nao convertido em issue por mim.** Nao pertence ao C1 (que e o canal de
veredicto) e nao e pre-requisito de nenhum crumb C0–C9 — a cobertura fica CORRECTA com o C0/C2/C3/C4
tal como estao. E uma questao de CUSTO, nao de correccao, e a decisao de a puxar e do dono.

**O `TestAnalyze` foi verificado tambem** (era o caminho que faltava auditar): ele despeja **um
ficheiro por teste** em `$TEKO_ANALYZE_DIR/<idx>.tkcov` (`emit_test_main_analyze`,
`codegen.tks:12043`) — N escritas, mas cada uma continua a ser um despejo unico de um conjunto em
memoria, sem reler nada para decidir. Ja e endereçado pela identidade do escritor (o indice do teste),
o que confirma o modelo em vez de o contrariar. Herda porem o defeito 3: o directorio e partilhado
entre corridas e um `<idx>.tkcov` obsoleto seria lido como desta corrida — o **C2** cobre-o pela raiz
carimbada. O `ProgramCov` despeja por `atexit` para `$TEKO_TKCOV` (`codegen.tks:11955`), forma unica,
sem releitura.

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
| ~~C4b~~ | **DISSOLVIDO NO C4 por §14.** Existia para reconstruir o que a morte levou; com a captura o arnes chega sempre ao fim e o tally e um contador. Os registos por teste ficam (valem para o `--replay` depois de morte EXTERNA), mas deixam de ser a fonte do numero | — | — |
| **C5** | braco dos sinais educados (INT/TERM/HUP/QUIT) + `tk_journal_note` no manipulador de crash | `teko_rt.c` | cancelamento de CI e Ctrl-C deixam de ser mudos; fecha o buraco existente do despejo de arena no crash |
| **C6** | G1 alargada a `.tks`, compositor unico, inversao estendida | `src/test/scratch_guard_test.tkt` | reincidencia por grafia fica bloqueada nas 28 familias, nao nas 14 visiveis |
| **C7** | G2 observacional + inversao de tres bracos (incl. vivacidade do instrumento) | `src/journal/journal_guard_test.tkt` (novo) | reincidencia por caminho composto em execucao fica bloqueada; a guarda pode falhar, e a inversao prova-o |
| **C8** | a prova por colisao forcada (3 `#test` + 2 filhos `sh` com rendezvous) | `src/journal/journal_collision_test.tkt` (novo) | "corrigi" vira "provei", com o ANTES na mesma corrida |
| **C9** | `teko test --replay <run>` (o mesmo sumario, depois do facto) + o aviso de corrida nao-sumarizada no `sweep` + fase de regressao repartida por `run_pool`, com a medicao build-vs-linhas de §9 | `project.tks`, `regression.tks` | um OOM deixa de perder o veredicto — a corrida seguinte **sumariza a morta** (§13.5); a fase de regressao entra no mesmo mecanismo |

**Pontos de ritual (gate completo obrigatorio):** depois de **C0** (§14 — muda o arnes de TODOS os
1167 `#test`), depois de **C2** (muda caminho em toda a arvore),
depois de **C4b** (muda o arnes emitido, logo muda TODA a fase unitaria), depois de **C8** (a prova
tem de correr verde e o braco "antes" tem de continuar a falhar quando o mecanismo e removido),
depois de **C9**.

**O custo do adendo, dito em separado para ser auditavel:** o sumario **nao** cabe inteiro no C4.
`summarize`/`render_summary` sao um `fold` puro e cabem (C4 cresce ~1 unidade); a releitura cabe no
C9 (que ja era o comando `--replay`). O que **nao** cabia e o crumb novo **C4b**: hoje a fase
unitaria nao emite nada alem de `test <label> ... ` / `ok` para stdout, e um sumario nao pode ser
lido do proprio stdout sem parsing fragil nem distinguir "morreu" de "nunca correu" (§13.2).
**Total: 9 -> 10 crumbs, um novo, dois crescidos.**

**E o ruling do dono (§14) devolve o que o adendo custou:** entra o **C0** (a captura, e vai a
frente de tudo) e o **C4b dissolve-se** nele, porque o tally deixa de precisar de reconstrucao.
**10 -> 10.** Ordem final: **C0** · C1 · C2 · C3 · C4 · C5 · C6 · C7 · C8 · C9.

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
   que o journal existe **alem** do braco de sinais, e nao em vez dele. (§14 corta a outra metade:
   o que o programa faz a SI PROPRIO — `panic`/`exit` — deixa de o matar em modo teste, e so esta
   linha, a externa, sobra.)
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
teko:     regression  r0  own_native.tkr[x86_64-windows row 3]: x86_64-w64-mingw32-gcc (the cross-linker for target x86_64-windows) not found on PATH
teko:     regression  r0  own_native.tkr[x86_64-windows row 7]: x86_64-w64-mingw32-gcc (the cross-linker for target x86_64-windows) not found on PATH
teko:
teko:   COVERAGE (4 shard dumps + 63 regression dumps merged, 0 missing):
teko:     functions   92% (4811/5219)    floor 90   ok
teko:     lines       88% (61233/69582)  floor 85   ok
teko:     branches    74% (18220/24486)  floor 70   ok
teko: ===== 1 failed, 2 skipped — run FAILED =====
```

Quatro coisas que este bloco faz e que hoje ninguem faz:

1. **Soma as duas fases.** A linha `TOTAL` nao existe em lado nenhum da arvore.
2. **Nomeia cada skip com a sua razao.** O `... (the cross-linker for target ...) not found on PATH` que
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
    teko::assert::str_contains(block, "own_native.tkr[x86_64-windows row 3]")
    teko::assert::str_contains(block, "not found on PATH")
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
2. ~~**Nao torna a fase unitaria tolerante a falhas.** O `panic` continua a matar a shard.~~
   **RISCADO PELO RULING DO DONO (§14).** Isto era o defeito, nao um limite. Com a captura o `panic`
   deixa de matar a shard, o tally deixa de ser reconstrucao e `never-ran` passa a 0 por construcao no
   caso auto-infligido. O que continua fora de alcance e apenas o **isolamento** entre testes
   (`teko::isolate`, S8) — um limite estreito, nao uma desculpa para deixar a suite morrer.
3. **Nao inclui a cobertura de uma shard cujo despejo falta.** Ela e reportada como `missing`, e a
   corrida falha (C4). Estimar o que falta seria inventar o numero que a fasquia julga.

---

## 14. A captura em modo teste — ruling do dono, 2026-07-30

> *"E PARA CAPTURAR E SAIR ELEGANTE ANTES SEM ENVIAR SYSCALL DE SAIDA QUANDO COMPILAR TESTES... Uma
> coisa e um aborto externo, de fora do programa, outra coisa e o proprio programa sair, e para sair
> ele tem que ser deterministico."*

**A critica e minha e e justa.** Escrevi em §11.2 que *"o `panic` continua a matar a shard"* e
tratei o fail-fast como dado. Nao e dado: **e o defeito**. O meu `C4b` — journalizar `plan`/`begin`
para poder calcular `never-ran` — existia inteiro para **reconstruir o que a morte levou**. A morte
nao devia acontecer.

### 14.1 A lei, e onde ela corta

| | quem manda | o que se faz |
|---|---|---|
| o **proprio programa** sai (`panic`, `exit`, assercao falhada) | nos | **captura-se**, deterministico, **sem syscall de saida** em modo teste |
| aborto **externo** (SIGKILL do OOM, SO, hardware, energia) | ninguem nosso | **convive-se** — o journal, o `--replay`, o varrimento |

O journal **nao deixa de fazer falta**: a segunda linha continua a existir e foi ela que nos matou
duas vezes hoje. O que muda e o **peso**: o journal deixa de ser a defesa principal e passa a ser a
defesa do que nao controlamos.

### 14.2 O mecanismo, e porque o salto vive no RUNTIME e nao no codigo emitido

A captura e um `setjmp`/`longjmp` — e **inteiro dentro de `teko_rt.c`**. O codigo emitido nunca
nomeia `setjmp`.

Isso nao e estetica. `setjmp` tem exigencias que nenhum gerador de codigo geral quer honrar (a moldura
que o chamou tem de continuar viva; locais modificados entre o `setjmp` e o `longjmp` tem de ser
`volatile`). Metendo-o no runtime, **o que o backend proprio tem de saber fazer reduz-se a uma coisa:
tomar o endereco de uma funcao de topo e passa-lo**. Isso e um `lea`/`adr` de um simbolo que o
backend ja emite para chamar. Nomeio-o como requisito da escada da morte do C para nao ser surpresa.

```c
// teko_rt.h — como um #test terminou. TRES resultados, nenhum deles um estado do processo.
#define TK_TEST_OK        0   /* o corpo retornou */
#define TK_TEST_PANICKED  1   /* panic / assercao falhada / panic implicito (div0, oob, cast) */
#define TK_TEST_EXITED    2   /* o corpo chamou exit(n); `code` traz o n */
typedef struct { int32_t how; int32_t code; } tk_test_end;

// tk_test_run — correr UM corpo de #test capturando a saida que ele proprio provoque.
//
// O SALTO VIVE AQUI, e e por isso que o codigo gerado nao precisa de saber que ele existe: o
// backend proprio so tem de saber tomar o endereco de `body`. `body` e `volatile` porque um
// parametro modificado (ou apenas mantido) entre o setjmp e o longjmp e indeterminado sem isso —
// e a unica exigencia do setjmp que nao se pode delegar.
tk_test_end tk_test_run(void (* volatile body)(void));
```

```c
// teko_rt.c
static jmp_buf tk_test_jb;
static volatile sig_atomic_t tk_test_capturing = 0;
static volatile int32_t tk_test_how = TK_TEST_OK, tk_test_code = 0;

tk_test_end tk_test_run(void (* volatile body)(void)) {
    tk_test_how = TK_TEST_OK; tk_test_code = 0;
    tk_test_capturing = 1;
    if (setjmp(tk_test_jb) == 0) { body(); }
    tk_test_capturing = 0;
    tk_test_end e; e.how = (int32_t)tk_test_how; e.code = (int32_t)tk_test_code; return e;
}
```

E os **dois** pontos de estrangulamento, que ja existiam e nao ganham chamadores novos:

```c
_Noreturn void tk_panic_str(tk_str msg) {
    if (tk_test_capturing) {                       // MODO TESTE: para o teste, nao para o processo
        tk_test_note(TK_PANIC_MARKER, msg);        // no canal DESTE teste, nunca no fluxo partilhado
        tk_test_how = TK_TEST_PANICKED;
        tk_test_capturing = 0;
        longjmp(tk_test_jb, 1);
    }
    fputs(TK_PANIC_MARKER, stderr); fwrite(msg.ptr, 1, msg.len, stderr); fputc('\n', stderr);
    tk_backtrace();
    tk_regions_free_all();                         // (W9.3b) abort() salta o atexit
    abort();
}

_Noreturn void tk_exit(int32_t code) {
    if (tk_test_capturing) {
        tk_test_how = TK_TEST_EXITED; tk_test_code = code;
        tk_test_capturing = 0;
        longjmp(tk_test_jb, 1);
    }
    tk_regions_free_all(); exit(tk_exit_status(code));
}
```

Cinco propriedades, e cada uma foi verificada na arvore antes de eu a escrever:

1. **Nenhuma assinatura muda.** `_Noreturn` continua honesto: `longjmp` nao retorna. Zero chamadores
   tocados, zero mudanca no codegen dos sitios de chamada.
2. **Cobre os panics implicitos.** `tk_panic_div0`, `tk_panic_oob` e `tk_panic_cast`
   (`teko_rt.c:1890-1900`) desaguam todos em `tk_panic`, e as assercoes desaguam em `tk_assert_fail`
   -> `tk_panic` (`src/assert/assert.c:57,69`). Um so ponto, ja partilhado.
3. **Nao muda a semantica de `defer`.** Medido, e ao contrario do que eu supunha: o replay dos
   `defer` acontece **no sitio de chamada, ANTES** da chamada divergente — e o que o cenario
   `defer_cascade_exit` prova (`regressor.tkr:228`: *"its own argument is read AFTER the replay, so
   the exit code itself is the LIFO proof"*), e `defer_not_duplicated_on_diverging_call` guarda o
   outro lado. Quando o controlo chega a `tk_exit`/`tk_panic_str`, os `defer` **ja correram**. O
   `longjmp` nao salta nenhum. O limite residual documentado (um panic IMPLICITO nao tem sitio de
   chamada, logo o seu `defer` nao corre — *contrato, nao lacuna*) mantem-se identico.
4. **A arena passa a rebobinar de verdade.** `tk_arena_push`/`tk_arena_pop` envolvem cada teste
   (`emit_test_call`); hoje um teste que panica **nunca chega ao pop** porque o processo morre. Com
   a captura o pop e sempre alcancado.
5. **A mensagem passa a ser atribuida ao teste certo.** O comentario de `tk_flush_out`
   (`teko_rt.c:1610-1620`) narra uma investigacao inteira perdida por isto: *"an abort was attributed
   to a spine test ~66 tests before the real one"*. Sob captura a mensagem entra no canal **deste**
   teste. A ma atribuicao deixa de ser possivel.

### 14.3 A superficie em Teko

```teko
/**
 * TestHow — como um `#test` terminou. Um resultado do TESTE, nunca um estado do processo.
 *
 * @since 0.3.1
 */
pub type TestHow = enum { Ok; Panicked; Exited }

/**
 * TestEnd — o fim de um `#test`: como acabou e, quando `Exited`, com que valor.
 *
 * O VALOR VIVE AQUI E NAO NO PROCESSO, e e essa a diferenca inteira. `exit(7)` dentro de um `#test`
 * e um FACTO SOBRE O TESTE que o arnes relata e continua; `exit(7)` num programa e o contrato do
 * programa com quem o invocou, e esse fica intocado (§14.5).
 *
 * @since 0.3.1
 */
pub type TestEnd = struct {
    /** como o corpo terminou. */
    how: TestHow
    /** o valor de `exit(n)` quando `how` e `Exited`; 0 nos outros casos. */
    code: i32
}

/**
 * run_capturing — correr `body` capturando qualquer saida que ele proprio provoque, e devolver o
 * controlo ao arnes SEM syscall de saida.
 *
 * DETERMINISTICO NOS TRES EIXOS que o dono exigiu: quem captura sabe QUAL teste terminou (o arnes
 * chamou-o e nao largou o rotulo), COMO terminou (`TestHow`) e, no caso de `exit`, COM QUE VALOR.
 * Nao ha corrida, nao ha sinal, nao ha ordem por descobrir: e um retorno de funcao.
 *
 * @param body  o corpo do `#test`, passado por endereco
 * @return      como o corpo terminou
 * @since 0.3.1
 */
pub extern fn run_capturing(body: cabi fn()) -> TestEnd = "tk_test_run" from "teko_rt"
```

### 14.4 O arnes, e o que ele deixa de precisar

`emit_test_call` (`src/codegen/codegen.tks:12079`) passa de

```c
tk_arena_push(); tk_print("test <label> ... "); tk_flush_out(); <fn>(); tk_println("ok"); tk_arena_pop();
```

para

```c
tk_arena_push(); tk_test_begin("<label>");
tk_test_report(tk_test_run(&<fn>));      /* imprime o veredicto E soma o tally E journaliza */
tk_test_end(); tk_arena_pop();
```

e `emit_test_main` (`:11990`) deixa de terminar em `return 0;` cego:

```c
tk_cov_branches_on(false); tk_cov_lines_on(false);
{ const char *dp = getenv("TEKO_TKCOV"); if (dp) tk_cov_dump(dp); }
tk_test_summary();                       /* o bloco de §13, agora natural */
return tk_test_any_failed() ? 1 : 0;
```

**E aqui e que o adendo de §13 se reordena inteiro:**

* o **tally deixa de ser reconstrucao e passa a ser um contador**. O arnes chega sempre ao fim, logo
  `passed`/`failed`/`exited` sao somas triviais;
* **`never-ran` passa a ser 0 por construcao** no caso auto-infligido. Continua a existir como coluna,
  mas so a classe EXTERNA a pode tornar nao-zero — que e exactamente onde ela devia ter estado;
* o **`.tkcov` passa a ser despejado sempre**. Hoje uma shard que panica nunca chega ao `tk_cov_dump`
  no fim do `main`. Era metade do defeito que o C4 tinha de DETECTAR; com a captura, um despejo em
  falta so pode vir de morte externa — e o C4 continua a detecta-lo, agora sobre um caso raro em vez
  de sobre o caso comum;
* o **`C4b` dissolve-se**. Ele existia para reconstruir o que a morte levou. Os registos por teste
  continuam a ser escritos (valem para o `--replay` depois de morte externa), mas deixam de ser a
  fonte do tally — passam a ser confirmacao, e o crumb encolhe para dentro do C4.

### 14.5 O que NAO e capturado, e a fronteira e estrutural e nao um interruptor

A captura e de **modo teste**, palavra do dono: *"quando compilar testes"*. E o modo ja existe:
`CgMode = enum { Program; TestPlain; TestCov; TestAnalyze; ProgramCov }`
(`src/codegen/codegen.tks:59`). `tk_test_run` e emitido **so** pelos tres perfis `Test*`; `Program` e
`ProgramCov` nunca o emitem, logo **nunca poem `tk_test_capturing` a 1**, logo `tk_panic` e `tk_exit`
comportam-se byte a byte como hoje.

**A fronteira e estrutural: nao ha bandeira que a possa desligar por engano, porque a unica coisa que
liga a captura e codigo que so o modo teste emite.**

Por isso as filas cujo contrato **E** o codigo de saida continuam intactas: `Given source` +
`When built and run` + `Then exit = N` compila em modo `Program`. Os alvos cruzados 42/210/7 e o par
oraculo do degrau 30 (cinco filas, medidas pelo integrador linha a linha) medem o observavel que
existem para medir, e capturar ali apagava-o. `defer_cascade_exit` (`Then exit = 235`,
`regressor.tkr:228,234`) e o caso mais afiado disto — o codigo de saida **e** a prova LIFO — e
continua a sair pela syscall.

### 14.6 A guarda, e a inversao por reversao

Tres bracos, e o segundo e o que o dono pediu por nome.

```teko
#test
/**
 * tc_a_test_that_exits_is_reported_and_the_suite_survives — o caso que hoje mata a suite.
 *
 * `exit(7)` no meio de um corpo tem de voltar como `Exited { code = 7 }`, sem syscall, e o arnes tem
 * de continuar. Sem o mecanismo este `#test` nao FALHA: o processo desaparece e nenhum dos seguintes
 * chega a ser reportado — que e a diferenca entre um teste vermelho e uma suite muda.
 *
 * @throws quando a saida nao volta capturada com o seu valor
 */
fn tc_a_test_that_exits_is_reported_and_the_suite_survives() {
    let e = teko::test::run_capturing(tc_body_that_exits_7)
    teko::assert::is_true(e.how == TestHow::Exited)
    teko::assert::eq_i64(e.code to i64, 7)
}

#test
/**
 * tc_the_suite_reached_this_test — A PROVA DE QUE A SUITE SOBREVIVEU, e ela tem de ser um teste
 * SEPARADO e POSTERIOR.
 *
 * Um teste nao pode afirmar credivelmente que a suite lhe sobreviveu; so o SEGUINTE o pode. Se a
 * captura regredir, este nunca e reportado — e um teste que nao aparece no sumario e uma falha que o
 * §13 nomeia (`never-ran`), nunca um verde.
 */
fn tc_the_suite_reached_this_test() {
    teko::assert::is_true(tc_previous_test_was_recorded())
}

#test
/**
 * tc_a_panicking_body_comes_back_too — o outro estrangulamento, incluindo o panic IMPLICITO.
 *
 * @throws quando um panic explicito ou uma divisao por zero nao voltam como `Panicked`
 */
fn tc_a_panicking_body_comes_back_too() {
    teko::assert::is_true(teko::test::run_capturing(tc_body_that_panics).how == TestHow::Panicked)
    teko::assert::is_true(teko::test::run_capturing(tc_body_that_divides_by_zero).how == TestHow::Panicked)
}
```

**A inversao e por REVERSAO, e sem interruptor** — o mesmo padrao que fechou o degrau 29. Um botao
`TEKO_TEST_CAPTURE=0` seria uma maneira nova de partir a arvore; a reversao honesta ja existe na
linguagem, e chama-se modo `Program`. Duas filas no canal **existente** `own_native`, sobre a **mesma
fonte**:

```
  Scenario: capture_off_in_program_mode_still_exits_for_real
    Given source = "cases/capture_exit_7.tks"
    When built and run
    Then exit = 7

  Scenario: capture_on_in_test_mode_reports_and_continues
    Given source = "cases/capture_exit_7.tks"
    Given args = ["--as-test"]
    When built and run
    Then stdout pattern = "scenario capture_on_in_test_mode_reports_and_continues: ok"
```

A primeira fila **e** a inversao: prova que o mecanismo esta desligado fora do modo teste — logo, que
ele esta LIGADO dentro dele por decisao e nao por acidente, e que a lei de §14.5 e observavel. A
segunda prova o relato e a continuacao. **A guarda pode falhar nos dois sentidos**, que e a unica
forma de nao ser decoracao.

### 14.7 O crumb, e ele vai a frente

**C0 — a captura.** `tk_test_run` + os dois estrangulamentos + `tk_test_report`/`tk_test_summary`/
`tk_test_any_failed` em `teko_rt.{c,h}`; `emit_test_call`/`emit_test_main` em
`src/codegen/codegen.tks`; `teko::test::run_capturing` em `src/test/test.tks`; a guarda e as duas
filas de reversao.

**Vai antes de tudo o resto**, e nao so porque o dono disse *"imediatamente, a dor e latente"*: vai
antes porque **encolhe os crumbs que vem depois**. O C4b dissolve-se no C4, o `never-ran` deixa de
precisar de reconstrucao, o `.tkcov` passa a ser despejado sempre e o C4 deixa de perseguir o caso
comum. **Total: 10 -> 10 crumbs.** O adendo de §13 tinha custado um; o ruling do dono devolve-o.

Ordem final: **C0** · C1 · C2 · C3 · C4 (com §13 e o antigo C4b dentro) · C5 · C6 · C7 · C8 · C9.

**Ponto de ritual novo, e obrigatorio: depois do C0.** Ele muda o arnes de **todos** os 1167 `#test`.

### 14.8 O que a captura NAO cobre

1. **Aborto externo.** `SIGKILL`, o OOM killer, um ecra azul, falta de energia. Palavra do dono:
   *"o software nao tem que lidar com isso, tem que conviver e fazer o melhor para se proteger"* — e
   e para isso que o journal (C1–C9) continua a existir, agora no seu lugar certo.
2. **Sinal de crash dentro de um teste** (SIGSEGV por memoria `unsafe`). `tk_rt_crash_handler` corre
   em contexto de sinal e um `longjmp` de la e formalmente indefinido. Fica como aborto externo: o
   manipulador journaliza (C5) e o processo morre. E a fronteira certa — um SIGSEGV nao e o programa
   a sair, e o programa partido.
3. **Recursos nao-memoria que um teste que panica tenha aberto** (descritores, ficheiros). A arena
   rebobina (§14.2, ponto 4); descritores nao. Nomeado como limite conhecido, nao escondido.
4. **Isolamento entre testes.** A captura faz a suite CONTINUAR; nao faz cada teste correr numa
   moldura sua. Estado global que um teste corrompeu antes de panicar continua corrompido para o
   seguinte — isso e `teko::isolate` (S8), e continua a nao existir. A diferenca em relacao ao que eu
   escrevi antes e que agora isto e um limite estreito e nomeado, em vez da desculpa para deixar a
   suite morrer.

---

## 15. O tunel e um TUBO, e o laco que o drena — directiva do dono, 2026-07-30

> *"Para processos eu ja falei, e direcionar um stdout e stderr virtual para o processo, por isso um
> orquestrador, para ler estes canais e apendar. Algo burro mas extremamente eficiente e eficaz."*
>
> *"Para o caso de 'espera' e `loop {}`, sempre lendo a saida enquanto ela estiver aberta... ha muita
> coisa simples e ja ao nosso alcance que resolve o paradigma sem criar um monstro."*

### 15.1 O tunel ja existe — e esta feito de FICHEIROS. Parte do §3 nao migra: DESAPARECE.

`spawn_redirected(argv, dir, env, in_path, out_path, err_path)` recebe **caminhos**. Medido em
`teko_rt.c:2336-2350`: o PAI abre os tres com `open(..., O_WRONLY|O_CREAT|O_TRUNC)`, faz `fork`, e o
FILHO faz `dup2` deles sobre os seus proprios fluxos antes do `execvp`.

Dai vem uma fatia inteira do inventario de §3: o trio `.in`/`.out`/`.err` **por filho lancado**. Ele
nasce num sitio so — `spawn_spec` (`regression.tks:182-186`) — e por isso **morre num sitio so**.

**Correccao ao §3, e e uma subtraccao e nao uma migracao:** com tubos, o par `.out`/`.err` de cada
filho **deixa de existir**. Nao ganha carimbo, nao ganha raiz, nao ganha nada — o **descritor E o
segmento**. Isso apaga o par em todas as familias de captura de §3.2/§3.3 de uma vez (as seis
`.regr-pool-*` — as 56 entradas que o integrador contou —, as duas `pt-probe`, as duas
`c4_test_probe`, `.toolquery`, `.compile`, `.run`, `.dep_compile`, `.wellformed`, e a base de shard).

**O `.in` FICA, e e escolha e nao esquecimento.** Um tubo de stdin traz a segunda direccao do
impasse de §15.3: o pai a escrever mais do que o buffer para um filho que ainda nao le. O
`stdin_data` e conhecido de antemao e escreve-se uma vez; um ficheiro nunca bloqueia. Trocar dois
ficheiros por filho e ficar com uma direccao de impasse em vez de duas e o negocio certo, e e
exactamente o *"algo burro mas extremamente eficiente"*.

O `.chan` tambem fica: e o TERCEIRO canal, e ha exactamente tres fluxos herdaveis em POSIX e em
Windows. Ele e o segmento de journal (§2.2) e sempre foi essa a sua razao.

### 15.2 A peca que falta e UMA: `pipe`

Medido: `fork` e `dup2` estao la (`teko_rt.c:2344-2350`), `_dup2` do lado Windows (`:2253`).
**`pipe` nao existe em lado nenhum do runtime.** E a unica peca nova, mais `CreatePipe` do outro lado.

```c
// tk_rt_spawn_piped — como tk_rt_spawn_redirected, mas para cada fluxo cujo caminho venha VAZIO
// cria um tubo em vez de abrir um ficheiro. Devolve o pid; os descritores do lado do PAI ficam na
// tabela de filhos vivos, lidos por tk_rt_child_fd.
//
// A TABELA E PROCESSO-LOCAL E E O ORQUESTRADOR QUE O DONO DESCREVE. Ela e o unico estado partilhado
// que isto acrescenta, e quando as raias chegarem protege-se com UM mutex — exclusao DENTRO do
// processo, segmento por escritor FORA dele, que e a regra que este documento ja segue.
int64_t tk_rt_spawn_piped(tk_str payload);
// tk_rt_child_fd — o descritor de leitura do fluxo `which` (1 = stdout, 2 = stderr) do filho `pid`.
int64_t tk_rt_child_fd(int64_t pid, int32_t which);
// tk_rt_wait_readable — bloquear ate que ALGUM dos `n` descritores tenha bytes ou EOF, ou ate
// `timeout_ms`. Devolve o indice do primeiro pronto, -1 no timeout. poll(2) / WaitForMultipleObjects.
//
// ESTE E O `loop {}` DO DONO, e e o que torna a espera uma LEITURA em vez de uma paragem.
int64_t tk_rt_wait_readable(tk_str fds_packed, int32_t n, int32_t timeout_ms);
// tk_rt_read_fd — ler ate `cap` bytes de `fd`. 0 = EOF, negativo = erro.
int64_t tk_rt_read_fd(int64_t fd, tk_str into, int64_t cap);
```

### 15.3 O PERIGO, e e o unico, e trocar ficheiros por tubos INTRODU-LO

Um ficheiro nunca bloqueia quem escreve. **Um tubo bloqueia.** O buffer e finito (tipicamente 64 KiB
no Linux), e os nossos filhos imprimem muito: um `teko test` de shard despeja a transcricao inteira.

Portanto: **um filho que encha o tubo PARA. Um pai parado no `wait_one` nunca le. Impasse.**

Isto **nao pode acontecer hoje** — os destinos sao ficheiros. **A troca para tubos e que o cria**, e
digo-o assim em vez de vender o tubo como um ganho sem contrapartida: o tubo troca dois ficheiros por
filho por uma **disciplina de drenagem obrigatoria**. A disciplina e uma linha de invariante:

> **NUNCA `wait_one` antes de EOF nos dois tubos do filho.** Espera-se a LEITURA, nunca o processo.

```teko
/**
 * pump — o laco do orquestrador: drenar TODOS os filhos vivos enquanto algum tubo estiver aberto, e
 * so depois recolher os estados.
 *
 * A ORDEM E O INVARIANTE INTEIRO, e ela e ao contrario do que parece natural: NAO se espera o filho
 * e depois se le o que ele deixou. Espera-se pela LEITURA. Um filho que encha o buffer do tubo fica
 * bloqueado no seu `write`, e um pai parado em `wait_one` nunca chega a esvaziar o buffer que o
 * desbloquearia — impasse dos dois lados, sem nada que o resolva.
 *
 * E O MESMO LACO QUE DA A AGREGACAO INCREMENTAL: cada bloco lido e apendado ao segmento do seu
 * escritor no momento em que chega, logo uma morte externa do orquestrador deixa em disco tudo o que
 * chegou ate ali. Uma peca, dois problemas.
 *
 * @param hs     os filhos em voo
 * @param sinks  o segmento de journal de cada filho, na mesma ordem
 * @param deadline_ms  quanto tempo o laco pode ficar sem progresso antes de desistir; 0 = sem limite
 * @return       o estado de saida de cada filho, em ordem de indice (nunca de chegada)
 */
pub fn pump(hs: []ProcHandle, sinks: []Journal, deadline_ms: i32) -> []i32
```

### 15.4 O `ProcHandle` ganha os descritores e o evento de fecho

*"Melhorar a estrutura do `Process` com evento de fecha"* e **estender uma struct**, nao inventar um
paradigma:

```teko
/**
 * ProcHandle — o filho em voo: a referencia do SO, os tubos por onde ele fala, e se ja se calou.
 *
 * @since 0.3.1.2
 */
pub type ProcHandle = struct {
    /** a referencia opaca do host (pid em POSIX, um HANDLE em Windows). */
    raw: i64
    /** o descritor de leitura do stdout do filho; -1 quando esse fluxo foi para um ficheiro. */
    out: i64
    /** o descritor de leitura do stderr do filho; -1 quando foi para um ficheiro. */
    err: i64
    /**
     * O EVENTO DE FECHO: verdadeiro quando os dois tubos deram EOF.
     *
     * E ESTA a condicao que autoriza o `wait_one`, e nao o inverso. Enquanto for falso, `wait_one`
     * pode ficar a espera de um filho que esta ele proprio a espera do pai (§15.3).
     */
    closed: bool
}
```

`wait_one` mantem a assinatura e ganha uma pre-condicao documentada. Nenhum chamador de hoje quebra:
um `ProcHandle` com `out = -1` e `err = -1` e exactamente o comportamento actual.

### 15.5 A prova por impasse forcado

Nao provo com um filho que imprime pouco — isso passa nas duas ordens. Provo com um filho que
**enche o buffer**, e o braco "antes" tem de bater no prazo.

```teko
#test
/**
 * pp_a_child_that_fills_the_pipe_is_drained — o filho escreve 200 000 bytes, mais do triplo do
 * buffer tipico de 64 KiB, e o laco recupera-os INTEIROS.
 *
 * Um filho que imprima pouco passa nas duas ordens e nao prova nada; e por isso que este escreve mais
 * do que o buffer. A afirmacao e sobre o TAMANHO recuperado: um impasse trunca, um laco correcto nao.
 *
 * @throws quando algum byte se perde ou o laco nao termina dentro do prazo
 */
fn pp_a_child_that_fills_the_pipe_is_drained() {
    let seg = pp_segment_of(pp_run_drained(pp_child_writing(200000), 5000))
    teko::assert::eq_u64(seg.len, 200000)
}

#test
/**
 * pp_waiting_before_draining_deadlocks — A INVERSAO, e ela tem de PODER falhar e nao pode PENDURAR.
 *
 * A ordem errada — `wait_one` primeiro, ler depois — nao "corre mais devagar": nao termina. Um teste
 * que pendura nao e um teste, logo a inversao corre com PRAZO e afirma que o prazo foi atingido. Sem
 * este braco a guarda nao prova nada, e tres guardas cegas ja nos deram ZERO hoje.
 *
 * @throws quando a ordem errada TERMINA — o que significaria que esta prova deixou de provar
 */
fn pp_waiting_before_draining_deadlocks() {
    teko::assert::is_true(pp_run_wait_first(pp_child_writing(200000), 5000).timed_out)
    teko::assert::is_false(pp_run_drained(pp_child_writing(200000), 5000).timed_out)
}
```

### 15.6 O `verdict_emit`: o destino passa a PARAMETRO, e o desviante e o ficheiro

O integrador tem razao na leitura: os dois destinos ja la estao, mas como um `or` — e **o ramo do
`stderr` ja apenda** (`eprintln` escreve num fluxo). O desviante e o ramo do ficheiro, que le tudo e
reescreve tudo (`process.tks:215-220`). A correccao nao e acrescentar um destino: e **pôr o ramo do
ficheiro a apendar como o do `stderr` ja apenda**, e entao o destino deixa de ser um `or` e passa a
ser um argumento.

```teko
/**
 * Sink — para onde um registo vai. Um PARAMETRO, nunca um `or` decidido dentro do emissor.
 *
 * @since 0.3.1
 */
pub type Sink = enum { Stderr; Segment }

/**
 * emit_to — apendar UM registo ao destino nomeado.
 *
 * OS DOIS RAMOS APENDAM, que e o que o `or` de hoje ja fazia de um lado e nao do outro. O destino ser
 * argumento e o que permite ao orquestrador escrever no segmento do FILHO que acabou de ler, em vez
 * de cada processo adivinhar sozinho onde escreve.
 *
 * @param sink  para onde
 * @param seg   o segmento, quando `sink` e `Segment`
 * @param line  o registo, sem quebra de linha
 * @throws      quando a escrita falha (§4, modo 5 — a falha e pegajosa)
 */
pub fn emit_to(sink: Sink, seg: Journal, line: str) -> null | error
```

### 15.7 `chan<T>`: o primeiro consumidor real, registado e NAO construido

> **RECTIFICADO POR §16 (correccao do dono, 2026-07-30).** O requisito 1 desta tabela esta
> **REFUTADO** — eu desenhei um laco central a esperar em `2 x jobs` origens, e a forma e FAN-IN: um
> tubo por constructo, um handler por filho, e o orquestrador com um `recv` so. A tabela fica com a
> linha riscada para o erro ser legivel; a lista corrigida esta em §16.4.

`docs/design/concorrencia-adiantada-s8.md` §3.3 reservou `chan<T>` e recusou congela-lo com uma razao
explicita: *"Congelar `chan<T>` agora seria congelar a peca que os casos reais nao usam."*

**Agora ha um caso real, e e o `pump`.** Construo-o com tubo e descritor, **nao** com `chan<T>` —
usar `chan<T>` hoje obrigava a construir a primitiva primeiro, e isso e o monstro que o dono nao
quer. Mas registo aqui **a forma que o uso mediu**, que e a evidencia que o S8 pediu antes de
congelar:

| requisito medido no `pump` | porque, e o que reprova |
|---|---|
| ~~**espera multi-origem**~~ **REFUTADO (§16)** | eu escrevi que um `recv` de uma origem so nao exprimia o caso. Exprime — a multiplicidade vive nos HANDLERS, cada um bloqueado no SEU tubo, e o orquestrador tem uma origem so. A peca que eu dava como o requisito dificil **nao existe**, e com ela cai o `poll(2)`/`WaitForMultipleObjects` e a assimetria do Windows que eu declarara por medir |
| **fecho distinguivel de vazio** | `recv` tem de responder "fechado" diferente de "nada agora", porque e o fecho que autoriza o `wait_one` (§15.4). `-> T \| closed` |
| **limitado, com contrapressao** | o tubo do SO da-a de graca: quem escreve bloqueia. Um `chan<T>` ilimitado tem a memoria como unico limite — e nos morremos de OOM duas vezes hoje. **Limitado, por omissao** |
| **ordem por origem, nao global** | o relatorio e por indice de filho (§determinismo do `run_pool`). O canal so tem de preservar a ordem DENTRO de cada origem; uma ordem global seria uma promessa mais cara e inutil |

Quatro requisitos vindos de necessidade, nenhum de antecipacao. **Nao construo nenhum aqui.**

### 15.8 O custo, e o que isto muda nos crumbs

**Nao acrescenta crumb.** O tubo e o laco sao a implementacao do **C1** (o sumidouro do escritor) e do
**C4** (a releitura, que passa a ser drenagem incremental). O que muda e o CONTEUDO:

* **C1** ganha `tk_rt_spawn_piped`/`tk_rt_child_fd`/`tk_rt_wait_readable`/`tk_rt_read_fd`, o
  `ProcHandle` estendido e o `emit_to`;
* **C3** ENCOLHE: o par `.out`/`.err` de cada familia de captura nao migra — some;
* **C7** ganha a prova por impasse forcado de §15.5;
* **C0** fica como esta.

**Total: 10 crumbs, sem alteracao.**

### 15.9 O que isto NAO cobre

1. **O `stdin` continua ficheiro**, por decisao (§15.1) — uma direccao de impasse em vez de duas.
2. **Um filho que feche stdout/stderr e continue a correr** da EOF sem ter saido. O laco entao espera
   no `wait_one`, correctamente, e sem nada por ler. Nao e impasse; e um filho lento.
3. **`chan<T>` nao e construido aqui**, so medido (§15.7).
4. **Windows nao esta medido.** `CreatePipe` + `WaitForMultipleObjects` sao a forma equivalente e
   estao declarados, mas eu nao os corri. A perna Windows tem de trazer a sua propria medicao — e
   dizer que "deve funcionar igual" seria a suposicao que este documento inteiro existe para nao
   fazer.

---

## 16. FAN-IN: um tubo por constructo — correccao do dono, 2026-07-30

> *"Nao e 'Um' tubo, mas um por constructo, e isso e o pq de ter um orquestrador e operar com
> `chan<>`, cada processo recebe um handler que le a saida e grava no canal, o orquestrador le do
> canal e apenda no arquivo."*

**O erro e meu e e de forma.** A §15.3 desenhou um **laco central** a esperar em `2 x jobs` origens
com `poll(2)`. A forma e **fan-in**:

```
filho 1 ──tubo──▶ handler 1 ─┐
filho 2 ──tubo──▶ handler 2 ─┼──▶ chan ──▶ orquestrador ──▶ apenda no segmento
filho N ──tubo──▶ handler N ─┘
```

Um tubo **por constructo**, um handler **por filho** que drena o SEU tubo e grava no canal, e o
orquestrador com **um `recv` so**.

### 16.1 O que a correccao apaga do meu desenho

| eu tinha | a forma do dono |
|---|---|
| `tk_rt_wait_readable` (`poll(2)` / `WaitForMultipleObjects`) | **nao existe.** Cada handler faz um `read(2)` bloqueante no seu tubo |
| um laco central com `2 x jobs` descritores | **um `recv`.** A multiplicidade vive nos produtores |
| "Windows precisa de forma equivalente e nao esta medido" | **desaparece.** `ReadFile` bloqueante e simetrico com `read(2)`; nao ha `WaitForMultipleObjects` |
| o invariante da drenagem no laco central | **no handler.** Mesmo invariante, outra casa |

**A parte que eu dava como o requisito dificil evapora-se.** Isso e o sinal de que a forma esta certa:
a que eu propus precisava da primitiva mais assimetrica entre os dois SO, e esta nao precisa dela.

### 16.2 A DEPENDENCIA, dita e nao contornada

**A forma do dono exige `spawn` e `chan<T>`. Nenhuma das duas existe.** Sao duas das cinco primitivas
RESERVADAS de `docs/design/concorrencia-adiantada-s8.md` §3.3, e nao ha por onde fugir: um handler que
bloqueia num `read` precisa de **contexto de execucao proprio**, e um processo por handler seria
absurdo (dobrava a contagem de processos para drenar tubos).

E o S8 nomeia o que trava a primitiva GERAL, medido: `tk_arena_push(void)` / `tk_arena_pop(void)`
(`teko_rt.h:156-157`) **nao recebem parametro** — empilham marcas numa pilha global sobre a regiao
raiz do processo. Duas raias a faze-lo corrompem-se. O pre-requisito real da S8 e **regiao raiz por
tarefa**, e o proprio S8 classifica-o: *"e caro e toca a disciplina de memoria inteira"*.

### 16.3 Porque esse bloqueio NAO trava ESTE caso — e e a medicao que o desbloqueia

**O corpo de um handler de dreno nao aloca da arena.** Ele le bytes de um descritor e empurra-os para
um canal. Verificado: `tk_str_concat` e a familia de strings usam **`malloc`**, nao a arena
(`teko_rt.c:139`; o proprio comentario de observabilidade chama-lhes *"dark matter ... outside the
arena"*), e `malloc` e seguro em threads nas duas libc que usamos.

Portanto o **minimo viavel** nao e a primitiva geral. E isto:

> **O corpo do handler vive no RUNTIME, em C, e nunca chama codigo Teko.** Zero risco de arena, zero
> mudanca no codegen, zero mudanca no verificador.

```c
// tk_chan — a fila MPSC de REGISTOS que o fan-in precisa: N produtores, UM consumidor, limitada.
//
// REGISTOS E NAO UM FLUXO DE BYTES, e esta e a descoberta desta forma: se o canal transportasse
// bytes, dois filhos entrelacavam-se num fluxo so e a ATRIBUICAO perdia-se — que e exactamente o
// defeito que este documento inteiro existe para fechar, reencarnado uma camada acima. Cada registo
// traz o seu escritor.
typedef struct tk_chan tk_chan;
// tk_chan_open — abrir um canal com `cap_bytes` de folga. LIMITADO por lei (§15.7): um canal sem
// limite tem a memoria como unico travao, e nos morremos de OOM duas vezes no dia deste desenho.
tk_chan *tk_chan_open(uint64_t cap_bytes, uint32_t producers);
// tk_chan_recv — bloquear ate haver um registo. Devolve 0 no registo, 1 quando TODOS os produtores
// fecharam. A distincao entre "vazio agora" e "fechado" e o que autoriza o wait_one (§15.4).
int32_t tk_chan_recv(tk_chan *c, tk_str *writer_out, tk_str *bytes_out);
// tk_rt_spawn_drainer — um handler: uma thread cujo corpo e ESTA funcao C — `read(fd)` ate EOF,
// empurrando cada bloco para `c` com o rotulo `writer`, e no fim baixa a contagem de produtores.
//
// NAO E `teko::isolate::spawn`. Nao corre codigo Teko, nao toca na arena, nao tem valor de retorno
// que o mundo seguro veja. E o fundo sobre o qual `spawn` VAI assentar, e nao a primitiva.
int32_t tk_rt_spawn_drainer(int64_t fd, tk_chan *c, tk_str writer);
```

O custo, em linhas de C mantido, e para ser comparado com o que ja ha:

| peca | POSIX | Windows | forma |
|---|---|---|---|
| `tk_chan` (anel + mutex + duas variaveis de condicao + contagem de produtores) | ~120 | ~120 | `pthread_mutex`/`pthread_cond` · `CRITICAL_SECTION`/`CONDITION_VARIABLE` |
| `tk_rt_spawn_drainer` | ~40 | ~40 | `pthread_create` · `CreateThread` |
| `tk_rt_spawn_piped` + `tk_rt_child_fd` (§15.2) | ~50 | ~50 | `pipe` · `CreatePipe` |

**~210 linhas de C mantido, simetricas entre os dois SO** — contra *"caro e toca a disciplina de
memoria inteira"* da regiao raiz por tarefa, que este caminho **nao precisa de tocar**.

### 16.4 A superficie em Teko, e o orquestrador com UM `recv`

```teko
/**
 * Chan — a fila de registos por onde os handlers falam com o orquestrador.
 *
 * @since 0.3.1
 */
pub type Chan = struct {
    /** a referencia opaca do runtime. */
    raw: i64
}

/**
 * ChanMsg — um registo tirado do canal: de QUEM veio e o que ele disse.
 *
 * O ROTULO VIAJA COM OS BYTES, nunca ao lado. Um canal de bytes puros entrelacava dois filhos num
 * fluxo so e perdia a atribuicao — o mesmo defeito deste documento, uma camada acima.
 *
 * @since 0.3.1
 */
pub type ChanMsg = struct {
    /** a identidade do escritor (`s2`, `r0`) — a mesma chave do segmento de journal (§2.2). */
    writer: str
    /** o bloco de bytes que o handler leu, na ordem em que o leu. */
    bytes: str
}

/**
 * drain_into — pôr um handler a drenar cada tubo de `h` para `c`.
 *
 * UM POR CONSTRUCTO, e e por isso que isto recebe UM `ProcHandle` e nao a lista: a multiplicidade
 * vive aqui, num handler por fluxo, e nao no consumidor.
 *
 * @param h       o filho cujos tubos vao ser drenados
 * @param c       o canal para onde os blocos vao
 * @param writer  a identidade com que os blocos deste filho sao rotulados
 * @throws        quando o handler nao pode ser criado (limite de threads)
 * @since 0.3.1
 */
pub fn drain_into(h: ProcHandle, c: Chan, writer: str) -> null | error

/**
 * recv — tirar o proximo registo do canal, bloqueando ate haver um.
 *
 * UMA ORIGEM SO, e e a rectificacao de §15.7: eu tinha escrito que o caso exigia espera
 * multi-origem. Nao exige. Cada handler bloqueia no SEU tubo; aqui chega um `recv` — e com ele cai o
 * `poll(2)`, cai o `WaitForMultipleObjects` e cai a assimetria de Windows que eu declarara por medir.
 *
 * @param c  o canal
 * @return   o proximo registo, ou `closed` quando TODOS os produtores fecharam
 * @since 0.3.1
 */
pub fn recv(c: Chan) -> ChanMsg | closed
```

E o orquestrador inteiro, que e o ponto do *"algo burro mas extremamente eficiente"*:

```teko
/**
 * orchestrate — o consumidor: um `recv`, um `append`, ate todos os produtores fecharem.
 *
 * O INVARIANTE DA DRENAGEM MUDOU DE CASA, NAO DESAPARECEU (§15.3). Ele vive agora no handler, que le
 * o seu tubo ate EOF e so entao baixa a contagem. Aqui a regra e a irma dele, e e a UNICA coisa que
 * este laco tem de respeitar:
 *
 *   **o orquestrador nunca bloqueia em nada que nao seja `recv`.**
 *
 * Se ele parasse num `wait_one` a meio, os handlers encheriam o canal limitado e parariam — o mesmo
 * impasse de §15.3, uma camada acima. Por isso o `wait_one` de cada filho corre DEPOIS de `closed`,
 * e nesse ponto o filho ja fechou os seus fluxos: a espera passa a ser limitada pela saida dele, e
 * nunca pela leitura do pai.
 *
 * @param c      o canal onde todos os handlers falam
 * @param sinks  o segmento de journal de cada escritor
 * @return       quantos registos foram apendados
 */
fn orchestrate(c: Chan, sinks: []Journal) -> u64 {
    mut n: u64 = 0
    loop {
        match recv(c) {
            ChanMsg as m => {
                match teko::journal::append(sink_of(sinks, m.writer), "out", m.bytes) { error as e => return n; null => { } }
                n = n + 1
            }
            closed => break
        }
    }
    n
}
```

### 16.5 Os requisitos, CORRIGIDOS — o que este uso mede sobre `chan<T>`

| requisito | estado | o que o uso mediu |
|---|---|---|
| ~~espera multi-origem~~ | **REFUTADO** | o consumidor precisa de `recv` de origem UNICA; a multiplicidade e dos produtores |
| **fan-in de N produtores para 1 consumidor** | **NOVO, e e o requisito central** | e a forma inteira. Um `chan<T>` que so admita um produtor nao serve |
| **fecho POR PRODUTOR** | confirmado, e afinado | nao chega saber "fechado": e preciso que o canal conte os produtores e so diga `closed` quando o ultimo sair. E o que autoriza o `wait_one` sem impasse |
| **limitado, com contrapressao** | confirmado, e agora e OBRIGATORIO | com fan-in, N produtores enchem mais depressa do que um consumidor esvazia. Ilimitado = OOM, e nos morremos disso duas vezes |
| **ordem por origem, nao global** | confirmado, e agora e ESTRUTURAL | um handler por origem empurra na ordem em que leu; a ordem por origem sai de graca e a global continua a nao fazer falta |
| **registos, nao fluxo de bytes** | **NOVO** | sem o rotulo a viajar com os bytes, o fan-in entrelaca dois filhos e perde a atribuicao — o defeito deste documento, uma camada acima |

Seis linhas, **duas novas e uma refutada**, todas vindas de um caso real. O S8 recusou congelar
`chan<T>` porque *"seria congelar a peca que os casos reais nao usam"*. **O caso real chegou.** Isto e
a diferenca entre congelar por antecipacao e congelar por necessidade medida — e continuo a **nao**
congelar: registo a forma, construo o fundo minimo, e a primitiva geral e do dono.

### 16.6 A prova, com o segundo braco que a forma nova exige

A prova de §15.5 mantem-se (um filho que escreve 200 000 bytes, mais do triplo do buffer) e **ganha o
braco do consumidor**, porque o fan-in traz um segundo impasse possivel:

```teko
#test
/**
 * fi_n_children_fan_in_without_interleaving — N filhos a escrever muito ao mesmo tempo, e cada
 * segmento recebe os SEUS bytes inteiros e por ordem.
 *
 * O que isto apanha e o que um canal de bytes puros faria: os blocos chegam entrelacados no tempo
 * (e devem — sao concorrentes), mas o rotulo separa-os, e a ordem DENTRO de cada escritor tem de ser
 * a ordem de leitura.
 *
 * @throws quando algum segmento perde bytes ou os recebe fora de ordem
 */
fn fi_n_children_fan_in_without_interleaving() {
    let segs = fi_run_fan_in(4, 200000, 5000)
    teko::assert::eq_u64(fi_total_bytes(segs), 800000)
    teko::assert::is_true(fi_each_writer_is_in_read_order(segs))
}

#test
/**
 * fi_an_orchestrator_that_waits_mid_loop_deadlocks — O SEGUNDO BRACO, e ele so existe por causa do
 * fan-in: um canal LIMITADO com N produtores para num consumidor que se distrai.
 *
 * A inversao de §15.5 provava o lado do handler. Esta prova o lado do consumidor: um `wait_one` a
 * meio do laco enche o canal, para todos os handlers, e nada avanca. Corre com PRAZO, porque um
 * teste que pendura nao e um teste.
 *
 * @throws quando a ordem errada TERMINA — o que significaria que esta prova deixou de provar
 */
fn fi_an_orchestrator_that_waits_mid_loop_deadlocks() {
    teko::assert::is_true(fi_run_waiting_mid_loop(4, 200000, 5000).timed_out)
    teko::assert::is_false(fi_run_fan_in(4, 200000, 5000).timed_out)
}
```

### 16.7 O custo em crumbs, e o prazo

**Continua sem crumb novo.** O **C1** ja era "o sumidouro do escritor"; passa a incluir o `tk_chan`, o
handler de dreno e os tubos. O **C3** encolhe (§15.1). O **C7** ganha os dois bracos de impasse. O
**C0** fica.

**Total: 10 crumbs.** E o C1 cresce em conteudo — ~210 linhas de C mantido, simetricas entre POSIX e
Windows — sem tocar na regiao raiz por tarefa, que continua a ser a divida da S8 e continua a nao ser
paga aqui.

Prazo do dono: *"imediatamente, a dor e latente"*. O C0 e o C1 entram ja.

### 16.8 O que isto NAO e, dito para nao ser lido a mais

1. **Nao e `teko::isolate::spawn`.** O handler e uma thread cujo corpo e uma funcao C do runtime que
   nunca chama codigo Teko. Nao ha superficie de threads em Teko no fim disto.
2. **Nao e `chan<T>`.** E `tk_chan`: um canal de REGISTOS DE BYTES, sem tipo generico, interno ao
   runtime. Quando `chan<T>` for construido, este e o fundo sobre que assenta — nao e trabalho
   deitado fora, mas tambem nao e a primitiva.
3. **Nao resolve a regiao raiz por tarefa.** Contorna-a, e o contorno tem uma condicao verificavel: o
   corpo do handler nao aloca da arena. Se algum dia ele precisar de correr codigo Teko, a condicao
   cai e a divida da S8 volta inteira.
4. **Windows continua sem medicao minha.** A forma agora e simetrica (thread + leitura bloqueante dos
   dois lados), o que remove a assimetria que eu tinha declarado — mas simetrico no papel nao e
   medido, e a perna Windows tem de trazer a sua propria medicao.

---

## 17. A arena por tarefa — PRE-REQUISITO, nao divida — ruling do dono, 2026-07-30

> *"Mas ai nao resolve nada, o processo precisa funcionar tanto nativo quanto em C, sem excecoes."*

**A minha saida de emergencia esta invalidada, e a razao esta na arvore.** Escrevi em §16.3 que o
corpo do handler *"vive no runtime, em C, e nunca chama codigo Teko"*. `feat/issue-runtime-em-teko`
esta viva e `docs/design/romaneio-morte-do-c-031.md` lista `src/runtime/teko_rt.c` entre os ficheiros
que MORREM. As minhas ~210 linhas iam para dentro de um ficheiro com morte planeada, e a condicao que
suspendia a divida **cai por construcao** no dia em que o runtime for Teko. Nao e risco: e o objectivo
declarado desta lane. **Isso e o meio-termo que o dono proibiu.**

### 17.1 Primeiro a medicao, porque "caro" nao e um numero — e eu repeti um adjectivo

Classifiquei a arena por tarefa como *"cara e toca a disciplina de memoria inteira"*. **Essa frase e
do `concorrencia-adiantada-s8.md` e eu repeti-a sem a medir.** Fui medir. E menor do que o adjectivo,
e esta concentrada num ficheiro so.

**As variaveis que uma segunda tarefa corrompe** (todas em `src/runtime/teko_rt.c`):

| # | variavel | linha | porque corrompe |
|---:|---|---:|---|
| 1 | `tk_g_regs` | 1149 | registo intrusivo de regioes vivas; duas tarefas a prepender perdem nos |
| 2 | `tk_g_root` | 1150 | a propria regiao raiz — o comentario ja diz *"single-threaded seed (S8 revisit)"* |
| 3 | `tk_arena_marks[64]` | 1551 | a pilha de marcas: o `pop` de A liberta o que B usa |
| 4 | `tk_arena_msp` | 1552 | o topo dessa pilha |
| 5 | `tk_push_cache[]` | 2773 | cache de cauda viva, chaveada por regiao+geracao — um falso acerto escreve em memoria alheia |
| 6 | `tk_free_bins[]` | 1168 | bins da lista livre |
| 7 | `tk_free_large` | 1169 | lista livre grande |
| 8 | `tk_free_parked_bytes` + `_reused_bytes` + `_reused_count` | 1170 | contadores da lista livre |

**A que NAO pode ser por tarefa, e e a armadilha do "poe tudo em thread-local":**

| # | variavel | linha | porque |
|---:|---|---:|---|
| 9 | `tk_g_region_gen` | 1151 | a geracao tem de ser unica **entre tarefas**, nao dentro de cada uma. Por tarefa, duas tarefas emitem a mesma geracao e o `tk_push_cache` volta a falsear. **Fica global e passa a atomica.** |

**As tres familias com a mesma patologia, fora da arena:**

| # | familia | linha | nota |
|---:|---|---:|---|
| 10 | `tk_fn_stack`/`tk_fn_sp`/`tk_fn_cap` | 2568-2570 | pilha de atribuicao de cobertura — por tarefa |
| 11 | tabelas `tk_obs_*` | 1236-1254 | observabilidade — por tarefa, ou atomicas |
| 12 | `tk_test_jb` / `tk_test_capturing` (§14) | C0 | **por tarefa, obrigatoriamente**: um `panic` numa tarefa que saltasse para a moldura de outra e pior do que o `abort` que substitui |

**TOTAL: 12 familias de variavel, num ficheiro so.**

**E o numero que decide o custo e o outro — o que NAO muda:**

| simbolo | referencias | muda? |
|---|---:|---|
| `tk_alloc` | 54 | **nao** |
| `tk_region_alloc` | 41 | **nao** |
| `tk_region_new` | 35 | **nao** |
| `tk_region_drop` | 34 | **nao** |
| `tk_region_root` | 27 | **nao** |
| `tk_arena_pop` | 20 | **nao** |
| `tk_regions_free_all` | 17 | **nao** |
| `tk_arena_push` | 12 | **nao** |
| `tk_region_register` / `tk_region_lookup` | 19 | **nao** |

**~259 referencias, e nenhuma muda de forma** — porque as assinaturas ficam identicas e passam a ler
`tk_task_current()` por dentro. E por isso que as 64 referencias em `src/codegen/codegen.tks` (que sao
literais de C emitido) tambem nao mudam: o C emitido continua a dizer `tk_arena_push();`.

**A conta honesta: 12 declaracoes movem-se, ~259 chamadas ficam.** O custo esta concentrado, nao
espalhado — o contrario do que o adjectivo que eu repeti dizia.

### 17.2 A pergunta central: uma tarefa com raiz propria numa linguagem sem tarefas

`_Thread_local` resolve o C de hoje e **nao** resolve o nativo. A resposta que serve as duas
encarnacoes nao e uma tecnica — e uma **costura**:

> **Todo o estado da tabela de §17.1 colapsa num `tk_task`, e ha UM acessor: `tk_task_current()`.
> A costura entre as duas encarnacoes e essa funcao, e so essa.**

```c
// tk_task — TODO o estado que uma tarefa nao partilha: a sua regiao raiz, a sua pilha de marcas, a
// sua cache de cauda, as suas listas livres, o seu buffer de captura (§14).
//
// UMA STRUCT E NAO DOZE VARIAVEIS, e e isso que torna a costura possivel: com doze globais haveria
// doze acessos a portar; com uma, ha um.
typedef struct tk_task tk_task;
// tk_task_current — a tarefa que corre AGORA. A UNICA linha do runtime que sabe o que e uma thread.
tk_task *tk_task_current(void);
```

E as duas encarnacoes da mesma funcao:

| encarnacao | implementacao | custo de backend |
|---|---|---|
| **C de hoje** | `static _Thread_local tk_task *tk_g_task;` — uma linha (C11, e a arvore ja usa `_Noreturn`) | nenhum |
| **Teko nativo de amanha** | `pthread_getspecific` / `TlsGetValue`, ligados por `extern fn` — a FFI que ja existe | **nenhum** |

**E aqui esta a resposta a objeccao, e ela e o ponto inteiro:** a versao nativa **nao precisa de
nenhuma capacidade nova do backend proprio.** `pthread_getspecific` e uma chamada C ordinaria a um
simbolo externo — exactamente o que `extern fn` ja emite hoje, dezenas de vezes. Nao ha relocacao de
TLS por inventar, nao ha registo reservado, nao ha modelo de armazenamento por escolher.

E se um dia se quiser o acesso mais rapido (relocacoes TLS reais no emissor ELF/COFF), **troca-se uma
funcao** e nenhuma das ~259 chamadas sabe. Isso e a diferenca entre uma costura e uma saida de
emergencia: a costura tem duas implementacoes hoje e admite uma terceira amanha; a saida tinha uma so
e expirava.

**O custo que eu NAO medi, e digo-o:** quanto custa um `pthread_getspecific` por `tk_alloc` nesta
arvore. A mitigacao esta desenhada — `tk_alloc_in(tk_task *, size)` com `tk_alloc` a ser o invocador
de uma consulta so, e os ciclos quentes (`tk_slice_push`) a buscarem uma vez — mas **o numero e a
primeira coisa do crumb**, e se ele desmentir a forma, e a medicao que fica.

### 17.3 A resposta a pergunta de ordenacao, e ela e SIM, trava

**O fan-in NAO pode entrar antes da arena por tarefa.** Digo-o em vez de arranjar outra condicao.

A razao e estrutural e sobrevive ao romaneio: um handler de dreno, **assim que o runtime for Teko**,
aloca — qualquer codigo Teko aloca. E mesmo hoje, o empurrao de um bloco para o `tk_chan` copia bytes,
e uma copia que passe por `tk_alloc` toca a raiz partilhada. Fazer o fan-in primeiro e construir por
cima de uma suposicao com data de validade.

**Portanto entra um crumb, e ele e um PRE-REQUISITO:**

**C-A — a arena por tarefa.** As 12 familias de §17.1 colapsam em `tk_task`; `tk_g_region_gen` fica
global e passa a atomica; `tk_task_current()` com as duas encarnacoes; a guarda de §17.4.

**Ordem: C0 · C-A · C1 · C2 · C3 · C4 · C5 · C6 · C7 · C8 · C9. Total: 10 -> 11 crumbs.**

O **C0 continua a poder ir a frente**: a captura corre no arnes de uma shard que ainda e
single-threaded, e nao ha handler nenhum antes do C1. Mas o C-A **tem de mover `tk_test_jb` para
dentro do `tk_task`** quando aterrar, e isso esta na sua lista (familia 12) para nao se perder.

**Ponto de ritual: depois do C-A.** Ele mexe na disciplina de memoria do compilador inteiro, e o
fixpoint (gen1 == gen2) e o unico juiz que serve.

### 17.4 A guarda, e a regra que sobrevive

Mesmo com arena por tarefa sobra **uma** regra, e portanto sobra uma guarda: **nenhuma tarefa
rebobina ou liberta a regiao de outra.** Ela tem de poder falhar.

```teko
#test
/**
 * ta_two_tasks_do_not_share_a_root — duas tarefas alocam, uma rebobina, e os bytes da outra ficam
 * INTACTOS.
 *
 * @throws quando a rebobinagem de uma tarefa toca a canaria da outra
 */
fn ta_two_tasks_do_not_share_a_root() {
    let r = ta_run_two_tasks_with_canaries()
    teko::assert::is_true(r.other_canary_intact)
}

#test
/**
 * ta_the_guard_is_not_vacuous — O BRACO DE VIVACIDADE, e sem ele os outros dois nao valem nada.
 *
 * Se `tk_task_current()` devolvesse a MESMA tarefa as duas raias, o teste acima passaria por nao
 * haver duas coisas para corromper — verde sobre um instrumento cego, que e a patologia que ja nos
 * deu ZERO tres vezes nesta lane. Por isso afirma-se primeiro que as raizes sao DIFERENTES.
 *
 * @throws quando as duas tarefas observam a mesma raiz, ou a mesma geracao
 */
fn ta_the_guard_is_not_vacuous() {
    let r = ta_run_two_tasks_with_canaries()
    teko::assert::ne_i64(r.root_a, r.root_b)
    teko::assert::is_true(ta_all_generations_distinct(r.gens))
}

#test
/**
 * ta_a_shared_root_corrupts — A INVERSAO POR REVERSAO: a mesma canaria contra uma raiz PARTILHADA
 * (a forma de hoje) tem de ser destruida.
 *
 * Sem este braco a guarda so diz "nao vi problema", que e o que uma guarda cega tambem diz.
 *
 * @throws quando a forma partilhada NAO corrompe — o que significaria que esta prova nao prova
 */
fn ta_a_shared_root_corrupts() {
    teko::assert::is_false(ta_run_two_tasks_on_one_root().other_canary_intact)
}
```

A geracao tem a sua propria afirmacao (`ta_all_generations_distinct`) porque e a variavel que **fica**
global: duas tarefas a criar regioes em ciclo nunca podem observar a mesma geracao, e um incremento
nao-atomico colide de forma reprodutivel com duas raias e 100 000 voltas.

### 17.5 O que continua por resolver, e nao escondo

1. **`pthread_getspecific` por alocacao nao esta medido** (§17.2). E o primeiro passo do C-A.
2. **`tk_region_register`/`tk_region_lookup` (DI) sao por regiao**, logo seguem a raiz para dentro do
   `tk_task` — mas o S8 ja tinha reportado que um `#singleton` resolvido em duas tarefas e uma segunda
   corrida da mesma familia. Com a raiz por tarefa, dois `#singleton` passam a ser **duas instancias**,
   e isso e uma mudanca de SEMANTICA de DI, nao so de seguranca. **REPORTADO, nao decidido por mim:**
   e uma escolha do dono se um `#singleton` e por processo ou por tarefa.
3. **Windows continua sem medicao minha.** `TlsGetValue` e a forma simetrica de `pthread_getspecific`
   e a costura nao muda, mas simetrico no papel nao e medido.
4. **A arena por tarefa nao da threads a Teko.** Ela remove o que travava a S8; `spawn` e `chan<T>`
   continuam por construir, e o §16 continua a construir apenas o fundo minimo.

---

## 18. `chan<T>` e MPSC — a fronteira, fixada pelo dono (2026-07-30)

> *"`chan<T>` e fan-in, varios escritores, um leitor. Para ter um fan-out 'N:M', teria que ser outro
> tipo de estrutura, que ate pode operar sobre um `chan<T>` mas que precisaria de uma segunda
> estrutura capaz de broadcasting e multiplas copias."*

**Lei do desenho: `chan<T>` e MPSC. N escritores, UM leitor. Nao e MPMC, nao e difusao.**

### 18.1 Como o TIPO o impoe — e a resposta nao e "por convencao"

O tipo parte-se em dois, e os dois extremos nao sao o mesmo valor:

```teko
/**
 * Tx — o extremo de ESCRITA de um canal. Copiavel de proposito: os N escritores sao a metade que
 * pode ser multipla.
 *
 * @since 0.3.1
 */
pub type Tx = struct { raw: i64 }

/**
 * Rx — o extremo de LEITURA. UM, e a unicidade e a razao de ele existir como tipo separado.
 *
 * PORQUE UM TIPO E NAO UMA REGRA: com um `chan<T>` unico, "nao chames `recv` em duas tarefas" e uma
 * frase num comentario, e uma frase nao falha. Com o extremo separado, ter dois leitores exige ter
 * dois `Rx` — e passa a ser um ACTO explicito que o §18.2 faz falhar, em vez de um descuido que
 * ninguem ve.
 *
 * @since 0.3.1
 */
pub type Rx = struct { raw: i64 }

/**
 * open — abrir um canal e devolver os seus dois extremos.
 *
 * @param cap_bytes  a folga do anel; LIMITADO por lei (§15.7/§16.5)
 * @param producers  quantos escritores vao existir (o `recv` so diz `closed` quando o ultimo sair)
 * @return           o extremo de escrita (copiavel) e o de leitura (unico)
 * @throws           quando o canal nao pode ser criado
 * @since 0.3.1
 */
pub fn open(cap_bytes: u64, producers: u32) -> ChanEnds | error

/** send — empurrar um registo rotulado. N escritores, sem coordenacao entre eles. */
pub fn send(t: Tx, writer: str, bytes: str) -> null | error

/** recv — tirar o proximo registo. UM leitor; um segundo PANICA (§18.2). */
pub fn recv(r: Rx) -> ChanMsg | closed
```

### 18.2 Erro de verificador, erro de runtime, ou corrida silenciosa? — **runtime, hoje; e nunca silenciosa**

Fui medir antes de prometer, porque foi a §17 que me ensinou a nao repetir adjectivo emprestado.

| camada | estado MEDIDO | o que apanha |
|---|---|---|
| **tipo separado (`Tx`/`Rx`)** | desenhavel hoje | obriga a que dois leitores sejam um acto explicito: copiar o `Rx` |
| **verificador** | **NAO disponivel hoje.** A rede de unicidade existe — `spine.tks` tem o reticulado `UsUnique < UsShared < UsTop` — mas **nao e consultada em lado nenhum**; o proprio `borrow.tks` diz do seu par: *"Consulted NOWHERE yet ... fixpoint is preserved by construction"* | (nada, hoje) |
| **runtime** | **disponivel, e e o que garante** | tudo o resto, incluindo o que chegue por FFI ou pelo runtime em C |

**A garantia efectiva e de RUNTIME e e deterministica:** `tk_chan` grava a identidade da tarefa que
fez o **primeiro** `recv`. Um `recv` vindo de outra tarefa **panica** com a mensagem nomeada
(`teko: chan has a second reader — chan<T> is MPSC`). Nao ha corrida silenciosa: ou e o dono do
`Rx` a ler, ou o programa para e diz porque.

E ela **pode falhar**, que e a unica coisa que a distingue de decoracao:

```teko
#test
/**
 * ch_a_second_reader_panics — o segundo leitor tem de PARAR o programa, com a fronteira nomeada.
 *
 * @throws quando um segundo `recv` de outra tarefa NAO panica — o que significaria que a fronteira
 *         do dono e um comentario
 */
fn ch_a_second_reader_panics() {
    teko::assert::str_contains(ch_run_two_readers().panic_text, "chan<T> is MPSC")
}

#test
/**
 * ch_the_guard_is_not_vacuous — vivacidade: o canal com UM leitor tem de entregar os registos dos
 * DOIS escritores. Sem isto, um canal que panicasse sempre passaria o teste acima.
 */
fn ch_the_guard_is_not_vacuous() {
    teko::assert::eq_u64(ch_run_two_writers_one_reader().received, 2)
}
```

**O que NAO prometo:** rejeicao em compilacao. Ela e alcancavel — o consult site seria o reticulado de
`spine.tks`, um `Rx` que ele junte a `UsShared` e um erro — mas **ligar a espinha e trabalho que nao
esta nos 11 crumbs** e nao o contrabandeio para dentro deles. Fica **REPORTADO** como o upgrade
natural: quando a espinha for consultada, esta regra e um dos seus primeiros clientes, e o painel de
runtime continua como rede para o que vier por FFI.

### 18.3 O meu fan-in ja e estritamente MPSC — verificado, e o unico ponto duvidoso nomeado

Percorri o desenho a procura de um segundo leitor. **Nao ha.**

* §16.4 `orchestrate` — o **unico** `recv` do documento;
* §16.4 `drain_into` — so `send`, e ha **2 por filho** (stdout e stderr), logo `2N` escritores para
  `N` filhos e **1** leitor;
* §2.4 `gate_summary` e §13 `summarize` — leem **segmentos de journal em disco**, nunca o canal;
* `pump` (§15.3) era a forma central que a §16 rectificou; ela nao lia canal nenhum, porque nessa
  versao nao havia canal.

**O ponto que merecia ser nomeado e a invariante do consumidor, e ela ja implicava isto sem o dizer:**
*"o orquestrador nunca bloqueia em nada que nao seja `recv`"* (§16.4). Um segundo leitor seria uma
segunda tarefa bloqueada em `recv` — proibida por essa linha antes de a lei do dono existir. A lei do
dono torna explicito o que a invariante ja exigia.

### 18.4 A segunda estrutura (N:M) — NOMEADA, nao desenhada, e com a divida precisa

O dono disse que o N:M pode assentar sobre um `chan<T>` mas exige difusao e **multiplas copias**. A
divida nao e a difusao: e a **posse**.

**A diferenca essencial, em uma linha:** num MPSC cada registo e consumido **uma** vez, logo nao ha
contagem de referencias e nao ha pergunta de tempo de vida. Numa difusao o registo e consumido **N**
vezes, e e *por isso* — nao por gosto — que ela precisa de uma regra de posse.

**E com a raiz por tarefa (C-A, §17) a pergunta fica afiada:** um valor alocado na raiz da tarefa A e
um ponteiro pendurado no instante em que A rebobina. Portanto uma difusao **nao pode transportar
referencias para arenas alheias**. Quem pegar nisto tem de decidir, e ainda ninguem decidiu:

| pergunta | opcoes conhecidas, nenhuma escolhida |
|---|---|
| **quem e dono do valor difundido** | (i) ninguem — o runtime guarda-o numa area `malloc`ada e cada receptor **copia para a SUA arena ao receber** (e a forma que o `tk_chan` de §16 ja usa para bytes); (ii) uma regiao partilhada com contagem de referencias; (iii) valor imutavel com prova de escape de que sobrevive a todos os receptores |
| **quando morre** | (i) na ultima copia feita — precisa de saber quantos receptores ha; (ii) na contagem a chegar a zero; (iii) no fecho do ultimo receptor |
| **o que acontece a um receptor lento** | com contrapressao, ele trava os outros; sem ela, a memoria cresce. O MPSC resolve-o com um anel limitado; a difusao tem de o resolver **por receptor** |

**Nao construo nada disto, e nao e preciso para os 11 crumbs.** Fica registado com detalhe suficiente
para nao ser redescoberto: a difusao e um problema de **tempo de vida atraves de fronteiras de
arena**, e so se torna respondivel **depois** do C-A, porque e o C-A que cria as fronteiras.

### 18.5 O que isto muda nos crumbs

**Nada.** Os 11 crumbs ficam como estao — o fan-in ja era MPSC (§18.3), e a fronteira do dono
confirma-o em vez de o alterar. A unica adicao e dentro do C1: o `Tx`/`Rx` separados e o painel de
segundo-leitor, que sao ~15 linhas do `tk_chan` que aquele crumb ja constroi.
