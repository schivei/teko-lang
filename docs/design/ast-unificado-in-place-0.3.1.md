# AST único enriquecido in-place — avaliação e desenho (0.3.1)

Base de leitura: `origin/fix/retirement` @ `c824e9d7`. Autor: arquiteto (DESIGN-ONLY; não
toca código de produto — este doc é o único artefato). Proposta a versionar, argumentar e
ratificar pelo dono; **os forks do §8 NÃO estão decididos** — são fork+recomendação.

**Leia o VEREDITO primeiro.** Resumo do julgamento do arquiteto: **como alavanca de MEMÓRIA, NÃO
vale unificar** — o ganho é ~0,4–0,6 GB do residual (os 7%), a um custo de refactor comparável ao
expurgo de `push`; a meta ≤1,5 GB já sai do Eixo A sozinho. Há caminhos mais baratos (§V.4). O
desenho completo (§§1–10) fica registrado para o caso de o dono decidir unificar mesmo assim, ou
para reaproveitar peças (o `delete<T>` e o short-circuit dos passes valem independentemente).

Complementa (não substitui) `docs/design/reducao-memoria-arrays-0.3.1.md` (Eixos A/B/C) e
`docs/design/io-streaming-0.3.1.md`. O Eixo C (pipeline por-namespace) é **ortogonal** a este
doc — a composição está no §5.

---

## VEREDITO (resposta direta às 4 perguntas do dono — sem tomar hipótese alguma como dada)

O dono pediu julgamento, não um "como fazer". Aqui está, cético inclusive com a hipótese de
NÃO-fazer, e cético com as próprias premissas do dono (objeto-vs-struct, `delete<T>`,
monomorph-via-List). **As §§1–10 abaixo desenham o COMO caso ele escolha a unificação; este
veredito é o SE/QUANTO/VALE e a alternativa recomendada.**

### V.1 — Faz sentido trocar para o AST-unificado in-place ("objeto")? **NÃO — não agora, não como alavanca de memória.**

Recomendação clara, não "depende". E antes de tudo, uma premissa do dono que **não se sustenta**:
**não existe "trocar para objeto".** O item 14 (fat value-struct) já dá **mutação in-place por
`ref`/`self`** sobre os `struct` que já temos — não é preciso virar `class`/`service` (objeto de
identidade/heap). A pergunta "struct vs objeto" **dissolve**: o que a unificação exigiria é
**ref-disciplina** (percorrer e mutar por `ref`) sobre os structs atuais, nenhum construto novo.
Então a decisão real não é "objeto sim/não" — é "**fundir os tipos de nó e reescrever os passes
para mutação in-place, sim/não**". E a resposta é **não**, pelos números de V.2 e o custo de V.3.

### V.2 — Existe ganho REAL e QUANTIFICADO? **Sim, real, mas MODESTO e no lugar errado (~0,4–0,6 GB do residual, não os 93%).**

O que EU VERIFIQUEI no código (não aceitei de premissa) — e aqui o dono está **mais certo do que
eu supus na primeira volta**: as ~6 espinhas são **reais**. `mono_tstmt`/`mono_block`
(`monomorph.tks:688,740`) **reconstroem TODO nó incondicionalmente, mesmo com substituição vazia**
(função não-genérica) — não há short-circuit; monomorph emite uma espinha nova quase completa.
`expand_comptime` (`comptime_expand.tks:44`) e `inline_consts` idem: uma vez que exista QUALQUER
genérico/comptime/const (e o self-build tem os três), **cada passe reconstrói todos os corpos**. E
cada reconstrução ainda **embute** pedaços da espinha anterior (`target = b.target`,
`params = f.params`) → o padrão subárvore-compartilhada/UAF **se repete em CADA fronteira de
passe**, não só parser→TAST. Logo, no pico (lower), coexistem: parser AST + ~4 versões de TAST +
LIR, **nenhuma liberável**.

**Decomposição honesta do ~1,24 GB** (residual não-push; faixas estruturais, sem profiler — o dono
deve exigir a medição real do `tk_obs` antes de decidir, isto é estimativa):

| Parcela | Faixa | Some com a unificação? |
|---|---|---|
| (a) ~4–5 espinhas de wrapper redundantes (structs de nó) | ~0,4–0,6 GB | **SIM** — colapsam em 1 |
| (b) folhas (strings/nomes/descritores de tipo) — já compartilhadas | ~0,3 GB | **NÃO** — contadas 1× hoje |
| (c) instâncias vivas do monomorph (dado real) | ~0,15–0,25 GB | **NÃO** — dado vivo |
| (d) LIR (forma distinta, 1 cópia) | ~0,2 GB | **NÃO** — não é TAST |

**Ganho da unificação ≈ parcela (a) ≈ 0,4–0,6 GB** — ou seja, derruba o residual de ~1,24 para
~0,7–0,8 GB. Real, mas: (i) é ~35–50% de um resíduo que **já é os 7%**, não os 93% do push;
(ii) a meta ≤1,5 GB **já é entregue pelo Eixo A sozinho** (o próprio `reducao-memoria-arrays`
projeta ~1,2–1,3 GB só com C4/C5). **A unificação NÃO é necessária para a meta.**

### V.3 — Vale o esforço/risco? **NÃO. Custo alto (refactor de front-end inteiro), benefício modesto e não-crítico.**

Custo real: fundir duas famílias de nó (`Expr`+`TExpr`, `Statement`+`TStatement`, `Item`+`TItem`)
atravessando **~1220 usos de `parser::` no checker + ~27 arquivos de match + lir/codegen/emit/build**;
converter **cada passe** de "constrói espinha" para "muta por `ref` in-place"; introduzir
`delete<T>` + drop-glue + prova-de-posse; e manter **fixpoint byte-idêntico** o tempo todo, com
reseed iterativo. É comparável em churn ao expurgo de `push` (4917 sítios) — para ganhar **0,5 GB
de um resíduo que já cabe** sob a aceitação `<6 GB`. **Custo × benefício não fecha** como jogada
de memória. (Como refactor de *manutenibilidade* — uma família de nó em vez de duas — pode ter
mérito próprio, mas o dono perguntou de MEMÓRIA, e nesse eixo é over-engineering.)

### V.4 — Se não vale, como liberar os ~1,24 GB? (comparação + recomendação)

| Caminho | Libera | Custo/Risco | Já planejado? |
|---|---|---|---|
| **1. Terminar o Eixo A (expurgo push)** | leva 6,2→~1,2–1,3 GB (os 93%) | em curso | **sim, em curso** |
| **2. Short-circuit de compartilhamento nos passes (SURGICAL — recomendado)** | ~1–2 espinhas redundantes (~0,2–0,4 GB) | **BAIXO** — cirúrgico | não (achado deste doc) |
| **3. `delete<T>` + arena-escopo nos órfãos de fold/inline/lift** | os órfãos de reescrita (~0,1–0,2 GB) | baixo | `delete<T>` é do dono; útil aqui |
| **4. Eixo C (drop de arena por-unidade)** | limita a LARGURA: pico = maior unidade, não o programa | médio-alto (linker interno, fixpoint sob streaming) | **sim** (C10–C16) |
| **5. Aceitar ~1,24 GB** | zero | zero | está sob `<6 GB` |
| **6. Unificação in-place (este doc)** | parcela (a) ~0,4–0,6 GB | **ALTO** (V.3) | não |

**O achado cirúrgico (caminho 2) é a joia — "adiantar o que der":** como `mono_block`/`cx_block`/
`inline_consts` reconstroem corpos **mesmo sem substituição/expansão a aplicar**, fazê-los
**COMPARTILHAR o nó de entrada quando a transformação é identidade** (subst vazia / sem comptime /
sem const naquele corpo) elimina uma espinha quase-inteira por passe, **byte-idêntico** (nó
idêntico → mesmo codegen), com uma mudança **local e de baixo risco** — sem fundir tipo de nó
nenhum. Isto captura boa parte do ganho (a) que a unificação buscaria, por uma fração do custo.

**Recomendação: NÃO unificar. Fazer, nesta ordem: (1) terminar o Eixo A → já entrega a meta; (2)
o short-circuit cirúrgico dos passes; (3) `delete<T>`+arena-escopo nos órfãos; (4) Eixo C para
limitar a largura.** Os quatro juntos derrubam o residual **abaixo** do que a unificação daria,
com risco muito menor e reusando trabalho já planejado. A unificação fica **arquivada como refactor
de manutenibilidade** (não de memória), a reavaliar por mérito próprio se algum dia a duplicação
das duas famílias de nó doer na evolução — não antes, e não por causa de memória.

**Ceticismo com as sub-hipóteses do dono (como pedido):** (i) "objeto-vs-struct" — falso dilema,
item 14 já basta (V.1); (ii) `delete<T>` — **se sustenta e é útil**, mas como ferramenta targetada
(caminho 3), independente da unificação; (iii) "monomorph expande via List<T>" — verdadeiro, mas o
problema MAIOR que achei não é a expansão (dado vivo) e sim a **reconstrução incondicional de
corpos não-genéricos** — e essa se resolve pelo short-circuit (caminho 2), não pela unificação.

**A honestidade que o dono cobrou:** a unificação in-place é uma bela peça de engenharia e o
desenho abaixo fecha — mas para MEMÓRIA ela é a alavanca **errada** (mira os 7%, custa como os 93%).
O dinheiro está no push (já em curso), no compartilhamento-por-identidade dos passes (barato) e no
drop por-unidade do Eixo C (já planejado). **Antes de qualquer decisão, medir com `tk_obs` a
parcela (a) real** — se ela vier < 0,3 GB, o veredito NÃO só se mantém como endurece.

---

## 0. O muro, em uma frase (já verificado — não re-derivado aqui)

O programa passa por passes (`parse → type → monomorph → fold → inline → lift → lower`); hoje
**cada passe cria uma camada de nós-invólucro nova**, deixando ~6 espinhas do AST do programa
inteiro vivas ao mesmo tempo (~1,24 GB do residual não-push). As velhas **não podem ser
liberadas** porque o TAST **embute nós do parser** (subárvore compartilhada → *use-after-free*
se a espinha velha for liberada). Provado em `src/checker/tast.tks`:

| Sítio | Campo | Nó do parser embutido |
|---|---|---|
| `TCall` (:24) | `callee: parser::Path` | `parser::Path` |
| `TFunction` (:75) | `params: []parser::Param` | `parser::Param` |
| `TItem` (:97) | membro do `@TItem()` | `parser::TypeDecl` / `parser::UseDecl` |

As FOLHAS já são compartilhadas hoje; as ~6 cópias são as **espinhas de invólucro por-passe**.

---

## 1. O modelo do nó unificado + como cada passe muta in-place

### 1.1 Um nó por construto, com todos os campos

Hoje há **duas famílias paralelas** de nós que espelham uma à outra:

| Construto | Parser (`src/parser/ast.tks`) | Checker (`src/checker/tast.tks`) |
|---|---|---|
| expressão | `Expr` + `@ExprKind()` (28 ramos) | `TExpr` + `@TExprKind()` (26 ramos) |
| statement | `Statement` (13 ramos) | `TStatement` (11 ramos) |
| item | `Item` + `@ItemKind()` (7 ramos) | `TItem` (5 ramos) |

A proposta do dono: **um único tipo de nó por construto**, construído no parse com TODOS os
campos — sintáticos (`kind`, `line`, `col`), tipo (`type`), mono (marca de instância / type-args
resolvidos), fold (valor const-dobrado), lift/inline (id de lambda içada, corpo enxertado). Os
campos "de etapas posteriores" nascem em **estado-nulo** (`type = Void{}`, `folded = false`, …)
e cada passe **preenche/reescreve** os seus, IN-PLACE, sem construir espinha nova.

```teko
/**
 * O nó unificado de expressão: nasce no parse, é enriquecido por cada passe in-place.
 * @field kind    o descritor da expressão (união boxed {tag;ptr;len}) — pode ser REESCRITO
 *                pelo type-check (ex.: MethodCall -> Call) e pelo fold (ex.: Binary -> Number).
 * @field type    preenchido pelo type-check; Void{} enquanto não-tipado.
 * @field line    posição de origem, imutável após o parse.
 * @field col     posição de origem, imutável após o parse.
 */
exp type Expr = struct {
    kind: @ExprKind()
    type: @Type()
    line: u32
    col: u32
}
```

O ponto mecânico que **habilita** isso já existe: a união `@ExprKind()` emite descritor fat
`{tag; ptr; len}` com **payload boxed** (§9.D). Ou seja, o dado do ramo vive **atrás de um
ponteiro** — o `ptr` do box **é a alça de identidade do nó**. Enriquecer/reescrever um nó =
escrever através desse `ptr` (mutar o payload) ou trocar `tag`+`ptr` (reescrever o kind). Nenhuma
cópia de espinha.

### 1.2 Como cada passe muta in-place — e onde o dono pode estar ERRADO

O dono pediu contra-argumento, não aceitação. Aqui está, passe a passe.

**`type` → o dono diz "anotação pura (preenche campos de tipo)". PARCIALMENTE CERTO.**
Contra-argumento com base no código: o type-check **não é anotação pura** — ele também
*resolve* e *dessugariza*, mudando o KIND de alguns nós:
- `parser::MethodCall` **desaparece** (vira `TCall` com `iface_slot`/`callee_type`);
- `parser::StructLit` → `TStructInit`; `parser::Block` → `TBlockExpr`;
- `parser::MacroCall` é **expandido** (some, alargando a AST no local — como monomorph);
- `Var` ganha `func_ns`/`is_func` resolvidos; `Call.callee` resolve namespace.

No modelo unificado isso é **legítimo e in-place** — "anotar" vira "preencher `type` **e**
possivelmente reescrever `kind` no mesmo slot". Mas NÃO é aditivo puro: **muta o discriminante**.
Isso tem consequência de soundness (§3): qualquer estrutura que retenha ponteiro para o nó
esperando a forma pré-reescrita fica obsoleta. No modelo sequential-single-tree isso é seguro
SE nenhuma estrutura cachear uma visão pré-reescrita — é uma **restrição de disciplina**, não um
bloqueio. **Encaixa, com a ressalva de que "anotação" inclui reescrita-de-kind e expansão-de-macro.**

**`monomorph` → o dono diz "expande a árvore (apenda instâncias); instâncias são dado VIVO, não
cópia morta". CERTO — e é o ÚNICO passe que genuinamente cria nós.** Contra-argumento afiando a
premissa: monomorph **não é enriquecimento** — é **duplicação por natureza**. `foo<T>` chamado
com `i64` e `str` produz DOIS corpos a partir de UM template; não dá para "enriquecer in-place"
um template em duas instâncias. Confirmado no código (`monomorphize`, `monomorph.tks:935`):
constrói `kept` e **apenda** instâncias; o template genérico é **descartado** do output
(`monomorph.tks:974`: só `type_params.len == 0` entra no `kept`). Portanto:
- as instâncias são dado vivo (o dono está certo) — não se deletam;
- **o template, após materializadas todas as instâncias, é LIXO** — não é emitido; precisa de
  descarte (é o `delete<T>` do §2 aplicado à subárvore do template).

**Como a expansão convive com o expurgo de `push` (a pergunta do dono):** o conjunto de
instâncias é descoberto **transitivamente** (instanciar `foo<i64>` pode requisitar `bar<i64>`)
— não dá para pré-contar em uma passada. É a **natureza 2 (PARSE/SCAN → duas passadas)** do
expurgo de arrays:
1. **1ª passada — worklist a fixpoint:** enumerar o CONJUNTO de instâncias requisitadas
   (chaves baratas: `(fn, tupla-de-type-args)`), sem materializar corpos. Conta `N`.
2. **2ª passada — `of_len`:** alocar `of_len(kept_count + N)` e materializar por índice
   (`items[k] = instancia(...)`). Zero `push`, zero copy-grow.

Isso substitui o `teko::list::push(kept, …)` atual por pré-alocação exata — alinhado à REGRA
DURÍSSIMA (zero crescimento dinâmico). **A expansão convive; só troca push por conta+of_len.**

**`fold` / `inline` / `lift` → o dono diz "REESCREVEM; o reduzido (`2+3`→`5`, corpo inline
enxertado) deve ser DESCARTADO senão vaza pra raiz da arena". CERTO — e é aqui que mora o
`delete<T>`.** No modelo unificado:
- **fold** (`inline_consts`/consteval, `project.tks:259`): reescreve `kind` de `Binary{2,+,3}`
  para `Number{5}` no MESMO slot. A subárvore `Binary` (com seus dois filhos boxed) fica
  **órfã** — vivia na arena do AST (que sobrevive ao passe), então o drop-de-escopo **não a
  recolhe**. → `delete<T>` na subárvore órfã.
- **inline**: o corpo do callee é enxertado no sítio de chamada; o `TCall` original e seus `args`
  já consumidos ficam órfãos → `delete<T>`.
- **lift** (içamento de lambda): a lambda vira `LFunc` no nível de item; o nó `TLambda` inline
  original fica órfão → `delete<T>`.

**Resumo do modelo:** enriquecimento-in-place para `type`/`fold`/`inline`/`lift`; **append**
(conta+of_len, não cópia-do-programa-inteiro) para `monomorph`; **`delete<T>`** para os órfãos
de reescrita e para o template genérico. As "6 espinhas" viram **1 espinha + os appends do
monomorph (vivos) + os deletes dos órfãos**. Isto é o cerne: **não é "zero cópia jamais"; é
"zero cópia de invólucro do programa inteiro"** — as únicas alocações que crescem são as
instâncias do monomorph, que são dado vivo. O dono está certo no todo; a precisão é essa.

---

## 2. `delete<T>(ref v): bool` — free explícito + o purge automático (dois mecanismos distintos)

Ruling do dono (correção 2026-08-19): **há DOIS mecanismos, com regras OPOSTAS de segurança.**
`delete<T>` **explícito** NÃO tem guard — é `free` deliberado, responsabilidade do chamador. O
guard de posse (R2) vale SÓ para o **purge automático** que o compilador insere sozinho. Registrar
a distinção com precisão — misturá-las foi o erro do brief original.

### 2.1 `delete<T>` EXPLÍCITO — free direto, sem guard (o chamador sabe o que faz)

```teko
/**
 * Expurga incondicionalmente o ponteiro de `v` e os valores que ele possui, devolvendo o
 * backing à free-list da arena; zera o slot. É free deliberado — o chamador garante que
 * nenhuma referência viva sobra (evitar UAF é responsabilidade de quem chama, não do helper).
 * @param v   slot (ref) a expurgar; zerado após o expurgo.
 * @return    true se havia algo a expurgar (expurgou); false se já era nulo/vazio.
 */
global exp fn delete<T>(ref v: T): bool
```

- **Sem guard de posse, sem no-op-defensivo.** Libera `v` e os backings possuídos por `v`
  (drop-glue por-tipo gerado pelo compilador: para cada campo `[]U`/objeto possuído, `delete`
  recursivo) — **incondicionalmente**. É a mesma semântica de `free()` de C: poderoso e cortante.
- **`ref` porque zera o slot** após o expurgo (sentinela null → um deref posterior dispara o guard
  null-deref `arquivo:linha:coluna:`, não segfault cru). O zeramento é conveniência de
  diagnóstico, NÃO uma garantia anti-UAF — se o dev guardou outro alias antes do `delete`, esse
  alias vira dangling e o dev respondeu por isso.
- **`: bool` = houve-o-que-expurgar (sucesso), NÃO um gate de segurança.** `false` só quando `v`
  já era nulo/vazio (nada a liberar). Não é "recusei por não provar posse" — o `delete` explícito
  nunca recusa por posse.
- **Escolha de retorno `: bool` (não `: bool | error`) — decisão de design, registrada.** O dono
  abriu `: bool | error` como opção (falha carregando `error` rico). **Decisão: fica `: bool`.**
  Racional: o `delete` explícito é um `free` incondicional — ele **sempre expurga** o que houver; o
  único `false` é o caso benigno "já era nulo/vazio", que **não é uma falha com causa** (não há I/O,
  parse nem alloc que possa dar errado). O idioma `T | error` do corpus existe para operações com
  **modo de falha real e causa a comunicar** (`read_file`, `aes_gcm_seal`, alloc) — forçar o
  chamador a casar um `error` que na verdade significa "nada a fazer" é **menos** limpo, não mais.
  `free` não erra; `delete` tampouco. Se um dia surgir um modo de falha genuíno (ex.: um backend de
  arena que possa recusar a devolução por corrupção detectada), aí sim migra-se para `: bool | error`
  — hoje não há esse modo, então `: bool` é o mais idiomático.
- **Coerência com a lei selada** (CLAUDE.md): *"UAF é responsabilidade do DEV no DESIGN de uso,
  NÃO do backend"*. O compilador auto-compilando conhece tudo → só chama `delete<T>` onde o órfão
  é comprovadamente inalcançável (fold/inline/lift/template do monomorph, §1.2) → nunca dispara
  UAF. O helper não policia; o chamador projeta o uso.

### 2.2 O purge AUTOMÁTICO (`assign_frees_old`) — AQUI SIM vale o guard R2

O purge-na-reatribuição é **inserido pelo COMPILADOR** sem o dev pedir (`a = <novo>` → libera o
backing anterior de `a`). Como a decisão é da máquina e ela **não pode corromper vizinho sozinha**,
aqui o guard conservador de R2 é **obrigatório**: só liberar quando o backing anterior é
comprovadamente **possuído**.

| Caso do backing anterior | Purge automático libera? | Por quê |
|---|---|---|
| alocação de arena fresca possuída pela variável | **SIM** | posse provada |
| **sub-slice** `s[a..b]` | **NÃO** (no-op) | backing possuído pelo pai → corromperia o pai |
| **reinterpret** `str`↔`[]byte` (alias vivo) | **NÃO** (no-op) | mesma memória de um `str` vivo |
| **literal / rodata** | **NÃO** (no-op) | não é da arena |
| **parâmetro emprestado** | **NÃO** (no-op) | possuído pelo caller |

O predicado que prova posse é estático, no checker (generaliza `assign_frees_old`, hoje em
`codegen.tks:emit_assign` para o ramo `list::push`). **Conservador: na dúvida, NÃO libera** — o
drop-de-escopo recolhe depois. Este é o ponto de maior cuidado do purge automático, e é o R2 do
`reducao-memoria-arrays-0.3.1.md` **aplicado exclusivamente aqui** (não ao `delete` explícito).

```teko
/**
 * Prova estática de que o backing anterior de `v` é alocação de arena fresca possuída pela
 * variável — condição para o purge automático da reatribuição ser sound.
 * @param v   o lvalue sendo reatribuído.
 * @return    true só quando o free automático é seguro (não sub-slice/reinterpret/literal/emprestado).
 */
fn owns_fresh_backing(v: @Expr()): bool
```

**A diferença que sela os dois:** o `delete` explícito confia no chamador (sem guard); o purge
automático não tem chamador humano (o compilador decide) → precisa do guard. Não há furo de
soundness insolúvel em nenhum dos dois: o explícito **transfere** a responsabilidade (por design,
como `free`); o automático **retém** a responsabilidade e por isso é conservador.

### 2.3 `delete<T>` × arena-por-escopo × purge automático — quando cada um

Três mecanismos de reclamação, **estratificados por tempo de vida e por quem decide**:

| Mecanismo | Quem decide | Guard? | Usa QUANDO |
|---|---|---|---|
| **arena-por-escopo** (`region_drop_subtree` na saída) | compilador (fronteira de escopo) | n/a (drop em massa) | scratch que **nasce e morre no escopo fino** — a maioria esmagadora. Dispensa free individual. |
| **`delete<T>` explícito** (free deep, incondicional) | o **chamador** (dev / código do compilador) | **NÃO** | órfão que vive numa arena **LONGA** (arena do AST sobrevive ao passe → drop-de-escopo não recolhe): o `Binary` dobrado, o `TCall` inlinado, o template do monomorph |
| **purge automático** (`assign_frees_old`, no `=`) | **compilador** (implícito) | **SIM (R2)** | `a = <novo backing>` onde `a` possui o antigo — o compilador prova posse antes de liberar |

**A regra de ouro:** se o valor morre no escopo → deixe o drop-de-escopo (mais barato). O `delete`
explícito é a ferramenta dos passes de reescrita (fold/inline/lift) e do template do monomorph,
cujos órfãos moram na arena do AST inteiro (mais longa que o escopo do passe) — e ali o compilador
SABE que o órfão é inalcançável, então chama `delete` sem cerimônia. O purge automático é o único
que carrega o guard, porque é o único onde a máquina libera sem um chamador que assinou embaixo.

---

## 3. Segurança de aliasing sob mutação SEQUENCIAL

A troca do modelo whole-program-copy pelo in-place introduz um risco que o modelo antigo não
tinha: no modelo antigo, a espinha velha ficava **intacta**, então qualquer ponteiro cacheado
para ela permanecia válido. No in-place, **reescrever o `kind` de um nó invalida qualquer alias
que esperava a forma antiga.**

**A tese de segurança (por que sequencial basta):** os passes rodam **um de cada vez**, cada um
percorrendo a árvore **top-down por `ref`** e escrevendo através do slot. Enquanto:
1. **nenhuma estrutura fora da árvore cachear ponteiro-para-nó esperando forma pré-reescrita**, e
2. **cada passe termine de mutar antes do próximo começar** (sem passes concorrentes),

a mutação sequencial é sound: a única "visão" de um nó é a própria árvore, sempre atual.

**Onde isso pode QUEBRAR (auditoria obrigatória — load-bearing):** estruturas que HOJE guardam
ponteiro/índice para nós do AST e sobrevivem a um passe:
- **tabela de tipos** (`TypeTable`) e **tabela de símbolos** — guardam ponteiro-para-nó ou
  chave/cópia? Se ponteiro, a reescrita-de-kind pode obsoletá-los.
- **worklist do monomorph** — guarda requisições `(fn, type-args)` (chaves) ou ponteiros para
  callsites? Chaves são seguras; ponteiros não.
- **diagnósticos pendentes** (`line`/`col`) — `line`/`col` são imutáveis após o parse (bom);
  se o diagnóstico guarda só posição, seguro.

**Regra de design (a rede):** toda estrutura auxiliar guarda **chave/valor imutável** (posição,
nome, id estável), **nunca ponteiro-para-nó-mutável através de um passe**. Onde precisar
referenciar um nó entre passes, usar um **id estável** (índice no vetor de itens + caminho), não
o ponteiro. Isto é uma pré-condição da unificação e vai no crumb de auditoria (§7, U1).

**Determinismo (fixpoint):** a reescrita in-place tem que produzir `teko.c` byte-idêntico. Como
a ordem dos passes é fixa e a mutação é determinística, o resultado é idêntico — MAS o gensym
derivado de `buf.len` (R4 do doc de arrays) continua sendo a armadilha; a unificação não a
remove nem a agrava.

---

## 4. Encaixe com o fat value-struct do item 14 + a arena escopada fina

### 4.1 O item 14 BASTA? (a pergunta direta do dono) — SIM, condicionado a ref-disciplina

O dono selou o **item 14**: value-struct **mutável + FAT** (cabeçalho `tk_struct_hdr { uintptr_t
self_ptr }`, já vivo em `codegen.tks:7057` e `teko_rt.h:80`; `self` é `ref`; cópia remat novo
cabeçalho). E pede: "Objetos ao invés de structs — NÃO criar construto novo (só interface é
permitida); confirme que isto basta para identidade de objeto + mutação in-place por referência."

**Análise mecânica (o que basta e o que NÃO é preciso):**
- **A identidade do nó já vem de graça** do payload boxed da união (`{tag;ptr;len}`, §9.D): o
  `ptr` é a alça. Mutar in-place = escrever através do `ptr`. Isto **não precisa** do item 14 —
  vem do boxing que a união já emite.
- **O item 14 é o que torna a mutação-por-`ref`/`self` ergonômica e uniforme:** um passe escrito
  como método (`fn (ref self) enrich_type(...)`) muta o nó in-place sem copiá-lo a cada chamada
  (exatamente a performance que motivou o item 14). Sem o item 14, os passes teriam que ser
  funções livres recebendo `ref node` e escrevendo através do box manualmente — funciona, mas
  menos uniforme.
- **NÃO é preciso um construto "objeto" novo.** A combinação **[união boxed = identidade por
  ponteiro] + [item 14 fat header = `ref`/`self` muta in-place] + [ref-disciplina nos passes]**
  entrega identidade-de-objeto-para-o-nó **sem** criar classe/serviço para o AST. O
  value-struct-fat É suficiente.

**A RESSALVA que sela o "basta" (soundness):** o value-struct do item 14 **ainda copia na
atribuição/passagem por valor** (cópia remat novo header — é o que preserva value semantics). Se
um passe **passar um nó por valor** e mutar a cópia, a mutação **se perde**. Portanto o "basta"
está condicionado a: **todo passe percorre e muta por `ref` (ou como método `self`), nunca por
valor.** Essa é a mesma ref-disciplina do §3. Com ela, item 14 basta; sem ela, item 14 não salva
(a cópia-por-valor engole a mutação). **Confirmação: SIM, item 14 basta — condicionado à
ref-disciplina; nenhum construto novo é necessário.**

### 4.2 Arena escopada FINA (ruling do dono) — o encaixe

A arena escopada é por **escopo fino**: braços de `loop`/`if`/`elseif`/`else`/`match`, **bloco nu
`{}`**, + **arena de objetos** (pros que escapam). O escape-check por profundidade-de-região
decide scratch-que-morre-no-escopo vs. objeto-que-escapa.

Encaixe com o AST unificado:
- **A árvore do AST ESCAPA** (vive do parse ao lower) → mora na **arena de objetos**, não numa
  arena de escopo fino. Correto: um nó de AST não morre no braço de `if` que o construiu.
- **O scratch de cada passe** (buffers temporários, listas de trabalho) mora na arena de escopo
  fino do braço/bloco que o cria → drop em massa na saída. Isto é o Eixo B.
- **Os órfãos de reescrita** (§2.3) moram na arena de objetos (a mesma do AST) porque são
  fragmentos que ERAM AST → o drop-de-escopo NÃO os pega → é exatamente por isso que `delete<T>`
  existe (free targetado dentro da arena longa).

O escape-check já é o discriminador certo: nó-de-AST escapa → arena de objetos; scratch de passe
não escapa → arena de escopo. `delete<T>` opera **dentro** da arena de objetos, sobre o
sub-fragmento comprovadamente órfão.

---

## 5. Composição com o Eixo C (pipeline por-namespace) — COMPÕEM, não either/or

O dono selou: **C* NÃO é descartado — é ORTOGONAL e complementar.** Aqui está a composição
precisa (o que cada um resolve):

| Dimensão | AST unificado in-place (este doc) | Eixo C / pipeline por-namespace |
|---|---|---|
| O que ataca | a **ALTURA**: 6 espinhas de invólucro → 1 (+ appends do mono + deletes) | a **LARGURA**: N unidades residentes → 1 unidade por vez |
| Resolve | as ~6 cópias do programa inteiro **e o UAF** (árvore só, sem subárvore-parser compartilhada liberada) | o working-set whole-program (só os corpos de UM namespace vivos) + **endgame native** (`.o` por unidade → `ld`, aposentar `teko.c`) |
| Mecanismo de reclamação | `delete<T>` (órfãos de reescrita) + arena-por-escopo (scratch) | `region_drop_subtree` por unidade (a unidade inteira cai na fronteira) |

**Multiplicativo, não redundante:** o AST unificado torna **cada unidade mais barata** (uma
espinha, não seis); o Eixo C garante que **só uma unidade** esteja viva. Pico ≈ (altura de UMA
espinha) × (1 unidade) — em vez de (6 espinhas) × (todas as unidades). Um sem o outro deixa
metade do ganho na mesa: só-unificado ainda segura o programa inteiro (uma vez); só-C* ainda paga
6 camadas por unidade.

**A "coleta de descarte" que o dono citou é a MESMA máquina nos dois:** o Eixo C derruba a região
da unidade (`region_drop_subtree`); o AST unificado usa `delete<T>` para os órfãos DENTRO do
processamento de uma unidade. Ambos são a arena-Teko de `src/runtime/arena.tks` — não há duas
implementações de free.

**Sequência recomendada de composição:** o AST unificado é **pré-requisito de conforto** do Eixo
C — quando o Eixo C processa uma unidade por vez, é o modelo unificado que garante que essa
unidade custe uma espinha e não seis. Recomenda-se **unificar primeiro** (reduz a altura de todas
as unidades, ganho imediato mesmo no whole-program atual), depois estreitar com o Eixo C. Mas os
dois são independentemente gate-áveis (ver §7).

---

## 6. Blast-radius da unificação de tipos de nó

Medido no base `c824e9d7`:

- **`parser::` embutidos em `tast.tks`: 10 tipos distintos.** Divididos por natureza:
  - **Espinha-compartilhada (as FONTES do UAF): 4** — `parser::Path` (`TCall.callee`),
    `parser::Param` (`[]parser::Param` em `TFunction`), `parser::TypeDecl`, `parser::UseDecl`
    (membros de `@TItem()`). São estes que impedem liberar a espinha velha.
  - **Descritores-folha (pequenos, copiáveis por valor — acoplam mas NÃO são a espinha do UAF):
    6** — `parser::NumInt`, `parser::Pred`, `parser::DocSpan`, `parser::Visibility`,
    `parser::BindKind`, `parser::AssignKind`.
- **Famílias a fundir:** `Expr`(28 ramos)/`Statement`(13)/`Item`(7) ↔ `TExpr`(26)/`TStatement`(11)/
  `TItem`(5). ~50 `pub type T*` em `tast.tks` que casariam com seus gêmeos do parser.
- **Sítios de match / uso:** ~1220 usos de `parser::` no `src/checker/`; 27 arquivos com `match`
  no checker; a família toca também `src/lir/lower.tks`, `src/codegen/codegen.tks`,
  `src/emit/tkb_*.tks`, `src/build/*.tks`, `src/lsp/diagnostics.tks`.
- **Precedente de viabilidade (§9.D):** a migração `type X = variant` → `@X()` inline tocou
  ~2000 sítios e **provou** que migração em massa de tipo-de-nó é factível, byte-idêntica,
  gate-ável por-tipo. O `@Statement()` já é chamado dentro de `@ItemKind()` (`ast.tks:257`) —
  **macro-em-macro já funciona**, então a splicagem `@ExprKind()` dentro do nó unificado tem
  precedente direto.

**Leitura do blast-radius:** a unificação FULL (fundir as duas famílias) é grande (milhares de
sítios de match tocados). A quebra-do-UAF MÍNIMA (só desacoplar os 4 tipos de espinha) é
pequena. Isto é o eixo do **fork F1** (§8).

---

## 7. Sequência de crumbs (gate-ável — cada um isolado, fixpoint `gen2==gen3` a cada verde)

Ordem obrigatória: **construir/provar ANTES de reescrever a raiz** (metodologia do dono). Guard
6,5 GiB inviolável. Todos os snippets já em full-Javadoc (implementer copia verbatim).

### U1 — Auditoria de aliasing (pré-condição de tudo; NÃO muda `src`)
Varrer `TypeTable`, tabelas de símbolos, worklist do monomorph, diagnósticos: cada um guarda
**chave/id estável** ou **ponteiro-para-nó**? Listar todo sítio que guarda ponteiro que sobrevive
a um passe (§3). Entregável: lista de conversões ponteiro→id-estável. Bloqueia U4+. Sem reseed.

### U2 — `delete<T>` explícito + purge automático guardado (aditivo, convive com o velho)
Expor `global exp fn delete<T>(ref v: T): bool` (§2.1) — free deep incondicional sobre a arena-Teko
(`ar_free_block`, reusa `region_free` de C8 do doc de arrays), **sem guard** (responsabilidade do
chamador). Separadamente, o purge automático (`assign_frees_old` em `codegen.tks:emit_assign`)
ganha o predicado de posse `owns_fresh_backing` (§2.2) — só o AUTOMÁTICO carrega o guard R2.
Fixpoint. **NÃO usa `teko_rt.c`** (mesma família da arena-Teko).

### U3 — Quebra-do-UAF MÍNIMA: TAST possui seus 4 tipos de espinha
Substituir os 4 `parser::` de espinha (`Path`, `Param`, `TypeDecl`, `UseDecl`) por equivalentes
**possuídos pelo TAST**, copiados no type-check. Resultado: a espinha do parser deixa de ser
compartilhada → pode ser liberada após o type-check (`delete<T>` na AST-parser). Isto SOZINHO
mata o UAF e libera 1 das 6 camadas, com blast-radius pequeno. Gate: fixpoint byte-idêntico.
(Este crumb entrega valor mesmo se o fork FULL do §8 for adiado.)

### U4 — Nó unificado por construto (atrás do fork F1) — expressão primeiro
Fundir `parser::Expr` + `checker::TExpr` → um `Expr` com todos os campos (`kind`,`type`,…, §1.1),
`kind` em estado-nulo no parse. Migrar por-ramo (precedente §9.D). Cada ramo é um gate. Reescrever
os ~26 sítios de match de `TExprKind` para o kind unificado. Fixpoint por-ramo.

### U5 — `type`/`fold`/`inline`/`lift` mutam in-place o nó unificado
Converter cada passe de "constrói espinha nova" para "muta `ref` in-place" (§1.2). `fold`/`inline`/
`lift` chamam `delete<T>` (U2) no órfão de reescrita (§2.3). Fixpoint por-passe. **Maior queda de
residual isolada.**

### U6 — `monomorph` = conta+`of_len`+append, com `delete<T>` no template
Trocar `push(kept,…)` por duas-passadas (worklist→conta→`of_len`, §1.2). Após materializar
instâncias, `delete<T>` no template genérico. Fixpoint.

### U7 — Fundir `Statement` e `Item` (repetição de U4/U5 para os outros dois construtos)
Mesma técnica, construtos restantes. Ao fim: uma família de nós, uma espinha do parse ao lift.
Fixpoint por-construto.

### U8 — Composição com Eixo C
Onde o Eixo C já processa por-namespace (crumbs C11/C12 do doc de arrays), a unidade agora carrega
UMA espinha unificada; `region_drop_subtree` por unidade + `delete<T>` para órfãos intra-unidade.
Sem trabalho novo de unificação — só confirmar que os dois eixos coexistem verdes. Fixpoint.

---

## 8. FORKS REAIS (fork + recomendação — o dono decide; NÃO decididos aqui)

### F1 — Escopo da unificação: MÍNIMA vs. CONVERGENTE vs. FULL-de-uma-vez

| Fork | O que é | Ganho de memória | Blast-radius | Risco |
|---|---|---|---|---|
| **A — MÍNIMA (quebra-UAF só)** | TAST possui os 4 tipos de espinha (U3); NÃO funde as famílias | libera 1 camada (a do parser) | pequeno (4 tipos + cópia no type-check) | baixo |
| **B — CONVERGENTE (RECOMENDADA)** | U3 primeiro, depois funde construto-a-construto (U4→U7), cada ramo/construto um gate | todas as 6 → 1 (+append mono) | grande, mas **incremental e gate-ável** | médio, **de-riscado pelo §9.D** |
| **C — FULL de uma vez** | funde tudo num crumb | idem B | grande e **atômico** | alto (um fixpoint gigante, difícil isolar regressão) |

**Recomendação: B (convergente).** Entrega a quebra-do-UAF cedo (A é o primeiro passo de B, com
valor isolado), depois converge por-construto reusando exatamente a técnica que o §9.D já provou
(migração por-ramo, byte-idêntica, gate-ável). C tem o mesmo ganho de B mas troca o
de-risco incremental por um único fixpoint monolítico — contra a lei "commit a cada crumb verde".

### F2 — Escopo do drop-glue de `delete<T>` (profundidade do free deep)

O `delete` explícito é incondicional (ruling do dono, sem guard). O fork restante é **quão fundo**
o drop-glue por-tipo desce ao liberar `v`:

| Fork | O que é | Prós | Contras |
|---|---|---|---|
| **A — deep-por-posse (RECOMENDADA)** | libera `v` + todo campo/elemento que `v` **possui** (não segue sub-slice/reinterpret/emprestado de campo) | libera a subárvore órfã inteira num gesto; casa com fold/inline/lift | precisa do compilador gerar drop-glue por-tipo (conhece posse de campo estaticamente) |
| **B — shallow (só o backing de topo)** | libera só o bloco de `v`; campos possuídos vazam | trivial de emitir | vaza os filhos boxed do nó (o `Binary` dobrado deixa os 2 operandos) — não resolve o órfão |

**Recomendação: A.** O uso-alvo (órfão de reescrita) é justamente uma subárvore com filhos
possuídos; shallow deixaria os filhos vazando na arena longa — o problema que `delete` existe para
resolver. O drop-glue por-tipo é estático (o compilador conhece a posse de cada campo); segue só o
que o tipo POSSUI, pulando campo que é sub-slice/reinterpret/emprestado (esses o `delete` do dono
não precisa proteger — mas também não deve seguir, senão libera memória de terceiro). Nota: isto é
posse ESTÁTICA de campo (o que o tipo declara possuir), não o guard-de-runtime que o dono removeu.

### F3 — Ordem entre AST-unificado e Eixo C

| Fork | Ordem | Racional |
|---|---|---|
| **A — unificar primeiro (RECOMENDADA)** | U1–U7, depois compor com C | ganho imediato mesmo no whole-program atual; torna cada unidade do C barata |
| **B — Eixo C primeiro** | C11–C16, depois unificar | estreita a largura antes; mas cada unidade ainda paga 6 camadas até unificar |

**Recomendação: A.** A unificação dá ganho **sem depender** do Eixo C (reduz altura no
whole-program de hoje), e prepara o terreno para o C processar unidades já-magras. Sem tensão de
Lei — é sequenciamento; registrado para o dono decidir a prioridade contra os outros eixos em
curso (A/B do doc de arrays).

---

## 9. Riscos herdados e o que fica em aberto

- **R-align (aliasing sob mutação):** a auditoria U1 é load-bearing; se alguma estrutura-chave
  guardar ponteiro-para-nó através de passe, a reescrita in-place a obsoleta. Mitigação: id
  estável, não ponteiro (§3). **Se U1 achar um sítio que NÃO se converte a id estável, PARAR e
  reportar** — não maquiar.
- **R-mono (append é a única fonte de crescimento):** monomorph continua multiplicando genéricos;
  a meta ≤1,5 GB do doc de arrays não depende deste doc (Eixo A entrega), mas a unificação NÃO
  reduz o custo intrínseco das instâncias — só remove as 5 camadas de invólucro em volta. Não
  superestimar o ganho: é residual (altura das espinhas), não os 93% do push.
- **R-fixpoint:** a mutação in-place tem que sair byte-idêntica; o gensym-derivado-de-`buf.len`
  (R4 do doc de arrays) segue como pré-condição independente — a unificação não o remove.
- **Sem furo insolúvel nos dois mecanismos de free (§2):** o `delete<T>` explícito **transfere** a
  responsabilidade ao chamador (por design, como `free` — sem guard); o purge automático **retém** a
  responsabilidade e por isso carrega o guard conservador R2 (só libera backing comprovadamente
  possuído). A separação é o que sela a soundness — o único mecanismo sem chamador humano (o
  automático) é o único que precisa de guard. **Cuidado load-bearing:** o guard R2 do purge
  automático (`owns_fresh_backing`) NÃO pode vazar para o `delete` explícito (o explícito é
  incondicional) nem o contrário (o automático nunca é incondicional).

---

## 10. Resumo executivo

O modelo do dono (uma árvore, enriquecida in-place) é **correto e implementável**, com três
precisões: (1) não é "zero cópia jamais" — **monomorph inerentemente apenda** instâncias vivas
(vira conta+`of_len`, não whole-program-copy); (2) o item 14 fat value-struct **basta** (nenhum
construto novo), **condicionado a ref-disciplina** nos passes (passar por valor engole a mutação);
(3) há **dois** frees distintos — `delete<T>` explícito é `free` incondicional (`: bool`, sem
guard, responsabilidade do chamador) e o purge automático da reatribuição carrega o guard de posse
R2 (só o automático). A unificação e o Eixo C **compõem multiplicativamente**
(altura × largura). Recomenda-se a rota **convergente** (F1-B): quebrar o UAF cedo (U3, ganho
isolado), depois fundir construto-a-construto reusando o precedente §9.D.
</content>
</invoke>
