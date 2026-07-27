---
section: process-law
created: 2026-07-25
source: owner rulings 2026-07-23/24/25 (fila serial → trem empilhado → dreno LIFO)
---

# Trem empilhado — disciplina de vagões (0.3.1+)

O modelo de entrega deixou de ser "PRs paralelos na main" e passou a ser um **trem**: cada
vagão é um PR cuja **base é a branch do vagão anterior**, não a main. Trabalhar sobre a
superfície do vagão de baixo é deliberado — expõe problema e solução cedo.

## O modelo

**Empilhamento (owner 2026-07-23):** nada em paralelo; cada vagão só abre baseando-se no
último vagão engatado. Equalização LOCAL antes de existir na origin (merge/cherry-pick).

**Dreno LIFO (owner 2026-07-24):** o trem drena de **cima para baixo** — o último vagão
engatado mergeia **dentro** do vagão de baixo, que mergeia no de baixo, até o vagão de base
entrar na main **uma única vez** carregando o trem inteiro integrado. Não há retarget e não
há cascata durante o dreno.

**Auto-correção do trem (owner 2026-07-25) — A REGRA QUE MAIS ECONOMIZA TRABALHO:** se um
vagão está **vermelho** e o vagão que **deriva dele** está **verde**, o de baixo **fica
verde quando o de cima desagua nele**. Consequências, todas obrigatórias:

- **NUNCA cascatear um fix para baixo.** Propagar manualmente o que o dreno propaga de graça
  queima runners, tempo e contexto. (Violado 9× em 2026-07-24 — o erro que originou esta nota.)
- **Correção pequena/pontual → entra no ÚLTIMO VAGÃO ENGATADO** (o topo).
- **Correção grande → vagão NOVO no topo.** O trem se corrige sozinho no dreno.
- **GATE ÚNICO QUE IMPORTA: o topo do trem verde.** Vagões de baixo NÃO precisam estar verdes
  isoladamente; não os dirija ao verde um por um.
- **Vagão só se reabre quando um merge dá erro.** Fora disso, vagão fechado não se toca.
- **Nunca pushar em vagão já fechado/verde** (o owner aperta as rules de branch se repetir).

**Achado de auditoria nunca edita vagão existente (owner 2026-07-24):** o romaneio produz um
**vagão novo** de correções, não edições retroativas. Assim o vagão auditado permanece
exatamente como foi validado e cada achado tem diff próprio.

## O dreno é UM merge, não N (owner 2026-07-25)

A branch do topo **contém**, por construção, todo o histórico de cada vagão abaixo (cada um é
baseado no anterior). Logo, mergear o topo na main produz **a mesma árvore** que N merges
encadeados — o dreno vagão-a-vagão é trabalho redundante. A receita:

1. contra-máquina verde no topo;
2. **retarget da base dela para `main`** (o diff do PR passa a ser o trem inteiro, e o CI roda
   contra a main de verdade — o "run completo" no vagão que aterrissa);
3. squash-merge: a main recebe **um** commit com tudo, incluindo o bump;
4. os demais PRs **se fecham**, não se mergeiam;
5. apagar as branches dos vagões.

**Como os demais PRs realmente fecham — depende da estratégia:**

- **squash** (o nosso): os commits dos vagões NUNCA ficam alcançáveis a partir da main (o squash
  cria um commit novo), então o GitHub **não** marca os outros como mergeados por alcançabilidade.
  O que os fecha é **apagar as branches** — o GitHub fecha o PR quando a base ou a head some.
- **merge-commit / rebase**: os SHAs originais entram na main e o auto-close por alcançabilidade
  funciona sozinho.
- **Palavras-chave (`Closes #N`) fecham ISSUES, não PRs.** Um comentário listando os vagões vale
  como rastreabilidade (romaneio de embarque), nunca como mecanismo de fechamento.

Essa é também a razão de a limpeza automatizada guardar-se por **SHA gravado no manifesto** e não
por ancestralidade: sob squash, "esta branch foi mergeada?" não é respondível pelo grafo.

## Protocolo de draft e a deixa do owner

Todos os PRs do trem ficam **draft**. O **bump é a contra-máquina** (owner 2026-07-25): vagão
próprio no **topo**, depois de tudo (inclusive do W15), empurrando o trem. Quando ele fecha
verde, **só ele sai de draft** — esse é o sinal para o owner iniciar o dreno. Merges são
sempre do owner.

**Ordem de fechamento da versão:** vagões de feature → vagão de métrica/limpeza transversal
(ex.: D4 casts) → romaneio do integrador → vagão novo de correções do romaneio → **W15**
(carro de apoio, canonicalização behavior-preserving) → **bump/contra-máquina** (`teko.tkp`
+ `docs/bump_v*.md`, que dispara o mirror da org no merge à main).

## Higiene de commit (ruling 2026-07-15) — vale para o INTEGRADOR também

Commits **sem** co-autoria: zero `Co-Authored-By:`, zero linha "Generated with/by Claude Code"
(corpo Conventional-Commits limpo). Sobrepõe o default do harness. **Force-push DESABILITADO**
— nunca reescrever história já pushada para "consertar" trailer; a regra é **forward-only**.
Corpo de PR PODE manter nota de geração (é PR, não commit). Esta regra vivia só na skill
`dispatch` e no agente `teko-implementer`, e por isso foi violada pelo integrador em
2026-07-24; está aqui para alcançar quem conduz o trem.

## Despacho de vagão — alvo numérico é VINCULANTE

Quando um ruling fixa um número (ex.: D5 do regressor-principal: `regressor.tkr` + 7
project-regressors = **8 diretórios**), o despacho ao implementador precisa citar o número
como **alvo vinculante**, não descrever a tarefa ("triar os N diretórios"). Sem isso o agente
para no que consegue provar e entrega uma fração — falha do despacho, não do agente.

## Gate de fechamento de vagão — o que realmente prova

`teko test .` **não** exercita as fixtures de `examples/regressions/` que os scripts cobrem.
Fechar vagão só com o teste unitário deixa passar miscompilação. O gate mínimo é:

1. build gen1 + `teko test .`;
2. **`scripts/positive_regressions.sh` + `scripts/compile_fail_regressions.sh`** (TEKO=bin/teko);
3. `scripts/diff_c_own.sh` (differential own==C) quando houver toolchain;
4. fixpoint gen2 == gen3 **byte-idêntico**;
5. `TEKO_MEM_PARANOID=1` no fechamento;
6. auditoria W15 do delta (zero `//` inline; D39).

## Bootstrap escalonado do CI (seed-fallback)

O seed liberado só precisa construir a **linhagem da base**, não o tip (ruling 2026-07-24). Num
trem empilhado há mais de um salto de capacidade, então o fallback é uma **escada iterativa**:
`repete { se o compilador corrente constrói o tip, fim; degrau := ancestral first-parent MAIS
NOVO que o corrente consegue construir; corrente := gen(degrau) }`, com guarda de
não-progresso. Invariantes aprendidos na marra:

- **O degrau é DESCOBERTO por sondagem, nunca assumido.** Um vagão pode ADICIONAR capacidade e
  em seguida DELETAR o corpus que a dispensava (ex.: W-RULE + varredura de casts) — sua própria
  cabeça exige compilador novo. O degrau construível é o vagão que já tem a capacidade mas
  ainda não o corpus que depende dela.
- **`TK_RT_DIR` é pinado por estágio.** O compilador resolve `teko_rt.{h,c}` relativo ao
  próprio binário (argv[0]); sem pinar, mistura eras de runtime e o link falha.
- **A saída da sondagem mora DENTRO do worktree sondado** (mesma razão: era do runtime).
- **O clean preserva os diretórios que guardam os compiladores da escada.**
- Build intermediário é seco (`--no-verify`): medida **transitória da .31**, a desfazer na .32.

## Seeds commitados — REMOÇÃO AGENDADA para o primeiro vagão da .32

Ruling do owner (2026-07-25): *"versionar binário (nightlys do fork) será somente para o trem
atual, no próximo devemos remover isso"*. O que fica permanente é o **nightly + promoção**; o que
sai é o blob no repo. Sem esta lista escrita a gambiarra vira permanente, que é exatamente o modo
como gambiarra sempre vira permanente.

**O que sai, item a item:**

| o quê | onde |
|---|---|
| os cinco blobs + o manifesto | `bootstrap/seeds/` (diretório inteiro) |
| a regra binária | linha `bootstrap/seeds/*.xz binary` em `.gitattributes` |
| o consumo | `seed_from_committed`, `host_seed_label`, `unxz`, o ramo `TEKO_SEED_PREFER_COMMITTED` e o fallback pós-loop em `scripts/ci_provision_teko.sh` |
| a produção | job `seeds` + `scripts/cross_compile_seeds.sh` |
| o consumo em CI | job `seed-debut` (`native.yml`) |
| a escada pinada | `scripts/build_with_seed_fallback.sh` inteiro (já agendado no cabeçalho dele) |

**Por que o registro está aqui e não no `<!-- train-manifest -->` do bump, como o briefing do
vagão 20 pedia:** aquele bloco mora em `docs/bump_v<versão>.md`, e `mirror-pr-to-org.yml` dispara
em `push` à `main` com `paths: ['teko.tkp', 'docs/bump_v*.md']`. Criar o arquivo do bump num vagão
que **não é** o bump abriria o PR de promoção na org antes da hora — ou, com o nome de versão
ainda não bumpado, falharia com `bump doc não encontrado` numa `main` verde. **O vagão da
contra-máquina copia esta tabela para o `<!-- train-manifest -->` que ele cria**; é lá que ela
termina, mas não é lá que ela pode nascer.

## Lane que estreia na aterrissagem → vagão próprio de correção (owner 2026-07-25)

O split light/full do CI (`base_ref == main` ⇒ full) tem um efeito de segunda ordem: as lanes
full-only rodam **pela primeira vez** no vagão que aterrissa. Na .31 isso concentrou OITO estreias
num único momento — lane riscv64 restaurada, ASan/UBSan/LSan, `TEKO_MEM_PARANOID`, Windows
self-host, `test / macos`, três `ar validation`, cross-smoke — e duas delas (ASan e mem-paranoid)
**nunca haviam rodado na história do projeto**, porque gateavam em `github.ref == 'refs/heads/main'`,
condição que nunca casa num evento `pull_request`.

Duas saídas foram propostas ao owner e **as duas foram rejeitadas**: aceitar o risco e resolver na
contra-máquina; ou gastar um PR descartável apontado para `main` só para forçar um run completo.

**Ruling: vagão NOVO de correção, antes do W15.** Ordem de fechamento passa a ser:

> features → métrica/limpeza transversal (D4) → **vagão de correção de estreia** → W15 → contra-máquina

O run completo sai de graça, sem PR de mentira: o vagão de correção recebe **retarget da base para
`main`** — a mesma mecânica já ratificada para o dreno ("o CI roda contra a main de verdade") — o
full roda contra a árvore quase-final, as correções entram **nesse mesmo vagão**, e depois a base
volta para o vagão anterior para o W15 empilhar em cima.

Por que é melhor que as alternativas: um PR descartável mede uma árvore que ainda vai mudar e o
trabalho de correção não tem onde morar; aceitar o risco trava o owner no último passo. O vagão
resolve os dois — é lugar de trabalho **e** é o gatilho do run.

**Corolário: pré-carregue o vagão em vez de reagir ao vermelho.** As classes de falha de cada lane
são estaticamente caçáveis. Prova empírica da .31: a lane do Windows, ao ser ligada, entregou dois
defeitos reais em duas horas — um seed de assert morto em PE/COFF (weak cruzando unidade de tradução
não existe nesse formato) e um `"/tmp/..."` hardcoded num fixture. O segundo era achável com um
`grep`. Uma varredura por classe antes do run faz o vagão nascer com lista.

### O que a estreia realmente encontrou (medido, 2026-07-26)

O ruling acima foi tomado sobre uma previsão. A estreia aconteceu no vagão 20, com a base
retargetada para `main`, e o resultado confirma a previsão com folga. Registrado por CAUSA, não por
lane, porque uma causa aparecia em várias lanes e isso escondia a contagem:

| causa | achada por | o que era |
|---|---|---|
| `dladdr` fora da `libc` | `artifact / linux-*-glibc` | mora em `libdl` até a glibc 2.33; o 2.39 do runner escondia a dependência que o piso de 2.28 exige. **Só apareceu porque o zig morreu** e o build passou a acontecer no piso. |
| ABI do Win64 | `test / windows-x86_64` **e** `windows-arm64` | `tk_str` (16 bytes) era passado em par de registradores. A MS x64 só passa agregado em registrador nos tamanhos 1/2/4/8; acima disso vai por referência. O callee lia os 16 primeiros bytes da string como `{ptr; len}`. O objeto PE/COFF sempre esteve correto — a hipótese "é do COFF" era minha e estava errada. |
| `memcmp(NULL, …)` | `ASan+UBSan+LSan` | UB em `tk_str_eq` e `tk_str_ends_with`. **Primeira execução na história do projeto** desta lane (o J2). |
| sem alvo ELF/aarch64 | `test / linux-arm64-{glibc,musl}` | os assets arm64 eram publicados sem que `teko test .` jamais tivesse rodado naquele hardware. |
| sonda de capacidade incompleta | `regressor / all capabilities` | ter `qemu-riscv64-static` no PATH não é a capacidade; a capacidade é conseguir executar. |

**A lição que generaliza** é a última linha da tabela, e ela vale para toda lane nova: uma sonda que
verifica o NOME de uma ferramenta em vez do FATO que ela deve produzir passa verde e a falha reaparece
depois, disfarçada de regressão real. O mesmo padrão apareceu neste vagão em três lugares diferentes
(a sonda do qemu, o gatilho com `paths:` que não disparava, e o agregador que aceitava `skipped` como
aprovação). É a mesma família do "portão que não gateia".

**Corolário para a contagem de risco:** duas das cinco causas (o `dladdr` e a ABI do Win64) eram
invisíveis por construção antes deste vagão — a primeira porque cross-compilar escondia o piso, a
segunda porque nenhum fixture do corpus tinha uma chamada de runtime que RETORNA e leva agregado.
Nenhuma varredura estática as teria achado. O "pré-carregue o vagão" acima continua certo, mas não
substitui a estreia: ele reduz a lista, não a zera.

### Um comentário errado é pior que nenhum

Duas das causas acima estavam documentadas como SEGURAS por comentários que argumentavam mal:

- `tk_str_eq`: *"a zero-length pair compares equal without touching ptr (memcmp of 0 bytes is
  well-defined)"* — verdade sobre a leitura, falso sobre a chamada (`nonnull`).
- `is_str_arg_builtin`: achatar `tk_str` em `(ptr, len)` *"reproduz a verdadeira ABI C sem nenhuma
  mudança de isel/regalloc/stackify"* — verdade em SysV/AAPCS64/LP64D, falso em Win64.

Nos dois casos o comentário afirmava a conclusão certa para o caso comum e foi ele que tornou o
defeito invisível à revisão. Padrão diagnóstico útil: nas mesmas famílias, as funções que GUARDAM o
caso de borda não fazem a afirmação; as que afirmam não guardam. **Ao corrigir, corrija o comentário
junto** — senão o próximo leitor reintroduz o defeito com a bênção da documentação.

## O DEGRAU que aposenta o C (owner 2026-07-26)

> *"este trem deve remover toda a compilação C para no trem seguinte ser 100% nativo. Isso não
> inclui o linker, pois ainda temos libs a debater até lá."*
> *"como faremos degrau aqui, gen1 != gen2 quando usar native, logo o degrau corrige e aposenta C"*

A .31 mata a compilação C; a .32 nasce 100% nativa. O **linker fica de fora** — as bibliotecas de
plataforma ainda estão em debate (inventário fechado é a tarefa pré-linker).

**O critério de aceite NÃO é `gen1 == gen2`.** É o mesmo que o bootstrap original já usa, e que o
`docs/BUILDING.md` já enuncia para a transição C→self-host: *"only the one-time C→self-host
transition differs cosmetically. Gen-2 vs gen-3 must be byte-identical."*

```
gen1 = seed (backend C) compila a árvore        → compilador que emite C
gen2 = gen1 sob TEKO_BACKEND=native             → compilador nativo   ← O DEGRAU: difere de gen1
gen3 = gen2 compila a árvore                    → BYTE-IDÊNTICO a gen2
```

`gen1 != gen2` é a definição do degrau, não um defeito. Quando `gen2 == gen3` valer, o seed do trem
seguinte passa a ser um binário nativo e o C sai do caminho por consequência — ninguém precisa
apagá-lo à mão.

### Três coisas que decorrem disso e que já mordem hoje

1. **Determinismo virou pré-requisito, não higiene.** O degrau exige igualdade byte-a-byte entre
   duas execuções. A ordem de descoberta de fontes vinha do `readdir` (ver a armadilha abaixo), o
   que tornaria `gen2 == gen3` falso por construção — e a falha apareceria como "o degrau não
   fecha", sem causa visível. Corrigido em `tk_rt_list_dir` antes de o degrau começar, por sorte.

2. **O portão `nightly === gen1` precisa de um QUARTO antecedente: a geração.** Ele mede seed,
   toolchain e árvore, e afirma que o mesmo commit dá os mesmos bytes. No commit em que o degrau
   acontece, o binário publicado muda de geração e o portão vai acusar não-reprodutibilidade num
   commit são. Corrigir junto do degrau, não depois.

3. **Teste unitário e teste de C morrem com o C** (ruling do owner na mesma conversa: *"não deve
   focar esforços neles"*). Isso reescreve o corte de testes: não é "medir cobertura e podar", é
   consequência da morte do C. O que sobrevive é o corpus de regressão, que roda o binário.

### O que morre JUNTO com o C — quatro oráculos, e sete lanes

Levantado a pedido do owner (*"não esqueça de lanes de CI que exigem C"*), 2026-07-26. A conta é
maior que ajustar YAML: metade da verificação do projeto existe **porque** o caminho passa por C.

| lane de `pr.yml` | por que depende do C |
|---|---|
| `artifact / <producer>` (7 legs) | `native_linux_asset.sh` compila `teko.c` + `teko_rt.c` + `assert.c` com gcc — a produção de asset INTEIRA é compilação C |
| `TSan` | baixa o artefato `teko-c` e linka gen1 instrumentado |
| `ASan+UBSan smoke` | idem |
| `ASan+UBSan+LSan / default dispatch` | idem — a auditoria pesada |
| `clang-tidy audit` | varre `src/runtime/teko_rt.c src/assert/assert.c` |
| `Memory paranoid (native self-host)` | `TEKO_MEM_PARANOID` é um botão do `teko_rt.c` |
| `codeql.yml` (`c-cpp`) | analisa o C |

**Os quatro oráculos que somem:**

1. **O diferencial `own == C`.** Era o que provava o backend nativo correto por igualdade contra um
   caminho maduro. Sem C não há contra o que diferir; 31 linhas `| c |` do `regressor.tkr` perdem
   o sentido.
2. **Os sanitizadores.** ASan/UBSan/TSan/LSan são o clang instrumentando o C emitido. Um backend
   que emite objeto direto não tem clang para instrumentá-lo.
3. **O SAST (`clang-tidy`).**
4. **A análise CodeQL `c-cpp`.**

**O que sobra provando o compilador:** o valor que cada cenário do corpus afirma por si (exit,
stdout, trap), o fixpoint `gen2 == gen3`, e o `TEKO_MEM_PARANOID`.

### O oráculo que NÃO morre — e é o que cobria o que os outros não cobriam

Owner, 2026-07-26: *"a arena vai para teko, não há exceções de manter algo em C"*. Consequência
que melhora a conta acima em vez de piorá-la.

`TEKO_MEM_PARANOID` são **três linhas** em `tk_free_take`: ler o env uma vez e, quando ligado,
`memset(p, 0xDD, usable); return;` — envenena o bloco e nunca o devolve ao pool. Isso é
propriedade da **arena**, não do C, e migra com ela sem perder nada.

E o comentário que já estava ao lado dele é o ponto:

> *"Arena reuse is invisible to ASan, so a wrong linearity proof would corrupt silently; with
> poison, any read-after-park yields 0xDD garbage and the gate/diff harness fails LOUDLY."*

Ou seja: **o ASan nunca cobriu reuso de arena.** O oráculo que cobre o modelo de memória mais
distintivo do projeto é exatamente o que sobrevive, e ele nunca foi redundante com os
sanitizadores. Dos quatro que se perdem, nenhum cobria a arena.

### Os *San's: teacháveis, mas não pela .31 (owner 2026-07-26)

> *"quanto aos *San's, se não houver como ensiná-los (não vejo motivos se compararmos com outras
> linguagens que fazem a mesma abordagem), não tem pq mantê-los."*

O precedente é real e vale registrar o MECANISMO, porque ele não é o que a intuição sugere: Rust e
Go não pedem ao clang que instrumente — **o próprio compilador emite** a instrumentação (chamadas
`__asan_*` em torno de load/store; `__tsan_read`/`__tsan_write` em cada acesso) e linka a runtime
de sanitizador da plataforma. Como o linker do sistema continua nosso até a .33, o caminho existe
para nós também.

Mas é trabalho de CODEGEN, não de configuração. Ruling aplicado: **saem com o C**, e voltam se e
quando o backend nativo souber emiti-las. Não seguram a .31.

**Armadilha imediata, e é a da família "portão que não gateia":** se o C sumir e o job `c-cpp` do
`codeql.yml` continuar no lugar, ele passa **verde analisando nada**. Um gate vazio é pior que
gate nenhum, porque parece cobertura. Ou o job sai junto com o C, ou entende Teko — e é por isso
que *ensinar Teko ao CodeQL na .32* (owner, mesma conversa) é **reposição**, não polimento.

### O método: escada medida, não inventário de comentário

Contar `honest_stop` em comentário dá 332 e não significa nada. O número que significa é onde o
compilador para ao compilar A SI PRÓPRIO pelo backend nativo. Medido em 2026-07-26, na árvore do
vagão 20: passa lexer, parser, **checker (6139 itens)**, monomorph e consteval (591 consts), e para
em

```
const struct: initializer is not a struct literal (Tier-A follow-up) (#594)
```

Esse é o degrau 1. O método é o laço: fechar o stop que ele nomeia, rodar de novo, ler o próximo.
Cada volta é medível e o progresso é o número de fases vencidas, não a contagem de TODOs.

## PUSH FREQUENTE, verde ou não — e a carga é o destino que não acorda o CI (owner 2026-07-26)

> *"Não interessa se está verde ou não, é por isso também de usar as cargas caso queira evitar
> disparar CI, mas é importante sempre exercitar pushes frequentes."*

A `cargo/**` tem DUAS funções, e usar só a primeira é o erro que esta nota registra:

1. **paralelizar a produção** (o que a seção da esteira já dizia), e
2. **ser o destino de push do trabalho em voo** — `cargo/**` não tem PR e os cinco portões são
   `pull_request`-only, então empurrar para lá **não dispara CI nenhum**. É push de graça.

**O ambiente é efêmero.** Worktree, branch local e commit local vivem no container; quando ele é
reciclado, somem. "Guardar para empurrar quando fechar verde" não protege nada — protege um verde
que nem sempre existe — e arrisca tudo.

**O erro cometido em 2026-07-26, para não repetir:** o integrador segurou o push do vagão
"até o gate fechar", enquanto o vagão estava vermelho POR DECISÃO do owner (a inversão do backend
para nativo). Resultado do inventário quando o owner perguntou: **18 commits** não empurrados no
vagão e **13 branches `cargo/*` sem remoto nenhum** — incluindo o fecho do `B2-bigimm`, a correção
de ordem da monomorfização, o desdobramento inteiro e 551 linhas de excisão de CI. A carga viva
tinha mais 2 commits pesados soltos (a excisão do enum `Backend` e o `extern` mirando o símbolo C).
Tudo a um crash de distância de sumir.

**A regra invertida é a certa:** vagão vermelho por decisão do owner é vagão que se empurra MAIS
cedo, não menos. O vermelho já é o estado desejado; não há o que preservar segurando.

**Operacional:** commit por fatia, push por fatia — vagão e cargas. Uma carga que acumula trabalho
no worktree para "reportar no fim" está guardando o trabalho no lugar mais frágil que existe. O
briefing de carga deve exigir push, não só commit.

## Armadilhas do worktree compartilhado

- **NUNCA `git stash` em worktree de vagão.** O `.git` é compartilhado entre todos os worktrees e a
  **pilha de stash é global**. Um `git stash -q` numa árvore já limpa não cria entrada (sai 0 em
  silêncio), e o `git stash pop -q` emparelhado vai então buscar o topo da pilha **de outro agente** —
  em 2026-07-25 isso deletou um design doc alheio ao vagão. Para comparar antes/depois use
  `git archive` ou copie para `/tmp`.
- **Um achado de auditoria vale no snapshot em que foi feito, não no topo.** Dois achados de revisores
  na .31 eram verdadeiros no worktree auditado e **já corrigidos num vagão acima** (a guarda de nome
  de membro do `ar`, que chegou no commit `d1aab7ba`). Reverificar no TOPO antes de virar trabalho não
  é desconfiança do revisor — é o passo que evita um vagão de correção inútil.
- **Ramo de erro inalcançável não é bug.** Um `error => ""` cuja condição de entrada é a negação exata
  da condição de erro do chamado não pode disparar. Vira higiene (ramo morto que finge ser possível),
  não MEDIUM de corrupção.
- **Intercalação de stdout com stderr não é causalidade.** O nome do teste vai para stdout, que é
  block-buffered fora de tty; o pânico, o `abort()` e o segfault vão para stderr, sem buffer. Um crash
  se atribui ao último nome que passou por um flush — em 2026-07-26 isso custou uma investigação
  inteira e fez uma carga reportar um defeito de compilador FABRICADO ("acrescentar campo a um struct
  quebra um teste de spine não relacionado"); o teste real estava ~66 nomes adiante. Medido depois:
  numa suíte pequena a perda é TOTAL — 41 testes, zero linhas no arquivo. Corrigido na raiz
  (`tk_flush_out` antes do corpo de cada teste, `709d41c1`), mas a disciplina fica: **antes de tratar
  uma atribuição de crash como fato, reproduza com `stdbuf -o0`.** Vale para qualquer saída em que
  dois descritores com políticas de buffer diferentes contam a mesma história.
