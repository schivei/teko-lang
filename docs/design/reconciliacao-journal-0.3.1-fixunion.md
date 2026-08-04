# Reconciliação do subsistema `teko::journal` — vagão fix/union linear

status: DESIGN/ANÁLISE. Nenhuma linha de produto escrita nesta carga. Validação de build é
CI-only, sobre a reconstrução linear (SEM reprodução local — foi o degrau privado não rastreado
que causou o problema).

Autoridade: o spec do dono (artifact `952484a6-...`, "Journaling de corrida — premissa, produto e
entregável") é a régua de 100%. O design em `docs/design/journaling-de-corrida-0.3.1.md` (C1–C9) é
o roteiro dos crumbs. Ambos foram usados como critério.

---

## 1. Veredito da linhagem canônica

**Canônica = a biblioteca da MAINLINE evoluída (journal.tks 1186 linhas / summary.tks 559 linhas),
JÁ presente na `fix/union` em `5b6a638e`.** A `origin/cargo/0.3.1.0-journal-impl` (738/774) é uma
biblioteca PARALELA, desconectada, e é a versão errada. Justificativa por evidência do spec:

1. **Serve de base às peças "rodando" do spec.** As peças marcadas "rodando" são 3.1 binding
   (`8eb85369`) e 3.2 captura C0 (`81a3d9a0`) e 3.9 transporte medido. A captura C0 (`81a3d9a0`,
   toca `src/test/test.tks`, `codegen.tks`, `teko_rt.c`) é o canal de veredicto que o spec quer
   ATRIBUÍDO. Os hooks `note_test_begin/ok/panicked/exited/end` e `note_row_*` da canônica estão
   **fiados** a esse canal — provado em:
   - `src/codegen/codegen.tks:12909-12974` (o arnês de `#test` emitido chama `note_test_end` /
     `note_test_exited`);
   - `src/build/regression.tks:3125-3179` (o runner de regressão chama `note_row_begin/ok/fail/skip/end`).
   A `journal-impl` (738) **não tem nenhum desses hooks** e **não é chamada por lugar nenhum** —
   nunca foi fiada. Ela não serve de base a nenhuma peça "rodando".

2. **Atribuição (o PRODUTO do spec) só a canônica entrega no braço de sinal.** O `tk_journal_note`
   da canônica (runtime em `5b6a638e:src/runtime/teko_rt.c:4694+`) escreve o **prefixo armado**
   `run\twriter\tstop\t` ANTES do número do sinal (`tk_journal_arm(prefix: str)` formata a
   identidade no open, §6 do design). O `tk_journal_note` da `journal-impl`
   (`origin/cargo/0.3.1.0-journal-impl:src/runtime/teko_rt.c`) escreve `stop <sig>\n` FIXO, sem
   run/writer — **perde a atribuição**, que é exatamente o produto que o spec exige. `tk_journal_arm`
   da impl recebe só um `i64` (o fd), não o prefixo. Contradição direta do spec → impl é a errada.

3. **Invariante "sem timestamp": ambas cumprem, canônica de forma auditável.** `record_line`
   (`5b6a638e:src/journal/journal.tks:370`) é 4 campos `run\twriter\tkind\tpayload`, sem tempo;
   `mono_ns_rt` é usado SÓ para cunhar `run_id = <ns>-<pid>` (identidade única da corrida, linha
   245), não como campo de registro. Não é discriminador, mas confirma que a canônica não viola o
   spec.

4. **Completude.** Canônica (1186) é superconjunto da base `a5f66e92` (929) + a fiação J1–J6. Tem
   varredura de corrida-anterior com ferrolho vivo (`lock_pids`/`pid_in_lock`/`record_self_pid`/
   `root_is_live`/`rmtree`/`sweep_under`), `claim_root`, `is_dir`, `open_seg`. A impl (738) não tem
   nada disso — é uma biblioteca menor e incompleta.

Nota sobre §3.4 do spec (Rec{writer,seq,body}, RecBody = variant Assert|Cov, veredito como CAMPO da
asserção e não espécie de registro; MPSC bounded `push -> error|null`; `.tkj` binário append-only;
site_ids ⊆ chegaram; waitgroup; filtro #os/#arch): **NENHUMA das duas** bibliotecas implementa essa
superfície. Ambas usam `type Record{run,writer,kind,payload}` com `kind` string (ok/fail como
ESPÉCIE) e segmentos de texto tab-separados por descritor — o modelo PRÉ-spec. Toda a §3.4 é "por
construir" a jusante desta reconciliação, e aplica-se igualmente às duas; não é discriminador entre
elas. Esta reconciliação NÃO reescreve a superfície para o spec — só garante que a base linear certa
(a canônica, alinhada às peças "rodando" e à atribuição) é a que segue para o theory.

---

## 2. Contribuições genuinamente ÚNICAS da journal-impl — auditoria função-a-função

Comparei os inventários de símbolo das três `journal.tks` e das três `summary.tks`. Símbolos que a
`journal-impl` tem e a canônica não:

| símbolo (journal-impl) | file:line (impl) | tem análogo canônico? | veredito |
|---|---|---|---|
| `open_at(path,run,writer)` | journal.tks:129 | sim: `open_seg` + `open` (5b6a638e:388,406) | REJEITAR (duplicata inferior) |
| `close(j)` | journal.tks:189 | sim: `close_fd_rt` via `tk_rt_close_fd` (5b6a638e:991, usado em `note_open_append_close`:1048) | REJEITAR |
| `append_line(path,line)` | journal.tks:210 | sim: `append_raw` (5b6a638e:479) | REJEITAR (duplicata) |
| runtime `tk_journal_close` | teko_rt.c (+41) | NÃO — canônica fecha com `tk_rt_close_fd` (5b6a638e:3624) | REJEITAR: enxerto = DEAD CODE (zero chamadores na árvore canônica) |
| runtime `tk_rt_rmtree` / `tk_rmtree_cstr` | teko_rt.c (+~90) | NÃO — canônica faz `rmtree` em TEKO puro (5b6a638e:journal.tks:896, via `teko::fs::list_dir`/`remove_file`) | REJEITAR: enxerto = DEAD CODE (zero chamadores); ver §4 (achado reportado) |
| `summary.tks` inteiro (WriterStat, LabelReason, `fold_writer_record`, `is_dead`, `cov_of_payload`, `cov_add`, `collect_findings`, `render_dead`) | summary.tks:109–714 | superfície PARALELA e incompatível | REJEITAR no enxerto; ver §3 (decisão do dono) |
| `summary_test.tkt` (152 linhas) | src/journal/summary_test.tkt | testa a API impl (WriterStat/collect_findings/is_dead) que NÃO existe na canônica | REJEITAR: não compila contra a summary canônica; ver §3/§4 |

**Confirmação de coerência (o teste decisivo):** procurei na árvore inteira da `5b6a638e`
(`src/**/*.tks` + `*.tkt`) por qualquer chamador de `tk_journal_close`, `tk_rt_rmtree`,
`close_seg_rt`, `rmtree_rt`, `open_at`, `append_line`, `journal::close` — **zero resultados**. E o
runtime da `5b6a638e` **não define** `tk_journal_close` nem `tk_rt_rmtree`. Logo: enxertar o runtime
da impl é acrescentar C que ninguém chama = dead code puro, contra a lei de "issues são 100% sem
regressão" (introduz superfície morta).

**Conclusão inesperada mas firme: a `journal-impl` não contribui NADA que PRECISE ser enxertado na
`fix/union` linear para satisfazer as peças "rodando" do spec.** Tudo que ela tem de único ou é
duplicata inferior do que a canônica já faz, ou é dead code contra o grafo de chamadas real, ou é
uma summary paralela acoplada a si mesma. O runtime NOVO da impl (+152/+41) casa apenas com a
PRÓPRIA impl (que não é chamada) — não com a canônica.

---

## 3. Conflito irreconciliável para o DONO decidir

Há UM ponto onde não dá para decidir sozinho, porque é escolha de PRODUTO, não de correção:

**`src/journal/summary.tks` — canônica (559, simples, FIADA) vs journal-impl (774, mais rica,
NÃO-fiada).**

- Canônica (`5b6a638e:src/journal/summary.tks`, idêntica à base `a5f66e92`): `never_ran_tally`,
  `with_coverage`, `cov_metric_pct`, `render_findings`. É a que `src/build/project.tks` consome hoje.
  A `RunSummary` tem os campos que os call sites esperam.
- journal-impl (`origin/cargo/0.3.1.0-journal-impl:src/journal/summary.tks`): adiciona detecção de
  teste-morto `is_dead(st) = has_plan && !has_end` (summary.tks:492), dobra de cobertura
  `cov_of_payload`/`cov_add` (:239/:259), `collect_findings` por kind (:512), `render_dead` (:693),
  máquina `WriterStat` (:352-491). Estruturalmente mais perto da medição estática×executada e da
  detecção de shard-sem-`end` que o design C4 e o spec §3 pedem — MAS tem uma `RunSummary` de campos
  diferentes e uma API pública diferente (`summarize`/`render_summary` mesmas assinaturas, mas
  corpo/campos divergentes), e traz `summary_test.tkt` acoplado a ela.

**Trade-off exato:** adotar a summary da impl exige RE-FIAR `project.tks` (os campos de `RunSummary`
mudam) e portar `summary_test.tkt` — NÃO é behavior-preserving. Manter a canônica preserva o
comportamento fiado, mas deixa a detecção de teste-morto e a dobra de cobertura da impl na mesa.

**Recomendação do arquiteto (law-first, behavior-preserving):** para ESTA reconstrução linear,
**MANTER a summary canônica** (fiada, sem regressão). A summary da impl (dead-test + cov-folding)
NÃO é pré-requisito de nenhuma peça "rodando" do spec — a §3 (site_ids ⊆ chegaram, RecBody variant)
é "por construir" e reescreverá a summary de qualquer forma. Reporto abaixo as boas ideias da impl
como candidatas a crumb futuro, para o dono ratificar SEPARADAMENTE da reconstrução do vagão.

Se o dono decidir o contrário (adotar a summary da impl AGORA), é uma issue própria com re-fiação de
`project.tks` + porte de `summary_test.tkt`, não parte desta reconstrução linear.

---

## 4. Achados adjacentes — REPORTADOS, não convertidos em issue por mim

1. **`tk_rt_rmtree`/`tk_rmtree_cstr` (C) da impl** poderia substituir o `rmtree` em Teko da canônica
   (5b6a638e:journal.tks:896) por um recursivo em C, mais rápido e sem depender de `teko::fs`. É
   otimização, não correção; hoje seria dead code. Candidato a follow-up SE a varredura em Teko
   provar ser gargalo.
2. **Detecção de teste-morto `is_dead` + dobra de cobertura `cov_of_payload`/`cov_add`** da summary
   da impl mapeiam para a medição estática×executada do spec §3 (site_ids ⊆ chegaram). Boa matéria-
   prima para o crumb que construir a §3, mas contra a superfície `Rec/RecBody` do spec, não contra
   `Record`.
3. **`summary_test.tkt` (152 linhas)** tem ideias de teste válidas (shard sem `end` = falha nomeada,
   cobertura em falta é VISTA) que devem ser reescritas contra a summary que vencer.

---

## 5. PLANO de reconciliação (sobre a `fix/union` linear `5b6a638e`)

**O plano é a AUSÊNCIA de enxerto.** A `fix/union` em `5b6a638e` já contém a linhagem canônica
correta, fiada e alinhada às peças "rodando" e à atribuição do spec. Portanto:

1. **`src/journal/journal.tks` (1186)** — MANTER como está. Nenhuma mudança.
2. **`src/journal/summary.tks` (559)** — MANTER como está (ver §3; decisão do dono para mudar).
3. **`src/journal/journal_test.tkt`** — MANTER (é o teste da canônica; a impl não o tinha).
4. **`src/runtime/teko_rt.{c,h}`** — MANTER como está. NÃO enxertar `tk_journal_close` nem
   `tk_rt_rmtree`/`tk_rmtree_cstr` da impl (dead code; zero chamadores; runtime é C mantido/congelado
   fora do seed).
5. **`origin/cargo/0.3.1.0-journal-impl`** — ABANDONAR. Não fazer merge nem cherry-pick para a
   reconstrução linear. O scaffold `5d4cd170` criou uma journal.tks do zero a partir de um pai
   (`37579148`) que não tinha journal.tks; essa árvore é uma linhagem morta. Preservar o branch como
   registro histórico, mas não incorporá-lo.
6. **`src/journal/summary_test.tkt`** — NÃO trazer (não compila contra a summary canônica).

**Linearidade:** como não há enxerto, a `fix/union` reconstruída não ganha nenhum commit novo de
journal — a história permanece a da mainline→testparallel-close (929→1186 + fiação J1–J6 + governor
`par1` `13cdcb2c`). Se o dono ratificar a summary da impl (§3), aí sim UM commit no lugar certo
(depois do C4 da canônica), mas isso é decisão explícita, não parte desta reconstrução.

**Coerência do runtime enxertado:** N/A — nada é enxertado. O runtime da canônica
(`tk_journal_open/append/arm/note` + `tk_rt_close_fd` + `tk_rt_rename` + `tk_rt_pid/pid_alive/
monotonic_ns`) casa exatamente com os externs de `journal.tks` (5b6a638e:32-109,991) e com os hooks
fiados em codegen/regression. Sem símbolo órfão.
