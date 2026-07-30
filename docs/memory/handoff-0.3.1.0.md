# Passagem de sessão — lane 0.3.1.0 "Linux gera NATIVE" (2026-07-30)

Escrito porque o container reiniciou duas vezes num dia e o dono precisa de poder mudar de sessão sem
perder o fio. **Este documento é a fonte única do estado.** Tudo aqui é medido, com SHA; onde é
hipótese, está dito.

- **PR:** `schivei/teko-lang#99`
- **Vagão:** `remodel/0.3.1.0-linux-native-2`, HEAD **`b48227de`** (worktree `/home/user/wt-lin`)
- **Como ler o CI sem cegueira:** `scripts/ci_full_log.sh` (§2c). Foi o instrumento que faltava todo o
  dia, e a §2b existe porque eu não sabia que existia.
- **Objetivo da lane:** as pernas Linux gerarem com o backend NATIVO (o `fixpoint_backend` por perna
  vive em `scripts/ci_producer_matrix.sh`)
- **O repo é um FORK.** `schivei/teko-lang`; o upstream é `teko-org/teko-lang`. `release.yml` e
  (desde hoje) `nightly.yml` só correm na org.


## 0. MODO AUTÓNOMO — 2026-07-30, o dono foi dormir

*"Vou dormir, te deixo no modo autônomo, tem bastante trabalho por aí."*

**O que o integrador faz enquanto ele dorme:** drenar agentes que terminam, despachar da fila ao teto de
4, verificar CI pelo log integral (`scripts/ci_full_log.sh`), e manter este documento vivo. **Não** promove
ao tronco, **não** faz bump de versão, **não** fecha a lane — isso é dele.

### CINCO AGENTES A CORRER (teto 4, um a mais por ordem explícita)

| agente | branch | porquê |
| --- | --- | --- |
| **degrau 27 — `ftoa`** | `cargo/0.3.1.0-degrau-27` | a paragem viva do self-host; destranca o ponto de fixo **e** torna efectiva a lei da emissão nativa |
| **último abort unitário (`zext`)** | `cargo/0.3.1-zext-expectativa` | provado por sonda que é o ÚLTIMO: sem ele, **1117** testes arrancam e a fase unitária fica verde |
| **leitura fora de fronteira** | `cargo/0.3.1-leitura-fora-de-fronteira` | divergência medida: nativo devolve lixo, rota C panica. Valor errado calado |
| **gémeo de macOS** | `cargo/0.3.1-gemeo-macos` | teste sem dependência do host que falha só em macOS |
| **`kind` desconhecido panica** | `cargo/0.3.1-kind-desconhecido-panica` | **5º por ordem directa**: *"precisa de correção já, ou estaremos ferindo nossas leis"* |

**Quando o do `kind` terminar, voltar ao teto de 4.**

### FILA, por valor

1. **`.exe` no Windows** — brief pronto e MEDIDO, ver §3d. Dois sítios de chamada, e o desenho certo já existe no mesmo ficheiro.
2. **Terceira passagem do documento do `tdb`** — em forma de **PROPOSTA** (lei nova de forma), com interop fora, alvo *"a melhor experiência de dev"*, e "SEM C LANG".
3. **`kind = "tool"`** — **BLOQUEADO** pelo portão do `tdb` (proposta, não entra nesta versão nem na seguinte).
4. **As duas regressões "expected a compile failure but the build succeeded"** (`native_iface_fat_known_stop.tkr`, `diagnostics.tkr`) — **atenção: é KNOWN-STOP a ficar vermelho, e pela lei do dono isso NÃO significa necessariamente que o defeito foi corrigido; pode significar que uma GUARDA se perdeu.** Não owned. Vale investigar.
5. `own_native.tkr → own_cross_x86_64_windows_emits_coff` — o `cc` falha no C gerado. Não owned.


### MEDIÇÕES DA PRIMEIRA EXECUÇÃO COM `Arm64Linux` (SHA `ebfb6be8`, perna macOS)

Quatro coisas, e três são notícia boa:

1. **Degrau 28 FECHADO no CI** — `slice element index-assignment` já não aparece. O 29 apareceu no seu lugar,
   que é o comportamento esperado de uma escada.
2. **`regressions 10 run, 0 skipped, 1 failed`** — **ZERO skips.** Os 21 skips da perna arm64 eram todos
   `unsupported TEKO_TARGET "arm64-linux"` e desapareceram com o crumb 3, **sem tocar em CI**. Confirma a
   decisão de não aplicar o crumb 5.
3. **A pergunta do agente do AArch64-ELF está RESPONDIDA:** a linha nova `own_cross_arm64_linux_emits_elf`
   **não saltou** em `test / macos-arm64`, logo o host macOS **tem** desmontador e religador LLVM
   cross-capable. **Nenhum provisionamento é necessário.**
4. **CORRECÇÃO À MINHA PRÓPRIA FILA, e importa:** as duas regressões que dois agentes reportaram como
   *"expected a compile failure but the build succeeded"* — `native_iface_fat_known_stop.tkr` e
   `diagnostics.tkr` — estão **`regression ok` no CI**. Não reproduzem. A causa provável é o compilador
   que os agentes semearam à mão de `bootstrap/teko.c` (porque `fetch_teko.sh` falha nesta sessão) diferir
   do que o CI usa. **Portanto NÃO despachar "guardas perdidas" — não há prova de que exista guarda
   perdida.** O que existe é uma discrepância entre a escada local dos agentes e a do CI, e isso é o
   achado a registar. Prioridade da fila baixa de 2 para o fim.

### CORRECÇÃO À CORRECÇÃO — três agentes contra uma leitura de CI, e eu dispensei depressa demais

Escrevi acima *"NÃO despachar guardas perdidas — não há prova de que exista guarda perdida"*. **Isso foi
prematuro.** Contagem actual: **três agentes independentes** (`zext`, `cast-narrow`, e o do degrau 28)
reportam as mesmas duas linhas a falhar, com a mesma mensagem, em corridas separadas:

```
native_iface_fat_known_stop.tkr → "expected a compile failure but the build succeeded"
diagnostics.tkr                 → "expected a compile failure but the build succeeded"
```

E o CI mostra `regression ok` para as duas. **Três observações concordantes não são ruído.** O que eu tenho
não é "nenhuma prova de defeito" — é **uma discrepância reproduzível**, e essa é a coisa a investigar.

### O EIXO PROVÁVEL, e é verificável com um comando

**O CI e os agentes não correm a suíte com a MESMA geração.** As pernas `test` correm o **asset publicado**
(que é a **gen1**, produzida por `produce_assets.sh`) sobre a árvore. Os agentes correm a **gen2** que
construíram. E `scripts/fixpoint_gate.sh` **assere `gen2 == gen3`, nunca `gen1 == gen2`** — o próprio
cabeçalho di-lo, e com razão: sob a cadeia 0.3.1.0 a gen1 vem de um gerador diferente, logo `gen1 != gen2`
é a forma saudável.

**Consequência que ninguém escreveu ainda:** se a gen1 e a gen2 divergirem em **comportamento** (não só em
bytes), as pernas `test` medem a gen1 e ninguém mede a gen2 — e uma rejeição que só a gen2 perde é
**invisível ao CI por construção**. Isso é um buraco de cobertura, não um defeito de fixture.

**O PASSO que o fecha** (e é um passo, não um alarme — lei de forma do dono): correr as duas linhas com a
**gen1** e com a **gen2** da MESMA árvore e comparar. Três resultados possíveis, e cada um diz o que fazer:

| resultado | significado |
| --- | --- |
| gen1 rejeita, gen2 **não** | **a gen2 perdeu a guarda.** É defeito real e o CI não o vê. O mais grave dos três |
| as duas rejeitam | o que os agentes viram vem da semente deles (`bootstrap/teko.c`), não da gen2 — e aí a lição da semente aplica-se |
| nenhuma rejeita | o CI está a medir outra coisa, e a pergunta muda para *o que o asset publicado é de facto* |

**Custo: uma escada, que o agente já constrói de qualquer maneira.** É o próximo despacho depois do `.exe`.

**A LIÇÃO, e é geral:** um agente que semeia de `bootstrap/teko.c` está a construir a partir da SAÍDA
desta árvore, não do release. As falhas que ele vê e o CI não vê podem ser artefactos dessa diferença —
como já aconteceu hoje com "três erros de tipo" que eram do binário obsoleto. **Um relatório de agente
que nomeia uma regressão tem de dizer com que semente correu**, e o integrador tem de a confrontar com
o CI antes de a pôr na fila. **Mas confrontar não é dispensar:** quando o agente e o CI discordam, o
resultado é uma DISCREPÂNCIA a medir, não um lado a acreditar. Eu fiz as duas coisas erradas em sequência —
primeiro aceitei sem confrontar, depois dispensei sem medir.

### CRUMB 5 do AArch64-ELF — NÃO APLICADO, e a razão é medição, não preguiça

O agente deixou-mo por ser workflow (só o integrador toca `.github/workflows/`). **Medi antes de aplicar, e
ele ficou em grande parte OBSOLETO pelo próprio dreno do crumb 3:**

- os **21 skips** da perna `linux-arm64-glibc` eram **todos** `unsupported TEKO_TARGET "arm64-linux"`. Com
  `Arm64Linux` a existir, vão a **zero** sem tocar em CI. A metade valiosa do crumb 5 aconteceu sozinha.
- o que sobraria era acrescentar `no_skips_gate.sh` + provisionar wasmtime aarch64. **E aí colide:**
  `scripts/no_skips_gate.sh` rejeita **qualquer** skip, incluindo a linha wasm — logo, sem wasmtime, a perna
  ficaria vermelha pela linha wasm. **Mas pôr wasmtime numa perna de teste faz `scripts/wasm_known_stop_gate.sh`
  ficar VERMELHO por desenho** (ele assere que existe **exactamente um** provedor de motor wasm, o
  `regressor-full`), e retirar esse pin é a *promoção* que o dono ruleou ser trabalho da versão dedicada do
  wasm: *"KNOWN-STOP, wasm terá a própria versão para refinar."*

**Portanto é uma colisão entre dois rulings do dono** (skip é falha × wasm refina na sua versão), e negociação
de KNOWN-STOP é **dono↔integrador**, nunca de agente. **Fica para ele decidir, com o número na mão:** depois do
crumb 3, quantos skips restam de facto na perna arm64? Se for **só a linha wasm**, o pin já cobre e não há nada
a fazer. **A próxima execução do CI sobre `36b2ab45` ou posterior responde** — é a primeira com `Arm64Linux`.

### O PATCH DO AGENTE **NÃO** DEVE SER APLICADO VERBATIM, se algum dia entrar

Ele propôs `run: … teko test . 2>&1 | tee teko-test.log`. Isso **reintroduz** o defeito que custou a esta lane
um `exit 127` opaco no Windows: os passos correm com `-e -o pipefail`, e sem `set +e` o teste que falha mata o
passo antes do gate. E `rc=$?` depois de um pipe dá o estado do **último** comando do pipe. A forma correcta
está no passo do Windows em `pr.yml`: `set +e` → comando → `rc=$?` → `set -e` → `cat` → gate.

## 1. A escada de degraus — onde está

Cada paragem do backend nativo é um "degrau". A escada é o produto desta lane: enquanto ela não
fechar, o ponto de fixo nativo não fecha e as duas pernas nativas ficam vermelhas **por desenho**.

| degrau | o quê | estado |
| --- | --- | --- |
| 24 | `f64_bits`/`f64_from_bits` — alias do próprio VReg | **fechado**, confirmado no CI |
| 25 | união-nula em colocações sem tipo declarado | **fechado**, confirmado no CI |
| 26 | `append_fo` sem lowering, em `teko::codegen::cb` | **fechado e DRENADO** — confirmado: já não aparece |
| **27** | **builtin `ftoa` sem lowering**, em `teko::codegen::cb_f64_literal` | **ABERTO — é a paragem viva do self-host**, idêntica em `artifact/linux-x86_64-glibc` e `artifact/linux-arm64-musl` |
| **29** | **`A4-fp`: codificação de operação de float / FPR em arm64**, em `own_arith_exit` | **ABERTO — descoberto ao drenar o 28.** É o **gémeo arm64** do arco `b1-fp-x86`, que fechou os floats só para x86-64 |
| **28** | **atribuição a elemento de slice (`s[i] = v`) sem lowering**, em `own_native::f_implicit_widen_targets` | **ABERTO, e é REGRESSÃO DO MEU DRENO** — parte a linha `own_arith_exit` em **todas** as pernas |

Texto exacto das duas, do log completo (§2c):

```
teko: .: native backend N1: builtin `ftoa` not yet lowered (N2) [in `teko::codegen::cb_f64_literal`]
teko: examples/regressions/own_native: native backend N1: slice element index-assignment not yet lowered (N2) [in `own_native::f_implicit_widen_targets`]
```

**O 28 é um caso de escola, e o erro é meu.** `f_implicit_widen_targets` é a fixture do alargamento
implícito que veio do arco da **aridade numérica** — que eu drenei. Ela escreve num elemento de slice,
e o backend nativo não sabe lá chegar. Ou seja: **a fixture que provava a aridade é ela própria fora
do alcance do backend**, e eu drenei-a sem que nenhuma perna nativa a tivesse compilado. É a SEGUNDA
VAGA a chegar exactamente onde estava avisado — e chegou por um dreno meu, não por descoberta do
self-build. **Não a "conserte" mudando a fixture para evitar o slice:** isso troca uma paragem honesta
por cobertura fingida. `s[i] = v` é linguagem corrente; o lowering é que falta.

**A SEGUNDA VAGA, e não a esqueça:** os degraus são só o que o SELF-BUILD encontra. O corpus, as
regressões e os `.tkt` **nunca** foram compilados pelo backend nativo em CI, porque o ponto de fixo
falha antes do job `test`. **Não prometa "faltam N degraus".** O degrau 28 é a prova: apareceu sem
que o self-build tenha avançado um passo.

## 2. AS CINCO BRANCHES — TODAS DRENADAS (2026-07-30, depois do segundo reinício)

**Estado: DRENADAS.** O vagão está em `8f94c0b1` e as cinco entraram, nesta ordem: degrau 26,
`b1-fp-x86`, `saida-equalizada`, `aridade-numerica`, `mingw-fora-da-rota-c`. O CI deste SHA é a
primeira corrida em que o ponto de fixo nativo pode passar do degrau 26 **e** as três pernas que
tinham `B1-args` podem ficar verdes de uma vez.

**Depois do dreno, também feito:** mingw removido dos DOIS workflows (nenhuma invocação resta), com o
ruling de 2026-07-27 registado como **superseded por medição** em vez de apagado.

### TRÊS COISAS APRENDIDAS NO DRENO, que valem mais que ele

1. **O conflito em `lower_cast` tinha as duas resoluções ingénuas ERRADAS.** `cast_unop_of` mudou de
   assinatura numa das branches (passou a devolver `LUnOp | error`). "Ficar com o nosso" **não
   compilaria** (o vagão chamava-o inline); "ficar com o deles" **perderia calada** a correção do
   valor errado do `f32`. Eram duas guardas **disjuntas** de dois agentes no mesmo dispatch, e ambas
   tinham de sobreviver. Só apareceu por ler as TRÊS versões da função (base, vagão, branch) em vez de
   compor à vista.
2. **A conferência de integridade deu um FALSO POSITIVO que quase apagou cobertura.** Acusou três
   cenários duplicados no `.tkr`; eram os pares `own-native` + `C route`, que é o padrão do projeto, e
   o detetor colapsava-os por apanhar só o primeiro token do `Scenario:`. **Chave correta é a LINHA
   inteira do cenário.**
3. **"Manter os dois lados" num conflito aditivo duplica o que o git já juntou FORA do hunk.** Foi o
   que aconteceu, e foi a conferência de marcadores que apanhou (`main.tks` ainda tinha `<<<<<<<`
   depois de eu ter dado o merge por resolvido, porque o `tail -6` do output do merge me escondeu dois
   conflitos).

### O QUE ENTROU EM CADA UMA (para o histórico)

Os SHAs abaixo são os das branches **como estavam ao serem drenadas**. Todas estavam integralmente
empurradas quando o container reiniciou — zero commits à frente do remoto, verificado um por um. **Os
agentes morreram com o container; o trabalho não, e foi a regra de "empurrar sempre" que o garantiu.**

### `cargo/0.3.1-aridade-numerica` @ `f1ee57df` — 19 ficheiros, +1290
Ruling do dono: menor cabe em maior sem cast; estreitamento **nunca trunca** e panica em runtime só
se não couber; `teko::casting` é a forma recuperável (`T | error`).
- `fix(nativo)`: a guarda de ajuste em runtime que faltava na rota nativa
- `feat(checker)`: alargamento implícito, e **`teko::casting` ganhou o seu primeiro consumidor**
- fixtures por VALOR nas duas rotas
- **Faixa de saída usada: 170–189.** Toca `src/checker/typer.tks`, `bigint`, `dec`.

### `cargo/0.3.1.0-degrau-26` @ `8e3df0d7` — 4 ficheiros, +262
`append_fo` baixado para o append de bytes em bloco do runtime; fixtures por valor nas duas rotas.
- **Faixa de saída usada: 190–192.** Toca `src/lir/lower.tks`.

### `cargo/0.3.1-mingw-fora-da-rota-c` @ `ec25b511` — 6 ficheiros, +928
Quatro peças: guarda que grita na rota C, clang+MSVC no Windows, `-v` na falha de link, cross que
para no objeto. **E agiu sobre a sonda que eu corri**: a peça 2 encolheu para o que a medição
mostrou, e o `link` cru saiu da lista de candidatos.
- Também: linha de link do Windows MSVC-correta (`-lm` fora, `/Brepro` no lugar do flag GNU)
- Toca `src/build/project.tks`, `src/build/linker.tks`, `src/build/regression.tks`

### `cargo/0.3.1-b1-fp-x86` @ `89870d1e` — 12 ficheiros, +2157
- **`B1-args` (argumentos em pilha) e `B3-xmm-callee-saved` FECHADOS** — o `B1-args` era a única
  falha real em TRÊS pernas ao mesmo tempo (macOS, musl x86-64, Windows)
- família SSE2 pinada **por byte**, cruzada contra o `as` da GNU
- **achado próprio:** *"the LIR lost float WIDTH, and f32 computed the wrong value"* — outro valor
  errado calado
- **Faixa de saída usada: 150–159.** Toca `src/lir/lir.tks`, `src/lir/lower.tks`, `isel_x86_64`,
  `encode_x86_64`, `regalloc_x86`

### `cargo/0.3.1-saida-equalizada` @ `ff293632` — 6 ficheiros, +195
Equaliza o código de saída ao byte baixo. **Eu mapeei três portas; ele achou QUATRO.** Toca
`src/runtime/teko_rt.{c,h}`, `src/lir/lower.tks`, `src/codegen/codegen.tks`.
- **Faixa de saída reservada: 200–209.**
- Quando isto drenar, a regressão `defer_cascade_exit` passa a verde no Windows **sem tocar no
  harness**.

### A ORDEM QUE FOI USADA, e funcionou
`src/lir/lower.tks` é tocado por quatro delas. A ordem por delta crescente evitou conflito nos três
primeiros; o quarto (aridade) deu quatro conflitos, todos resolvidos por composição de peças provadas.

**Antes de cada dreno**, o ritual que evitou duas regressões hoje: conferir chaves `{`/`}` e
`/**`/`*/` balanceadas, códigos de saída duplicados, chamadas sem definição no corpus, e correr os
gates estruturais (`objfile_gate_test.sh`, `wasm_known_stop_gate.sh`,
`native_selfhost_known_stop_test.sh`, `ci_gate_coverage.sh`).

**A LACUNA QUE ESTE DRENO EXPÔS, e o conserto — com uma correção do dono dentro.** Os gates acima
conferem *que ficheiros* entram e a *forma* do CI; **nenhum confere se a soma ainda funciona.** Cinco
branches verdes em separado não fazem um vagão verde: o dreno acendeu quatro pernas por **um** teste,
e o `src/lir/lower_test.tkt` foi **auto-fundido pelo git sem conflito** produzindo expectativas que não
batem com o `lower_cast` fundido — o git junta duas edições de teste e o resultado corresponde a
nenhuma das duas.

Eu propus como conserto "construir gen1 e correr a suíte". **O dono corrigiu: os testes correm na
gen2/gen3, não na gen1.** A gen1 é construída pelo compilador LANÇADO; a gen2 é a primeira construída
pelo compilador novo a partir do fonte novo, e é nela que a suíte tem sentido. O ritual correto é
`scripts/fixpoint_gate.sh` (que produz gen2 e gen3 e prova gen2 == gen3) **e a suíte sobre a gen2** —
não a gen1. A minha corrida local com gen1 achou a falha por acaso, porque era de tipagem de teste;
com outra classe de defeito teria mentido.

## 2b. REGRESSÕES DO DRENO (2026-07-30) — duas, e uma NÃO está explicada

O dreno das cinco branches ficou verde no ritual local mas **acendeu duas pernas que estavam verdes**.
Ambas são minhas: eu drenei um arco que muda o Windows **sem a medição no Windows que eu próprio
declarei necessária** no mesmo dia. A regra existia; não a apliquei ao meu dreno.

### `artifact / windows-x86_64` — EXPLICADA, conserto a decidir por medição
O link passou a ir por `link.exe` da MSVC (era o objetivo) e morre em **seis símbolos de inteiro de
128 bits**:

```
__divti3 __udivti3 __modti3 __umodti3 __floattidf __floatuntidf   (LNK1120)
```

São helpers que o mingw trazia pela **libgcc**; a MSVC não tem libgcc e o **compiler-rt do clang não é
ligado por omissão em alvo MSVC**.

**Medido na árvore:** a linguagem **rejeita** `i128`/`u128` na superfície (fixtures de compile-fail
`reject_i128`/`reject_u128`) e **nenhum `.tks` invoca** os helpers — mas `teko_rt.h` tem **56**
ocorrências de `__int128`, e os braços i128 dentro de `tk_div`/`tk_rem`/`tk_int_to_float` é que puxam
os builtins.

**Dois consertos, e a sonda decide:** (a) ligar o compiler-rt do clang, se a lib existir na imagem;
(b) excluir os braços i128 em alvo MSVC, que são inalcançáveis da superfície. A sonda
(`theory/sonda-toolchain`) foi estendida para reproduzir o `LNK2019` e medir três candidatos: clang
nu, `--rtlib=compiler-rt`, e a lib de builtins nomeada. **Uma vaga de agente está guardada para este
conserto.**

### `test / linux-arm64-glibc` e `Memory paranoid (linux-arm64-glibc)` — METADE explicada

**Explicado: 21 skips.** As linhas que precisam do alvo próprio do host saltam porque **`arm64-linux`
não existe em `NativeTarget`**:

```
unsupported TEKO_TARGET "arm64-linux" — supported: x86_64-linux, x86_64-windows, arm64-macos, wasm32-wasi, ...
teko: regressions 10 run, 21 skipped, 1 failed (8 builds)
```

O corpus cresceu muito com os cinco drenos e **toda** linha own-native nova salta ali. Sob a lei do
dono, skip é falha. **É exatamente o que o agente dos crumbs AArch64-ELF está a corrigir** (crumb 3
cria `Arm64Linux`); quando aterrar, as 21 correm.

**EXPLICADO em 2026-07-30 com o log completo (§2c).** O `1 failed` e o exit 134 são **duas coisas
distintas**, e a minha leitura anterior confundia-as:

- **exit 134 = SIGABRT do `teko-tktest`.** A fase de testes unitários **ABORTA na PRIMEIRA assertiva
  que falha** e não continua. A fase de regressões corre depois e propaga o 134 no fim.
- **`1 failed` é da fase de REGRESSÕES**, não dos unitários — é a linha `own_arith_exit`, e a causa é
  o **degrau 28** (§1), que é uma regressão do MEU dreno.

**A CORREÇÃO QUE ISTO IMPÕE AO MEU PRÓPRIO REGISTO.** Eu escrevi que falhava **"um teste em 849"**.
Isso não é demonstrável a partir desta prova: como a fase unitária aborta no primeiro `assert` falhado,
**o que está atrás do primeiro nunca correu**. E o primeiro falhado é **diferente por host**:

| perna | primeiro `assert` a falhar | nota |
| --- | --- | --- |
| `linux-arm64-glibc`, `linux-x86_64-musl` | `str_contains` em `teko::lir::lwt_prim_kind_of_resolves_enum_to_int_cast_widens` | expectativa desatualizada, já na fila |
| `macos-arm64` | `is_true` em `teko::build::pt_target_name_and_objfmt_are_one_source` | **NOVO, não estava registado em sítio nenhum** |

Logo há **pelo menos DOIS** testes unitários a falhar, não um, e quantos estão atrás de cada abort é
**desconhecido**. Consertar o do cast não garante verde — garante ver o próximo.

**O de macOS é um GÉMEO QUE DIVERGIU, e é o achado mais interessante do dia.** O corpo de
`pt_target_name_and_objfmt_are_one_source` (`src/build/project_test.tkt:738`) é **inteiramente
independente do host**: as 14 assertivas comparam `target_name`/`target_objfmt`/`target_os_name` de
variantes **literais** de `NativeTarget` com literais de string. Nenhuma toca `host_target_for_os`.
Um teste sem dependência do host que falha **num** host não pode ser expectativa errada — é
**divergência de geração/runtime no arm64-macho**. Medido também que **não é novo**: falha igual na
execução 30508737150 (SHA `8f94c0b1`), portanto é a "1 falha" de macOS que eu tinha atribuído
inteiramente à regressão do own_native — eram **duas**, e eu contei uma.

**Recomendação anterior REVOGADA.** Eu tinha registado "não escavar antes do crumb 3, porque o log não
cabe na cauda". A premissa era falsa: o log completo sempre foi obtível (§2c). A escavação custou
quatro chamadas.

## 2c. O INSTRUMENTO QUE FALTAVA — log INTEGRAL de CI, e uma correção a mim mesmo

O dono, 2026-07-30: *"Quanto aos logs, que só consegue a cauda, pode instruir o CI a guardar o log
completo como artefato quando uma falha ocorrer, assim consegue baixar o artefato para analisar. Use
theory para isso."* **Ele tinha razão sobre o problema e eu estava errado sobre a causa.** O problema
era real — eu lia CI por `get_job_logs`, que devolve uma **cauda** (`tail_lines`, 500 por omissão), e
diagnosticava pernas com 180 KB de log por 500 linhas do fim. O que estava errado era supor que fazia
falta **mudar o CI**.

**MEDIDO (execuções 30509216571 e 30508737150 da PR #99):**

| pergunta | resposta medida |
| --- | --- |
| `get_workflow_run_logs_url` + `curl` dá o log completo? | **sim** — 660 KB comprimidos, **225 ficheiros**, 2.5 MB de texto, **21 jobs**, um ficheiro por PASSO, nada truncado |
| funciona em execuções que já passaram? | **sim**, retroativamente |
| funciona numa execução **em curso**? | **não** — 404, e o 404 vem já no pedido do URL (medido na 30509727122) |
| e artefactos normais, dá para os descarregar? | **sim** — `download_workflow_run_artifact` devolve URL assinado, e o `curl` do sandbox traz o ZIP (provado com `teko-c-macos-arm64`, 1.2 MB → `teko.c` de 10.6 MB) |

**Fixado em `scripts/ci_full_log.sh`** (guardas provadas por inversão), com o comando que interessa:

```
grep -rn 'assertion failed\|native backend N1\|Process completed with exit code [^0]' <dest> | cut -c1-220
```

**RECOMENDAÇÃO, e é NÃO mexer em `pr.yml`.** O artefacto-em-falha resolveria uma cegueira que já não
existe, ao preço de `upload-artifact` em 27 jobs — churn de CI na lane, contra a barra do tronco. Fica
**uma** fronteira registada e não implementada, porque mexe em `pr.yml` e precisa da palavra do dono:
**enquanto a execução corre, o log completo não existe**; para espiar uma perna vermelha antes do fim,
só a cauda por job serve.

**O QUE NENHUM DOWNLOAD DESFAZ, e é um achado à parte:** quando uma linha de regressão falha, o
**nosso** harness imprime `captured output tail:` e **corta**. Esse truncamento é do produto, não do
GitHub. Não custou nada hoje (a cauda continha o diagnóstico), mas custará no dia em que o erro
estiver no meio.

## 3. FILA — não despachado

**Vagas: 4 de 4 OCUPADAS** (teto 4). A correr: **AArch64-ELF crumbs 2–5**, **`MRelocKind::None`**,
**`fmt --apply`**, **remoção dos 128 bits** (que é o conserto do Windows). **Nada sai daqui até uma
vaga abrir** — ruling do dono: *"Se já tem 4 agentes, segura sua onda, enfileire."*

**Ordem recomendada quando abrir vaga:** (1) degrau 28, porque é regressão de dreno e parte TODAS as
pernas; (2) o teste do cast, que destranca quatro; (3) o gémeo de macOS; (4) degrau 27.

| item | porquê | nota |
| --- | --- | --- |
| **Degrau 28 — lowering de `s[i] = v`** | **regressão do meu dreno**, e parte a linha `own_arith_exit` em **todas** as pernas (macOS, x86_64-musl, arm64-glibc, regressor). Prioridade 1 | `native backend N1: slice element index-assignment not yet lowered (N2) [in own_native::f_implicit_widen_targets]`. **Não mudar a fixture para evitar o slice** — trocaria paragem honesta por cobertura fingida |
| **Consertar `lwt_prim_kind_of_resolves_enum_to_int_cast_widens`** | é o **primeiro** `assert` a falhar em `linux-arm64-glibc` e `linux-x86_64-musl`, e a fase unitária **aborta** ali (SIGABRT/134) — logo destranca a visão do resto, não necessariamente o verde | expectativa desatualizada, não defeito: afirma `%1 = sext %0`, e a aridade decidiu que um alargamento sem perda **não emite conversão nem guarda** (`lower_cast_fit_guard` começa por `if cast_is_lossless_widen { return ctx }`). **Não apagar** — tem de passar a afirmar a AUSÊNCIA da conversão. O sinal negativo já está provado por VALOR em `f_cast_widen_keeps_value` (`-5 to i64`) e no alargamento implícito (`-2147483648`), nas duas rotas |
| **Gémeo divergido de macOS: `pt_target_name_and_objfmt_are_one_source`** | teste **sem dependência do host** que falha **só** em `macos-arm64` (`assertion failed: is_true`) → divergência de geração/runtime no arm64-macho, não expectativa errada. Não é novo (falha já em `8f94c0b1`) | mandato: **primeiro dividir o teste** para saber QUAL das 14 assertivas cai (o rasto só dá `+636` no símbolo), depois caçar a divergência de lowering. Instrumento certo: `agent-fast-lane.yml` com `runner: macos-latest`, que É despachável. Sob a regra do oráculo, divergência é bug do nativo até prova em contrário |
| **SEGUNDA PASSAGEM DO DEBUGGER — brief pronto, ver §3b** | o dono leu o orçamento e reprovou: *"o trabalho do arquiteto foi pessimo, nao tem um exemplo de prova de conceito, de como seria a superficie para isso ou como utilizar em cada tipo de debugger mencionado"* | **a falha é do MEU brief**, não do arquiteto: pedi orçamento e não pedi PoC, superfície, nem contra-medida. Entra na próxima vaga |
| **O Windows não põe `.exe` no executável** | **ACHADO NOVO, 2026-07-30, e é DEFEITO DE PRODUTO.** Descoberto porque o dreno dos 128 bits destrancou o `artifact / windows-x86_64`: ele passou a publicar, e a perna `test / windows-x86_64` — **que nunca tinha corrido em toda a lane** — falhou logo com `ERROR: the producer's upload has no dl/windows-x86_64/teko.exe`, tendo publicado `teko` | **medido: `src/build/` não acrescenta `.exe` em host nenhum.** O ficheiro É um PE válido (`assert_asset_arch` passou), mas o Windows resolve um nome sem extensão **acrescentando** `.exe`, logo `teko` não é lançável por nome. **O CI está correcto nos dois lados** — `produce_assets.sh` já trata `*.exe` e o consumidor espera `teko.exe`; é o produto que erra. Conserto: o nome do executável ganha `.exe` quando o alvo é Windows. Segunda vaga outra vez: consertar uma perna acendeu outra que nunca tinha corrido |
| **`kind` desconhecido panica + o kind `tool` novo** | **RULING DO DONO 2026-07-30**, duas ordens: *"esse else não deveria fazer fallback mas causar panico, kind desconhecido é erro no compilador"* e *"Tem que adicionar o novo kind proposto"*. Pré-requisito de `tdb` | **MEDIDO, e não é um enum de uma linha.** Ver §3c abaixo: a árvore codifica a dicotomia "Binary ou não-Binary" em TRÊS sítios, e `Tool` quebra-a por ser executável (tem `main`) **e** empacotável |
| **Degrau 27 — builtin `ftoa`** | é a paragem VIVA do self-host nas duas pernas nativas; a escada não avança sem ela | `native backend N1: builtin 'ftoa' not yet lowered (N2) [in teko::codegen::cb_f64_literal]`. O pin `scripts/native_selfhost_known_stop.sh` já a aceita como paragem honesta (deixou de nomear o degrau, de propósito) |
| **`fmt --apply` explícito** | dono aprovou: *"Sim: fmt --apply explícito"* | o meu despacho foi **recusado na camada de permissão** logo depois; nunca chegou a correr, e eu não o repeti (chamada recusada trata-se como decisão). **Precisa da palavra do dono para andar.** Contrato pinado em `scripts/fmt_cli_test.sh` |
| **Híbrido do `main`** | desenho **fechado** no digesto de leis | precisa do arquiteto para ordenar crumbs |
| **AArch64-ELF crumbs 2–5** | crumb 1 (relocação) fechado e provado em hardware | crumb 3 cria `Arm64Linux` em `NativeTarget` e destranca a perna arm64-Linux |
| **`MRelocKind::None`** | `plain_word`/`branch_word` (`encode_arm64.tks:117,139`) põem `Call` como default inerte — o valor "branch" como default de um campo que toda instrução carrega. Foi a semente do bug de relocação | mata a classe na raiz |
| **Debugger, Camada 1** | orçamento entregue e drenado (`docs/design/debugger-orcamento-0.3.1.md`) | 6 crumbs; recomendação é parar ali |


## 3b. BRIEF PRONTO — segunda passagem do debugger (o dono reprovou a primeira)

**A crítica do dono, 2026-07-30, verbatim:** *"eu li o doc do debugger e o trabalho do arquiteto foi
pessimo, nao tem um exemplo de prova de conceito, de como seria a superficie para isso ou como
utilizar em cada tipo de debugger mencionado. Embora eu nao tenha pedido um debugger proprio, ja que
ele levou mais de uma hora pra produzir isso, poderia ter orcado o restante dos pontos e tambem a
contra-medida (debugger proprio)."*

**A CULPA É DO BRIEF, E O BRIEF É MEU.** Eu pedi *"orçar a implementação de um debugger"* e o
arquiteto orçou exactamente isso, com quatro experimentos medidos e sete correções ao esboço do dono
— trabalho sólido no que foi pedido. O que **eu** não pedi, e o dono queria: prova de conceito, a
superfície de utilização, o uso por debugger, o orçamento das camadas restantes, e a contra-medida.
Um arquiteto que corre mais de uma hora tinha orçamento de sobra para as cinco. **Lição: quando o
pedido é "orça X", perguntar antes se o dono quer também o custo de NÃO fazer X.**

**O QUE A SEGUNDA PASSAGEM TEM DE ENTREGAR — cinco peças, nenhuma opcional:**

1. **PROVA DE CONCEITO REAL.** O Experimento D já produziu um objeto que gdb *e* lldb aceitaram. Isso
   tem de virar artefacto reproduzível e versionado, não prosa: o `.tks` de referência, os bytes das
   três seções, e o comando que qualquer pessoa corre para ver o breakpoint parar. Sem isto o
   orçamento é uma promessa.
2. **A SUPERFÍCIE, concreta.** Qual é a flag? `teko build . -g`? Um perfil no `teko.tkp`? O que sai no
   `--help`? Onde ficam os bytes de depuração num `.tkl`? Isto está no orçamento como uma linha
   ("o interruptor de perfil") e tem de ser um desenho.
3. **USO EM CADA DEBUGGER MENCIONADO**, com o texto que o dono escreve/cola: gdb no terminal, lldb no
   terminal, VSCode via `cppdbg`, VSCode via CodeLLDB. Um `launch.json` completo por cada, não uma
   referência a "um exemplo em docs/".
4. **AS CAMADAS RESTANTES ORÇADAS**, não "o penhasco": Camada 2 (com a sondagem dos nomes através do
   regalloc identificada como crumb próprio e o resto orçado *condicionalmente* a ela), Camada 3, e
   Windows/CodeView com número. "5+ crumbs, um deles perigoso" não é orçamento.
5. **A CONTRA-MEDIDA: DEBUGGER PRÓPRIO, ORÇADO.** O dono não pediu um, e a recomendação de não fazer
   pode manter-se — mas uma recomendação de não fazer **sem o custo do que se recusa** não é
   decidível. Orçar: ptrace/`mach_vm`, breakpoints por `int3`/`brk`, leitura da nossa própria tabela
   de linha (que a Camada 1 cria de qualquer forma), e um adaptador DAP. E dizer o que um debugger
   nosso daria que gdb/lldb **não** dão — se a resposta for "nada", isso é a prova da recomendação, em
   vez de a asserção que está lá hoje.

**RESTRIÇÕES:** o arquiteto **não implementa produto**; escreve em `docs/design/`. Não abre PR. Empurra
para a branch em que trabalha assim que escreve. Nunca toca `.github/workflows/**`.


## 3c. `kind = "tool"` — medido, e a armadilha que triplica o trabalho

**Ordens do dono, 2026-07-30:** (1) *"esse else não deveria fazer fallback mas causar panico, kind
desconhecido é erro no compilador"*; (2) *"Tem que adicionar o novo kind proposto"*.

**Onde vive:** `src/build/tkp_rule.tks:9` — `type Artifact = enum { Binary; Static; Shared; Package }`.
O fallback silencioso está em `src/build/manifest.tks:565`.

**A ARMADILHA, e é o que faz isto não ser um enum de uma linha.** A árvore não codifica quatro kinds;
codifica uma **DICOTOMIA** — `Binary` contra tudo o resto — e escreve-a por extenso:

```teko
// (C7.1m) The three non-Binary kinds are LIBRARY kinds — they forbid a main.tks.
fn check_main_file_rule(artifact: Artifact, has_main: bool) -> Artifact | error {
    if artifact == Artifact::Binary && !has_main { return error { … "requires a main.tks" } }
    if artifact != Artifact::Binary && has_main  { return error { … "may not have a main.tks" } }
```

E outra vez em `src/build/project.tks:3094`: `if m.artifact != Artifact::Binary { return base }`.

**`Tool` quebra a dicotomia**, porque é as duas coisas ao mesmo tempo: **é executável** (tem `main.tks`,
compila como executável normal na máquina do dev) **e é empacotável** (emite um `.tkl`). Logo
`artifact != Artifact::Binary && has_main` **rejeitaria** um `tool` legítimo, e a mensagem de erro
mentiria dizendo *"a library project (static/shared/package)"*.

**Portanto o conserto certo NÃO é acrescentar um membro e remendar os `if`.** É trocar os testes de
VARIANTE por testes de PROPRIEDADE — algo como `artifact_requires_main(a)` e
`artifact_is_packageable(a)` — de modo que acrescentar um kind futuro não obrigue a caçar dicotomias
espalhadas. É o mesmo padrão que fechou o degrau da relocação: **tornar o estado errado
inexpressável**, em vez de corrigir cada sítio que o expressa.

### CORRECÇÕES DO DONO, 2026-07-30 — e a metade que faltava na dele

**(1) O `if` ajusta-se, não se refactoriza.** Verbatim: *"é visível que SIM tem que acrescentar o novo
tipo E ajustar o if adicionando um AND (&&) NOT (!=) Tool"*. Aceito: a forma é
`artifact != Artifact::Binary && artifact != Artifact::Tool && has_main`.

**MAS A DELE É NECESSÁRIA E NÃO SUFICIENTE, e a razão é um corolário que ele próprio ratificou:**
*"uma paragem que dispara para 1 dos 4 membros de uma família é pior que nenhuma."* `check_main_file_rule`
tem **DOIS** `if`, e ele nomeou o segundo:

```teko
if artifact == Artifact::Binary && !has_main { return error { … "requires a main.tks" } }   // <- ESTE também
if artifact != Artifact::Binary && has_main  { return error { … "may not have a main.tks" } }
```

Se só o segundo levar `&& != Tool`, um **`tool` SEM `main.tks` passa em silêncio** — e um `tool` sem
`main` não tem comando para instalar. O primeiro `if` tem de virar `(Binary || Tool) && !has_main`.
Um buraco no sentido oposto é a mesma classe de defeito.

*(Nota: quando se escreve `Binary || Tool` num sítio e `!= Binary && != Tool` no outro, isso **é** o
predicado — dar-lhe nome é só grafia, e é barato. Mas a decisão da forma é do dono, e ele escolheu o
`&&`; a metade que falta é o que não é negociável.)*

**(2) Um `tool` em `[deps]` NÃO é recusado — é TOLERADO e IGNORADO.** Verbatim: *"Não precisa recusar,
só não precisa existir como dependência, e se existir (aqui sim tem trabalho) precisa ser ignorado pelo
compilador (para não importar/linkar)."*

Ele tem razão que aqui há trabalho, e **localizei-o**: `src/build/project.tks:215-232`,
`load_deps_program`. O laço faz, por cada entrada de `m.deps`, um `load_dep_program(m.deps[di])` que lê
o `.tkb` do dependente e **injecta os seus itens** no ambiente de tipos.

```teko
if di >= m.deps.len { break }
let dep_prog = match load_dep_program(m.deps[di]) { … }
```

**O conserto: resolver o KIND do dependente ANTES de carregar o `.tkb`, e saltar quando for `Tool`.** E
uma consequência que simplifica: como um dependente Teko entra por **injecção de itens**, saltar o
carregamento **salta o link por construção** — não há um segundo sítio a tratar. O que existe hoje é o
oposto do que se quer: `load_dep_program` vai directo ao `.tkb`, sem consultar o kind.

**A fixture obrigatória:** um projeto que declara um `tool` em `[deps]` **constrói**, e um símbolo do
`tool` **não é resolúvel** no projeto. Provar as duas metades — que não estoura E que não importa. Só a
primeira passaria se o `tool` fosse carregado e por acaso não colidisse.

**A ORDEM DOS CRUMBS, e ela importa:**

1. **`kind` desconhecido passa a ERRO DURO**, com a lista dos aceites na mensagem. Sozinho, e primeiro
   — porque enquanto o fallback existir, `kind = "tool"` escrito por alguém é silenciosamente um
   binário comum, e um verde sobre isso não significa nada. A fixture é de compile-fail sobre um `.tkp`
   com `kind = "binari"`.
2. **A dicotomia vira predicado.** Refactor sem mudança de comportamento — os quatro kinds actuais têm
   de dar exactamente as mesmas respostas. Prova: as fixtures existentes de `check_main_file_rule` sem
   uma alteração.
3. **`Tool` entra**, e entra num sítio onde a grafia errada grita e onde a dicotomia já não existe.
   Fixtures: um `tool` **com** `main.tks` é aceite; a mensagem de erro do caso de biblioteca deixa de
   mentir sobre quais são os kinds de biblioteca.
4. **O `.tkl` de um `tool`** — o que o escritor de pacote põe lá dentro, e o que `[deps]` recusa. É aqui
   que vive a parte que o dono nomeou: *"sem adicionar como dependência de projeto (não entra nas
   dependências do tkp)"*.

**Referência nomeada e aplicável: C#.** `dotnet tool` (`PackageType=DotnetTool`) é o único dos quatro
com um TIPO de pacote declarado; `cargo install` e `go install` dão o mesmo efeito instalando algo que
por acaso tem binário, **sem** tipo próprio. O dono atribuiu C# para addins, e aqui aplica-se de facto.


## 3d. O `.exe` do Windows — medido, e o brief está pronto

**O sintoma:** `test / windows-x86_64` morre em `ERROR: the producer's upload has no dl/windows-x86_64/teko.exe`.
O produtor publicou `teko`. O CI está correcto nos **dois** lados (`produce_assets.sh` já trata `*.exe`, o
consumidor espera `teko.exe`); é o **produto** que nomeia a saída sem extensão em todos os hosts. Um ficheiro
PE chamado `teko` **não é lançável por nome**, porque o Windows resolve um nome sem extensão acrescentando
`.exe`.

**OS DOIS SÍTIOS, medidos em `src/build/project.tks`** — e são dois, o que faz disto um caso de família:

```
1827:    let binp = teko::str::concat(od, "/", stem)     <- rota C
2635:    let binp = teko::str::concat(od, "/", stem)     <- rota NATIVA
```

**Consertar só um é o defeito "um dos membros da família".** As duas rotas produzem executáveis e as duas
nomeiam-nos igual.

**E O DESENHO CERTO JÁ EXISTE, 800 linhas abaixo, no mesmo ficheiro** — não se inventa nada:

```teko
fn archive_output_path(od: str, stem: str, format: ArchiveFormat) -> str {
    match format {
        Coff => teko::str::concat(od, "/", teko::str::concat(stem, ".lib"))
        _    => teko::str::concat(od, "/", teko::str::concat("lib", teko::str::concat(stem, ".a")))
    }
}
```

O arquivo **já** é nomeado por formato de alvo (`.lib` em COFF, `lib*.a` no resto). O executável não. **O
conserto é um irmão desta função** — `binary_output_path(od, stem, target)` — chamado dos dois sítios, e
**não** um `if` improvisado em cada um. Assim, o próximo alvo que precise de sufixo entra num só lugar.

**O que o brief tem de exigir além disso:**
- **quem CONSOME `binp`** nos dois sítios — se algum passa o caminho ao linker, ao `chmod`, ou o imprime,
  todos têm de ver o mesmo nome. Um sítio que continue a montar o nome à mão é o defeito de volta.
- **`teko test .` e o harness de regressão**: se algum invoca o binário construído por nome derivado, tem de
  seguir o mesmo helper. Medir, não presumir.
- **fixture**: construir para alvo Windows e afirmar que o ficheiro emitido termina em `.exe`; e que nos
  outros alvos **não** termina em `.exe`. As duas metades — só a primeira passaria se o sufixo fosse posto
  em todos os hosts, o que partiria Linux e macOS.
- **não tocar** `produce_assets.sh` nem `pr.yml`: ambos já estão certos, e o segundo é do integrador.


## 4. DECISÕES DO DONO EM ABERTO

Uma só, e não é bloqueante para a lane:

- **`fmt --apply`: dizer se pode andar.** Ele **aprovou** (*"Sim: fmt --apply explícito"*), mas o
  despacho do agente foi **recusado na camada de permissão** imediatamente depois, e eu não o repeti —
  chamada recusada trata-se como decisão, não como falha. As duas coisas contradizem-se, então a
  próxima sessão deve **perguntar se foi clique errado** antes de despachar. Uma palavra basta.

Todo o resto levantado neste dia foi respondido e está executado ou registado.

## 5. LEIS E DIRETRIZES — onde vivem

O digesto é `docs/memory/teko-laws-digest.md`; a disciplina de trem é
`docs/memory/teko-stacked-train-discipline.md`. **Leia os dois antes de despachar qualquer coisa.**
O que segue é o que se usa em todo despacho:

### Sobre o dreno
- **`scripts/drain_guard.sh <ref>`** antes de todo merge: recusa um dreno que traga
  `.github/workflows/**`. Já existe e provou-se por inversão contra a minha própria branch de sonda.
  `--allow` para uma mudança de CI deliberada.

### Sobre agentes
- **Máximo 4 em paralelo** (o dono subiu para 5 e voltou a 4 em 2026-07-30: *"vamos manter o pace, 4
  agentes no máximo"*). Seis implementadores derrubaram o sandbox uma vez.
- **Esperar CI não é estado permitido.** Um agente que chegou a uma pergunta que só o CI responde está
  **TERMINADO**: escreve handoff (branch + SHA + qual pergunta ficou pendente + o que fazer com cada
  resposta), termina, e **liberta a vaga**. Com teto de 4, uma vaga em espera é 25% da capacidade
  parada.
- **Escreveu? Comita e empurra**, na branch onde estiver — `cargo/**` ou `theory/**`. Não é "quando
  estiver pronto", é ao escrever.
- **Empurrar SEMPRE**, a cada avanço, mesmo trabalho feio. Numa `cargo/**` custa ZERO e não dispara
  CI. **É esta regra que fez o reinício de hoje custar nada.**
- **NUNCA abrir PR.** O integrador dreno.
- **NUNCA autorar ficheiro sob `.github/workflows/`**, em nenhuma branch, incluindo `theory/**`. Se o
  workflow estiver num commit do agente, um cherry-pick de volta traz o CI restrito para o vagão — **e
  isso já aconteceu: foi assim que a fast-lane foi para a main numa sessão anterior.**
- **Force-push bloqueado para TODOS, inclusive o dono.** Push recusado: merge forward-only, ou
  committe local e reporte o SHA (o object store é partilhado entre worktrees).
- **O agente não cunha KNOWN-STOP** — levanta red-flag. Negociação é entre o dono e o integrador. Ele
  PODE promover um known-stop que ficou vermelho por o vão ter fechado.
- **`theory/**` é o campo de provas.** Um push a `theory/**` dispara a `agent-fast-lane.yml`
  (validação completa). **O agente empurra e SEGUE — nunca espera**, porque não tem acesso à API do
  GitHub (403) e ficaria ocioso **e cego**. Ele reporta a branch e a pergunta; **ler o CI é trabalho
  do integrador**.
- `workflow_dispatch` **não** existe para workflow que só vive numa theory (404 — o GitHub só o expõe
  a partir do branch default). A **fast-lane vive no default**, logo o integrador consegue pedir-lhe
  `runner: windows-latest`/`macos-latest`/`ubuntu-24.04-arm`.
- **Faixas de código de saída fechadas nas duas pontas.** Houve duas colisões por faixas abertas.
  Ocupadas: 100, 130–140, 150–159, 160–169, 170–189, 190–199, 200–209. **A próxima começa em 210.**
- **Nunca `git add -A` em `examples/`** — um agente levou seis binários de build assim.

### Sobre o produto
- **`bootstrap/teko.c` é SAÍDA, não entrada.** Nunca se toca, nem os campos de versão de `teko.tkp`.
  Ficheiros C de geração são construídos pelo CI, e só quando o CI passa verde.
  `src/runtime/teko_rt.{c,h}` é seed escrito à mão e **pode** crescer por razão genuína de runtime.
- **Regra do oráculo:** enquanto a rota C existe, divergência entre nativo e C é **bug do nativo até
  prova em contrário**. A prova em contrário já apareceu uma vez (a rota C pára onde o nativo acerta,
  num closure com parâmetro de união-nula) — registado para ninguém corrigir o nativo para imitar um
  defeito do C.
- **mingw é PROIBIDO.** Cross-compiling é objetivo declarado, **depois** dos nativos, e nunca por
  mingw. MSVC no Windows. Linker nativo em vez de `cc`/`gcc` quando houver binário fim-a-fim.
- **Zero menções a "VM"** em texto novo (só `docs/design/vm-retirement.md`).
- **`void` é banido. Sobrecarga é banida.** Ao invocar uma das quatro referências de desenho
  (superfície → Rust, controlo → Zig, addins → C#, comportamentos → Go), **verificar que a NOSSA
  superfície suporta o que a referência oferece.** Eu escorreguei nisto hoje.
- **W15:** só doc-comments `/** */` na declaração, zero `//` inline, sem valores mágicos.
- **SKIP conta como falha.** Limitação conhecida vira KNOWN-STOP: verde enquanto quebrado, VERMELHO
  quando fecha. Um KNOWN-STOP vermelho **não** significa necessariamente que o erro foi corrigido —
  pode ser que a direção mudou.
- **`fmt` NÃO é portão de CI.** É convenção, não imposição (dono). **Nunca armar `fmt --check`.** 16
  ficheiros em `src/` estão fora de formato; é dívida opcional.
- **Régua do tronco:** só vai ao tronco se passar pelo CI, sem erros, alertas ou erros escondidos que
  não disparam.

## 6. VERMELHOS DO CI, todos com causa conhecida

| perna | o quê | fecha com |
| --- | --- | --- |
| `artifact / linux-x86_64-glibc` e `linux-arm64-musl` | ponto de fixo nativo no degrau 26 | dreno do degrau 26 |
| `test / macos-arm64` | `B1-args` (1 falha; eram 2 antes da relocação fechar) | dreno do `b1-fp-x86` |
| `test / windows-x86_64` | `B1-args` + `defer_cascade_exit` | `b1-fp-x86` + `saida-equalizada` |
| `regressor / all capabilities` | `B1-args` | `b1-fp-x86` |
| `CI gate`, `Test suite gate`, `Sanitizer gate` | agregadores ancorados numa perna nativa | a escada |

O `Sanitizer gate` **não pode** ficar verde antes de a escada fechar: exige o `mem-paranoid`, que
monta a perna nativa. Mas deixou de ser vermelho **vazio** — os dois oráculos novos
(`mem-paranoid-linux-x86_64-musl` e `-linux-arm64-glibc`) passam.

## 7. Achados medidos e NÃO corrigidos

1. **Despacho por interface através de local tipado-interface dá SIGSEGV no nativo**, `0` na rota C,
   **sem união à vista**. Medido idêntico antes do degrau 25, logo pré-existente. Família que o
   `native_iface_fat_known_stop` circunda.
2. **A rota C pára onde o nativo acerta** — closure com parâmetro de união-nula. Vão do oráculo.
3. **`/usr/bin/link.exe` no Git-Bash é o `link` do MSYS, não o linker da MSVC.** O agente do mingw já
   tirou o `link` cru dos candidatos; confirmar ao drenar.
4. **`teko::casting` tinha zero consumidores** — só o medidor de métrica o vigiava. O agente da
   aridade deu-lhe o primeiro.
5. **`str` está em obra:** `teko_rt.h` tem dois words com `len` em BYTES; a decisão de 29/07 põe
   `.len` em caracteres e um terceiro word. Um pretty-printer escrito hoje erraria duas vezes.
6. **Nomes de locais não atravessam o `regalloc`** — bloqueia orçar a Camada 2 do debugger sem sondar.
7. **16 ficheiros fora de `fmt`**, sem portão (e por ruling, sem portão a criar).

## 8. Erros MEUS deste dia, para não se repetirem

Registados porque cada um custou tempo e alguns quase custaram correção errada:

0. **DECLAREI-ME CEGO SEM PROCURAR O INSTRUMENTO, e depois desenhei CI para uma cegueira inventada.**
   Passei o dia a diagnosticar CI pela **cauda** de `get_job_logs`, escrevi no handoff que a mensagem
   do pânico estava "fora de alcance", e **recomendei não escavar** com base nisso. O log integral
   sempre esteve a uma chamada de distância (§2c). Pior: quando o dono propôs guardar o log como
   artefacto, o meu instinto foi **implementar em `pr.yml`** em vez de medir primeiro se fazia falta —
   teria posto `upload-artifact` em 27 jobs para resolver um problema meu. **Antes de mudar o sistema,
   medir se o instrumento já existe.**
1. **Contei UMA falha onde havia DUAS, por não saber que o harness aborta.** Escrevi "único teste a
   falhar em 849". A fase unitária faz **SIGABRT no primeiro `assert` falhado** — o que está atrás
   nunca corre. E o primeiro falhado **difere por host**: em macOS é outro teste, que assim ficou
   invisível no meu registo. *"Primeiro falhado" nunca é "único falhado" num harness que aborta.*
2. **Drenei a fixture da aridade sem que perna nativa nenhuma a tivesse compilado** — e ela usa
   `s[i] = v`, que o backend nativo não sabe baixar (degrau 28). É a terceira vez neste dia que drenei
   algo cuja medição eu próprio tinha declarado necessária.
3. **Aceitei "não pude verificar" como resposta** do agente da relocação, em vez de o mandar provar em
   `theory/**`. O dono corrigiu: *"É pra isso que DEVE usar uma theory/**"*.
4. **Recomendei NÃO travar a fast-lane em `theory/**`.** Errado — é a exclusividade que a torna campo
   de provas previsível.
5. **Propus `-> void` invocando o C#**, quando `void` e sobrecarga são banidos aqui. Invocar a
   referência sem medir a nossa superfície.
6. **Concluí que a ausência de portão de `fmt` era buraco**, esticando "sem erros escondidos" até
   cobrir estilo. Não cobre.
7. **Disse que `MRelocKind` precisava de variantes GOT.** Refutado por medição: zero relocações contra
   símbolo indefinido.
8. **Disse que a Camada 1 do debugger era `.debug_line` só.** Refutado: sem `.debug_info` +
   `.debug_abbrev` o gdb não põe o primeiro breakpoint.
9. **`rc=$?` depois de um `| head`** dá o estado do `head`. Li rc=0 e quase concluí que o
   `fmt --check` não falhava.
10. **Escrevi `set -u` sem `set +e`** ao extrair um gate, reintroduzindo no mesmo ficheiro o defeito que
   tinha corrigido horas antes. **Todo passo que captura `$?` sem limpar o `-e` é este bug.**
11. **Atribuí o `exit 127` do Windows ao `sed -E`.** Era o `set +e` ausente; o gate nunca corria.
12. **Deixei um comentário mentir** no cabeçalho da minha própria sonda ("ONE host") depois de a
    converter para matriz.

**O padrão:** invocar uma lei ou referência sem medir se ela se aplica, e medir a coisa errada com
confiança. O antídoto que funcionou todas as vezes: **prova por inversão** — aplicar, reverter, ver o
comportamento errado reaparecer.

## 9. O padrão técnico dominante da lane

**Gémeos que divergiram** — duas rotinas irmãs onde uma foi corrigida e a outra não. **Nove instâncias
medidas.** E o corolário que o degrau 25 deu, que vale como régua:

> Uma paragem que dispara para 1 dos 4 membros de uma família é pior que nenhuma: **certifica os
> outros 3.**

Ao fechar qualquer coisa nesta lane: **varra a família inteira e liste os irmãos**, inclusive os que
não vai corrigir.
