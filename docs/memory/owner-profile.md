# owner-profile.md — como o dono delibera (para o agente sair de tensões)

Registro vivo das impressões sobre o **estilo de deliberação do dono** e dos **aprendizados**, para o
agente resolver tensões sobre o que já foi definido como fechado — sem reabrir o que está selado, e sem
pedir de novo o que já foi decidido. Versionado de propósito (pedido do dono).

## Idioma e papel
- Comunicação **sempre em PT-BR**.
- O dono é o **único merger de trunk** do teko-lang (compilador AOT all-native, self-hosting, escrito em
  Teko). O trabalho flui por **agentes**, **um reseed de cada vez**, cada um independentemente gated.

## Como delibera
- **Parte a parte.** Segmenta um tema (§10, item 14, …) e **confirma cada parte** antes de avançar. Não
  atropelar; não gravar sem o martelo dele.
- **Fork + recomendação, não survey.** Apresentar a superfície com opções (tabela) e **uma recomendação
  clara** (a recomendada primeiro). Evitar despejo exaustivo de alternativas.
- **Counter-argue antes de gravar.** Pesquisar o histórico/código, questionar, expor os forks **REAIS**.
  Quando o dono aponta uma inconsistência, ela costuma ser **genuína** (ex.: `RecBody = variant` vs §9.D).
- **Correção honesta, sem teimar.** Quando o agente erra, o dono empurra até acertar (structural traits:
  "cadê os Eq, Ord?"). Reverter a própria recomendação com **base firme** é bem-visto; insistir num erro
  não. Ele lê as letras miúdas e cobra precisão.
- **Grafia atual, não velha.** Não puxar grafia de artefatos antigos (ex.: `let`/`mut` do artifact de
  journaling — já retirados no §1). Sempre validar contra o compilador/decisões atuais.

## Princípios de design que o dono aplica
- **Explícito > mágica (no-shadow).** Se algo tem corpo, o dev vê e escreve. *Structural traits* (síntese
  oculta de `eq`/`hash`/…) foram **aposentadas** por violarem isso.
- **Não criar exceções.** *"Não causar exceções, já temos muitas."* Preferir **colapsar construtos**
  (3→2→1) a manter casos especiais.
- **Minimalismo de construto.** Sobrou **UM** construto de capacidade: a **interface** (tipo, dispatch,
  constraint, e obriga operador por contrato). O **trait-decorador** é mixin de código (achata, não é tipo).
  Nada de terceiro construto.
- **Precisão de mecânica de runtime.** O dono pensa em fat header vs thin transiente, `ref` = alias de
  slot, residência de arena (F1/F2). Registrar a **mecânica**, não só a superfície.

## Sequenciamento
- **Destrava dependência primeiro.** Antecipar itens do caminho crítico (item 14 antes da Intent).
- **Adiar alto-churn pro fim.** `exp`/`pub` (visibilidade) **por último** — muitas falhas a corrigir, e a
  grafia é forward-compatible, então atrasá-la minimiza retrabalho.
- **Design-ahead pro arquiteto.** Passar itens abertos/definidos pelo **arquiteto** e **versionar** o
  retorno, para depois **argumentar e definir** (o dono decide; o agente prepara o material).
- **Nada de N agentes em paralelo com reseed** — gera conflito de reseed. Design-only (arquiteto) pode
  paralelizar; reseed é um de cada vez.

## Segurança (regra-mor, aprendida a duras penas)
- **NUNCA rodar `teko test` em forma alguma** — há um leak no `monomorph` (item 13) que **derruba o
  container inteiro**. Nem guardado com `ulimit`/`timeout` em subagentes. Foi a causa de restarts
  repetidos. Única exceção histórica: um check `ulimit -v ~3G + timeout` **autorizado pelo dono**, só na
  sessão principal, nunca em implementer.
- **Build seco:** `TEKO_BACKEND=c <compilador> <dir> -o <FRESCO> --no-verify --release` (o `--no-verify`
  descarta o parser de `.tkt` de propósito). O default do compilador é **native** e não emite `teko.c` —
  fixar `TEKO_BACKEND=c` para o fixpoint.
- **Reseed:** cc seed `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
  src/assert/assert.c -lm -o gen0`; `TK_RT_DIR` setado; `-o` fresco (cache velho segfaulta self-build);
  fixpoint **byte-idêntico**; **gate independente** após cada reseed.
- **Sweep de `.tkt`/`.tkr` obrigatório** após mudança de AST/campo/assinatura/enum — `--no-verify` não
  compila `.tkt`, então quebras ficam caladas até o CI (§9B/§9C quebraram 32 sítios assim).

## Git / registro
- Onda drena em **`fix/retirement`**, **sem PR**, **forward-only** (force-push desabilitado); fetch+rebase
  antes de push.
- Commits: **English Conventional-Commits**, **sem trailers** (no `Co-Authored-By`, no "Generated with"),
  **sem identificador de modelo** em nada versionado (só no chat).
- Decisões vivem no **Doc 2** (`docs/design/mudancas-superficie-0.3.1.md`, a lei) + conversa; atualizadas
  **deliberadamente**.
- O dono **reverte o checkout local de propósito** às vezes (deixa greps em árvore velha). A verdade é
  **`origin/fix/retirement`** — sempre grepar/re-sincronizar contra ela (`git reset --hard`).

## O que está FECHADO (não reabrir sem informação nova)
- **§9.A–F**, **match-universal fase 1**, **properties** — reseeded.
- **Traits = decorador achatável** (não-tipo; só métodos-com-corpo; sem campos; contrato estrutural
  `_nome`↔`nome`; `&`-composição em class/struct/service; colisão = erro salvo sobrecarga; sem match sobre
  trait). **Structural aposentadas.** **Constraint = interface-only.** Capacidade via **interface que
  obriga operador** + **counter-part** (igualdade por negação, ordem por reflexão).
- **match `as` = alias-ref transparente** (readonly raso: variável não re-vincula, props/métodos internos
  transparentes; mutabilidade herda do subject; sem sintaxe nova).
- **§10 inteiro** (spawn/chan MPSC/await/Intent/journal). **Intent protegida** — struct puro, backing
  privados, `exp get`/`pub set`, `pub static new`, campo do valor = **`value`**; exp/pub forward-compatible.
- **item 14** — tudo `var`; `val` só marca interna (alias de match + literais); parâmetros = `var`; `self`
  = ref implícito (mutação gruda, método mutante inferido); fat header (struct) vs thin transiente (subtipo
  primitivo/enum/flags, readonly `val-ref`); **`readonly` opt-in** (struct inteira estilo C# e campo; só
  definível na construção/default).
- **§9.D — `type X = variant` aposentado** (ruling do dono). NÃO vira wrapper (`struct { case: … }`) nem
  alias (`type X = A | B`): a única forma-soma é a **união `|` estrutural inline, escrita POR EXTENSO** em
  cada campo/param/retorno/var. O agregado nomeado some; **os membros seguem nomeados** (`match … as` e
  coerção membro→união inalterados, byte-idênticos). Recursão fecha pelo **box que a própria união já emite**
  (`{tag;ptr;len}`). **Array-de-união exige parênteses** — `[](A | B)` (`[]` liga mais forte que `|`).
  **Verbosidade é o preço, aceito** (verbatim: *"Vai ser verboso mesmo, e isso não é problema, é o preço."*).

## Aprendizados sobre sair de tensões
- Tratar o **fechado como vinculante**; não re-litigar decisão tomada.
- Tensão entre **artefato antigo e decisão selada** → a **decisão selada ganha** (ex.: RecBody/variant →
  união inline no campo).
- Antecipar-se em **dependência**, mas **não decidir sozinho** bifurcações de superfície com impacto —
  essas são do dono; trazer fork + recomendação.
- Reverter local sem drama; a verdade é `origin`.
- O dono confia no agente para **sair de tensões** sobre o que já foi conversado — este perfil é a rede.

## Aprendizados desta sessão (§9.D + arquitetos)
- **A solução do dono costuma ser mais enxuta que a do arquiteto.** No §9.D o arquiteto trouxe wrapper /
  newtype / classe selada (Soluções A/B/C); o dono cortou tudo com uma **união inline por extenso**
  (*"Tão simples e o arquiteto gastou tokens pra… nada"*). Lição: **não sobre-engenheirar**; quando houver
  uma via que evita nominalidade/abreviação/wrapper, ela costuma ser a preferida — mesmo que verbosa.
- **O valor do arquiteto é a RECON, não a forma.** O aproveitável do §9.D foi o **fato descoberto no
  código** (a `variant` já é descritor fat `{tag;ptr;len}` com payload boxed → a recursão fecha sem virar
  referência), não a recomendação de representação. Despachar arquiteto para **levantar fatos + 3+ opções
  com exemplos**; esperar que o dono escolha uma quarta via mais simples.
- **Verbosidade > nominalidade oculta.** O dono aceita repetir a união em centenas de sites para não ter
  um alias/tipo-soma nominal escondendo mágica. Explícito ganha de conciso quando conciso = ocultação.
- **Artefato de avaliação = código real do codebase.** Quando o dono pede material para avaliar, ele quer
  **onde e como** algo é usado hoje, com snippets reais (não abstrações) e a solução confrontada com cada
  padrão de uso. Levantar do `origin`, por posição de declaração (campo/param/retorno/var).
- **Apontar o furo com honestidade.** O dono valoriza quando o agente **nomeia o furo comum** de todas as
  opções (ex.: auto-recursão exige indireção — é teorema) em vez de vender uma recomendação.
