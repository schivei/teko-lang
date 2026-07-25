---
section: language-law
created: 2026-07-13
source: TEKO_CONSTITUTION.md, TEKO_LEGISLATION.md, TEKO_MASTER_PLAN.md (wave constraints)
---

# Teko Laws Digest

**Teko-only (ruling 2026-07-04):** compiler source canonical in `.tks`; frozen C bootstrap (`0.0.1.3-bootstrap`) archived; C runtime (`teko_rt.c`) maintained.

**Twins retired (2026-07-13, #524):** VM interpreter, REPL, C bootstrap all retired; native AOT sole engine.

**W15-from-now:** code quality (doc-comments only, no inline; flatten, extract; cyclomatic <N) applied continuously, not deferred.

**Issues-100% + NO-DEFERRAL (reforçado 2026-07-16):** every open item ships complete; no deferrers ("future wave"); tensions → law-first ruling + tribunal. **Toda falha achada (mesmo antiga, mesmo que "não bloqueie") é resolvida AGORA, in-wave — "não bloqueia" não é desculpa (é falha de design/desleixo). Se o fix precisa de peça planejada para o futuro (0.4/pós-1.0), a peça é ADIANTADA agora.** "follow-up / não bloqueia / workaround-em-vez-de-fix / completar-depois" para uma falha real = PROIBIDO. Recorte de roadmap (feature futura não-começada que nenhuma falha exige) continua ok — até uma falha exigi-la.

**Memory-is-rules-only (governança 2026-07-16):** memória = SÓ conjuntos de regras/design-rulings/convenções; definição de agente = regras + como-agir; skill = superpoderes (+regras). Um ACHADO que precisa ser resolvido (bug/gap/limitação) NÃO vive em memória → valida se já foi feito → senão vira ISSUE (`bug`) rastreada. Ao migrar p/ issue, REMOVE a nota da memória/skill/agente (o detalhe técnico vai no corpo da issue).

**Resolve-in-same-task / don't-ask (2026-07-13):** an error found now is fixed now; a future-planned piece the task needs is pulled forward. This is LAW-decided, not an owner call — never ask the owner to choose fix-now-vs-defer or whether a disproportionate rework is "in scope" (asking is itself the violation). Owner-decision tensions = product taste or law-vs-law ONLY.

**100%-coverage-on-delta:** new/altered code covers all branches + lines; arm inalcanzaable only if listed with reason.

**Main-integrity/never-merge-on-snapshot:** all checks `completed + success` before merge; `gh pr merge`, not direct push.

**Trem empilhado + dreno LIFO (2026-07-23/25):** entrega em vagões (PR baseado no vagão anterior, nunca na main); drena de cima para baixo, cada vagão mergeia no de baixo, main recebe uma integração única. **Vagão vermelho cujo filho está verde fica verde quando o filho desagua** → NUNCA cascatear fix para baixo; correção pequena vai no último vagão engatado, grande vira vagão novo no topo; gate único = topo verde; vagão fechado não se toca; vagão só reabre se merge der erro. Bump = **contra-máquina** (vagão próprio no topo, o único a sair de draft = deixa do owner). Regras completas: `docs/memory/teko-stacked-train-discipline.md`; procedimento: skill `train`.

**Commit hygiene (2026-07-15) — inclui o INTEGRADOR:** zero `Co-Authored-By:` e zero linha "Generated with/by Claude Code" em commits (corpo Conventional-Commits limpo); sobrepõe o default do harness. **Force-push DESABILITADO**, regra forward-only — nunca reescrever história pushada para consertar trailer. Corpo de PR pode manter nota de geração.

**DRY-last:** the whole-codebase DRY refactor is final phase; every other item lands first.

**Metaprogramming-out-of-LTS:** comptime/macros deferred to post-`1.0.0.0`; traits (structural derive) stay.

**STS-before-LTS (2026-07-13):** sequential-task-structure ruling stabilizes before LTS lockdown; waves solve independently.

- **Gate que não gateia é pior que gate ausente (2026-07-25).** Quatro instâncias na .31: dois jobs de
  sanitizer com condição que nunca casa (`github.ref` em `pull_request` é `refs/pull/N/merge`); o
  runner de regressivo contando pulo como sucesso e escondendo no agregado (`N run, 0 failed` com
  tudo pulado); o `REGRESSION_REQUIRE_TOOLS`, o toggle fail-closed que o projeto construiu e nunca
  ligou em lugar nenhum; e o gate de tag esperando 60 min por workflows que **não rodam em push**
  (`on: pull_request:` apenas), o que travou release E seed novo. Regra: **todo gate afirma o modo em
  que está e falha quando o que devia rodar pulou.** `skipped` no caminho obrigatório é ERRO, não
  aprovação — e um gate que **não pode** passar bloqueia mais do que protege.
- **Prova removida tem que ser substituída por prova, não por prosa (2026-07-25).** O drop-128 tirou
  carriers de 128 bits que eram provas carregando peso, e em três lugares distintos a substituição foi
  uma frase de doc-comment que deixou de ser verdadeira: os `@throws panics on overflow` do
  `teko::time` (a flag nunca é definida), o "wrapping, never checked" do `numint_to_i64` (é UB de
  signed), e o "as fixtures são não-negativas" do `lir_interp` (com i64, u64 de bit alto é negativo —
  o oráculo divergiu do backend que ele valida). O contra-exemplo correto está no mesmo trem:
  `numint_hi`/`numint_lo`, com prova escrita e verificável no doc-comment. **Frase sobrevive a
  refactor; significado não.**
- **Um build por cenário é erro de design; a separação de fases é o habilitador, não a economia
  (owner 2026-07-25).** O runner de regressivo sintetiza um projeto descartável por cenário **e por
  linha de `Examples`** (`<prefix>.proj/` com `.tkp` mínimo + os statements), e paga **quatro
  processos** por build: `sh -c` (captura por redireção) → `teko` (start + init de runtime +
  front-end sobre 3 linhas) → `cc` (clang sobre o C gerado + runtime) → o binário. Medido na .31:
  318 builds em 8m33s = **1,61 s por build**, com fontes de no máximo 9 linhas — ou seja, custo
  **fixo**, não proporcional à fixture.

  Duas coisas que o ruling do owner NÃO precisou corrigir, porque já eram verdade: todo build de
  cenário já passa `--no-verify`, e a regressão que É o próprio binário (Feature R0) é
  **declarativa** — quatro cenários sem fonte e sem build, apontando por `# verified-by:` para fatos
  que o pipeline já estabelece (self-host, `teko test .` == 0, fixpoint gen1==gen2, own==C).

  O que a separação de fases destrava, e é onde o ganho mora: **paralelizar** os builds (mutuamente
  independentes), **um scratch só** em vez de N árvores, e **amortizar o `cc`** (um processo clang por
  cenário é o dominante). Reordenar sem essas três não economiza nada.

  Ressalva de premissa, registrada para não se perder: os cenários **não** cabem num único projeto —
  cada um é um programa com seus próprios statements de topo, e projeto Teko compila para **um**
  binário. O que se compartilha é a fase, o scratch e a passada de compilação; nunca o artefato.

  Ordem decidida: **medir na .31, reestruturar na .32** — paralelizar o runner mexe em determinismo de
  saída (ordem de linhas, interleaving de captura) e esse risco não entra no trem que está fechando.
