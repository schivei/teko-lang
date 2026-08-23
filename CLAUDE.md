# Instruções permanentes — teko-lang

## Idioma (REGRA DURA, persistente)
O dono (schivei) **NÃO fala inglês**. **TODA** comunicação no chat é em **PT-BR**,
sempre, sem exceção. Esta regra vale para todas as sessões e **persiste através de
compactação de contexto** — se este arquivo existe, a regra está em vigor.
(Código, mensagens de commit e nomes técnicos seguem a convenção do repo; a
conversa com o dono é PT-BR.)

## Como perguntar (REGRA DURA, persistente — dono 2026-08-18)
**NUNCA usar quiz/menu de opções (ferramenta AskUserQuestion). O dono ODEIA.**
Toda pergunta é em **prosa no chat**, PT-BR, curta. Quando ele estiver respondendo
("ainda estou respondendo outras coisas"), **esperar** — não empilhar pergunta nem
executar por cima.

## Protocolo de fork (bifurcação) — REGRA DURA, dono 2026-08-19
Quando um agente, **operando**, encontra um **fork** (decisão de design, owner-gate,
ambiguidade "qual caminho", tensão de lei), antes de parar ele precisa **TER CERTEZA**
de que aquilo NÃO está já deliberado:
1. **Checar se já está deliberado** — buscar em `DECISION_LOG.md`, `docs/design/**`,
   `.crumbs/**` (Rulings) e nas docs-guarda (Doc-2 / Doc-1 / umbrella) uma deliberação
   sobre aquele ponto.
2. **Mais recente vence** — havendo mais de uma deliberação sobre o mesmo ponto, aplicar
   a **mais recente** (por data / id do DECISION_LOG); a nova supersede a velha.
3. **Só HALT em fork real** — parar e notificar o dono **apenas** quando NÃO existe
   deliberação (fork genuinamente aberto), enunciando o fork curto e claro.
Isto mata os **HALTs falsos** (agente parando por algo já decidido — foi o caso de
RT-L6/cov_dump, parcialmente resolvidos). **Todo dispatch carrega esta regra**; o
TEMPLATE de crumb a referencia.

## Execução de crumb — DUAS PASSADAS (scout → implementer, dono 2026-08-19)
Cada crumb roda em DUAS passadas, não uma:
1. **SCOUT** (teko-scout, barato) — lê o crumb e VERIFICA contra o `src/` atual: as citações do
   "Where" estão certas? A superfície já está landada (então é **verify-only**)? As deps estão
   satisfeitas? Há drift do que o crumb assume? Os artefatos que ele mandaria criar já existem
   (ex.: probes)? Veredito: **{JÁ-FEITO/verify-only | PRECISA-IMPLEMENTAR (+correções) | INCERTO}**.
2. **IMPLEMENTER** (teko-implementer) — SÓ se o scout disser **PRECISA** ou **INCERTO**, e despachado
   COM os achados/correções do scout. Se **JÁ-FEITO** → marca verify-only (confirma byte-idêntico, sem
   implementer). Se **INCERTO de forma que precisa do dono** → sobe pelo protocolo de fork.
Motivo: pega drift / já-feito / artefato-já-existe ANTES de gastar um implementer caro (foi o que
faltou no 0001, que recriou probe já existente).

## Ensino AGORA, uso depois (dono 2026-08-19 — refina "ensinar tudo na 1ª rodada")
Ao decidir deferir algo, SEPARE **ensino** de **uso**:
- **ENSINO** (a superfície: lexer/parser/checker ACEITAR o construto) — se o trabalho já está iniciado e
  só falta ensinar mais um pouco, **ensina AGORA** (dobra no SM-R1). **NÃO se defere ensino.** É a lei do
  "tudo que puder ser ensinado ao compilador, já na 1ª rodada".
- **USO / runtime** (o lowering/consumo PESADO da superfície ensinada — "o peso") — ESTE sim pode ficar
  **para mais tarde**, mas **SEMPRE no mesmo plano (0.3.1)** — nunca empurrado pra 0.3.2.
Ex.: superfície de `await`/`Intent<T>` e DI-scoped → ensina já (SM-R1); o lowering A4/CN1 (opção c) e o
binding de arena scoped → uso deferido-mas-in-plan. Errado (o que eu fiz): deferir o ENSINO junto com o uso.

## Ritmo de trabalho
- **BASE DO COORDENADOR = `fix/retirement` (lei dura, dono 2026-08-18). NUNCA opero na branch `main`.**
  Todo o trabalho parte de `fix/retirement`; agentes recebem branch própria a partir do que for
  relevante (ex.: `origin/feat/expurgo-arrays`) e eu dreno de volta para `fix/retirement`.
- Continuar o processo de forma autônoma, mas **fazer pausas para ler** o que o
  dono pode ter escrito no meio do caminho, antes de decisões que importam.
- **ERROU → PARA E REFAZ CERTO, NÃO INSISTE NO ERRO (lei dura, dono 2026-08-19).** Se um dispatch/base/
  escopo saiu errado, a resposta é **interromper e reiniciar certo** — NÃO remendar um agente em voo com
  mensagem de correção pra salvar um começo torto, NÃO contornar. Kill + re-dispatch limpo. Insistir no
  erro (patch em cima do errado) é o oposto de "sem workarounds".
- **Sem workarounds** (lei do dono): achar e resolver a **causa raiz**, nunca dar
  voltas para contornar o problema.
- **NUNCA FAÇO EU, SEMPRE UM AGENTE (lei dura, dono 2026-08-18 — endurece a de 2026-08-17).**
  TODO trabalho — implementação **E validação** (build seco, fixpoint, reseed, medição) — vai
  para um **agente**. O coordenador **NÃO roda build** na sessão principal; só faz **edições
  pontuais** (leis na CLAUDE.md, correção cirúrgica de uma linha) e **orquestra** (despacha,
  lê o retorno, drena por ff/cherry-pick). Rodar build eu mesmo queima a sessão principal e é o
  que a lei proíbe. **A ideia é preservar os tokens de memória da sessão principal e reduzir as
  compactações** (é a compactação que causa perda de contexto e erros caros) — logo evito também
  ler arquivos gigantes / rodar cadeias aqui.
- **CADA AGENTE COM SUA BRANCH/WORKTREE (lei dura, dono 2026-08-18).** Ao despachar, SEMPRE dar ao
  agente uma **branch própria** e **worktree isolado** (`isolation: "worktree"`) — nunca deixá-lo
  compartilhar o working tree principal comigo. Compartilhar causa colisão de git (o `checkout`/`reset`
  de um puxa a árvore de baixo do outro; scratch do agente polui o main). O agente pusha a branch dele
  no origin (sobrevive a revert de FS), eu dreno por ff/cherry-pick. Coordenador edita em worktree
  isolado; nunca encosta no git do main enquanto um agente roda nele.
- **SAÍDA LONGA → ARTEFATO HTML (lei dura, dono 2026-08-18).** Quando o retorno de um agente for
  **grande demais e puder comprometer minha memória**, ele NÃO passa pela sessão principal: **outro
  agente gera um artefato HTML**, eu **publico** (ferramenta Artifact), o **dono revisa** e me devolve
  **só o veredito** — a saída bruta nunca entra no meu contexto. O coordenador só recebe o resumo/decisão.
- Quando o dono está **levantando/analisando um problema comigo**, PARAR, LER o
  que ele manda e pensar JUNTO — não sair executando por cima nem ignorar as
  mensagens dele.

## Estilo de código (LEI DURA, dono 2026-08-18) — CLAREZA, MENOS TEXTO
Metas medidas: **doc-comment ≤ 10% do código; comentário `//` = 0%** (hoje: doc 46,6%, // 2,1%).
É para **REMOVER e REDUZIR — não mover** para outro lugar. Documentação de verdade mora em
`docs/design/`, escrita à parte; não é o dump dos doc comments do código.
- **Comentários inline `//` em `.tks`/`.tkt`: PROIBIDOS (0%).** Remover todos.
- **PASSO ANTERIOR ao doc — visibilidade (dono 2026-08-18):** `pub` é só interno — NÃO vai pro `.tkh`
  (`tast.tks` M.4: só `exp` alcança o header). **stdlib default = `exp`**: toda a superfície consumível
  da stdlib (list/str/map/io/fs/crypto/numeric/collections/encoding/sort/cmp/math/iter/fmt/text/…) DEVE
  ser `exp`. Exceção: implementação que **realmente** não precisa ser exposta (helper interno) fica
  `pub`/privado — **não dá pra expor 100%**. A **maquinaria do compilador** (parser/checker/codegen/lir/
  backend/build/lexer/names) em geral é `pub`/privado. **MAS CUIDADO (dono 2026-08-18): macro e
  comptime acessam a ABI em tempo de compilação** — tipos/helpers do compilador que código macro/comptime
  alcança DEVEM ser `exp` também (o "usuário" inclui o autor de macro/comptime, e o que ele toca em
  compile-time precisa estar no `.tkh`). Promover `pub→exp` o que é superfície stdlib OU superfície
  macro/comptime; SÓ DEPOIS aplica-se o doc. (Muda o `.tkh`/ABI → é mudança de superfície, exige reseed.)
  **TESTE de `exp` = valor pro usuário (dono 2026-08-18), NÃO mecânico:** ponderar cada decl —
  *faz sentido expor? em que cenário um usuário da lib se beneficiaria de chamá-la?* Sim → `exp`
  (`sort`, `nth_i64`, `aes_gcm_seal`). Plumbing interno que nenhum usuário chamaria → `pub`/privado
  (`merge_ord`/`msort_ord`, `quickselect`, `crypto_error` — o usuário RECEBE o erro, não o constrói).
- **Doc comment SÓ onde há `exp`** (superfície exportada). **Qualquer outro acessor DESCARTA doc**
  (dono 2026-08-18): `pub`, `global`, `comptime`, privado, o que for — REMOVER. Doc de topo/introdução
  de arquivo: REMOVER.
- **Um doc comment não pode ser maior que o que documenta.** Curto; só o que a assinatura não diz.
  PROIBIDO: referências a docs (§/#/plano/crumb), história, "por que", explicação de arquitetura.
- **Mensagens de erro/log = estilo compilador padrão:** `arquivo:linha:coluna: "causa curta"`
  (ex: `unexpected type`, `expected ':' after ')'`, `unsupported (os,arch)`). Sem novelas nem refs.
  **"Encurtar" = MELHORAR a frase, não truncar** (dono 2026-08-18): a causa tem que ser clara e
  rápida de identificar por quem NÃO conhece Teko e só quer bater o olho. Cortar palavra a ponto de
  virar críptico é errado; `aes_gcm: nonce must be exactly 12 bytes (96-bit)` já é boa. Tirar é
  novela/ref/arquitetura, não a clareza. **A melhoria do texto entra JUNTO no passe de limpeza**
  (dono 2026-08-18) — mesmo agente, mesmo arquivo. (O prefixo `arquivo:linha:coluna:` em si é
  mecanismo de codegen, à parte.) **ETAPA DEDICADA OBRIGATÓRIA (dono 2026-08-18):** em módulos
  exercitados pelo self-build (compiler-core), mudar string de mensagem diverge `src`↔`teko.c` — por
  isso os lotes de doc podem adiar; MAS **um passe de mensagens unificado sobre a árvore integrada,
  ANTES do reseed único, É PRA SER FEITO** (não pular). O compiler-core é o alvo principal da lei.
- Retroativo, tree-wide. Mudança que toca o `teko.c` (mensagens, ou deslocamento de linha) exige reseed.
- **Testes: não se escreve teste para o que o compilador exercita ao se compilar** — o fixpoint (self-build)
  já é essa prova. A linguagem é MONÓLITO: a stdlib É o compilador — o self-build a compila E `gen1` a
  EXECUTA em larga escala (o compilador chama `list`/`str`/`map`/`io`/`fs`/`arena`/… ao compilar). A prova
  de "o compilador usa" está no próprio `src/` (se o código chama a função, ela é exercitada).
  Compilar o projeto inteiro (incl. os próprios `.tkt`) já exercita a maior parte da implementação.
  REMOVER: (a) **testes que validam a COMPILAÇÃO** (strings sintéticas p/ parser/codegen/ast) — são
  TAUTOLÓGICOS: o compilador nem chegaria a rodá-los se o parser/codegen estivesse quebrado; retóricos,
  incapazes de dizer nada; (b) comportamento de toda função que o `src/` chama (gen1 a executa);
  (c) **genéricos/monomorph — REMOVER** (dono 2026-08-18): o compilador USA genéricos em si e compila o
  próprio código que os instancia, então o self-build exercita a monomorfização; (d) **backend native —
  REMOVER** (dono 2026-08-18): o CI tem 6 pernas, 4 native, e as 2 em C viram native quando tudo fecha
  verde — o native é exercitado pelo próprio CI.
  MANTER só o que o self-build genuinamente NÃO exercita: **casos de erro/diagnóstico** (o compilador só
  compila código VÁLIDO, nunca dispara os caminhos de rejeição) e comportamento de função que o compilador
  NUNCA chama ao rodar. Na dúvida, LISTAR para revisão — não remover.
- **MOVER-O-NÃO-USADO = ZERO TESTE (dono 2026-08-20, aperta a lei).** O que o compilador NÃO usa, ao ser
  **movido** para `./tklib`/pacote (D57 Fase A), NÃO ganha teste algum — nem os oráculos `.tkr` que a
  exceção acima permitiria. Motivo: (1) é **relocação pura** de código já-funcionando (behavior-preserving,
  não é código novo); (2) **sai do escopo de validação do compilador** — vira superfície exportada, e quem
  consome o pacote valida o próprio uso. O projeto-compilador não testa o que não usa. Dispatch da Fase A =
  move mecânico, **zero teste AFIRMATIVO** (`.tkt`/`.tkr` que asseguram saída correta). **Exceção estreita
  (dono 2026-08-20):** UMA ou OUTRA **check de FALHA** (assegura que algo *falhe* — reject /
  `EXPECT_COMPILE_FAIL` / guard de pânico) pode ficar, pois o path de falha é o que o self-build NUNCA
  dirige (bate com o carve-out "casos de erro/diagnóstico"). Afirmativo NÃO; rejeição rara SIM.
- **ENFORCEMENT desta lei (dono 2026-08-19, AVISO DURO).** Um agente já a furou (criou teste pro que o
  self-build exercita). **Reincidência → o dono PROÍBE testes (unitários E regressivos) de vez.** Duas
  camadas obrigatórias no meu processo: (1) **todo dispatch de implementador reproduz esta lei ALTO** —
  "NÃO escreva teste pro que o self-build/fixpoint exercita; só oráculo `.tkr` isolado para path que o
  fixpoint NÃO alcança, e SÓ os nomeados na seção Fixtures do crumb"; (2) **ao drenar, conferir o delta**
  e RECUSAR qualquer `.tkt`/`.tkr` novo que não seja um oráculo nomeado no crumb para path não-exercitado
  — kill + refaz, não drena.

## Triagem de CI (PR #110 = fix/retirement) — dono 2026-08-18
- **Falha em `fixpoint (native)` → ESPERADA** (native para no DEGRAU), não é problema real.
- **Falha em `produce this leg` (C ou native) → FALHA REAL.** Ler o step com `head=100` (as primeiras
  ~100 linhas — a CAUSA está no topo; o tail é só limpeza de git).
- Assinar o CI do PR #110 (`subscribe_pr_activity`) DEPOIS de drenar C + toda a limpeza + reseed, para
  verificar/corrigir as pernas.

## Convenções da linguagem/codebase
- **Só existe `VAR` e `CONST` — `let`/`mut` NÃO existem, tudo é mutável** (ruling do dono). Interno:
  `BindKind = enum { Var; Const }` — `Mut`→`Var`, `Let`→`Var` (sem repensar Let; discard/loop-var idem).
  Params seguem imutáveis por B.21 (eixo separado, não é BindKind).
- **NÃO DETECTAR/BARRAR O QUE NÃO EXISTE (lei do dono, recuperada 2026-08-18).** O compilador não
  escreve detecção, validação, ramo NEM mensagem para construção que a superfície NÃO produz ou que o
  parser já garante. `let`/`mut` não existem → o checker não deve ramificar em `BindKind::Mut`/`Let`
  nem citá-los; a checagem de caso-impossível é **código morto a REMOVER**, não a manter/reescrever.
  **Reword de mensagem que referencia o inexistente é BAND-AID — a causa-raiz é a detecção morta.**
  Corolário de mensagem: mensagem de erro NUNCA referencia construção removida (o usuário não deve nem
  saber que `let`/`mut` existiram); diz a causa real (ex.: "é imutável", "passe como `ref []T`").
- **Tipagem forte explícita na codebase do Teko** (via flag `--explicit`,
  default off, o checker só barra COM a flag — inferência segue recurso válido
  para quem USA Teko). Sob `--explicit`: `var`, `const`, parâmetros, campos e
  retorno (exceto void) exigem tipo na declaração — assinatura completa/
  explícita/forte/estática; e o gate de cast desnecessário vira ERRO (some do
  default). História viva em `mudancas-superficie-0.3.1.md` §11.2 (bloco EMISSÃO
  LIMPA), ANTES do §16.

## Leis de desenvolvimento (resumo — detalhe em docs/design/mudancas-superficie-0.3.1.md §11.2)
- **TESTES SÓ NO CI.** `teko test .` local dá OOM (ninguém roda). Validação local =
  **compilação** (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`, `ulimit -v 4194304`) +
  <!-- guard subiu 6→6,5 GiB (dono 2026-08-18): o codegen do #os empurrou o virtual; residente
       segue ~flat. A construção da AST é suspeita do pico — a otimizar. -->
  <!-- linha de validação original abaixo mantém a métrica -->
  fixpoint (tc2==tc3) + cross-check offline.
  - **LIBERAÇÃO CONDICIONAL — full `teko build .` (com testes) ao bater o marco de memória (dono
    2026-08-19):** enquanto o **build seco** NÃO picar **≤ 1,5 GB**, mantém-se o regime acima (só
    compilação/build seco, subshell `ulimit -v 4194304`, NUNCA `teko test .`). QUANDO um build seco
    medir **pico ≤ 1,5 GB** (1572864 KB) — este ≤ 1,5 GB é APENAS o gate que LIBERA `teko test .`
    local aos agentes, **NÃO** o alvo da campanha; o **alvo/marco REAL da campanha de redução de memória
    (RM-C / Eixo A/C) é `< 1 GB` de RSS no build seco** (dono 2026-08-20) —, as **execuções full `teko
    build .` que carregam testes ficam LIBERADAS** e a
    proibição de `teko test .` cai. O gatilho é o marco MEDIDO no build seco, não uma data. Agente que
    for validar mede o pico do build seco e reporta ao cruzar o marco.
- **GEN0 SAI DO SEED, NÃO DO `fetch_teko.sh` (dono 2026-08-20).** Todo agente que precise buildar linka o
  `gen0` a partir do `bootstrap/teko.c` commitado — **exatamente como o CI** (`CC=clang
  scripts/build_gen1_from_c.sh`) — e daí gen0→gen1→gen2→gen3. **NUNCA chama `scripts/fetch_teko.sh`**: o
  `gh` das sandboxes de agente não tem acesso ao repo (403), então `fetch_teko.sh` falha
  estruturalmente e sempre falhará aqui. O seed do `bootstrap/teko.c` é a **alavancagem do self-host** —
  sozinho basta pra reconstruir o compilador inteiro, sem binário externo. É caminho **PRIMÁRIO, não
  fallback**: não se gasta uma tentativa no `fetch_teko.sh` antes.
- **NATIVE É A ÚLTIMA ETAPA — gated em MEMÓRIA ESTÁVEL (dono 2026-08-19).** NÃO se toca no backend
  native (lowering, syscall, `.o`, endgame no-C, os stops de fronteira do gen2) enquanto a memória não
  estabilizar. O gatilho tríplice, TODO ele: **(1) build seco ≤ 1,5 GB · (2) fixpoint gen2==gen3
  byte-idêntico · (3) testes verdes.** Só então o native começa (M4). A campanha de memória (RM-C /
  Eixo A/C) vem ANTES; o native é o fim. **Corolário:** a onda de superfície (M1 G/S) e a reseed SM-R1
  seguem pela **rota C** — a buildabilidade do native NÃO é pré-requisito da reseed (isto SUPERSEDE a
  ressalva do pin SM-P1 de "não reseedar enquanto o gen2 native não builda": o native está deferido, não
  na fila da reseed). Eu (coordenador) não puxo native pra frente — foi o erro de 2026-08-19, corrigido.
  - **ATÉ O MARCO (C 100% verde + build seco ≤ 1,5 GB): ESCREVE EM TEKO, EMITE EM C — TUDO, não só DPS
    (dono 2026-08-20).** Enquanto o marco não bate, TODA feature é ensinada em Teko e **emitida pela
    rota C** (`src/codegen/codegen.tks`) — inclusive otimizações de arena como o DPS. **NÃO se apoia no
    comportamento do compilador C hospedeiro** (ex.: `sret` para retorno de agregado): o `codegen` tem
    que **emitir explícito** a semântica (arena, dest-passing, escape) controlada pelo compilador — o
    `sret` dá o slot, não a semântica de arena. O backend **native** (`src/lir/lower.tks` → object-file
    `.o`) é a **MESMA lógica, só muda a direção da emissão** (emite `.o` em vez de C) e o `lower.tks` **TAMBÉM se
    escreve agora** — o que é deferido é **RODAR o build native** (emitir `.o`, validar em runtime), **NÃO
    a escritura do código**. Cada feature escreve as DUAS direções: emissão C em `codegen.tks` (exercitada
    já) **e** emissão native em `lower.tks` (escrita já). A **rota C VALIDA** (compila, self-hospeda,
    reseeda — é a que RODA agora); a validação de **runtime do native espera o marco**. NÃO pular o
    `lower.tks`: ele entra no self-build (compila + reseeda), só não é EXECUTADO ainda. (Uma feature "sumida"
    pode estar em qualquer um dos dois — verificar contra o código, não presumir.)
- **GUARD SUBIU 4→4,5 GiB `ulimit -v 4718592` (dono 2026-08-24).** O build seco estoura no **VIRTUAL** a 4 GiB mesmo com RSS ~3,8 GB (o virtual tem ~350 MB de overhead sobre o RSS → OOM antes de completar o codegen). O guard sobe pra **4,5 GiB** pra os builds COMPLETAREM (necessário pra medir o pico real na contabilidade e pra a onda de expurgo do runtime C). **A lei do RATCHET (RSS só baixa, D68) segue governando** — subir o TETO virtual não autoriza o RSS a crescer; é só folga de overhead virtual. Meta segue `< 1 GB` de RSS; quando o RSS cair, o teto volta.
- **GUARD DE MEMÓRIA `ulimit -v 4194304` (4 GiB) = INVIOLÁVEL (dono 2026-08-20, baixado de 6,5→4 GiB) — atualizado p/ 4,5 GiB acima.**
  Os builds de ensino/aditivos agora picam ~3,3 GB → o teto cai pra 4 GiB: (a) pega regressão de memória
  (estouro acima de 4 GiB = sinal), (b) cabe **mais build simultâneo** nos ~15 GiB físicos (4 GiB×3 vs
  6,5 GiB×2) → habilita mais paralelismo. Se um build **estoura** o guard, o agente **encontra a causa-raiz
  do consumo e corrige** — NUNCA levanta o teto. Levantar o `ulimit` mascara exatamente o problema que o
  expurgo existe pra resolver (o `tk_slice_push_r`/inflação de arrays dinâmicos = 93% do pico), e saturar
  os ~15 GiB físicos causa OOM/thrash. Estouro = diagnóstico + correção da inflação, não `ulimit` maior.
  (Um agente subiu o teto e travou a máquina em 100%; foi parado.)
  - **EXCEÇÃO ÚNICA — agente da campanha de memória combinada (io/fs + arena + expurgo) roda com 10 GB
    (dono 2026-08-19):** enquanto o expurgo/streaming está EM CURSO, a build intermediária pode picar
    acima de 4 GB antes da reclamação fechar; SÓ esse agente recebe `ulimit -v` ~10 GB (10485760) como
    exceção temporária de desenvolvimento. **MAS o critério de aceitação é DURO: a conversão pra gen2 tem
    que usar < 6 GB** — senão não faz sentido. O 10 GB é folga de dev, não a meta; a meta é gen2 < 6 GB
    (rumo a ≤1,5 GB). Fora desse agente-exceção, o guard 4 GiB segue inviolável. A máquina tem ~15 GiB
    física — 10 GB de um processo ainda cabe; NÃO passar disso.
- **RATCHET DE MEMÓRIA — só a QUEDA passa; manter ou aumentar é REGRESSÃO (dono 2026-08-24).** Enquanto o
  ajuste de memória está em curso, TODA mudança tem que **BAIXAR o pico (estrito).** **Manter no mesmo
  patamar (flat, queda zero) OU aumentar = REGRESSÃO — reverter/corrigir ANTES de landar** (flat é regressão,
  mesma categoria que aumento, NÃO "trabalho à toa tolerável"). Só a queda medida landa. A métrica ÚNICA é a
  linha canônica `teko: memory: peak <N> MB` do
  **build seco** (a mesma que o CI reporta — ex. `3811.9 MB`); mede-se SEMPRE o MESMO build/geração (não
  misturar rung-1 com gen2 — baseline diferente falseia a comparação) e compara-se maçã-com-maçã contra o
  commit anterior. Consequência dura: uma conversão que remove `push` mas CRESCE o pico (ex.: FILTRO em
  duas-passadas que pré-aloca `[src.len]T=[]` com `count << src.len` = over-alloc) **viola a lei** — a
  correção é contar exato numa 1ª passada barata, não usar o limite folgado. O número só anda pra baixo.
  **ESCOPO (dono 2026-08-24): o ratchet vale pra TODA obra que toca o build durante a campanha.** As
  tarefas de REDUÇÃO (jardinagem/Eixo A/B/C, DPS) têm que BAIXAR (estrito). As demais — em especial o
  **EXPURGO DO RUNTIME C (teko_rt/assert/win → Teko)** — têm o piso **NÃO CRESCER**: o runtime reescrito em
  Teko tem que ser tão enxuto quanto (ou mais que) o C que substitui; crescer o pico = regressão. Ninguém
  pode piorar o pico, ponto — reduzir é a meta, não-crescer é o mínimo.
- **TAREFA LONGA → BRANCH REAL + PUSH FREQUENTE (dono 2026-08-19).** Trabalho longo (a campanha io/fs+
  arena+expurgo é lenta até pra agente mecânico) tem que **commitar e PUSHAR regularmente** (a cada crumb/
  passo) numa **branch real no origin** — não confiar no worktree local, que **some no restart de container/
  sessão** (perdemos o trabalho do agente de reclamação exatamente assim). Push frequente = trabalho vive no
  origin e sobrevive a restart; se o agente morrer, re-despacho continua da branch pushada.
- **MENOS BUILD, MAIS CÓDIGO (dono 2026-08-24).** Self-compile custa minutos + memória; **build-por-edição é
  desperdício que ninguém faz.** O agente escreve um LOTE/camada coerente inteiro RACIOCINANDO sobre a
  correção pelas regras da linguagem (na maioria das vezes não precisa de build pra saber que está certo) e
  builda só no CHECKPOINT que realmente precisa validar (camada fechada, degrau pra reseed, medição). Um
  build valida um lote, não uma linha; um commit = um lote validado. O gate de memória (ratchet, D68) se
  aplica aos builds que REALMENTE roda, não a um build-por-passo. Quem mede (contabilidade/perfil) instrumenta
  pra extrair TUDO num único run, não N builds pra N números.
- **COMPILADOR C LOCAL = CLANG (dono 2026-08-18).** Todo agente e toda medição local usam **`TEKO_CC=clang`**
  (e `CC=clang` para o caminho cru `scripts/build_gen1_from_c.sh` que linka o `teko.c` direto). Motivo-raiz: o
  `cc` default no Linux é gcc, **patologicamente lento** no TU único de 22 MB do `teko.c` (medição de gcc levou
  +20 min; clang faz em segundos). O CI já usa clang (`ci_cc_wrap.sh` shima `cc`→clang, `CC_UNDERLYING=clang`) —
  então clang local **espelha o CI**. NÃO trocar o default `cc` dos scripts (o `ci_cc_wrap` depende de `cc` ser o
  wrapper; trocar fura os sanitizers) — o override é por env. `teko` honra `TEKO_CC` antes do host `cc`
  (`project.tks` `resolve_cc_choice`); Windows já força clang por construção.
- **Forward-only, sem PR:** drenar para `fix/retirement` por ff/cherry-pick.
- **PROVENANCE/reseed-via-CI = LEI REVOGADA (dono, revogada há tempo; reafirmado 2026-08-18).**
  O `provenance_gate` está **DESABILITADO** — o que os comentários do PROVENANCE dizem NÃO importa.
  O seed do `bootstrap/teko.c` é **aceito à força** (ignorar provenance). Se o seed **falhar**, o CI
  **falha imediatamente** — **SEM** fallback pra versão publicada antiga. **NÃO** criar nada novo no
  CI pra satisfazer provenance.
- **Reseed** de `bootstrap/teko.c` quando mudança de **compilador** altera o C emitido: harvest local
  (fixpoint gen2==gen3), sem gate de provenance. **NÃO reseedar no meio: LIMPEZA PRIMEIRO, reseed só no
  fim, tudo junto** (dono 2026-08-18). Módulos-folha não exigem reseed.
- **Teko é um monólito e precisa cross-compilar.** A perna C emite **UM** `teko.c`
  que compila em toda arquitetura/SO via `#if` do C — tem que **emitir tudo**
  (todos os alvos), não podar para o host que emite. Só o backend **native** emite
  um executável por (arch, SO), e ainda assim cross-compila.
- **`teko.c` É MULETA — ENDGAME É BINÁRIO LINKÁVEL POR `ld` SEM COMPILADOR C (dono 2026-08-19).**
  O terminal do pipeline NÃO é `teko.c`; é a linguagem emitir seu **próprio objeto/binário linkável por
  `ld`** (backend native: `objfile_elf/macho/coff`, `objfile_ar`), sem depender de compilador C. `teko.c`
  (UM TU cross-compilado por `#if`) é a rota transitória enquanto o native não fecha. No pipeline por
  unidade (namespace), **cada namespace emite um OBJETO** → `ld`/link interno junta = compilação separada
  clássica (o despejo de memória por unidade fica natural: unidade→objeto no disco→libera). O fixpoint
  migra de "gen2.c==gen3.c byte-idêntico" para "**objeto native se reproduz**" (determinismo do objeto:
  sem timestamp, ordem estável de símbolos/seções, sem paths absolutos); a muleta C sai quando as pernas
  native do CI fecham verde.
- **A BUILD DO COMPILADOR EMITE O `.tkh` JUNTO COM O BINÁRIO (dono 2026-08-19) — pode começar já,
  independente do eixo de memória.** A emissão do executável final (rota C OU native) deve **também
  emitir o `.tkh` do próprio compilador** (a superfície `exp` agregada dele), e o **pacote entrega
  binário + `.tkh`**. O `.tkh` é backend-independente (interface de tipos) → sai igual nas duas rotas.
  Serve às features da próxima versão: o dev usa o `.tkh` do compilador para **intellisense na IDE**
  que quiser e para **linkar/estender o compilador** como quiser. (Só `exp` entra — a FFI interna
  `exp`+`pub` é transitória e NÃO embarca; ver R8.)
- Teko-only (.tks), W15 (doc-comments-only, flatten/extract, helpers com nome único
  tree-wide), sem VM/GC, arena.
- **NO PUSHES (LEI DURA, dono 2026-08-18) — inverte o antigo W15 "no index-assign".** Array é
  IMUTÁVEL; `teko::list::push`/`empty()`-em-loop está **PROIBIDO** — é a RAIZ dos 93% de memória
  (profiler `tk_obs`: `tk_slice_push_r` = 4980 MB = 93%, 20,3M copy-grows que vazam na arena `root`
  nunca-liberada). Index-assign passa de proibido a PREFERIDO. **AS 4 NATUREZAS E COMO CONVERTER
  (rulings dono 2026-08-18, sobre código real — NENHUM sítio é impossível, tudo é calculável):**
  1. **MAP** (um item por elemento da fonte) → tamanho = `fonte.len`; pré-aloca `[]T` de `fonte.len` +
     `loop i { xs[i] = f(fonte[i]) }`.
  2. **PARSE/SCAN** (split, tokenizar — `n` sai de varrer) → **dois loops**: 1º só conta `n`, 2º
     pré-aloca `[]T` de `n` e grava por índice.
  3. **FILTRO** (subconjunto — `push` condicional) → **começa pelo maior (alargar)**: pré-aloca o
     limite superior (`fonte.len`), grava os que encaixam num `count`, corta `slice[0..count]`.
  4. **BUFFER DE SAÍDA** (o `cb`/emissão de texto do codegen, o de 93%) → **literais + interpolação**
     (`$"..."`) / array de bytes literal. ZERO buffer que cresce; nada de stream nem coleção nova.
  (`typer.tks` `type_index_assign`, slice `[]T`, `loop var i in 0..n { xs[i] = … }` já suportam.)
- **FORMA DO ARRAY FIXO — ZERO-FILL, `count` UNIVERSAL, dev trata (dono 2026-08-18, design FINAL).**
  Header `{ptr, len}` (SEM `cap`, SEM ctrl, SEM tag). **`of_len<T>(n): []T` = `memset`-zero** — uma
  passada, ZERO inflação de memória. Presença/"qual slot foi preenchido" = **`count`/watermark**
  (universal, funciona pra TODO `T`): preenche contíguo `[0..count)`, corta `slice[0..count]`; o `count`
  cai natural no loop. **`isset`/`trim`/`ctrl`-array/tag-null: DESCARTADOS** — `isset` não funciona pra
  tipo-valor (`0` setado == `0` não-setado; e o ctrl-array inflaria), então `count` é a resposta única.
  - **Slot tipo-VALOR** (número/byte/char/bool/string/struct-de-valores): zero = default VÁLIDO, leitura
    de slot não-preenchido é segura (struct zerado é o "zero-value" do Go).
  - **Slot REFERÊNCIA não-null:** `of_len` zera o ponteiro (null = sentinela natural de não-preenchido,
    custo memória ZERO); **`x[i] = …` é trabalho do dev**. Segurar/ler o ponteiro `x[i]` é inofensivo;
    **DESREFERENCIAR** um slot zero (`x[i].campo`/método) → **PÂNICO em runtime** `arquivo:linha:coluna:
    "causa"` (guard de null-deref, não segfault cru). O compilador auto-compilando conhece tudo →
    preenche certo → nunca dispara; o guard é backstop pro usuário.
- **RESEED ITERATIVO, o AGENTE faz (dono 2026-08-18):** o expurgo NÃO tem um reseed único no fim. É
  `ENSINAR → SEED → SWEEP → SEED`, quantas voltas precisar. Staging do bootstrap: pra varrer consumidores
  usando a forma nova, o `gen0` já precisa entendê-la → ensina a capacidade no `src/`, reseeda (gen0
  ganha), varre os consumidores, reseeda de novo, repete. Como o **layout do elemento NÃO muda**
  (zero-fill puro, sem tag), a transição é **escalonada-verde**: o `push` velho passa a produzir o header
  novo e coexiste durante a migração; fixpoint gen2==gen3 a cada harvest.
- **METODOLOGIA DO EXPURGO — CONSTRUIR ANTES, COMPILADOR ENUMERA A LIMPEZA (dono 2026-08-18).** ORDEM
  (o agente estava invertendo — "limpando" ANTES de construir o backend novo, é ERRADO):
  1. **CONSTRUIR a nova maquinária PRIMEIRO**, aditiva, convivendo com a velha: o **literal byte-string
     `b"abc"` → `[]byte`** (estende o byte-char `b'A'`; NÃO é `.to_bytes()`) + o **cast IMPLÍCITO
     `str`↔`[]byte`** (mesma rep `{ptr,len}` de bytes → **reinterpret/identidade**, sem cópia; `str_from_bytes`
     = `tk_str_of_bytes_len` e `tk_bytes_of_str_len` já existem, só o checker ACEITAR o implícito), o array
     fixo de tamanho-runtime **`var x: [n]T = []`** (zero-fill; é o
     "of_len" como SINTAXE DE TIPO), o idioma de join por índice exato, o guard de null-deref, roteado
     pela arena-Teko existente. NÃO remover o velho, NÃO varrer ainda.
  2. **SHADOW** = programa AVULSO **não versionado** (scratch, fora do repo) OU um `.tkr` regressivo
     rodado ISOLADO (flag "rodar só este teste" — evita o OOM do `teko test .`): compila+roda com o
     comportamento esperado; 1ª versão com o compilador ATUAL extrai baseline (tamanho final, memória);
     evolui-se a maquinária nova reduzindo memória/binário/operações contra esse baseline.
  3. **SEED** (gen0 ganha a maquinária nova).
  4. **DESENSINAR + REMOVER AS RAÍZES do estado velho** (`tk_slice_push*`/`cb`/`append_fo`/`push_fo`,
     `list::push`/`empty`/`grow_inplace`/`with_cap`, deps do `teko_rt.c`). A remoção não falha "por não
     existir mais" — mas o **próprio compilador passa a ERRAR cru** onde ainda referencia o velho.
  5. **SEED.** Com o seed novo, o compilador **tenta se auto-compilar e ERRA** — e **esses erros SÃO a
     lista de limpezas**: cada erro aponta um sítio a converter pro idioma novo. NÃO caçar 4917 à mão —
     remover a raiz e deixar o compilador ENUMERAR. Corrige → seed → repete até verde (fixpoint gen2==gen3).
  **IDIOMA (sem `cb`/`append_fo` — múltiplo append PROIBIDO):** cada peça = UM spread-literal com
  `b"…"` nos literais e `..str` direto nos dinâmicos
  (`outs[i] = [..b"#define TK_ARENA_", ..suffix, b' ', ..sym, b'\n']`); acumula `total`; aloca
  `var final: [total]byte = []` e copia por índice (`final[k]=o; k++`). O compilador pode const-foldar
  pra literal puro quando as partes são conhecidas.
- **FASE 1 e 2 UNIFICADAS — EXPURGO TOTAL JÁ (dono 2026-08-18).** Não há interim: expurgar array
  dinâmico de uma vez. Superfície medida (recon 2026-08-18): **2698 `push` + 2202 `empty` + 11
  `grow_inplace` + 6 `with_cap` = 4917 sítios**. Núcleos: checker 1615, lir 877, build 652, backend
  633, parser 293, codegen 163. Aliasing a redesenhar (posse): `Env` (scope.tks), `LEnv` (6 arrays
  paralelos, lower.tks), `LowerCtx`, coleções `Dictionary`/`Map`/`Hashset`. Encenar por módulo,
  fixpoint como guarda (conversão é preservante), **reseed ITERATIVO** (ensinar→seed→sweep→seed, acima).
- **NADA em `teko_rt.c` PRO EXPURGO; sem novo `from "teko_rt"` (dono 2026-08-18).** O free-old em C era
  erro — abandonado (`perf/push-free-old`/`fase1-nopushes` descartadas). Estamos reescrevendo 100% em
  Teko: se runtime precisar, **transcreve pra Teko**, não patch no C. A máquina de slice-grow do C
  (`tk_slice_push_r`/`grow_inplace`/`with_cap`) vira **código morto a REMOVER** com o expurgo.
- **`grow_inplace` É WORKAROUND — PROIBIDO (dono 2026-08-18).** Manutenção de array é **100% MANUAL**.
  `teko::list::grow_inplace(ref x, …)` é da MESMA classe do `push` (copy-grow amortizado escondido) —
  banido junto. Nada de primitivo de crescimento; só literal / pré-alocação + `x[i]=y` / limite+corte /
  duas passadas. (`grow_inplace`/`with_cap`/`tk_slice_push_r` = código morto REMOVIDO no expurgo total.)
- **`ref []T` = SÓ ponteiro-de-posição (dono 2026-08-18).** Serve para **operar o ponteiro de UMA
  posição do array sem cópia** (`a[i]` como ref/escrita in-place). **NÃO** aceita crescer (push/grow) nem
  reatribuir o array inteiro (`a = […]` → arena do callee → segfault). Substituir o array = construir
  cópia local e **retornar ao caller** (DPS, arena do caller).
- **REGRA DURÍSSIMA — ZERO CRESCIMENTO DINÂMICO + PURGE IMEDIATO NA REATRIBUIÇÃO (dono 2026-08-18).**
  (a) **Array NÃO cresce dinamicamente de modo algum** — absoluto (reafirma NO PUSHES / `grow_inplace`
  banido). Tamanho é sempre conhecido/calculado na criação. (b) **Ao criar um novo array que SUBSTITUI a
  variável que continha o array** (`a = <novo array>` — spread-literal, pré-aloca+índice, corte), o array
  **ANTERIOR (o valor que a variável segurava) é PURGADO IMEDIATAMENTE da memória** — o gatilho é a
  reatribuição da variável, e a liberação é eager/no ato, não adiada. Semântica de posse: a variável-array
  possui seu backing; reatribuir libera o backing antigo na hora.
  **UAF é responsabilidade do DEV no DESIGN de uso, NÃO do backend** — o backend purga o velho de imediato;
  o desenvolvedor garante NÃO usar a referência antiga depois de reatribuir (o compilador auto-compilando
  conhece tudo → nunca dispara). Sintoma que a rule resolve: depois da limpeza a memória **SUBIU** em vez
  de cair, porque o `push` (copy-grow) LIBERAVA o buffer velho no realloc, e o rebuild-na-arena-append-only
  (que nunca libera) passou a **VAZAR** cada versão antiga. Com o purge imediato, memória cai de **+6 GB**
  (patamar atual da build, pico medido ~6,2 GB) para **≤1,5 GB** — não é 2,5→1,5 (os 2,5 GB eram só o
  consumo específico do push medido antes); o alvo real é **+6 GB → ≤1,5 GB**. Disparar agente de continuação com esta rule
  assim que drenar.
- **I/O STREAMING EM TEKO — DUAS FORMAS, BUFFER ≤1024 B, SEM `teko_rt` (dono 2026-08-19).** Causa-raiz
  achada: o I/O lê e grava TUDO de uma vez, materializado — `read_file`→`str` inteira, `write_file`/
  `write_file_bytes` gravam o conteúdo todo; o `teko.c` de 22 MB é montado inteiro na RAM antes do único
  write; e o `src/io/stream.tks` (`Buf`) é acumulador `list::push` (copy-grow), não stream real. Além de
  ser FFI `from "teko_rt"`. Desenho obrigatório, **tudo em Teko sobre syscalls (sem `from "teko_rt"`):**
  1. **DUAS formas de read/write:** **TOTAL** (tudo de uma vez — uma opção mantida) e **STREAM** (por chunk).
  2. **A forma STREAM tem variações com offset/seek** (acesso posicionado) e **opção append-only** (apenda no
     arquivo) e **modo read-only**.
  3. **Buffering de no máximo 1024 bytes por vez** (buffer pequeno reusável — sem acumulador que cresce).
  4. **O COMPILADOR usa ESTRITAMENTE a forma STREAM.** Todo local que gera SAÍDA usa o stream (de
     preferência append-only); todo local que LÊ usa o stream em read-only. (Migrar `teko.c`, `.tkh`, e
     todas as leituras/escritas do compilador.)
  - **CO-DEPENDENTE com a arena (dono 2026-08-19):** a arena sozinha NÃO faz mágica — os buffers
    materializados (22 MB do `teko.c`, arquivos lidos inteiros) continuam vazando; o streaming com buffer
    ≤1024 B é o que corta isso. Arena-por-escopo + I/O-streaming são co-dependentes: precisam vir juntos
    pra a memória cair.
- **NÃO EXISTE C CONGELADO (dono 2026-08-18, REVOGA a lei "§16 C congelado" de 2026-08-17).**
  `src/runtime/teko_rt.c`, `teko_rt.h`, `src/win32_compat.h`, `src/assert/assert.c`, `assert.h`
  **PODEM ser editados** para bug de memória/correção em C. **PORÉM (dono 2026-08-18): o expurgo de
  array dinâmico NÃO passa por `teko_rt.c`** — a máquina de slice-grow do C não é patchada, é REMOVIDA;
  o que sobrar de runtime de array mora em Teko (o free-old em C foi abandonado). A migração C→Teko do
  §16 segue como **meta** do Doc-2 (`docs/design/plano-s16-expurgo-libc-completo.md`).
- **§16 — SEM ATALHOS (lei do dono, 2026-08-17):** nenhum workaround/degrade no expurgo do C. Toda
  função de libc vira implementação **real** em Teko (raw syscall / FFI-da-ABI-do-SO). **Se existe em C,
  existe em Teko.** Rulings ratificadas R1–R5 em `docs/design/plano-s16-expurgo-libc-completo.md` §5.
