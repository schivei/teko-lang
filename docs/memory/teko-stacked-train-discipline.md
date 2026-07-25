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
