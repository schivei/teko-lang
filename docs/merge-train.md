# Merge train — o trem de merges

Por que juntar mudanças uma a uma quebra em escala, como enfileirá-las como vagões resolve, e
por que a Teko usa uma variante específica disso.

As seções 1–7 são **agnósticas**: valem para qualquer projeto com uma bateria de testes e mais de
uma pessoa mexendo nele. A seção 8 é a parte Teko.

---

## 1. O problema que ninguém vê chegando

Imagine dois colegas trabalhando ao mesmo tempo. A Ana renomeia uma função: `calcularTotal` passa a
se chamar `calcularTotalComImposto`. O Bruno, em outra parte do código, escreve uma tela nova que
*chama* `calcularTotal`.

Cada um abre sua proposta de mudança. Cada uma é testada isoladamente. **As duas passam.** Verde e
verde. E ainda assim, no instante em que ambas entram no projeto, ele para de compilar: a tela do
Bruno chama um nome que a Ana acabou de aposentar.

Ninguém errou. Não houve nem conflito de texto — as duas mexeram em arquivos diferentes, então a
ferramenta de versionamento junta tudo sem reclamar uma vírgula. O que houve é o que a literatura
chama de **conflito semântico**: as mudanças são compatíveis no papel e incompatíveis no
significado. E o detalhe cruel é que *testar cada mudança contra o estado antigo do projeto não
prova nada sobre o estado que vai existir depois que as duas entrarem*.

```
// cada uma testada contra o projeto COMO ELE ESTÁ HOJE
projeto + Ana   → verde
projeto + Bruno → verde

// mas o que vai existir amanhã é isto, e ninguém testou:
projeto + Ana + Bruno → VERMELHO
```

Com dois colegas isso é um susto ocasional. Com cinquenta, o projeto principal vive quebrado — e
quando ele está quebrado, *todo mundo* para: ninguém consegue distinguir "quebrei eu" de "já estava
quebrado", e o trabalho de todos passa a acontecer sobre areia.

O exemplo acima não é invenção didática: é a formulação canônica que a documentação do *bors* usa
para explicar por que a ferramenta existe — lá a função renomeada chama-se `bifurcate()`. A própria
ferramenta batiza o fenômeno de **merge skew**, ou conflito semântico de merge [[2]](#ref2). Na
academia o problema tem linha de pesquisa própria, incluindo trabalho brasileiro sobre detectá-lo
automaticamente [[11]](#ref11) [[12]](#ref12).

## 2. A regra que resolve, e o preço dela

A resposta canônica está enunciada num texto de 2014 de Graydon Hoare, criador da linguagem Rust,
sob um nome deliberadamente irônico — **a Regra Que Não É Ciência de Foguetes**: *"mantenha
automaticamente um repositório de código que sempre passa todos os testes"*. Não "quase sempre".
Sempre. Hoare credita a expressão a um antigo colega, Ben Elliston, e conta no mesmo texto que havia
escrito no ano anterior a ferramenta que a implementava para o Rust [[1]](#ref1).

Para conseguir isso, só existe um caminho lógico: **nada entra no projeto sem antes ser testado
exatamente na posição em que vai entrar** — não contra o estado antigo, mas contra o estado que
resultará da sua entrada. Isso obriga a serializar: uma mudança de cada vez, testada depois de
acoplada, e só então aceita.

É isso que uma **fila de merge** (*merge queue*) faz. Ela tira a decisão da mão das pessoas: você
não "mergeia", você *entra na fila*. A fila junta sua mudança ao estado atual, roda a bateria
completa, e só aceita se passar. Se falhar, sua mudança sai da fila e o projeto principal nunca
soube que ela existiu.

O preço é aritmética simples e brutal. Se a bateria leva 30 minutos e há 20 mudanças esperando, a
última entra **dez horas depois**. A fila resolveu a corretude e criou um engarrafamento.

## 3. O trem: testar o futuro antes que ele chegue

O trem é a saída desse engarrafamento, e a ideia é elegante: **em vez de testar uma mudança por vez,
aposte**. Assuma que as que estão na frente vão passar, e comece a testar as de trás *já empilhadas
sobre elas*, tudo em paralelo.

Daí o nome. Cada mudança é um **vagão**, engatado no vagão anterior. O vagão 3 não é testado contra o
projeto de hoje — é testado contra *projeto + vagão 1 + vagão 2*, que é exatamente o mundo em que ele
vai viver.

```
Vagão 1   testado sobre: projeto                 verde
Vagão 2   testado sobre: projeto + V1            verde
Vagão 3   testado sobre: projeto + V1 + V2       verde
          ↓ tudo verde → o trem inteiro entra de uma vez
```

Como as três baterias rodam ao mesmo tempo, o trem custa *uma* bateria de tempo em vez de três. Esse
"assumir que os da frente passam" tem nome técnico: **execução especulativa**. O termo vem do Zuul, o
sistema de *gating* nascido no OpenStack, cuja documentação o define com precisão e enuncia também o
princípio que fundamenta tudo isto — *"um sistema de gating deve sempre testar cada mudança aplicada
à ponta do ramo exatamente como ela vai ser mergeada"* [[5]](#ref5).

A aposta às vezes dá errado, e o comportamento nesse caso é a parte interessante. Se o vagão 2 falha,
todo vagão testado *em cima dele* foi testado num mundo que não vai existir — o resultado deles é
lixo. O trem então **desengata o vagão culpado**, e os de trás são re-testados sem ele.

Hoje isso é recurso de plataforma, não projeto de engenharia: o GitLab chama de *merge trains* desde
2019, com pipelines rodando em paralelo [[3]](#ref3), e o GitHub tem *merge queue* desde 2023
[[4]](#ref4). Vale notar o sinal de maturidade: o *bors-ng*, uma das implementações independentes
mais usadas, foi descontinuado com um aviso que recomenda justamente usar a fila nativa do GitHub
[[2]](#ref2).

> **O detalhe que decide o custo.** Quanto mais frequentes as falhas, mais o trem desperdiça trabalho
> especulativo — e mais ele se degrada para o comportamento da fila simples. Times grandes atacam
> isso prevendo *quais* baterias realmente precisam rodar, em vez de rodar tudo sempre: foi assim que
> a Uber tornou seu *submit queue* viável em escala de milhares de mudanças por dia [[6]](#ref6).

## 4. A outra família: pilhas de mudanças

Existe um segundo sentido de "empilhar", e confundir os dois gera muito ruído. O trem da seção
anterior é uma máquina de *entrada*: ela ordena mudanças de *pessoas diferentes* na hora de entrar no
projeto, e some depois.

A **pilha de mudanças** (*stacked changes*) é outra coisa: é como *uma única pessoa* divide *um*
trabalho grande. Em vez de uma proposta gigante de 3 mil linhas que ninguém consegue revisar, você faz
seis pequenas, cada uma partindo da anterior. A revisora lê seis peças coerentes, uma por vez.

Cultura de *stacked diffs* é antiga em empresas que construíram ferramenta própria: nasceu no
Phabricator, ferramenta interna do Facebook que virou open-source [[8]](#ref8), e a Meta segue nessa
linha com o Sapling [[9]](#ref9). Quem viveu esse fluxo tende a sentir falta dele em qualquer lugar
aonde vá — há um ensaio bem conhecido exatamente sobre esse contraste [[7]](#ref7). O mundo do pull
request demorou a ganhar ferramenta equivalente (hoje há produtos dedicados [[10]](#ref10)), e é por
isso que mudanças empilhadas ainda parecem exóticas para muita gente.

## 5. O híbrido, e a propriedade que economiza um dia de trabalho

Junte as duas famílias — uma pilha autoral *que também é* o trem de entrada — e aparece uma
propriedade que não é óbvia e que muda como se opera o dia a dia.

Numa pilha, o vagão de cima **contém** tudo que está embaixo dele. Então, se o vagão de baixo está
vermelho por um defeito, e o de cima está verde porque carrega a correção, **basta o de cima descer
para o de baixo ficar verde**. A correção não precisa ser aplicada em cada vagão: ela precisa existir
*uma vez*, no topo, e a drenagem a distribui.

A consequência prática: você **nunca conserta para baixo**. Correção pequena vai no último vagão
engatado; correção grande vira um vagão novo no topo. O único portão que importa é *o topo do trem
estar verde*. Os vagões de baixo não precisam ficar verdes um por um — eles ficam, sozinhos, conforme
o trem drena.

O contraste em números, num trem de cinco vagões todos vermelhos pelo mesmo defeito:

| estratégia | correções aplicadas | rodadas de teste |
|---|---|---|
| consertar cada vagão (cascata) | 5 | 5 |
| consertar só o topo + drenar | 1 | 1 + as da drenagem |

## 6. Quando vale, e quando é exagero

| Situação | O que usar | Por quê |
|---|---|---|
| Poucas pessoas, projeto pequeno, testes rápidos | Nada disso | O susto ocasional custa menos que a máquina. Não construa a solução antes de ter o problema. |
| O principal quebra de vez em quando e para o time | **Fila de merge** | Resolve corretude com configuração, não com engenharia. Hoje é recurso pronto nas plataformas. |
| A fila virou engarrafamento | **Trem** (fila + especulação) | Troca tempo de espera por trabalho paralelo. Só compensa se a taxa de falha for baixa. |
| Uma pessoa, um trabalho grande e coeso | **Pilha de mudanças** | É sobre revisão, não sobre concorrência: peças pequenas são revisáveis de verdade. |
| Trabalho grande, dependente em sequência, que precisa entrar integrado | **Pilha drenada** | Ganha a auto-correção da seção 5. Exige disciplina: só o topo é o portão. |

> **O pré-requisito de tudo.** Nenhuma dessas máquinas funciona sobre uma bateria de testes em que
> ninguém confia. Se o resultado vermelho é frequentemente "coisa da infraestrutura", a fila vira
> ritual: as pessoas re-rodam até passar, e a garantia evapora. **Teste instável derrota trem, fila e
> pilha.** Arrume a confiabilidade primeiro.

---

## 7. Por que a Teko usa isto

A Teko não adotou o trem por gosto de processo. Adotou porque, para um compilador que se compila a si
mesmo, as alternativas **não funcionam** — e isso vale a pena entender, porque o mesmo raciocínio se
aplica a qualquer projeto com as mesmas propriedades.

### 7.1 O motivo estrutural: o corpus depende do compilador que o vagão anterior produziu

Num projeto comum, duas mudanças independentes podem ser testadas em paralelo contra o estado atual.
Na Teko, frequentemente **não podem nem ser compiladas**.

Um exemplo real da onda 0.3.1: um vagão adicionou a *W-RULE* ao verificador de tipos (aritmética de
larguras mistas passa a resolver por peer-type). O vagão seguinte varreu o corpus **deletando** os
casts manuais que a W-RULE tornou desnecessários. O resultado é que o corpus do segundo vagão só
compila com o compilador que o primeiro produziu. Testá-lo contra a `main` não é "arriscado" — é
impossível: o compilador da `main` rejeita o código com erro de tipo.

Empilhar, aqui, não é uma preferência de fluxo. É a **única ordem em que o trabalho existe**.

### 7.2 A lei do fixpoint exige integração atômica

A Teko exige que o compilador, ao se reconstruir, produza bytes idênticos (`gen1 == gen2`). Essa lei
fala sobre um *estado inteiro e coerente* do projeto. Um trem meio integrado — três vagões dentro, dois
fora — não é um estado que a lei consiga descrever, muito menos validar.

Por isso a variante da Teko drena em **LIFO**: o último vagão mergeia dentro do de baixo, este no de
baixo, até a base entrar na `main` **uma única vez**, carregando o trem inteiro já integrado. A `main`
nunca vê um estado parcial. E, como consequência prática, o corte de seed e o espelhamento para a
organização acontecem **uma vez por onda**, não uma vez por vagão.

### 7.3 O que a Teko acrescenta ao modelo padrão

| Prática Teko | O que é | Por quê |
|---|---|---|
| **Dreno LIFO** | O topo mergeia no vagão de baixo, e assim por diante; a base entra na `main` sozinha | Integração atômica (§7.2) |
| **Nunca consertar para baixo** | Correção pequena vai no último vagão engatado; grande vira vagão novo no topo | A auto-correção da §5 torna a cascata puro desperdício |
| **Portão único: topo verde** | Vagões de baixo não são dirigidos ao verde individualmente | Eles ficam verdes na drenagem |
| **Vagão fechado não se toca** | Só reabre se um merge der erro | Preserva o estado exatamente como foi validado |
| **Achado de auditoria vira vagão novo** | O romaneio pré-fecho nunca edita vagão existente | Cada achado ganha diff próprio e rastreável |
| **Contra-máquina** | O vagão do *bump* de versão fica no topo, depois de tudo, e é o único a sair de *draft* | É o sinal combinado de que o trem está pronto; os merges são sempre do dono do projeto |
| **Escada de bootstrap** | O CI descobre por sondagem qual ancestral o compilador liberado consegue construir, e sobe em degraus até o topo | Consequência direta da §7.1: num trem profundo, o seed liberado não alcança o topo |

### 7.4 Onde estão as regras e o procedimento

Este documento explica **por quê**. O **o quê** e o **como** vivem em dois lugares, e é neles que se
mexe quando a prática muda:

- `docs/memory/teko-stacked-train-discipline.md` — as regras em forma normativa, com as datas dos
  *rulings* que as originaram.
- `.claude/skills/train/SKILL.md` — o procedimento operacional: engatar, equalizar, fechar vagão,
  ler CI, preparar o dreno.

### 7.5 A lição que custou caro

Vale registrar porque é o tipo de erro que qualquer um repete: durante a onda 0.3.1, a correção de um
defeito de infraestrutura foi **propagada manualmente para os doze vagões**, nove vezes seguidas. Cada
propagação disparava a matriz de CI inteira do trem. Todo esse trabalho era desnecessário — a
drenagem faria a propagação de graça, e bastava o topo estar verde.

A regra da §5 não é uma otimização. É a diferença entre uma tarde de trabalho e um comando.

---

## 8. Glossário

**Conflito semântico** — duas mudanças que a ferramenta de versionamento junta sem reclamar, mas cujo
significado é incompatível: o projeto quebra apesar de não haver conflito de texto.

**Fila de merge** (*merge queue*) — um porteiro automático: você entra na fila em vez de mergear. Cada
mudança é testada acoplada ao estado atual e só entra se passar.

**Execução especulativa** — testar uma mudança assumindo que as da frente vão passar. Permite
paralelizar a fila; o trabalho é descartado se a aposta falhar.

**Vagão** — uma mudança dentro do trem, testada empilhada sobre as anteriores.

**Desengate** (*eject*) — remover o vagão culpado quando o trem falha, e re-testar os de trás sem ele.

**Pilha de mudanças** (*stacked changes*) — um trabalho grande fatiado em propostas pequenas e
sequenciais, cada uma partindo da anterior, para viabilizar revisão.

**Drenagem** — fazer a pilha entrar: cada vagão entra no de baixo até a base entrar no projeto
principal, carregando o conjunto integrado.

**Contra-máquina** — na Teko, o vagão do *bump* de versão, posicionado no topo do trem; sair de
*draft* é o sinal de que o trem está pronto para o dono drenar.

**Desenvolvimento em tronco** (*trunk-based*) — a prática de todos integrarem no mesmo ramo principal,
em lotes pequenos e frequentes, em vez de ramos longos. É o contexto em que todas essas máquinas fazem
sentido.

---

## 9. Referências

Todas verificadas na fonte primária. Onde a atribuição popular está errada, a nota registra a
correção — vale mais acertar a procedência do que repetir o que circula.

<a id="ref1"></a>**[1]** Hoare, Graydon. *technicalities: "not rocket science" (the story of monotone
and bors)*. Blog pessoal, 2 fev. 2014. <https://graydon2.dreamwidth.org/1597.html>
*Nota de procedência:* o título do texto não é o nome da regra. A regra enunciada dentro dele é "The
Not Rocket Science Rule Of Software Engineering", e Hoare credita a expressão "not rocket science" ao
colega Ben Elliston, de cerca de 2001.

<a id="ref2"></a>**[2]** **bors** — três implementações distintas, comumente confundidas: o *bors*
original (Graydon Hoare, 2013, para o rust-lang); o *bors-ng* em Elixir (<https://bors.tech/>), hoje
**descontinuado**, cujo aviso recomenda usar a merge queue nativa do GitHub; e o *bors* atual do Rust,
em Rust (<https://github.com/rust-lang/bors>). A documentação do bors-ng contém o exemplo didático
canônico do conflito semântico e nomeia o fenômeno *merge skew*.

<a id="ref3"></a>**[3]** GitLab. *Merge trains* — documentação oficial.
<https://docs.gitlab.com/ci/pipelines/merge_trains/> — lançado no GitLab 12.0 (jun. 2019); *parallel
merge trains* no 12.1 (jul. 2019). *Nota:* a GitLab descreve o paralelismo mas **não** usa o termo
"speculative" — esse vocabulário é do Zuul [[5]](#ref5); não atribua à GitLab.

<a id="ref4"></a>**[4]** GitHub. *Pull request merge queue* — disponibilidade geral em 12 jul. 2023.
<https://github.blog/changelog/2023-07-12-pull-request-merge-queue-is-now-generally-available/>

<a id="ref5"></a>**[5]** Zuul. *Project Gating* — documentação oficial.
<https://zuul-ci.org/docs/zuul/latest/gating.html> — fonte primária do conceito de **execução
especulativa** aplicada a gating.

<a id="ref6"></a>**[6]** Ananthanarayanan, Sundaram; Saeida Ardekani, Masoud; Haenikel, Denis;
Varadarajan, Balaji; Soriano, Simon; Patel, Dhaval; Adl-Tabatabai, Ali-Reza. *Keeping Master Green at
Scale*. EuroSys 2019, Dresden. DOI 10.1145/3302424.3303970.
<https://dl.acm.org/doi/10.1145/3302424.3303970> — o paper do **SubmitQueue** da Uber.

<a id="ref7"></a>**[7]** Gabbard, Jackson. *Stacked Diffs Versus Pull Requests*. 29 set. 2018.
<https://jg.gg/2018/09/29/stacked-diffs-versus-pull-requests/>

<a id="ref8"></a>**[8]** **Phabricator** (revisão: *Differential*; CLI: *Arcanist*) — originado como
ferramenta interna do Facebook, liderado por Evan Priestley, aberto e depois descontinuado em 2021;
fork comunitário: *Phorge*. <https://secure.phabricator.com/>

<a id="ref9"></a>**[9]** Goode, Durham; Bolin, Michael. *Sapling: Source control that's user-friendly
and scalable*. Engineering at Meta, 15 nov. 2022.
<https://engineering.fb.com/2022/11/15/open-source/sapling-source-control-scalable/>

<a id="ref10"></a>**[10]** **Graphite** — produto comercial com *stacked PRs* e *merge queue* sobre o
GitHub. <https://graphite.com/> (o antigo graphite.dev redireciona para cá).

<a id="ref11"></a>**[11]** da Silva, Léuson; Borba, Paulo; Mahmood, Wardah; Berger, Thorsten; Moisakis,
João. *Detecting Semantic Conflicts via Automated Behavior Change Detection*. ICSME 2020, pp. 174–184.
DOI 10.1109/ICSME46990.2020.00026. <https://ieeexplore.ieee.org/document/9240661/>

<a id="ref12"></a>**[12]** da Silva, Léuson; Borba, Paulo; Maciel, Toni; Mahmood, Wardah; Berger,
Thorsten; Moisakis, João; Gomes, Aldiberg; Leite, Vinícius. *Detecting semantic conflicts with unit
tests*. Journal of Systems and Software, 2024. Ferramenta: **SAM** (SemAntic Merge).
<https://www.sciencedirect.com/science/article/pii/S0164121224001158> — linha de pesquisa do grupo do
Paulo Borba (UFPE).

<a id="ref13"></a>**[13]** Tannenbaum, Rachel. "Continuous Integration" (cap. 23), com o painel *TAP:
Google's Global Continuous Build* por Adam Bender. In: Winters, Titus; Manshreck, Tom; Wright, Hyrum
(eds.). *Software Engineering at Google*. O'Reilly, 2020. HTML livre:
<https://abseil.io/resources/swe-book/html/ch23.html>

<a id="ref14"></a>**[14]** Esfahani, Hamed; et al. *CloudBuild: Microsoft's Distributed and Caching
Build Service*. ICSE 2016 **Companion** (ICSE-C), pp. 11–20. DOI 10.1145/2889160.2889222.

<a id="ref15"></a>**[15]** Humble, Jez; Farley, David. *Continuous Delivery: Reliable Software Releases
through Build, Test, and Deployment Automation*. Addison-Wesley, 2010.

<a id="ref16"></a>**[16]** Forsgren, Nicole; Humble, Jez; Kim, Gene. *Accelerate: The Science of Lean
Software and DevOps — Building and Scaling High Performing Technology Organizations*. IT Revolution,
2018.

<a id="ref17"></a>**[17]** Hammant, Paul. *Trunk Based Development* — portal, 2017–2020.
<https://trunkbaseddevelopment.com/> — *nota:* circula como "livro", mas é o site convertido por
scripts, sem editora nem ISBN convencional; cite como portal.
