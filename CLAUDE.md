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

## Ritmo de trabalho
- Continuar o processo de forma autônoma, mas **fazer pausas para ler** o que o
  dono pode ter escrito no meio do caminho, antes de decisões que importam.
- **Sem workarounds** (lei do dono): achar e resolver a **causa raiz**, nunca dar
  voltas para contornar o problema.
- **SEMPRE despachar um agente** (lei do dono 2026-08-17) para o trabalho de
  implementação — não fazer eu mesmo; preserva a sessão principal (menos tokens).
  O coordenador valida (build seco + fixpoint + reseed) e drena ff.
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

## Triagem de CI (PR #110 = fix/retirement) — dono 2026-08-18
- **Falha em `fixpoint (native)` → ESPERADA** (native para no DEGRAU), não é problema real.
- **Falha em `produce this leg` (C ou native) → FALHA REAL.** Ler o step com `head=100` (as primeiras
  ~100 linhas — a CAUSA está no topo; o tail é só limpeza de git).
- Assinar o CI do PR #110 (`subscribe_pr_activity`) DEPOIS de drenar C + toda a limpeza + reseed, para
  verificar/corrigir as pernas.

## Convenções da linguagem/codebase
- **Não existe `let`/`mut` na superfície — só `var`** (e `const`).
- **Tipagem forte explícita na codebase do Teko** (via flag `--explicit`,
  default off, o checker só barra COM a flag — inferência segue recurso válido
  para quem USA Teko). Sob `--explicit`: `var`, `const`, parâmetros, campos e
  retorno (exceto void) exigem tipo na declaração — assinatura completa/
  explícita/forte/estática; e o gate de cast desnecessário vira ERRO (some do
  default). História viva em `mudancas-superficie-0.3.1.md` §11.2 (bloco EMISSÃO
  LIMPA), ANTES do §16.

## Leis de desenvolvimento (resumo — detalhe em docs/design/mudancas-superficie-0.3.1.md §11.2)
- **TESTES SÓ NO CI.** `teko test .` local dá OOM (ninguém roda). Validação local =
  **compilação** (`--no-verify --release`, `TEKO_BACKEND=c`, `ulimit -v 6815744`) +
  <!-- guard subiu 6→6,5 GiB (dono 2026-08-18): o codegen do #os empurrou o virtual; residente
       segue ~flat. A construção da AST é suspeita do pico — a otimizar. -->
  <!-- linha de validação original abaixo mantém a métrica -->
  fixpoint (tc2==tc3) + cross-check offline.
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
- Teko-only (.tks), W15 (doc-comments-only, flatten/extract, helpers com nome único
  tree-wide), sem VM/GC, arena.
- **NO PUSHES (LEI DURA, dono 2026-08-18) — inverte o antigo W15 "no index-assign".** Array é
  IMUTÁVEL; `teko::list::push`/`empty()`-em-loop está **PROIBIDO** — é a RAIZ dos 93% de memória
  (profiler `tk_obs`: `tk_slice_push_r` = 4980 MB = 93%, 20,3M copy-grows que vazam na arena `root`
  nunca-liberada). Construir array por **LITERAL** ou por **pré-alocação de `[]T` + atribuição
  posicional `x[i] = y`** (a linguagem JÁ suporta: `typer.tks` `type_index_assign`, slice `[]T`;
  `loop var i in 0..n { xs[i] = … }`). Tamanho desconhecido: pré-alocar+cortar (`slice[0..n]`) ou
  builder amortizado in-place — NUNCA o `push` copy-grow. Index-assign passa de proibido a PREFERIDO.
- **NÃO EXISTE C CONGELADO (dono 2026-08-18, REVOGA a lei "§16 C congelado" de 2026-08-17).**
  `src/runtime/teko_rt.c`, `teko_rt.h`, `src/win32_compat.h`, `src/assert/assert.c`, `assert.h`
  **PODEM ser editados** — corrigir bug de memória/correção em C é permitido (ex.: o fix do leak do
  `tk_slice_push_r`). A migração C→Teko do §16 segue como **meta** do Doc-2 (`docs/design/
  plano-s16-expurgo-libc-completo.md`), mas o C **não está congelado** no interim.
- **§16 — SEM ATALHOS (lei do dono, 2026-08-17):** nenhum workaround/degrade no expurgo do C. Toda
  função de libc vira implementação **real** em Teko (raw syscall / FFI-da-ABI-do-SO). **Se existe em C,
  existe em Teko.** Rulings ratificadas R1–R5 em `docs/design/plano-s16-expurgo-libc-completo.md` §5.
