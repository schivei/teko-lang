# Passagem de sessão — lane 0.3.1.0 "Linux gera NATIVE" (2026-07-30)

Escrito porque o container reiniciou duas vezes num dia e o dono precisa de poder mudar de sessão sem
perder o fio. **Este documento é a fonte única do estado.** Tudo aqui é medido, com SHA; onde é
hipótese, está dito.

- **PR:** `schivei/teko-lang#99`
- **Vagão:** `remodel/0.3.1.0-linux-native-2`, HEAD **`8f94c0b1`** (worktree `/home/user/wt-lin`)
- **Objetivo da lane:** as pernas Linux gerarem com o backend NATIVO (o `fixpoint_backend` por perna
  vive em `scripts/ci_producer_matrix.sh`)
- **O repo é um FORK.** `schivei/teko-lang`; o upstream é `teko-org/teko-lang`. `release.yml` e
  (desde hoje) `nightly.yml` só correm na org.

## 1. A escada de degraus — onde está

Cada paragem do backend nativo é um "degrau". A escada é o produto desta lane: enquanto ela não
fechar, o ponto de fixo nativo não fecha e as duas pernas nativas ficam vermelhas **por desenho**.

| degrau | o quê | estado |
| --- | --- | --- |
| 24 | `f64_bits`/`f64_from_bits` — alias do próprio VReg | **fechado**, confirmado no CI |
| 25 | união-nula em colocações sem tipo declarado | **fechado**, confirmado no CI |
| 26 | `append_fo` sem lowering, em `teko::codegen::cb` | **fechado e DRENADO** — aguarda confirmação de CI |

**A SEGUNDA VAGA, e não a esqueça:** os degraus são só o que o SELF-BUILD encontra. O corpus, as
regressões e os `.tkt` **nunca** foram compilados pelo backend nativo em CI, porque o ponto de fixo
falha antes do job `test`. **Não prometa "faltam N degraus".**

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

**NÃO EXPLICADO: `1 failed` + exit 134 (SIGABRT).** Não sei qual linha falha, nem se o 134 é crash
próprio ou o harness a abortar sobre a falha.

**Limite de ferramenta que causou isso, e vale saber:** os testes unitários correm no **início** do
log e o `get_job_logs` só dá a **cauda** — a mensagem do pânico está fora de alcance por essa via.
Descartei a hipótese óbvia por medição local: os 194 testes novos em `src/build/project_test.tkt`
(branch do mingw) **são seguros ao host** — ramificam em `teko::os()` e usam variantes de
`NativeTarget` explícitas, sem afirmar um ramo fixo de `host_target_for_os`.

**Recomendação registada: NÃO escavar antes do crumb 3.** Quando ele aterrar, esta perna muda de forma
(21 skips passam a corridas) e o vermelho restante, se sobreviver, aparece em contexto legível com o
log a caber na cauda. Escavar antes é gastar contexto num quadro que vai mudar.

## 3. FILA — não despachado

**Vagas: 3 de 4 ocupadas** (teto 4). A correr: **AArch64-ELF crumbs 2–5**, **`MRelocKind::None`**,
**`fmt --apply`**. **A quarta está GUARDADA de propósito** para o conserto do Windows i128, que entra
assim que a sonda responder.

| item | porquê | nota |
| --- | --- | --- |
| **Consertar `lwt_prim_kind_of_resolves_enum_to_int_cast_widens`** | **acende QUATRO pernas** (test+mem-paranoid em arm64-glibc e x86_64-musl). Único teste a falhar em 849 | expectativa desatualizada, não defeito: afirma `%1 = sext %0`, e a aridade decidiu que um alargamento sem perda **não emite conversão nem guarda** (`lower_cast_fit_guard` começa por `if cast_is_lossless_widen { return ctx }`, e o doc-comment de `lower_int_cast` di-lo). **Não apagar** — tem de passar a afirmar a AUSÊNCIA da conversão, senão troca-se expectativa velha por vazio. O sinal negativo já está provado por VALOR nas fixtures `f_cast_widen_keeps_value` (`-5 to i64`) e no alargamento implícito (`-2147483648`), nas duas rotas |
| **`fmt --apply` explícito** | dono aprovou: *"Sim: fmt --apply explícito"* | o meu despacho foi **recusado na camada de permissão** logo depois; nunca chegou a correr, e eu não o repeti (chamada recusada trata-se como decisão). **Precisa da palavra do dono para andar.** Contrato pinado em `scripts/fmt_cli_test.sh` |
| **Híbrido do `main`** | desenho **fechado** no digesto de leis | precisa do arquiteto para ordenar crumbs |
| **AArch64-ELF crumbs 2–5** | crumb 1 (relocação) fechado e provado em hardware | crumb 3 cria `Arm64Linux` em `NativeTarget` e destranca a perna arm64-Linux |
| **`MRelocKind::None`** | `plain_word`/`branch_word` (`encode_arm64.tks:117,139`) põem `Call` como default inerte — o valor "branch" como default de um campo que toda instrução carrega. Foi a semente do bug de relocação | mata a classe na raiz |
| **Debugger, Camada 1** | orçamento entregue e drenado (`docs/design/debugger-orcamento-0.3.1.md`) | 6 crumbs; recomendação é parar ali |

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

1. **Aceitei "não pude verificar" como resposta** do agente da relocação, em vez de o mandar provar em
   `theory/**`. O dono corrigiu: *"É pra isso que DEVE usar uma theory/**"*.
2. **Recomendei NÃO travar a fast-lane em `theory/**`.** Errado — é a exclusividade que a torna campo
   de provas previsível.
3. **Propus `-> void` invocando o C#**, quando `void` e sobrecarga são banidos aqui. Invocar a
   referência sem medir a nossa superfície.
4. **Concluí que a ausência de portão de `fmt` era buraco**, esticando "sem erros escondidos" até
   cobrir estilo. Não cobre.
5. **Disse que `MRelocKind` precisava de variantes GOT.** Refutado por medição: zero relocações contra
   símbolo indefinido.
6. **Disse que a Camada 1 do debugger era `.debug_line` só.** Refutado: sem `.debug_info` +
   `.debug_abbrev` o gdb não põe o primeiro breakpoint.
7. **`rc=$?` depois de um `| head`** dá o estado do `head`. Li rc=0 e quase concluí que o
   `fmt --check` não falhava.
8. **Escrevi `set -u` sem `set +e`** ao extrair um gate, reintroduzindo no mesmo ficheiro o defeito que
   tinha corrigido horas antes. **Todo passo que captura `$?` sem limpar o `-e` é este bug.**
9. **Atribuí o `exit 127` do Windows ao `sed -E`.** Era o `set +e` ausente; o gate nunca corria.
10. **Deixei um comentário mentir** no cabeçalho da minha própria sonda ("ONE host") depois de a
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
