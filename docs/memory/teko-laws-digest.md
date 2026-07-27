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

---

**A ESCADA DA MORTE DO C — critério BINÁRIO (dono, 2026-07-27, repetido; a primeira vez foi de
madrugada e a segunda porque eu o re-derivei em vez de aplicá-lo).** A sequência tem numeração
própria e começa no ZERO — ela NÃO é a escada de lowering nativo (const struct / enum carrier /
`lo_byte`), que é outro eixo:

  * **degrau 0 — seed (.30) compila gen1** a partir de um fonte que JÁ expurgou 100% dos emissores
    e das dependências de C. O `teko.c` produzido NESTE degrau é inevitável e legítimo: o seed só
    sabe passar por C.
  * **degrau 1 — gen1, ANTES de compilar gen2, remove todos os `.c` e `.h`** e compila sem eles.
    E tem que funcionar.

**O TESTE DE ACERTO, literal:** *"quando gen1 compilar gen2 e ainda houver dependência de C ou
quaisquer emissões de C (seja analisador, teste ou teko.c), é pq fez errado."*

Isso não admite exceção **e não admite justificativa**, e é aí que eu errei duas vezes: o baseline
não-vazio de `scripts/no_emitted_c.sh` (`bin/teko-tktest.c`, `bin/teko-regrcov.c`) NÃO é uma
"verdade medida e declarada" como o próprio arquivo diz — é o defeito, escrito por extenso. O
argumento de que portar o gate hoje trocaria "um gate que emite C" por "nenhum gate" descreve o
tamanho do trabalho, não uma licença: sob este critério o baseline tem de chegar a VAZIO.

Corolário de método, porque o modo de falha é meu e não do código: perseguir honest-stop nativo um
a um é reativo e não tem fim visível. O degrau 0 é o que força a lista inteira a aparecer de uma
vez — mesmo raciocínio do `all-diagnostics`, aplicado à escada.

**`unsafe` — o que ele significa, e o que NAO vira regra (dono, 2026-07-27).** *"O unsafe na struct
ou em uma classe serve para endereçar todos os métodos nela como unsafe e para habilitar ponteiros
crus, se não usa, não tem pq ser unsafe."*

Consequencia RATIFICADA e em vigor: `unsafe` num ALIAS estrutural (`unsafe type X = ptr<byte>`) e
erro de compilacao, porque um alias nao tem membros nem corpo — nao ha metodo a enderecar, e a
capacidade de ponteiro pertence ao tipo aliasado, nao ao alias. Antes disso a grafia compilava e o
carimbo era **silenciosamente inerte** (alias resolve THROUGH; o carimbo so e procurado num
`Named`), o que furava o portao de contagio. `unsafe type T = struct/class { … }` continua valido e
inalterado: ali ha membros.

Consequencia que o dono decidiu NAO transformar em regra: *"convenção apenas"*. Um `unsafe`
DECORATIVO — struct marcada que nao contem ponteiro cru nem chama nada unsafe — nao e recusado pelo
checker. Fica como criterio de REVISAO, nao como gate. Nao proponha implementa-lo de novo; a
decisao foi tomada sabendo que o buraco simetrico (o `unsafe` que anestesia o leitor sem precisar)
continua aberto, e o custo de o checker decidir NECESSIDADE foi julgado maior que o ganho.

**O MODELO DE EXECUCAO DE TESTE E DE CONCORRENCIA (dono, 2026-07-27).** *"cada um [teste] deve
executar em corotina e corotina deve ser thread isolada, logo, cada corotina tem sua própria root
assim que nasce (como se fosse outro programa). Depois entrariam estruturas de sincronização e
compartilhamento (como canais)."*

Tres invariantes, nesta ordem, e a ordem e a decisao:

  1. **corrotina == thread ISOLADA** (1:1, o que o MASTER_PLAN ja fixara com "1:1 OS threads
     first"; M:N e backing posterior sob a mesma superficie).
  2. **raiz de arena PROPRIA ao nascer** — "como se fosse outro programa". Nao e marca numa raiz
     compartilhada, e raiz de arvore: `tk_region_new(NULL)` no nascimento, `tk_region_drop` na
     morte.
  3. **sincronizacao e compartilhamento vem DEPOIS** (canais). Nao antecipe: o desenho S8 ja
     reservou `channel<T>` deliberadamente porque nenhum dos tres ganhos (gate, codegen,
     regressor) usa canal — todos sao fork-join com escrita disjunta e leitura apos barreira.

**A CONSEQUENCIA QUE VALE MAIS QUE A REGRA:** com raiz por corrotina, `tk_arena_push`/`tk_arena_pop`
deixam de ser NECESSARIOS nesse caminho. Eles existem porque os testes compartilham
`tk_region_root()` e precisam marcar/rebobinar dentro dela. O defeito que o desenho S8 mediu —
push/pop nao recebem alca, empilham sobre a raiz do PROCESSO, o gate faz push/pop ao redor de CADA
teste, duas raias rebobinam uma sobre a outra e **o sintoma e nenhum** (reuso de arena e invisivel
ao ASan) — **nao precisa ser consertado, precisa ser tornado desnecessario.** Tornar um defeito
inalcancavel e melhor resultado que corrigi-lo.

**O CHAO EM C JA ESTA COMPLETO** e isso foi verificado, nao suposto (`src/runtime/teko_rt.h:148-161`):
`tk_region_new(parent)` com `parent = NULL` ja devolve raiz de arvore independente; `alloc`, `drop`,
`drop_subtree` existem. O unico singleton de processo e `tk_region_root()`, e e so a ele que
push/pop se amarram. Threads bottom em `pthread` da libc. **Nao ha uma linha de C a escrever** para
nenhum dos tres invariantes — ruling do dono de que nao se escreve mais C continua integro.

**O NOME E `isolate` — ISOLAMENTO, NAO SUSPENSAO (dono, 2026-07-27):** *"não é suspensão (como
async/await) é isolamento"*, e em seguida, sobre a grafia: *"isolate então"*.

Os dois modelos garantem coisas OPOSTAS, e por isso a palavra importa:

  * **suspensao** (async/await): varias unidades dividem UMA thread, cedem em pontos de `await`, e
    **compartilham tudo**. Isolamento nao e propriedade — e o que se abre mao em troca de troca de
    contexto barata.
  * **isolamento** (este modelo): cada unidade e thread propria com raiz de arena propria, "como se
    fosse outro programa". Nada e compartilhado ate que uma estrutura EXPLICITA compartilhe (canais,
    e canais vem depois).

**POR QUE `isolate`, e por que NAO as obvias.** O nome tinha de nomear a PROPRIEDADE que o dono
definiu, nao o mecanismo — porque o mecanismo pode mudar (MASTER_PLAN:260 fixa 1:1 primeiro com M:N
como backing posterior SOB A MESMA SUPERFICIE).

  * **`isolate` ESCOLHIDO** — diz literalmente a frase do dono ("como se fosse outro programa"):
    unidade com heap proprio que nada compartilha ate uma mensagem explicita. Ha precedente (Dart,
    V8), entao ninguem importa `yield`/`await` junto. Sobrevive ao M:N sem passar a mentir. Zero
    colisao na arvore.
  * **`corrotina`/`coroutine` REJEITADO** — carrega expectativa de SUSPENSAO: quem le importa
    `yield`, `await`, escalonamento cooperativo e memoria comum, que e o oposto do que se garante.
  * **`task` REJEITADO** — era o que o desenho usava, e carrega o MESMO defeito: em C#, Rust e
    Python, *task* e justamente a coisa de suspensao. Trocar uma palavra que mente por outra que
    mente igual nao e correcao.
  * **`thread` REJEITADO** — honesto hoje, falso no dia do M:N, e descreve o mecanismo em vez da
    garantia.
  * **`actor` REJEITADO** — traz caixa-postal, identidade e supervisao junto; e mais modelo do que
    foi pedido.
  * **`process` e `lane` INDISPONIVEIS** — ja ocupados nesta arvore (`teko::process::run_quiet` e
    subprocesso de verdade; `lane` e lane de CI).

A superficie e `teko::isolate` com `Isolate`, `spawn`, `join`, `fork_join`,
`hardware_parallelism()` — os VERBOS ja estavam certos no desenho, mudou o substantivo. Zero
`yield`, zero `await`. As cinco palavras-chave (`scope{}`/`spawn`/`channel<T>`/`send`/`recv`) seguem
RESERVADAS e nao congeladas, conforme MASTER_PLAN:262.

**AGRUPAMENTO DE ISOLAMENTO — DESCARTADO por YAGNI, com gatilho escrito (dono, 2026-07-27).** A
pergunta era como isolar um CONJUNTO sob um mesmo dominio de arena, e a proposta na mesa era
`#isolation_group(str)`. Fecho: *"quando chegar o momento do `scope {}` isso se resolve"*.

Por que nao e necessario AGORA, e o argumento e concreto: os tres usos nomeados para concorrencia
(gate de teste, codegen, regressivos) sao TODOS fork-join com escrita disjunta e leitura apos
barreira. O pai aloca a entrada compartilhada ANTES de bifurcar, cada membro nasce com raiz propria
e escreve so na dele, junta-se, o pai le. O compartilhamento e somente-leitura de dado que o pai ja
alocou — nao precisa de dominio comum, precisa de um ponteiro que sobreviva ao fork, e ele sobrevive
porque o pai nao morreu.

O caso que PARECIA justificar ja fora morto pelo proprio desenho: se cada teste tem raiz propria, o
setup caro seria refeito por teste? Nao — `concorrencia-adiantada-s8.md` §5.3 decidiu "uma raia, nao
um isolate por teste". As raias sao poucas (`hardware_parallelism`) e cada uma roda MUITOS testes em
sequencia. O setup e por raia, e raia E um isolate. Nao sobra o que agrupar.

**O RISCO QUE A PROPOSTA CARREGAVA, registrado porque ele volta em qualquer superficie por string:**
`#isolation_group("x")` agrupa por casamento de string SEM namespace — a familia de defeito que esta
sessao pagou tres vezes (o `builtin_fn` por ultimo segmento sequestrando corpos de usuario; o
`find_enum_info` por nome nu; o `check_ar` por substring). Dois modulos que escolham o mesmo nome de
grupo sem se conhecerem fundem os dominios EM SILENCIO, e fusao indevida de arena e invisivel ao
ASan. Se a ideia voltar, a chave tem de ser QUALIFICADA pelo namespace de escrita.

**GATILHO para reabrir:** um consumidor real que precise de estado MUTAVEL compartilhado entre
isolates E que nao caiba em fork-join. E mesmo entao a resposta provavelmente nao e um grupo, e
**canal** — o mecanismo ja reservado para compartilhamento, que por ruling vem DEPOIS.
