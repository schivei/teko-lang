---
section: language-law
created: 2026-07-13
source: TEKO_CONSTITUTION.md, TEKO_LEGISLATION.md, TEKO_MASTER_PLAN.md (wave constraints)
---

# Teko Laws Digest

**Teko-only (ruling 2026-07-04):** compiler source canonical in `.tks`; frozen C bootstrap (`0.0.1.3-bootstrap`) archived; C runtime (`teko_rt.c`) maintained.

**The C runtime may GROW to serve the native backend (ruling 2026-07-29, literal):** *"Faz sentido e concordo com o agente sobre as funções que adicionou para conseguir resolver o runtime nativo através do C, isso não é proíbido, o mesmo vale para um bug conhecido que possa quebrar o runtime silenciosamente sem conseguir corrigir o nativo adequadamente."*

Two permissions, and they are narrow. Adding to `teko_rt.c`/`.h` is allowed **(a)** when the native backend needs a shape the existing runtime does not offer, and **(b)** as the escape hatch for a KNOWN bug that would otherwise break the runtime SILENTLY and that the native path cannot yet fix properly. Silent breakage is the operative word: a loud, addressed stop is a degrau to close, not a reason to reach for C.

This does not soften "no new C emissions" — that ruling is about the compiler EMITTING C, and `teko_rt.c` is the RUNTIME the native backend LINKS AGAINST, which outlives the C route. It also does not make the runtime a dumping ground: what lands there is still owed a written reason at the site, and it is still surface that `src/runtime/teko_rt.tks` must eventually absorb.

Worked example (0.3.1.0 degrau 9): `tk_str_concat_len` / `tk_i64_to_str_len` / `tk_u64_to_str_len` — out-parameter-length twins of three existing builders, because the native backend's `LCall` captures a result in ONE register while a `tk_str` returned by value occupies the two-eightbyte SysV/AAPCS64 pair. Thin wrappers over their own twins, owning no logic, mirroring `tk_slice_push`'s established `(…, &out_len)` shape.

**Twins retired (2026-07-13, #524):** VM interpreter, REPL, C bootstrap all retired; native AOT sole engine.

**W15-from-now:** code quality (doc-comments only, no inline; flatten, extract; cyclomatic <N) applied continuously, not deferred.

**Issues-100% + NO-DEFERRAL (reforçado 2026-07-16):** every open item ships complete; no deferrers ("future wave"); tensions → law-first ruling + tribunal. **Toda falha achada (mesmo antiga, mesmo que "não bloqueie") é resolvida AGORA, in-wave — "não bloqueia" não é desculpa (é falha de design/desleixo). Se o fix precisa de peça planejada para o futuro (0.4/pós-1.0), a peça é ADIANTADA agora.** "follow-up / não bloqueia / workaround-em-vez-de-fix / completar-depois" para uma falha real = PROIBIDO. Recorte de roadmap (feature futura não-começada que nenhuma falha exige) continua ok — até uma falha exigi-la.

**Memory-is-rules-only (governança 2026-07-16):** memória = SÓ conjuntos de regras/design-rulings/convenções; definição de agente = regras + como-agir; skill = superpoderes (+regras). Um ACHADO que precisa ser resolvido (bug/gap/limitação) NÃO vive em memória → valida se já foi feito → senão vira ISSUE (`bug`) rastreada. Ao migrar p/ issue, REMOVE a nota da memória/skill/agente (o detalhe técnico vai no corpo da issue).

**Resolve-in-same-task / don't-ask (2026-07-13):** an error found now is fixed now; a future-planned piece the task needs is pulled forward. This is LAW-decided, not an owner call — never ask the owner to choose fix-now-vs-defer or whether a disproportionate rework is "in scope" (asking is itself the violation). Owner-decision tensions = product taste or law-vs-law ONLY.

**100%-coverage-on-delta:** new/altered code covers all branches + lines; arm inalcanzaable only if listed with reason.

**Main-integrity/never-merge-on-snapshot:** all checks `completed + success` before merge; `gh pr merge`, not direct push.

**Trem empilhado + dreno LIFO (2026-07-23/25):** entrega em vagões (PR baseado no vagão anterior, nunca na main); drena de cima para baixo, cada vagão mergeia no de baixo, main recebe uma integração única. **Vagão vermelho cujo filho está verde fica verde quando o filho desagua** → NUNCA cascatear fix para baixo; correção pequena vai no último vagão engatado, grande vira vagão novo no topo; gate único = topo verde; vagão fechado não se toca; vagão só reabre se merge der erro. Bump = **contra-máquina** (vagão próprio no topo, o único a sair de draft = deixa do owner). Regras completas: `docs/memory/teko-stacked-train-discipline.md`; procedimento: skill `train`.

**Commit hygiene (2026-07-15) — inclui o INTEGRADOR:** ~~zero `Co-Authored-By:` e zero linha "Generated with/by Claude Code" em commits~~ (corpo Conventional-Commits limpo); ~~sobrepõe o default do harness~~. **Force-push DESABILITADO**, regra forward-only — nunca reescrever história pushada para consertar trailer. Corpo de PR pode manter nota de geração.

**A metade do trailer foi SUPERADA (ruling do dono, 2026-07-29, literal):** *"pode manter l Co-Authored, o hook vai ficar te torrando a paciência e tu acaba alarmando desnecessariamente, como os merges são squash e eu os executo, esse problema foi superado."* O trailer pode ficar: o merge é **squash** e é o dono que o executa, portanto o corpo que chega à `main` é o dele, não o do commit de trabalho. Perseguir o trailer só produzia alarme e tentativas de reescrever história.

**O resto da lei continua real, mas o force-push É POR RAMO — precisão do dono (2026-07-29, literal):** *"force push é liberado em `cargo/**` (a esteira que alimenta a branch de trabalho `remodel/`)"*.

- Em **`remodel/**`** (o vagão) está mesmo desabilitado: uma tentativa de `--amend` + `--force-with-lease` no vagão desta lane foi recusada com *"push declined due to repository rule violations"*. Aqui vale forward-only sem excepção — corpo estragado corrige-se com um commit NOVO que explique o estrago.
- Em **`cargo/**`** (a esteira) o dono diz que é PERMITIDO — a intenção é que uma carga possa rebasear e reescrever a sua própria história, porque o que interessa é o que chega ao vagão.

**MAS ISSO NÃO É O QUE ACONTECE PELO TOKEN DO AGENTE — medido em 2026-07-29:**
```
$ git push --force-with-lease origin cargo/0.3.1-auditoria-memoria
 ! [remote rejected] cargo/0.3.1-auditoria-memoria -> cargo/0.3.1-auditoria-memoria
   (push declined due to repository rule violations)
```
A recusa é a MESMA do vagão. Não sei qual das duas leituras é a verdadeira e **não a invento**: pode ser que a regra do repositório não distinga ramos, ou que a permissão exista para a conta do dono e não para o token da app que o agente usa. As duas explicações cabem na evidência.

**O que isto obriga na prática, até se esclarecer:** um agente **não pode contar com force-push em lado nenhum**. Uma carga que precise de base nova ou que queira desfazer um commit tem de o fazer **antes do primeiro push**, ou aceitar que a história fica como está e corrigir com commit novo. Quem drena para o vagão pode **saltar** os commits maus com `cherry-pick` selectivo — foi o que se fez com o commit duplicado desta auditoria — e a história do vagão continua limpa.

**A LINEARIDADE É EM DUAS RELAÇÕES — precisão do dono (2026-07-29, literal):** *"Tem que ser linear na `remodel/**` em relação a main e a main em relação ao org (repo principal)."*

1. **`remodel/**` linear em relação à `main` da fork.** Verificável aqui e verificado nesta lane: zero commits de merge desde a `main`, a `main` é ancestral do vagão, base comum `4e6c4e4`.
2. **A `main` da fork linear em relação à do org (repositório principal).** Esta NÃO é verificável do lado do agente: só a fork está configurada como remoto e o acesso ao GitHub está limitado a `schivei/teko-lang`. Quem a garante é o dono, que executa os merges.

**Foi a relação 2 que mordeu no arranque desta lane:** o vagão nasceu da `main` do ORG enquanto o PR apontava para a `main` da FORK. As árvores eram idênticas (`7bd2318a` nas duas) e a topologia divergia, por causa dos squash-merges — resultado, conflito impossível de ler pelo diff. O sintoma a reconhecer: **um vagão sem CI de PR é conflito, não gatilho partido** — o GitHub não calcula ref de merge para um PR em conflito, portanto nenhum workflow `pull_request` dispara.

**Consequência prática, e custou trabalho a aprender:** uma carga que precise de base nova deve **rebasear**, nunca fazer merge do vagão para dentro de si. Um agente desta lane fez merge por acreditar que não podia reescrever depois de ter empurrado, e o commit de merge teve de ser saltado à mão no dreno para a história do vagão continuar linear. Com o force-push disponível na esteira, esse merge era evitável.

Nota de método, porque foi assim que este erro se descobriu: um agente veio dizer, sem lhe ser perguntado, que os commits dele estavam limpos de trailer "matching commit hygiene rules". Ele conhecia esta lei e o integrador não. **Ler este ficheiro por inteiro é barato; descobrir cada lei por acidente não é.**

**AS QUATRO REFERÊNCIAS DE DESENHO (ruling do dono, 2026-07-29, literal):** *"os mais complexos podem ser resolvidos se espelhando na abordagem de outras linguagens parecidas, temos bastante referência de Rust na superfície e zig no controle, bem como C# em 'addins' e go para alguns comportamentos."*

| eixo | referência |
|---|---|
| superfície (sintaxe, tipos, ergonomia) | **Rust** |
| controlo (fluxo, erros, explicitude) | **Zig** |
| *addins* / extensibilidade | **C#** |
| certos comportamentos | **Go** |

Não é permissão para copiar: é onde ir buscar a abordagem quando o caso é complexo e a decisão não é óbvia. **Nomeia sempre QUAL das quatro estás a espelhar e porquê** — dizer "como as outras linguagens fazem" esconde que elas fazem coisas diferentes, e a escolha entre elas é a decisão.

Exemplo trabalhado, e mostra porque a distinção importa. O dono pediu estreitamento por teste de `null` num `if`:
```teko
let a: i32 | null = 0
if a != null { exit(a) }   // 'a' livre da parte null
```
Das quatro referências, **só o C# faz isto** — refina A MESMA variável por análise de fluxo. Rust (`if let Some(x)`), Zig (`if (opt) |v|`) e Go (`if v, ok := …`) **ligam um nome NOVO**, que é precisamente o que o `match` do Teko já faz com `i32 as n`. Logo o pedido não é corrigir o `if`: é acrescentar um eixo que a linguagem não tem e que apenas uma das referências tem.

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
     reservou `chan<T>` deliberadamente porque nenhum dos tres ganhos (gate, codegen,
     regressor) usa canal — todos sao fork-join com escrita disjunta e leitura apos barreira.
     *(GRAFIA: `chan<T>`, ruling do dono 2026-07-29 — a forma curta, por coerencia com os outros
     tipos curtos. E a RESERVA acima foi levantada no mesmo dia, e so para o harness de testes:
     o dono nomeou `chan<T>` como a via do veredicto entre threads unitarias. O perigo que a
     reserva nomeava — ordem dependente de tempo — nao desapareceu; e contido pela regra "o canal
     transporta, nao ordena", `docs/design/harness-de-testes-gerado.md` §6.10.)*

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
`yield`, zero `await`. As cinco palavras-chave (`scope{}`/`spawn`/`chan<T>`/`send`/`recv`) seguem
RESERVADAS e nao congeladas, conforme MASTER_PLAN:262 — com UMA excecao datada: `chan<T>` foi
NOMEADA pelo dono em 2026-07-29 como a via do veredicto entre threads unitarias, e a sua GRAFIA
ficou decidida no mesmo dia (a forma curta, por coerencia com os outros tipos curtos). As outras
quatro continuam intactas.

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

## CARGA ADITIVA: quem ensina um builtin novo NAO pode consumi-lo em `src/` (2026-07-27)

Quem tipa `src/` e o SEED — um binario ja congelado. Entao uma carga que ensina um builtin novo ao
compilador tem DOIS lados e eles nao podem viajar no mesmo degrau:

- lado COMPILADOR (`checker/scope.tks`, `codegen/codegen.tks`, `checker/typer.tks`): compila sob o
  seed sem drama, porque e CODIGO NOVO e nao USO NOVO. Vai junto, sempre.
- lado CONSUMIDOR (qualquer `.tks` sob `src/` que CHAME o builtin novo, ou dependa de uma
  assinatura RETIPADA): o seed rejeita. Pedir isso e pedir que o passado conheca o futuro.

Retipar conta como builtin novo. `as_cstr` continuou existindo e ainda assim quebrou tudo, porque
mudou de `ptr` opaco para `ptr<byte>` no mesmo commit que passou a depender da forma nova.

**A saida NAO e uma forma transitoria em `src/`.** Tentei: declarei o campo como `ptr` opaco com um
doc-comment longo explicando que apertaria depois. Meia-verdade em `src/` custa o vagao inteiro e a
proxima pessoa herda uma grafia que o desenho nao manda. A saida e ESTACIONAR o consumidor fora do
`source = "src"` (`staged/`), na grafia FINAL, com uma condicao de reentrada verificavel por grep
contra o seed vigente. Volta sem edicao quando o degrau chegar.

**Fixtures nao sao consumidoras.** As sete desta carga carregavam CÓPIA LOCAL declarada como tal —
por isso a saida do modulo nao quebrou nenhuma. Isso e o padrao correto: a fixture exercita o
builtin novo atraves do gen1 (que o tem), nunca atraves de `src/`.

**Como esta lei foi descoberta:** a carga irmã (`#arena_size`) ja a tinha enunciado — *"aditivo —
`src/` NAO adota nesta carga, e a razao e dura: o seed precisa continuar construindo gen1"*. A carga
de c_types nao a leu, e os cinco portoes ficaram vermelhos em 994bcc4 em TODOS os hosts. Foi a
TERCEIRA quebra consecutiva da mesma carga, e as tres tem a mesma raiz unica: **escrita por leitura,
nunca compilada.**

## O AGENTE TEM DE COMPILAR. A PROIBICAO ERA MINHA E CUSTOU TRES VAGOES (2026-07-27)

Tres quebras consecutivas do vagao no mesmo dia, todas com a mesma assinatura: carga escrita por
LEITURA, entregue sem nunca ter sido compilada. Eu tratei isso como falha dos agentes ate ler o
relatorio final de um deles, que fecha assim:

    "Per protocol I did not run `teko test .`, did not build gen1/gen2/gen3, and did not run the
     local gate or fixpoint check — since running the compiler was explicitly forbidden this round."

Ele obedeceu. **A PROIBICAO ERA MINHA.** Eu a escrevi para os agentes nao gastarem tempo em builds
pesados, e ela transformou cada carga num palpite bem argumentado. Um agente que nao compila nao
esta economizando tempo do trem — esta transferindo o custo para o vagao, onde ele custa um ciclo
de CI inteiro E o veredito de todas as outras cargas em voo.

**A REGRA AGORA:** quem toca `.tks`/`.tkt` COMPILA e RODA o que tocou antes de entregar. Se o
ambiente nao permitir construir, a entrega diz isso NA PRIMEIRA LINHA do relatorio, e a carga NAO
drena — vai para uma fila de "precisa de medicao" em vez de entrar como se estivesse provada.
"Verificacao por leitura cuidadosa do fluxo de tokens" nao e verificacao, e o custo de descobrir
isso e sempre pago por quem drena.

**E VALE PARA MIM TAMBEM.** Drenei o degrau 5 conferindo so que mergeava limpo e que respeitava a
regra aditiva. Nao conferi se RODAVA. Todas as lanes de teste cairam com SIGABRT dentro do proprio
teste do degrau. Quem drena verifica a mesma coisa que quem escreve.

**A ORDEM: EMPURRA PRIMEIRO, MEDE DEPOIS** (correcao do dono no mesmo dia). A lei acima, escrita
como "compila e roda ANTES de entregar", induz o erro oposto e o dono cortou na hora: *"O que eles
não devem fazer: rodar um build tão longo antes de empurrar para a carga, pois se cair a sessão e
não tiverem empurrado, o trabalho se perde."*

O push para a branch de carga custa segundos; o build custa dezenas de minutos. Medir antes de
empurrar aposta o trabalho inteiro numa sessao que pode cair no meio do build — e ja caiu. A
sequencia e:

    escreve -> COMMITA E EMPURRA na branch de carga (dizendo no commit que ainda nao mediu)
            -> mede -> se reprovar, conserta, EMPURRA DE NOVO, e so entao remede

Vale para trabalho PARCIAL tambem: meio degrau salvo na branch vale mais que um degrau inteiro
perdido. E vale para quem so VERIFICA, numa forma adaptada — grava o veredito de cada item em
disco conforme apura, e faz as checagens ESTATICAS (que nao exigem build) primeiro, porque sao as
que sempre cabem.

As duas leis nao brigam: medir continua OBRIGATORIO e carga nao medida nao drena. O que muda e so
a ordem, e a ordem e o que separa "carga salva mas sem veredito" de "carga inexistente".

## A DIVIDA DE CAST-WIDTH NAO E PAGAVEL: SEED E GEN1 EXIGEM GRAFIAS OPOSTAS (2026-07-27)

Medido, com A/B direto, depois de 271k tokens gastos para provar que o caminho NAO existe — e
esse resultado negativo vale mais que a maioria dos positivos do dia.

O seed 0.3.0.30 recusa aritmetica de largura mista com `B.22 (no promotion)`, e por isso nao
constroi o tip: e o que engata a escada e custa 392s de 780s por job. A correcao obvia e por o
cast explicito. Ela funciona — o checker do seed passou a atravessar a arvore INTEIRA pela
primeira vez (5929/5929, contra parar no item 784).

**E o gen1 rejeita exatamente os mesmos casts**, um a um:

    checker 7928/7928 ✗ encode_x86_64.tks:451: redundant cast: this `to i64` is a provable
                        no-op (cast-width-hygiene D1) — delete it

Porque o gen1 tem a W-RULE (`widen_int_binop`, typer.tks), que auto-alarga a largura mista que o
seed recusa. Com ela, o cast vira no-op PROVAVEL, e o sweep D1 — que veio no MESMO vagao que a
W-RULE — existe precisamente para recusar isso.

**NAO HA GRAFIA QUE SATISFACA OS DOIS.** O seed exige o cast; o gen1 o proibe. Nao e questao de
achar a forma certa: as duas geracoes tem regras contraditorias sobre a mesma linha. O proprio
`build_with_seed_fallback.sh` ja dizia isso e ninguem tinha ligado os pontos: *"the cast-width
wagon ADDED the W-RULE and DELETED the now-redundant manual casts, so its own head requires a
W-RULE-capable compiler while an older generation dies on it with B.22"*.

**E POR ISSO QUE A ESCADA EXISTE.** Nao e desleixo nem pino velho: e a unica ponte entre duas
geracoes cujas regras se excluem. E e por isso que a saida do dono e a UNICA limpa — versionar o
`teko.c` do primeiro degrau. Esse C carrega a W-RULE COMPILADA dentro dele, entao o compilador que
sai dali aceita o corpus na grafia D1 (sem casts), e a contradicao deixa de existir em vez de ser
contornada.

**A LEI GERAL, que vale alem deste caso:** quando o seed e o tip discordam sobre a FORMA de uma
linha (nao sobre uma capacidade que falta), nao existe patch no corpus que sirva aos dois. A ponte
tem de ser um COMPILADOR — binario ou C versionado — nunca uma reescrita do fonte para agradar o
passado. Reescrever o corpus para caber num seed que sera descartado e a cauda balancando o
cachorro, e este registro existe para a proxima pessoa nao gastar os mesmos 271k tokens
redescobrindo.

### ADENDO (mesmo dia, horas depois): O DONO DISSOLVEU A CONTRADICAO REBAIXANDO O D1 A WARNING

O registro acima continua correto no que MEDIU — e errado no que concluiu ser inevitavel. A
contradicao era real, mas ela nao vinha das duas geracoes: vinha do D1 ser um ERRO. Ruling do
dono, 2026-07-27: *"Mas, o cast, nestes casos, nao deveria ser falha, deveria ser warning, o que
nao nos exime de nao ter warnings no proprio compilador (lembra? <= 2%)."*

Com o D1 como WARNING, o mesmo texto compila nas duas geracoes: o seed le o cast e fica satisfeito
com B.22; o gen1 le o cast, imprime `teko: warning: redundant cast: ...` no stderr, e segue. Os
18 casts que o seed exige passam a ser pagaveis. A grafia que nao existia passa a existir.

**A LICAO, e ela e sobre projeto de diagnostico, nao sobre cast:** um diagnostico que e ERRO
define o que e ESPELHAVEL. Numa cadeia de bootstrap ha sempre duas ou mais geracoes lendo o mesmo
fonte com regras diferentes, e cada regra promovida a erro estreita a intersecao das grafias
aceitas por todas elas. Quando essa intersecao fica vazia, nao ha patch no corpus — foi o que o
registro acima mediu. A saida barata nem sempre e uma ponte de compilador: e checar se a regra
precisava mesmo ser erro. **Higiene de estilo vira WARNING; so vira ERRO o que produz programa
errado.** Um cast redundante nao produz programa errado — produz programa feio, e feiura tem
outro instrumento.

**O INSTRUMENTO QUE FICA COM A POLITICA e o D4: ARITH-CAST-RATE <= 2%** (`metrics.tks`,
`run_arith_cast_gate` em `project.tks`), que continua REPROVANDO o build. O dono citou o `<= 2%`
na mesma frase da reversao exatamente para isso: rebaixar o D1 nao afrouxa a politica, muda quem
a carrega — de "nenhum cast redundante e espelhavel" para "no maximo 2% das expressoes aritmeticas
carregam conversao". Consequencia direta na implementacao: o sitio de mesmo-tipo em `type_cast`
PRESERVA o no `TCast` na arvore tipada em vez de dobra-lo, porque `expr_has_conversion` conta nos
`TCast` — dobrar teria desligado o teto junto com o erro, silenciosamente.

**E a ESCADA, ela ainda morre?** Sim, e pela mesma porta: versionar o `teko.c` do primeiro degrau.
Este adendo nao substitui aquela saida, remove o motivo pelo qual ela era a UNICA. Sao coisas
independentes — uma resolve a grafia, a outra resolve o tempo.

## RETORNO DE STRUCT/CLASS POR VALOR DEVOLVE PONTEIRO PENDURADO (2026-07-27) — CORRUPCAO SILENCIOSA

Achado por um agente na bissecao do `bulk`, e REPRODUZIDO DE FORMA INDEPENDENTE aqui antes de ser
escalado — porque um achado desta gravidade nao se repassa no boca a boca.

**Repro minimo** (projeto de 3 linhas, backend nativo, compilador do vagao 20):

    let a = gad::Counter::make(1)
    let b = gad::Counter::make(2)
    exit(a.get())          // esperado 1 — MEDIDO: 2

Nao ha crash, nao ha aviso, nao ha panico. O valor simplesmente e outro.

**A causa, no assembly emitido** (`objdump`, x86_64):

    teko_sretprobe__gad__Counter__make:
        push %rbp; mov %rsp,%rbp
        sub  $0x10,%rsp        <- aloca o RETORNO no PROPRIO frame
        lea  (%rsp),%rcx       <- toma o endereco dele
        mov  %rax,(%rsi)       <- escreve o campo
        mov  %rcx,%rax         <- RETORNA esse endereco
        leave                  <- e destroi o frame que o contem
        ret

O chamador recebe um ponteiro para memoria morta. Duas chamadas seguidas reusam o MESMO offset de
pilha, entao a segunda sobrescreve o resultado da primeira antes que ele seja lido. Confirmado no
call site: dois `call` para o mesmo endereco, `%rax` guardado em `%rbx`, e `%rbx` ja aponta para o
que a segunda chamada escreveu.

**NAO E UM DEGRAU FALTANDO, E UMA ABI ERRADA.** Nao existe convencao `sret`
(caller-allocated return storage) em lugar nenhum de `src/lir/lower.tks` — o retorno agregado
simplesmente nunca foi projetado. Isso e diferente de todo honest-stop `N1/N2` que fechamos hoje:
um honest-stop RECUSA compilar e diz o endereco; este COMPILA e mente. A distincao importa na hora
de priorizar: um degrau custa uma feature, este custa confianca em todo programa ja compilado que
retorne struct por valor.

**Alcance:** transversal aos dois backends nativos (x86_64 e arm64 compartilham o front-end de
lowering). NAO afeta o caminho pelo backend C — la quem gerencia retorno de agregado e o compilador
C, que faz certo. E por isso que a escada nunca tropecou nisso e por isso que o `teko.c` versionado
CONTORNA o defeito em vez de esperar por ele.

**Consequencia pratica registrada:** enquanto isso nao fechar, nenhum binario produzido pelo backend
proprio que retorne struct/class por valor e confiavel, e o `bulk` nao fecha verde — nem depois de
resolvido o `fat-pointer receiver call` (N2), que e um problema SEPARADO no mesmo arquivo.

## Quatro decisões do dono — 2026-07-29 (comparações, `char`, ordem da lane)

Respondidas em bloco, na sequência das duas auditorias (`cargo/0.3.1-superficie-obvia` e
`cargo/0.3.1-comparacoes`) e do dimensionamento do fluxo (`cargo/0.3.1-estreitamento-fluxo`).

1. **`[]T ==` compara POR VALOR.** Referência espelhada: **Go**. Razão nomeada, como a lei exige:
   Go compara slice por valor sem cerimónia; a alternativa Rust exige `PartialEq` derivado, e o Teko
   não tem traits derivados — espelhar Rust exigiria maquinaria que não existe.
2. **`.len` de um `char` devolve BYTES** (`c'🐝'.len == 4`), enquanto `.len` de uma `str` devolve
   CARACTERES (`"café🐝".len == 5`). Ver `text-bytes-escape-hatch-0.3.1.0.md` para o argumento do
   dono e o invariante de reconciliação que daí sai.
3. **D2 fecha NESTA lane** (0.3.1.0 Linux-native), não em vagão próprio. É solidez do backend que
   esta lane constrói. D1 + M.4 + estreitamento vão a vagão separado.
4. **A quebra de D2 é ACEITE**, com nota de release. Corrigir a solidez faz o checker passar a
   rejeitar programas que hoje compilam (quem declare o seu próprio `exit`/`panic`). Mesmo espírito
   do "bora fazer barulho" já cravado para a migração do `.len`.

### Porque D2 é urgente e não uma curiosidade

`texpr_diverges` (`typer.tks:3281`) e a sua cópia `cg_expr_diverges` (`codegen.tks:3986`) reconhecem
divergência por **comparação de string com o nome nu** — `segments.len == 1 && (name == "panic" ||
name == "exit")` — sem nunca consultar o resolvedor. Um `pub fn exit(code: i32) -> str` do
utilizador ganha a resolução mas continua a ser tratado como divergente, e o checker salta a
verificação de tipo de retorno nesse ramo. **Na rota C o `cc` apanha por acidente. O backend nativo
não tem `cc`** — e esta lane é precisamente a que remove essa rede do Linux. Verificado por leitura
directa, não aceite do relatório.

## A regra de entrada na `main` — precisada pelo dono (2026-07-29)

Literal: *"Eu bloqueei force-push até para mim e tudo que vai pra main tem que passar pela
`remodel/**` com pr e bump."*

**Nada entra na `main` senão por um `remodel/**` com PR e bump de versão.** Sem atalho, sem empurrão
directo, sem reescrita de história.

Isto PRECISA duas leituras anteriores que estavam menos completas:

- O force-push não está recusado apenas ao token do agente — está bloqueado **para toda a gente,
  incluindo o dono**, por protecção que ele pôs sobre si próprio. Medições anteriores registaram a
  recusa do lado do agente e deixaram em aberto se era limitação de token ou regra de repositório.
  É regra de repositório, e é universal.
- A linearidade `main` ↔ org e `remodel/**` ↔ `main` não é um hábito: é o ÚNICO caminho que existe.

Consequência já apurada num caso concreto: os 13.4 MB de blobs `.fixpoint/` no histórico são
**permanentes** enquanto a protecção existir, porque nenhum squash remove um blob já ancestral e
nenhuma reescrita é possível. Ver `expurgo-fixpoint-historico.md`.

## A fábrica de `error` — desenho do dono, 2026-07-29

O dono reabriu o desenho do `error` a partir da paragem do degrau 22 (`unknown field \`line\` on
struct \`error\``): *"talvez tenhamos pintado ele errado e devesse ser algo mais como acontece em Go,
onde para gerar um error, chamasse uma fábrica e não uma construção de struct literal."*

| grau de liberdade | decisão |
|---|---|
| API | `error::new(msg: str)`, `error::new_pos(msg: str, line: u64, pos: u64, file: str)`, `error::join(left: error, right: error)` |
| literal `error { message = ... }` | **FICA** — *"não precisa restringir o dev, mas o ideal (convenção) seria a fábrica"* |
| `join` | **achata** — concatenação textual, sem cadeia, sem `unwrap` |
| referência espelhada | **Go**, no eixo dos comportamentos (`errors.New`) |
| conversão dos 2148 sítios existentes | **opcional**, arrumação gradual |

### Porque isto ENCOLHE o degrau 22 em vez de o aumentar

Hoje o backend nativo tem de conhecer o layout do struct `error` para lidar com `e.line`. Com
fábrica, passa a ter de baixar **chamadas** — e baixar chamada de builtin é o molde já repetido dos
degraus 9–17 e 21. O layout deixa de ser superfície e passa a detalhe de implementação.

### Uma afirmação minha que a medição desmentiu

Eu disse ao dono que manter o literal custaria "metade do benefício", por pinar o layout. **Falso.**
`src/checker/typer.tks:2513` já restringe o literal a exactamente um campo chamado `message`:

```teko
if sl.field_names.len != 1 || sl.field_names[0] != "message" {
```

`line`/`col`/`file`/`expected`/`actual` nunca foram construíveis por literal — só legíveis
(`typer.tks:2117` é acesso, não construção). Manter o literal pina apenas o `message`, que já estava
pinado. **Todo o resto do layout continua livre por trás da fábrica**, e a decisão do dono não custa
nada.

### A cadeia adiada, e porque é de graça

`join` achatado hoje não fecha a porta ao `errors.Join` verdadeiro do Go (com `Unwrap() []error`).
Precisamente porque a fábrica esconde o layout, crescê-lo depois não toca em código de utilizador.
Adiado, não descartado — e adiado sem juros.

## Lei — cross-compiling SIM, mingw NUNCA (dono, 2026-07-29)

Palavras dele, sobre a descoberta de que o CI instalava mingw de propósito:

> queremos sim Cross compiling quando todos os natives estiverem funcionando, mas não quero saber de
> mingw.

Duas metades, e a segunda não espera pela primeira.

### A proibição já existia e cobria só metade do compilador

`src/build/linker.tks` implementa-a bem — `linker_is_mingw`, `mingw_rejected_error`,
`resolve_linker` que nunca devolve `"cc"`. Mas medi: `linker_is_mingw|resolve_linker` **não aparece
em nenhum ficheiro fora de `linker.tks`**. Ela guarda a rota NATIVA e mais nada.

A rota C nunca foi guardada, e por isso passou por mingw em **dois** sítios, ambos medidos em log de
CI, não inferidos:

1. **O runner Windows.** `resolve_cc` devolve `"cc"` por padrão em todo host, e o passo de
   diagnóstico imprimiu `cc /c/mingw64/bin/cc`. Com o Windows em `fixpoint_backend=c`, o binário que
   publicamos e o corpus que ele compila saíam por mingw — e o clang da imagem nem era usado.
2. **A perna Linux `regressor-full`.** `.github/workflows/pr.yml` instalava
   `gcc-mingw-w64-x86-64` **deliberadamente**, para servir a linha
   `own_cross_x86_64_windows_emits_coff`.

### O segundo sítio não precisava de mingw para nada

A linha que supostamente exigia o cross-linker é:

```
Scenario: own_cross_x86_64_windows_emits_coff (0.3.1 C5 — the object is the claim)
  Given target = "x86_64-windows"
  When built
  Then object well-formed
```

Sem `and run`, sem `Then exit`. **A afirmação é o objeto**, e o próprio nome do cenário o diz. O
`scripts/check_coff.sh` é cross-format por desenho. Provar que um `.o` é COFF bem formado não pede
linker nenhum — o mingw estava lá porque o `When built` linkava, não porque a linha o conferisse.

A lição, que é a desta lane inteira: **uma capacidade instalada para satisfazer um passo é sinal de
que o passo afirma mais do que verifica.** Antes de instalar a ferramenta, leia a afirmação.

### Porque clang-com-alvo-MSVC e não `cl.exe`

O emissor de C usa 49 statement-expressions `({ ... })` mais `__builtin_`/`__attribute__`. O C23
padronizou `typeof` mas **não** as statement-expressions, e o `cl.exe` rejeita-as. O clang aceita-as
mesmo com `--target=x86_64-pc-windows-msvc`, porque quem compila é o clang e a MSVC entra só como
headers/libs/linker. Trocar para `cl.exe` obrigaria a reescrever os 49 sítios; o dono decidiu não
pagar isso agora.

### O que fica para depois, e com que instrumento

Cross-link completo (Linux → binário Windows) sem mingw exige o **linker próprio** que o dono
planeia para uma versão adiante. Até lá, o cross honesto é: emitir o objeto, validar o formato,
parar. Adiado por ordem explícita — *"quando todos os natives estiverem funcionando"* — não por
incapacidade.

## Lei — wasm prova-se com um app real, e WASI/Browser ganham versão própria (dono, 2026-07-29)

Palavras dele:

> Acredito que as falhas do wasm ocorrem pq o compilador está tentando compilar a si mesmo em wasm,
> o que não deveria acontecer, e por isso também WASI e Browser ganham cada um uma versão dedicada,
> compilando um app real.

### A premissa, corrigida por medição

O compilador **não** se compila em wasm, e nada tenta. Não existe perna produtora wasm:
`scripts/ci_producer_matrix.sh` declara `linux-*`, `macos-arm64` e `windows-x86_64`, e o
*"wasm regressor"* que ele cita em `:146` é **consumidor** — baixa o asset `linux-x86_64-glibc` e usa
esse compilador x86-64 para construir um programa wasm. Fica registado para ninguém re-derivar.

E não poderia ser de outro modo com proveito: **o WASI não tem `fork`/`exec` e o browser não tem
sistema de ficheiros.** Um compilador que lança processos e percorre árvores de ficheiros é o pior
candidato possível a binário wasm por esta rota.

### A conclusão, que fica de pé — e com razão mais forte

O programa que a linha wasm compila é `cases/wasm_exit7.tks`, e é uma linha:

```teko
exit(7)
```

Sem `alloca`, sem rodata, sem assinatura com `Ptr`. E esses três são **exatamente** o subconjunto que
o `C1-wasm64-scope` de `src/backend/stackify.tks` ainda recusa. Logo a fixture está calibrada para
passar por evitar tudo o que é difícil: **ela ficaria verde mesmo que o wasm estivesse quebrado para
qualquer programa real.**

É a quinta vez nesta lane que o mesmo padrão morde — *fixture que verifica que compila, não que
funciona*. A regra que já valia para valores vale igual para alvos: **uma fixture calibrada para o
subconjunto suportado não mede o alvo, mede a calibração.**

### O que isto muda no pin

O KNOWN-STOP do wasm (`scripts/wasm_known_stop_gate.sh`) continua válido, mas a sua migalha de
promoção passou a começar noutro sítio. Não é "dar motor a uma perna both-tier" — é **trocar a
fixture por um app real que exercite alloca, rodata e assinaturas `Ptr`**, e esperar vermelho no
início. Esse vermelho É o item de trabalho da versão do wasm, e casa com o seguimento já nomeado:
fiar `ptr64` pelo frame e pela rodata. Como WASI e Browser são só 64 bits por ordem do dono, o app
cai em wasm64 — onde a paragem vive. As duas restrições encontram-se na mesma migalha.

## Leis — sem `void`, sem sobrecarga, e o `main` híbrido (dono, 2026-07-30)

### Duas leis em que EU escorreguei, registadas para não se repetir

Propus ao dono `fn main() -> void` e `fn main() -> i32`, *"exatamente como o `Main` do C#"*. Ele
cortou:

> Não temos `void` eu os bani, do mesmo modo, não temos sobrecarga de função/metodo, também os bani

Ambas estão no código como lei, e eu podia tê-las medido antes de propor:

- **`void` não é valor.** `src/checker/resolve.tks:1741` — *"void is not a value, M.3"*; `Ref<void>`
  é rejeitado; `src/checker/typer.tks:1661` tipa argumentos *"(void rejected)"*. A grafia de função
  sem valor de retorno é simplesmente **sem seta**: `fn ensure_rt_dir_abs() {`.
- **Sem sobrecarga.** `src/checker/collect.tks:1291` — *"W10b's no-overloading/override-only
  ruling"*. O compilador trabalha em volta disso e **documenta** que trabalha: é a razão declarada
  do `<src>_to_<dst>` em `src/casting/casting.tks:16` e de dois nomes distintos em
  `src/lir/lower.tks:10565`.

**A lição de método**, não de conteúdo: quando eu invoco uma das quatro referências de desenho, tenho
de verificar que a *nossa* superfície suporta o que a referência oferece. O C# aceita `void` ou `int`
no `Main` **porque tem sobrecarga**; nós temos nenhum dos dois. O espelho do C# aqui vale para a
REGRA DE ENTRADA HÍBRIDA, não para o conjunto de assinaturas.

### O `main` híbrido — as três decisões, fechadas

> Creio que poderia ser híbrido, como no C#. Se existe funcao main, então usa ela e o main.tks é
> livre, se não, o arquivo é como é hoje. Mas, a funcao main teria que residir dentro do main.tks
> somente e haver somente um de cada (somente uma funcao e somente um arquivo main).

1. **Assinatura: `fn main() -> i32`, e só.** Dono: *"Sai com i32 (sem saída não será aceito)"* — um
   `fn main()` sem seta é **erro honesto**, nunca exit 0 implícito. Sem `void` para oferecer e sem
   sobrecarga para permitir duas formas, há exatamente uma.
2. **Misturar `fn main` com instruções no topo: REJEITAR.** Ambiguidade de ponto de entrada, e o C#
   também rejeita.
3. **Sem parâmetros** — e aqui a proibição de sobrecarga **responde sozinha**: o C# pode oferecer
   `Main()` e `Main(string[] args)` justamente porque tem sobrecarga. Nós não podemos, logo há uma
   forma, e os argumentos vêm de `args()`.

### `args()` bare — o precedente é o `println`, não um conceito novo

> Sem parâmetros, mas, `teko::env::args()` poderia ser global para reduzir o tamanho da escrita
> `args()`

A lista de builtins chamáveis sem qualificador é **fechada** e vive em `src/checker/scope.tks:525`:
`print`, `println`, `write`, `ewrite`, `eprint`, `eprintln`, mais `panic` e `exit`
(`src/checker/typer.tks:1613`/`:1621`). O `println` já existe nas DUAS grafias — bare e
`teko::io::println` — logo `args()` bare ao lado de `teko::env::args()` segue precedente e não abre
conceito.

**Duas armadilhas, ambas com regressão a apontá-las:**

- **Sequestro.** `src/checker/resolve.tks:782`: builtins R2 são casados **primeiro**, *"bare only — a
  builtin is never namespaced"*. A regressão `builtin_name_not_hijacked` existe porque uma função de
  utilizador com nome de builtin tem de correr o próprio corpo. Pôr `args()` na lista bare exige a
  mesma proteção, e essa regressão tem de crescer para cobrir `args()`.
- **A lei de 2026-07-29 do próprio dono** — *"a builtin call is resolved by NAME and QUALIFIER, not
  name alone"*, com regressões que rejeitam builtin por namespace inventado e por alias de `use`. A
  grafia bare nova não pode abrir buraco nisso.

**É UMA CHAMADA, NÃO UM IDENTIFICADOR GLOBAL** (dono, 2026-07-30, corrigindo a minha grafia: *"Eu
escrevi `args`? Desculpe, quis dizer `args()`"*). A distinção não é cosmética: `args()` entra com a
MESMA forma dos oito builtins bare que já existem, todos invocados com parênteses, e **não** abre a
categoria "variável global" — que a linguagem não tem, e que traria consigo perguntas que uma chamada
não traz (quando é avaliada, se é constante, se pode ser sombreada por um `let`).

**E uma primeira vez:** `args()` seria o primeiro builtin bare que **devolve dados**. Os oito atuais
são todos I/O ou controlo de fluxo, nenhum produz valor. Não é impedimento, é onde esperar a
surpresa.

### O que a medição já garante que é barato

A guarda que proíbe declarações no `main.tks` é **uma linha**, em
`src/parser/parse_file.tks:149`, e é puramente sintática. E o ponto de entrada **já** se chama
`main`: `src/build/project.tks:2255` identifica o main virtual *"by its exact, un-namespaced `main`
symbol"*, `src/backend/stackify.tks:4739` procura `funcs[i].symbol == "main"`, e
`src/checker/initanalysis.tks:281` já isenta `main` da análise de inicialização. O main virtual já é
baixado como uma função literalmente chamada `main` — o híbrido deixa o utilizador **escrever** a
função que hoje é sintetizada, em vez de acrescentar um conceito.

**O risco real está noutro sítio:** `teko test .` e o harness de regressão SINTETIZAM mains
(`src/build/regr_group.tks` dobra snippets num despachante; `project.tks` chega a descartar o
`LFunc` do main virtual). A regra "só um `main`, e só no `main.tks`" tem de valer sem quebrar os
mains sintetizados dos testes. É ali que o defeito vai aparecer.

### O sítio exato da colisão, medido — e é um descarte, não um conflito

Dono, 2026-07-30: *"o compilador precisa entender que se trata da main do próprio programa ao
encontrar um `main()` em um teste e fazer o mangle"*.

Medido: **o mecanismo de modos já existe.** `src/codegen/codegen.tks:11495` —

> prototypes and function bodies are emitted IDENTICALLY across modes; only the trailing `main()`
> differs: Program = the virtual-main (loose statements), TestPlain/TestCov = the native test gate.

Logo **não há colisão hoje**: em modo de teste o main virtual não é renomeado, ele **desaparece**, e o
portão de teste ocupa o símbolo. É por isso que `teko test .` funciona — e é exatamente por isso que a
main do programa **não é assertável**.

A instrução do dono converte um **DESCARTE** num **RENAME**, num sítio só: o switch de modo que
escolhe o `main()` final. O `strip_virtual_main` (`src/build/project.tks:2243`) NÃO é esse sítio — ele
tem um único chamador e é o caminho de biblioteca estática. E o harness de regressão também não é: ele
trabalha ao nível do FONTE, `regr_group_main_text` **gera um `main.tks`** por grupo dobrado.

**A invariante que cobre os dois casos sem caso especial:**

1. O símbolo bare `main` pertence a **quem é a entrada do artefacto que se está a construir** — modo
   Program: o programa; modo Teste: o portão de teste.
2. A entrada do programa é **sempre também** emitida sob `<project>__main`, em todo modo, logo é
   chamável e assertável.

O híbrido cai de graça nessa invariante: um `fn main() -> i32` escrito pelo utilizador **já** é
manglado para `<project>__main` pelo mangler ordinário, logo em modo de teste já é emitido e chamável.
O único que precisa de tratamento novo é o main **virtual** (instruções soltas).

### Dois factos do espécime real que mudam o desenho

O `main.tks` do próprio compilador é o único espécime grande, e vive na **raiz do projeto**, ao lado de
`src/` (com `source = "src"`), não dentro. Ele documenta a própria forma:

> SHAPE (§2.20 — entry point). A LINEAR virtual main: top-level statements + local var/const only
> (no fn/type declarations — those live in modules). Output contract: natural end → exit 0;
> `teko::exit(n)` → exit n; panic → stderr + exit ≠0. **NO value return.**

1. **`fn main() -> i32` é contrato NOVO, não renomeação.** Hoje o código de saída sai por
   `teko::exit(n)` DENTRO do corpo, e o main virtual explicitamente não devolve valor. As duas formas
   vão coexistir — o que torna "um `fn main` sem seta é erro honesto" ainda mais necessário: senão
   haveria um main que nem devolve nem tem contrato de saída.
2. **O caso motivador do `args()` está na PRIMEIRA linha do compilador**: `let args = teko::env::args()`.

E há uma disciplina a respeitar: o ficheiro declara-se par semântico do `main.c`
(*"main.tks and main.c must therefore stay SEMANTICALLY EQUIVALENT"*), logo mexer na convenção de
entrada toca a regra do par.

## Buraco de portão — 16 ficheiros commitados sem `fmt`, e nada no CI verifica (medido 2026-07-30)

Levantado por um agente como red-flag adjacente ao trabalho de relocações: `teko fmt --check`
devolvia rc=1 em `src/backend/objfile_elf.tks` **sem alteração alguma**, já no vagão.

### A medição, e uma hipótese minha refutada

`teko fmt --check src/` → **rc=1, 16 ficheiros**. (Cuidado com o método: `rc=$?` depois de um
`| head` dá o estado do `head`, não do comando — errei isso na primeira tentativa e li rc=0.)

O que o `fmt` quer em `objfile_elf.tks` é re-indentar sete campos de um literal de struct aninhado
de 8 para 12 espaços, **deixando o fecho `})` a 4**. Eu argumentei que corpo a 12 com fecho a 4 é
inconsistência interna, logo bug do formatador.

**Falso, e o teste que decide não depende de opinião: um formatador tem de ser PONTO FIXO.**
Depois de um `fmt`, o `--check` devolve rc=0; a segunda passagem não muda nada. Ele **converge numa
passagem**. Logo a forma estranha é a convenção tal como implementada, e os 16 ficheiros foram
commitados sem `fmt`. Terceira hipótese minha refutada por medição neste dia.

### NÃO É BURACO DE PORTÃO — e o dono cortou a minha conclusão

Eu concluí que "nenhum passo do `pr.yml` corre `fmt --check`" era um buraco, pela régua do tronco.
**Errado.** O dono, 2026-07-30:

> fmt check é dev mode e ele não está assim tão bom. E pq dev mode, para ter convenção e não
> imposição. Mais vale um `fmt --apply` que um `fmt --check`

O formatador é **ferramenta de convenção, não regra de CI**. Não existe portão de `fmt` e não deve
existir: um ficheiro fora de formato não é erro escondido, é estilo não convergido — e a régua do
tronco fala de erros que não disparam, não de convenções não aplicadas. **Nunca armar `fmt --check`
no CI.** Os 16 ficheiros são dívida de conveniência, opcional, sem portão a impô-la.

A lição de método, que é a minha e não do formatador: **invocar uma lei do projeto não dispensa
verificar que ela se aplica.** Estiquei "sem erros escondidos" até cobrir estilo, o que ela não
cobre. É a mesma forma do erro de hoje com o C# — invocar a referência sem medir se ela vale aqui.

### A superfície do `fmt`, medida, e a armadilha nela

```
usage: teko fmt [--check] <path>...
flags:  --check    report unformatted files without rewriting them
```

Uma flag só, e **aplicar já é o padrão** — `teko fmt <path>` reescreve no lugar. Logo o modo que o
dono valoriza já existe; o `--check` é que é o opt-in.

O que dá força ao `--apply` que ele sugeriu não é ter mais um nome: é que hoje **`teko fmt src/`
reescreve 16 ficheiros sem flag e sem confirmação**. Para ferramenta em dev mode, o caminho
destrutivo ser o mais curto é armadilha; um `--apply` explícito torna a reescrita deliberada. Fica
como sugestão registada, não como trabalho despachado — a decisão é do dono.

### Se algum dia se reformatar os 16, a ordem é esta

Reformatação é churn de ficheiro inteiro, e no dia da medição **cinco dos 16 estavam sob edição
ativa** por cinco agentes (`typer.tks`, `project.tks`, `lower_const.tks`, `codegen_test.tkt`,
`linker.tks`). Fazê-la com frentes abertas compra conflito sem ganho. No ponto calmo: drenar os
agentes, `teko fmt` sobre os 16, e **confirmar que o ponto de fixo continua a fechar** — reformatar
não deve mudar bytes emitidos, mas isso é afirmação a verificar, não a assumir. **Sem portão no
fim.**

## LEI — UM AGENTE QUE MEXE EM LOWERING TEM DE TENTAR A EMISSÃO NATIVA ANTES DE DIZER VERDE (dono, 2026-07-30)

**A pergunta do dono, e ela é uma acusação justa:** *"Que testes que os agentes estão fazendo e
reportando verde à ti sendo que quebra em seguida? Quem tratou de floats não fez o serviço certo."*

E a ordem que se segue dela, verbatim: *"Os agentes precisam melhorar os testes, rodar a suíte de
artifact como ocorre no CI, mesmo que só tenham Linux x64 glibc, se tivessem tentado emitir um teko
native a partir de um gen1, teriam pego o erro sem precisar de nova rodada."*

**O CASO CONCRETO, medido.** O agente do degrau 24 fechou `f64_bits`/`f64_from_bits` e reportou verde.
O passo seguinte do self-host morreu em:

```
teko: .: native backend N1: builtin `ftoa` not yet lowered (N2) [in `teko::codegen::cb_f64_literal`]
```

Outro builtin de float, na mesma vizinhança do que ele acabara de baixar, **num sítio que uma única
tentativa de emissão nativa teria exposto**. Custou uma rodada inteira de CI — seis pernas — para
descobrir o degrau seguinte que estava a um comando de distância.

**A LEI.** `teko test .` verde **não é prova suficiente** para uma mudança de lowering, isel, encode ou
runtime. Antes de reportar verde, o agente TEM de tentar a emissão nativa do próprio compilador a
partir do gen1:

```
TEKO_BACKEND=native <gen1> . -o /tmp/g2-nativo --no-verify --release
```

E tem de reportar, **textualmente**, a paragem que apareceu. O contrato é de três partes:

1. uma paragem `native backend N1: ... not yet lowered` **é esperada hoje** — o self-host não fecha;
2. o agente garante que **não é a dele**;
3. o agente garante que **não é NOVA** — que a mudança não introduziu nem desbloqueou outra.

**A VERSÃO COMPLETA, quando houver tempo**, é o que o job `artifact` faz, e corre num Linux x86-64
glibc qualquer:

```
sh scripts/produce_assets.sh linux linux-x86_64-glibc linux-x86_64-glibc
TEKO_FIXPOINT_BACKEND=native sh scripts/fixpoint_gate.sh out/teko . .fixpoint
```

**PROPORCIONALIDADE, e é o integrador que a aplica no despacho.** A exigência escala com o que a
mudança toca:

| a mudança toca | o que se exige antes de "verde" |
| --- | --- |
| lowering / isel / encode / objfile / runtime | a emissão nativa acima, **obrigatória**, com a paragem citada |
| fixtures, `.tkr`, corpus | idem — o degrau 28 nasceu de uma fixture drenada que nenhuma perna nativa compilou |
| só ficheiros de teste `.tkt` | contagem exacta antes/depois + diff dos nomes; não é preciso a emissão nativa |
| só docs, ou uma flag de CLI com guarda | nada disto; e **NÃO** mandar o agente correr a suíte completa — foi o que prendeu o agente do `fmt` numa tarefa de 5 minutos |

**A ÚLTIMA LINHA DA TABELA É TÃO IMPORTANTE COMO A PRIMEIRA.** No mesmo dia em que esta lei nasceu, o
agente do `fmt --apply` — um conserto de guarda que o dono orçou em *"menos de 5 min"* — ficou preso a
correr a suíte completa e o ponto de fixo. O dono apanhou: *"O agente de fmt já está há muito tempo em
execução em uma tarefa que não deveria passar de 5 min."* Exigir a prova pesada onde ela não se aplica
**também** é defeito de despacho, e colide com a regra de que esperar não é estado permitido.

**PORQUE ISTO É LEI E NÃO CONSELHO.** A escada de degraus é encontrada **um degrau por rodada de CI**
se ninguém tentar localmente. Cada degrau assim custa seis pernas de runner e uma volta ao integrador.
A emissão nativa local custa um comando e encontra o degrau seguinte **antes** de gastar a rodada.

### CORREÇÃO À LEI ACIMA, no mesmo dia, medida por um agente que a cumpriu

Um agente cumpriu a lei e devolveu o facto que ela não previa: *"o pedido do dono de 'emitir um teko
native a partir de um gen1' **não é satisfazível** nesta árvore até o degrau 27 cair — e isso é um
facto medido, não uma desculpa."*

**Ele está certo, e a lei precisa de ser lida com precisão.** O que se exige **não** é uma gen2 nativa
com sucesso — isso é impossível hoje, e continuará impossível até o `ftoa` cair. O que se exige é:

1. **a TENTATIVA**, e
2. **a CITAÇÃO TEXTUAL da paragem que apareceu**, com a garantia de que não é a do agente e não é nova.

**E a limitação honesta, que eu não disse quando escrevi a lei:** a paragem do `ftoa` acontece
**cedo** — em `teko::codegen::cb_f64_literal`, durante a geração do próprio compilador. **Tudo o que
um agente parta DEPOIS desse ponto é invisível a esta prova.** Portanto a lei apanha menos do que eu
afirmei: apanha regressões que impedem chegar ao `ftoa`, não as que vivem além dele.

**Consequência de prioridade, e é o argumento mais forte a favor do degrau 27:** enquanto o `ftoa` não
cair, esta lei é uma rede de malha larga. Fechá-lo não destranca só o ponto de fixo — **torna a lei
efectiva**.

### O NÚMERO QUE MEDE A OUTRA CEGUEIRA, e é grande

A fase de testes unitários faz **SIGABRT no primeiro `assert` falhado**. Medido em 2026-07-30: com o
abort em `lwt_prim_kind_of_resolves_int_to_enum_cast_narrows`, **269 dos 1117 testes declarados nunca
arrancam** — `lower_test.tkt` (83), `math/checked_test.tkt` (40), `parser_test.tkt` (38),
`regex_test.tkt` (16), `time_test.tkt` (14), `math_test.tkt` (14), `sort/cmp_test.tkt` (11) e mais 12
ficheiros.

**A regra que sai daqui:** um agente que conserta o primeiro falhado tem de **medir e reportar quantos
testes ficam cegos atrás do NOVO abort**. É esse número, e não a sensação de progresso, que diz ao
integrador se vale despachar outro imediatamente.
