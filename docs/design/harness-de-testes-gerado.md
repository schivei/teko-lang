---
section: design
created: 2026-07-29
status: DESENHO — nenhuma linha de produto escrita nesta carga
branch: cargo/0.3.1.0-harness-gerado (de remodel/0.3.1.0-linux-native-2 @ ecc0b44)
reconcilia: docs/memory/parallel-test-harness-0.3.2.md, docs/design/concorrencia-adiantada-s8.md,
            docs/design/gate-sem-c-0.3.0.31.md
---

# O harness de testes GERADO — unitários em threads, regressivos em processos

## 0. Os rulings do dono, literais — o desenho não pode divergir deles

**R1 (2026-07-29) — a divisão em duas paralelizações:**

> *"O que o compilador precisa aprender com testes, tanto unitários como regressivos é criar em tempo
> de compilação uma main para produzir um binário executável em teko, essa main deve disparar em
> threads isoladas cada uma das funções #test em paralelo, enquanto que, para as regressões, deve
> executar em paralelo novos processos (dado que o binário dos regressivos já existe)."*

**R2 (2026-07-29) — zero C, e a lane fecha vermelha:**

> *"esse é outro que não pode emitir C quando for compilar nativo, se ao final de uma sessão de testes
> houver um C gerado, tem que fechar vermelha a lane."*

**R3 (2026-07-29) — a via 1 para o veredicto atravessar a fronteira:**

> *"indicar pra ele um stderr, stdin e stdout customizado"*

**R4 (2026-07-29) — a via 2:**

> *"2. implementar canais `chan<T>`"*

**R5 (2026-07-29) — a atribuição de cada via, e o requisito que nasce dela:**

> *"Para os regressivos, que usarão processos, a primeira opção é a certa, para os testes unitários
> que estão no mesmo processo, a segunda seria a certa com uma mudança para capturar exit e panic
> quando executar testes unitários."*

**R0 (2026-07-28), que R3 generaliza e que fica INTEIRO:**

> *"podemos criar um canal de saída próprio, faz sentido: stdout, stderr e um terceiro, que quando
> roda direto, usa o stderr, mas que quando dizemos o canal, ele escreve em outro local."*

**R6 (2026-07-28), a lei de posse entre tarefas:**

> *"sem ref em threads, não só async/await, mas em isolation principalmente"*

**R7 (2026-07-29) — o estado de hoje, e as DUAS primitivas novas:**

> *"exit e panic hoje saem limpo, a única captura seria um 'catch' global que apenas permite tratar
> panico, mas sem interromper o panico, bem como não existe captura de saida, logo, são duas
> primitivas novas mas que devem ser mantidas apenas para rodar os testes e conhecidas somente pelo
> compilador."*

**R8 (2026-07-28), a solução já nomeada para o gate sem C, e que R1/R5/R7 confirmam um ano depois**
(`docs/design/concorrencia-adiantada-s8.md`, §introdução):

> *"O desenho que o owner deu para o gate sem C **é um ISOLATE por `#test`**, com handler próprio de
> exit e panic."*

Deste conjunto sai a tabela que governa tudo o que segue e que não é negociável:

| metade | unidade de execução | via do veredicto | transporte |
|---|---|---|---|
| **unitários (`#test`)** | THREAD no MESMO processo | **via 2** — `chan<T>` | valor TIPADO, sem serializar |
| **regressivos** | PROCESSO novo | **via 1** — descritores próprios | ficheiros que o pai nomeia |

---

## 1. O degrau em que isto cabe, e porquê

| peça | degrau | argumento |
|---|---|---|
| o gate passar a OBEDECER ao backend resolvido (§2) | **0.3.1.0** | é o defeito que a própria lane descobriu; Lei 4 (defeito revelado pela tarefa é corrigido agora, não vira issue) |
| `main` de gate SINTETIZADA, serial, consumida pelas DUAS rotas (§3) | **0.3.1.1** | não precisa de primitiva nova; é o que torna a prova de equivalência ESTRUTURAL |
| metade de PROCESSOS: `spawn`/`wait` + descritores, morte do andaime de `sh` (§5) | **0.3.1.2** | precisa de runtime C mantido, não de primitiva de linguagem |
| metade de THREADS: `cabi fn`, chão de thread, raiz de arena por thread, captura de panic/exit, `chan<T>` (§6) | **0.3.2** | `chan<T>` é SUPERFÍCIE DE LINGUAGEM nova; é carga da linguagem, não do módulo de build |

**A restrição de calendário que ninguém pode perder: a prova de equivalência EXPIRA.** A rota C morre
na 0.3.1.4 e é ela o oráculo (`docs/memory/0.3.1.0-linux-native-first-stop.md`, A REGRA DO ORÁCULO).
Toda a prova diferencial de §10 tem de ser EXECUTADA E ARQUIVADA antes da 0.3.1.4; depois disso não há
com que comparar. Se a metade de threads escorregar para lá da 0.3.1.4, ela nasce sem oráculo — e
então o desenho tem de ser reordenado, não a prova dispensada.

A fórmula de versão fica INTACTA: `A.B.C.D` + `suffix`, o quarto campo é o número de build que o
integrador incrementa. Nada aqui lhe toca.

---

## 2. O defeito que força a carga — e ele é de UMA linha que não existe

`run_native_gate` (`src/build/project.tks`:2818) emite o corpus `#test` como C e chama `run_cc`
**incondicionalmente**. Não lê `TEKO_BACKEND`. O resolvedor existe ao lado dele
(`c_backend_selected`, `project.tks`:1551) e o gate nunca o consulta.

Consequência medida, e registada em `docs/memory/0.3.1.0-linux-native-first-stop.md`: **1030 testes
verdes atravessam a rota C**, e uma miscompilação exclusiva do nativo (a variante DECLARADA a entrar
sempre no primeiro braço) passou por baixo deles sem tocar em nada. O `fixpoint` também não a vê, por
construção — compara dois binários do MESMO compilador errado.

Portanto: **o gate que não lê o backend não é uma ineficiência, é um portão cego.** É a peça 1 e é a
única do documento que não depende de primitiva nenhuma.

E é dela que sai o cumprimento de R2. O gate passa a despachar:

- `Backend::Native` → `.o` + `link_object`, **zero `.c` escrito**;
- `Backend::C` (só sob `TEKO_BACKEND=c`, o seletor em retirada) → o caminho de hoje, que existe
  APENAS como oráculo até 0.3.1.4.

O default é nativo, logo o caminho por omissão não emite C — que é exactamente o que R2 pede.

---

## 3. Descoberta dos `#test` e onde a geração entra no compilador

### 3.1 Como se descobre

Pela mesma travessia que `has_tests` (`project.tks`:2981) já faz: `prog.items`, arm
`checker::TFunction`, campo `is_test`. Nada de tabela nova, nada de registo em tempo de execução.

**O ÍNDICE DO TESTE É O ÍNDICE DO ITEM EM `prog.items`, e isso é contrato.** É o mesmo `idx` que
`cov_enter(idx)` usa hoje, portanto o sintetizador **não insere nem remove itens** — só ACRESCENTA um
`main` no fim. Se inserisse, a atribuição de cobertura do filho deixaria de casar com a do pai, em
silêncio.

Essa ordem é também a ordem do RELATÓRIO (§8). Ela é estável porque a descoberta de fontes já é
ordenada (vagão 20) e porque o sintetizador preserva a ordem de `prog.items` verbatim.

### 3.2 Onde entra

Módulo novo **`src/build/gate.tks`** — o mesmo nome que `concorrencia-adiantada-s8.md` §5.1 já
reservou, para não haver dois. É um transform de **AST TIPADA para AST TIPADA**, a correr **depois de
check + monomorph** e **antes do lowering**, e o seu produto é consumido pelas DUAS rotas.

```teko
/**
 * GateShape — a forma do `main` que o sintetizador produz.
 *
 * Existe porque a metade de threads chega numa versão posterior à metade serial, e as duas têm de
 * sair do MESMO sintetizador: uma forma escolhida por dado é uma peça; duas formas escritas à mão
 * são duas peças que divergem.
 *
 * @since 0.3.1.1
 */
pub type GateShape = enum {
    /** Um `main` que chama cada `#test` em sequência, no fio principal. A forma de arranque. */
    Serial
    /** Um `main` que lança cada `#test` numa thread e drena os veredictos de um `chan<TestVerdict>`. */
    Threaded
}

/**
 * GatePlan — tudo o que o `main` gerado precisa de saber e que NÃO vem do programa.
 *
 * NENHUM CAMPO DEPENDE DA MÁQUINA QUE COMPILA, e isso é uma exigência do fixpoint, não uma
 * preferência (§8.1). O grau de paralelismo e os caminhos de canal são lidos pelo `main` gerado em
 * TEMPO DE EXECUÇÃO, do ambiente; se fossem cozidos aqui como literais, o mesmo fonte compilado num
 * host de 4 núcleos e noutro de 64 emitiria bytes diferentes e `gen2 == gen3` partiria por uma razão
 * que não tem nada a ver com o compilador.
 *
 * @since 0.3.1.1
 */
pub type GatePlan = struct {
    /** a forma do `main` a sintetizar; decidida por POLÍTICA do compilador, nunca pelo ambiente. */
    shape: GateShape
}

/**
 * gate_program — devolve `prog` com um `main` SINTETIZADO que corre cada `#test`.
 *
 * O `main` sintetizado é a ÚNICA descrição do que um gate faz, e é por isso que ele é produzido aqui
 * e não no emissor: as duas rotas de código (a própria e a C, esta até 0.3.1.4) consomem o MESMO
 * `main`, logo uma divergência de veredicto entre rotas é, por construção, uma divergência de GERAÇÃO
 * DE CÓDIGO e nunca de harness. É essa a metade estrutural da prova de §10.
 *
 * Os itens de `prog` são preservados um a um, na ordem em que chegaram: o índice de um `#test` em
 * `prog.items` É o seu índice de cobertura (`cov_enter`) e o seu índice de relatório. Inserir ou
 * remover um item desalinharia as duas atribuições em silêncio.
 *
 * @param prog  o programa já verificado e monomorfizado, COM os `#test` dentro
 * @param plan  a forma do `main`, o grau de paralelismo e os caminhos dos canais
 * @return      o mesmo programa com um `main` acrescentado no fim
 * @throws      quando o programa não declara nenhum `#test`, ou já traz um `main` de gate
 * @since 0.3.1.1
 */
pub fn gate_program(prog: checker::TProgram, plan: GatePlan) -> checker::TProgram | error

/**
 * gate_test_indices — os índices, em `prog.items`, de cada função `#test`, por ordem crescente.
 *
 * Extraída do sintetizador porque é a ÚNICA definição de "quais são os testes e por que ordem": o
 * relatório do pai, a contagem de esperados (§6.6) e a atribuição de cobertura leem-na toda daqui.
 *
 * @param prog  o programa verificado
 * @return      os índices dos `#test`, crescentes; vazio quando não há nenhum
 * @since 0.3.1.1
 */
pub fn gate_test_indices(prog: checker::TProgram) -> []u64
```

Dois ajustes de lowering acompanham, ambos já nomeados por `gate-sem-c-0.3.0.31.md` §F2:

- `lower_item_function` (`src/lir/lower.tks`:8186) faz hoje `if f.is_test { return … }` — descarta
  todo corpo de teste. Sob modo gate deixa de descartar. **Consequência a dizer em voz alta: o
  backend próprio NUNCA foi exercitado sobre corpos de teste.** É risco não medido e é a primeira
  sonda da sequência (migalha 0).
- `lower_virtual_main` (`src/lir/lower.tks`:8218) sintetiza `main` a partir das statements soltas;
  sob modo gate, as soltas são descartadas e o `main` vem do sintetizador.

### 3.3 O emissor de C consome o MESMO `main`

`tk_emit_c_test(prog, cov)` deixa de ter um `main` próprio: `emit_test_main`/`emit_test_call`
(`codegen.tks`:10504/10593) são APAGADOS e a entrada passa a ser
`tk_emit_c_mode(gate_program(prog, plan), CgMode::Program)`.

É esta troca que torna a prova de §10 estrutural em vez de empírica. E é ela que respeita o ruling do
dono sobre o oráculo — *"se a versão C passa em um teste que falha nativo, tem que se apoiar no
emissor/codegen"*: quando as duas rotas correm o mesmo `main`, a única variável que sobra é o
`src/codegen/codegen.tks` contra o `src/lir` + `src/backend`, que é exactamente onde ele mandou
apoiar-se.

---

## 4. A LEI DA CASA — uma só, em duas materializações

As duas vias do dono são a mesma lei escrita em dois suportes:

> **A atribuição de um veredicto é pela CASA, nunca pelo FLUXO.** Quem executa nunca imprime; quem
> imprime nunca executa. A casa é identificada pelo ÍNDICE do teste, e o relatório é montado pelo pai
> percorrendo os índices por ordem, depois da barreira.

| | via 1 (processos) | via 2 (threads) |
|---|---|---|
| a casa é | um ficheiro por filho, cujo caminho o pai escolheu | uma posição `[]TestVerdict` indexada por `v.index` |
| o que atravessa | bytes num descritor | um valor de `TestVerdict` COPIADO para a região do recetor |
| a barreira é | `wait` do processo | `join` da thread |
| a ordem do relatório | índice da linha de regressão | `v.index`, nunca a ordem de chegada |

Isto **reconcilia** `docs/memory/parallel-test-harness-0.3.2.md` em vez de o substituir. O que aquele
documento chamou "canal de veredito com fallback para stderr" é a via 1 inteira, e continua válido
palavra por palavra — para os REGRESSIVOS. O que ele resolveu com processos para os unitários é
substituído por threads + `chan<T>`, por ruling directo (R1, R5). As duas correcções ao documento
antigo, e são só duas:

1. *"This language has no threads"* (§ "Why a PATH") deixa de ser premissa: R1 manda criá-las. O
   argumento do PATH continua a valer na via 1 pelos OUTROS dois motivos que ele próprio dá —
   sobrevive ao filho morrer a meio, e continua lá depois.
2. A auto-reexecução (`argv[1]` como selector) **está morta e não por escolha**: o `main` nativo é
   `new_func("main", 0, …)` (`lower.tks`:8219) e `tk_set_args` não é chamado em lugar nenhum de
   `src/lir` nem de `src/backend` — **um binário do backend próprio vê `teko::env::args()` VAZIO**.
   O desenho aqui não precisa de argv em sítio nenhum, e isso é deliberado.

---

## 5. VIA 1 — os regressivos, processos com descritores próprios

### 5.1 Quem cria os descritores, onde vivem, quem os fecha

O **pai** — o processo do runner — cria os quatro caminhos de uma linha de regressão ANTES de lançar
o filho, e é ele o dono de todos:

```
<prefix>.in     stdin  do filho   (escrito pelo pai antes do spawn)
<prefix>.out    stdout do filho
<prefix>.err    stderr do filho
<prefix>.chan   o TERCEIRO canal (R0/R3), só quando o pai o nomeia
```

**Onde vive o descritor, já que as arenas fecham ao fim do escopo:** não vive numa arena nenhuma. O
que a arena guarda é o *caminho* (um `str`), e o caminho é um valor do pai, criado antes do spawn e
vivo até à colheita. O descritor propriamente dito é uma alça do SISTEMA OPERATIVO, aberta dentro do
`spawn` e fechada por ele: o pai abre, duplica para os slots do filho, e fecha a sua cópia
imediatamente a seguir ao spawn — o filho fica com as dele. Nada atravessa a fronteira de arena
porque nada de arena atravessa a fronteira de processo.

**Quem fecha:** o `spawn` fecha as cópias do pai (senão o `.out` nunca vê EOF e um leitor futuro
bloqueia); o filho fecha as dele ao terminar, por acção do SO; o pai lê os ficheiros DEPOIS do `wait`.
Ninguém drena nada em concorrência, que é a propriedade que dispensa `select`/`poll` — e é o segundo
motivo, ainda de pé, para o canal ser um PATH e não um fd.

### 5.2 A superfície que falta, nomeada

Hoje `src/process/process.tks` tem exactamente duas funções, ambas SÍNCRONAS
(`run`, `run_quiet`), e nenhuma aceita redirecção escolhida pelo chamador — `run_quiet` redirige para
o dispositivo nulo, mas por dentro (`tk_rt_run_quiet`, `teko_rt.c`:1808, `_dup`/`_dup2` à volta do
`_spawnvp`). `src/io/io.tks` só tem ficheiro inteiro (`read_file`/`write_file`) e os quatro
escritores de stream padrão. **Não existe nenhuma peça de redirecção por processo filho exposta a
Teko.** É primitiva em falta e é a primeira da lista de §12.

```teko
/**
 * ProcHandle — uma alça de processo filho lançado e ainda não colhido.
 *
 * Nunca é um pid nu: no Windows o que se colhe é uma HANDLE e no POSIX um pid, e um `i64` cru
 * chamado `pid` em ambos seria mentira num deles. `TK_RT_SPAWN_FAILED` (-1) continua a ser o único
 * valor negativo e continua a significar "nenhum filho chegou a correr".
 *
 * @since 0.3.1.2
 */
pub type ProcHandle = struct {
    /** o valor opaco que o host devolveu; negativo significa que nenhum filho arrancou. */
    raw: i64
}

/**
 * spawn_redirected — lança `argv` SEM esperar por ele, com os três descritores padrão apontados aos
 * caminhos que o chamador escolheu.
 *
 * É esta função que materializa o ruling R3: o pai nomeia o destino, o filho escreve lá. Um caminho
 * vazio herda o descritor do pai, que é o que faz um `spawn_redirected` sem redirecção nenhuma ter
 * exactamente o comportamento de `run` sem a espera.
 *
 * `dir` é a directoria de trabalho do FILHO (o que o antigo `cd <dir> && ` fazia por shell) e `env`
 * são tokens `K=V` que se juntam ao ambiente herdado, sem tocar no ambiente do pai — a mesma
 * propriedade que `env_prefix_cmd` garantia por citação de shell.
 *
 * @param argv      o vector de argumentos; `argv[0]` é o executável
 * @param dir       a directoria de trabalho do filho ("" = a do pai)
 * @param env       tokens `K=V` acrescentados ao ambiente do filho ([] = herdar apenas)
 * @param in_path   o ficheiro ligado ao stdin do filho ("" = herdar)
 * @param out_path  o ficheiro ligado ao stdout do filho ("" = herdar)
 * @param err_path  o ficheiro ligado ao stderr do filho ("" = herdar)
 * @return          a alça do filho, ou uma alça com `raw` negativo quando nenhum filho arrancou
 * @since 0.3.1.2
 */
pub fn spawn_redirected(argv: []str, dir: str, env: []str, in_path: str, out_path: str, err_path: str) -> ProcHandle

/**
 * wait_one — bloqueia até `h` terminar e devolve o estado com que terminou.
 *
 * O estado é o do FILHO, com a mesma leitura que `run` já documenta: 0–255 para quem saiu, 128+signo
 * para quem foi morto por sinal no POSIX, e o código de saída no Windows, que não tem sinais. Um
 * número nunca identifica um panic — o panic identifica-se na sua linha de stderr
 * (`TK_PANIC_MARKER`).
 *
 * @param h  a alça devolvida por `spawn_redirected`
 * @return   o estado do filho, ou `TK_RT_SPAWN_FAILED` quando a alça nunca correspondeu a um filho
 * @since 0.3.1.2
 */
pub fn wait_one(h: ProcHandle) -> i32
```

O chão em C fica em `src/runtime/teko_rt.c` — o **C mantido**, a excepção explícita da lei Teko-only,
e o mesmo sítio onde `tk_rt_run` já faz `fork`/`execvp`/`waitpid` numa chamada só. Partir em dois
**não acrescenta uma syscall**: separa a espera.

### 5.3 Linux, macOS e Windows — o que muda e o que não

| | POSIX (Linux, macOS) | Windows |
|---|---|---|
| lançar | `fork` + `dup2` dos três fds + `execvp` (ou `posix_spawn` com `file_actions_adddup2`) | `CreateProcess` com `STARTUPINFO.hStdInput/hStdOutput/hStdError` e `bInheritHandle` |
| colher | `waitpid` | `WaitForSingleObject` + `GetExitCodeProcess` |
| **quantos slots de descritor há** | três, por convenção | **três, por estrutura** — `STARTUPINFO` tem exactamente esses campos |
| o terceiro canal | um PATH em `TEKO_VERDICT_CHANNEL` | o MESMO path, na mesma variável |

**Não existe um quarto stream padrão em nenhuma das duas plataformas**, e é por isso que o terceiro
canal é um CAMINHO e não um descritor: o `--status-fd` do GnuPG e o
`AnonymousPipeServerStream.GetClientHandleAsString()` do .NET são a mesma ideia com fichas
diferentes — o pai nomeia o destino e diz-lho por fora. Um caminho é a ficha que ambas as plataformas
já sabem ler, sobrevive ao filho morrer a meio, e fica lá para ser lido depois. (Correcção honesta ao
que o dono lembrou: o .NET não tem um quarto stream; tem uma maneira de PASSAR a alça. A forma
portável é sempre "o pai nomeia".)

**O fallback de R0 fica inteiro e é o que evita que o protocolo seja obrigatório:** sem
`TEKO_VERDICT_CHANNEL` no ambiente, o veredicto vai para **stderr**. Corre-se um binário de teste à
mão e vê-se o veredicto, sem flag nenhuma.

### 5.4 O que morre, e é muito

`sh_squote`, `sh_join`, `to_sh_path`, `windows_path_to_sh`, `windows_drive_to_mount`,
`captured_sh_cmd`, `cd_prefix_cmd`, `env_prefix_cmd`, `regr_batch_script`, `regr_batch_rc`,
`regr_rc_of_text`, `REGR_RC_SUFFIX`, `REGR_BATCH_UNREADABLE_RC` — a camada inteira de shell
(`src/build/regression.tks`:180–450). Existia porque o `sh` era o escalonador; com um lançador
próprio, deixa de haver `sh` para citar.

**E o ficheiro `.rc` morre com ela, por um motivo que é preciso dizer para não voltar:**
`parallel-test-harness-0.3.2.md` listou "o canal de veredito" como perigo nº1 porque um filho que
chama `exit(3)` legitimamente é indistinguível de um que falhou com 3. **Esse perigo é da metade
UNITÁRIA, não da regressiva:** numa regressão o código de saída ESPERADO é o contrato escrito no
`.tkr`, portanto o estado de saída É o veredicto e não há ambiguidade nenhuma a resolver. O `.rc`
existia por outra razão, puramente de shell — o estado de um filho em segundo plano não se lê do `$?`
depois do `wait`. Com `wait_one` a devolver o estado, o ficheiro não tem função.

O `.chan` sobrevive à morte do `.rc` para o que ele é realmente bom: **transportar o que um código de
saída não cabe** — a linha de panic, o índice em voo quando o filho morreu, e a imagem de cobertura.

### 5.5 O pool

```teko
/**
 * ProcPool — o lançador limitado da metade de regressões: mantém no máximo `jobs` filhos em voo,
 * colhe-os por ordem de ÍNDICE e devolve uma captura por linha, na ordem em que as linhas foram
 * dadas.
 *
 * A ordem de colheita não é a ordem de término, de propósito: ordenar por término é ordenar por
 * tempo, e uma saída ordenada por tempo não reproduz a corrida que a produziu.
 *
 * @since 0.3.1.2
 */
pub type ProcPool = struct {
    /** quantos filhos podem estar em voo ao mesmo tempo. */
    jobs: u64
    /** as alças em voo, indexadas pela linha a que pertencem. */
    inflight: []ProcHandle
}

/**
 * run_pool — corre `specs` em processos paralelos, no máximo `jobs` ao mesmo tempo, e devolve uma
 * captura por linha, na ORDEM DAS LINHAS.
 *
 * Substitui `run_captured_batch` inteira, com a mesma assinatura de intenção e sem uma linha de
 * shell. A janela é DESLIZANTE (colhe-se um, lança-se o seguinte), e não uma barreira a cada `jobs`
 * como o script de `sh` era obrigado a ser por falta de um `wait -n` portável: a cauda de cada lote
 * deixa de ser desperdiçada.
 *
 * @param specs  o que correr, uma entrada por linha de regressão
 * @param jobs   quantos filhos podem estar em voo (`regr_jobs()`)
 * @return       uma captura por linha, na ordem de `specs`
 * @since 0.3.1.2
 */
pub fn run_pool(specs: []ProcSpec, jobs: u64) -> []CapResult
```

`CapResult` fica com a forma que tem hoje (`exit`/`stdout`/`stderr`/`harness_ns`/`child_ns`/`cmd`), e
GANHA uma medição honesta que hoje não existe: com um `spawn` e um `wait` por filho, `child_ns` deixa
de ser a divisão do relógio do lote por N (`regr_batch_capture`, que documenta essa divisão como
partilha e não como medição) e passa a ser o intervalo real daquele filho.

---

## 6. VIA 2 — os unitários, threads no mesmo processo, `chan<T>`

### 6.1 A assinatura

Três palavras novas na linguagem, e apenas três. Ficam registadas contra o
`TEKO_MASTER_PLAN.md`:262, que proíbe congelar as cinco primitivas reservadas *"until parser + real
duplication data exist"*: **o dado existe agora e é o ruling R4 do dono**, que nomeia `chan<T>`
directamente e nomeia o caso de uso. `scope{}` e a forma de `spawn` como palavra-chave continuam
reservadas — nada aqui as exige.

```teko
/**
 * chan — um canal tipado de capacidade fixa entre threads do mesmo processo.
 *
 * O canal é criado pelo RECETOR e as suas casas vivem na região do recetor (§6.3). Um `send` COPIA
 * o valor para dentro dessa região; nenhum ponteiro para a região de quem envia atravessa a
 * fronteira, o que é o que torna o canal compatível com a lei R6 (sem `ref` entre threads) e com
 * arenas que fecham ao fim do escopo.
 *
 * @param T  o tipo transportado; tem de ser COPIÁVEL POR VALOR (§6.3)
 * @since 0.3.2
 */
pub type chan<T>

/**
 * chan_new — cria um canal com `cap` casas, na região de quem chama.
 *
 * Quem chama é o RECETOR, e essa não é uma convenção: as casas ficam na região do chamador, portanto
 * o canal não pode sobreviver a quem o criou, e quem o criou é quem vai drená-lo.
 *
 * `cap` é FINITA e obrigatória. Um canal sem limite trocaria um bloqueio visível por um crescimento
 * invisível de memória, que é o modo de falha que este projecto menos consegue ver.
 *
 * @param cap  quantas casas o canal tem; tem de ser maior que zero
 * @return     o canal, ou o erro quando `cap` é zero ou a região não consegue alojá-lo
 * @throws     quando `cap` é zero
 * @since 0.3.2
 */
pub fn chan_new<T>(cap: u64) -> chan<T> | error

/**
 * send — copia `value` para a próxima casa livre do canal e publica-a.
 *
 * BLOQUEIA enquanto não houver casa livre. A cópia acontece ANTES da publicação, e a publicação é um
 * passo único: um emissor que morra a meio da cópia não deixa uma casa meio-escrita visível ao
 * recetor — deixa uma casa não publicada, que é indistinguível de nunca ter enviado. É esta ordem
 * que faz a escolha de posse aguentar o panic (§6.5).
 *
 * @param c      o canal
 * @param value  o valor a transportar; é COPIADO, nunca referenciado
 * @return       nada em sucesso
 * @throws       quando o canal já foi fechado
 * @since 0.3.2
 */
pub fn send<T>(c: chan<T>, value: T) -> null | error

/**
 * recv — retira o valor publicado mais antigo, bloqueando enquanto não houver nenhum.
 *
 * Devolve `null` quando o canal está FECHADO e vazio — o "acabou" que distingue um canal esgotado de
 * um canal que ainda vai receber (§6.4). Devolve `error` só quando o canal nunca poderá progredir.
 *
 * @param c  o canal
 * @return   o valor mais antigo, ou `null` quando o canal está fechado e vazio
 * @since 0.3.2
 */
pub fn recv<T>(c: chan<T>) -> T | null

/**
 * chan_close — declara que nenhum `send` mais acontecerá neste canal.
 *
 * Fecha-o o RECETOR, e só ele (§6.4). Um `send` num canal fechado é `error`, nunca silêncio.
 *
 * @param c  o canal
 * @return   nada
 * @since 0.3.2
 */
pub fn chan_close<T>(c: chan<T>) -> void
```

### 6.2 O tipo transportado — o veredicto de um teste é um VALOR, não texto

```teko
/**
 * TestState — o que aconteceu a um `#test`, como facto e não como texto.
 *
 * Os estados são distintos NA ORIGEM, não por inspecção da mensagem: `Failed` é depositado por
 * `teko::assert`, `Panicked` pelo choke point de panic e `Exited` pelo de saída (§6.5). Distinguir
 * um do outro por prefixo de mensagem seria um trocadilho que parte no dia em que alguém escrever um
 * `panic("assertion failed: …")` à mão.
 *
 * @since 0.3.2
 */
pub type TestState = enum {
    /** o corpo do teste retornou normalmente. */
    Ok
    /** uma asserção de `teko::assert` falhou. */
    Failed
    /** o corpo entrou em `panic` (deliberado ou de guarda de runtime). */
    Panicked
    /** o corpo chamou `exit(code)`; `code` fica em `TestVerdict.code`. */
    Exited
    /** a thread desapareceu sem depositar nada — o caso silencioso, e é VERMELHO (§6.6). */
    Vanished
    /** o teste foi planeado e nunca chegou a arrancar (falha de criação de thread). */
    NotRun
}

/**
 * TestVerdict — tudo o que o pai precisa de saber sobre UM teste, num valor auto-contido.
 *
 * Auto-contido é a palavra que carrega o desenho: cada campo é um escalar ou um `str`, e o `send`
 * copia-os todos em profundidade para a região do recetor. Não há aqui nem uma referência, nem um
 * agregado aninhado, nem um fecho — porque nenhum deles sobreviveria ao fecho da arena da thread
 * emissora.
 *
 * @since 0.3.2
 */
pub type TestVerdict = struct {
    /** o índice do `#test` em `prog.items` — a CASA, e a chave de ordenação do relatório. */
    index: u64
    /** o nome qualificado do teste, tal como o rótulo o imprime. */
    name: str
    /** o que aconteceu. */
    state: TestState
    /** o código de `exit`, quando `state` é `Exited`; 0 nos restantes. */
    code: i32
    /** a mensagem da asserção ou do panic; "" quando o teste passou. */
    message: str
    /** o sítio (ficheiro:linha:coluna) que produziu a mensagem; "" quando não há. */
    site: str
    /** quanto tempo o corpo demorou, em nanossegundos monotónicos. */
    elapsed_ns: i64
    /** o que o corpo escreveu no seu stdout, capturado e replicado pelo pai por ordem. */
    out_text: str
    /** o que o corpo escreveu no seu stderr. */
    err_text: str
}
```

### 6.3 A DECISÃO CENTRAL — o `send` COPIA para a região do RECETOR

Três formas possíveis. A lei desempata sem consultar ninguém.

| forma | veredicto |
|---|---|
| **o canal guarda um ponteiro para o valor na arena do EMISSOR** | **REJEITADA.** As arenas fecham ao fim do escopo; a thread que envia morre logo a seguir; o recetor lê memória libertada. Sob panic é pior ainda: a arena fecha ANTES de o recetor chegar lá. |
| **o `send` MOVE o valor** | **REJEITADA.** Mover exige provar que o emissor não volta a tocar no valor — análise de posse linear que não existe. O `Spine` (`src/checker/spine.tks`) é hoje uma query pura que *"nothing consumes for a decision yet"*. Construir posse linear para entregar um harness é inverter a ordem do trabalho. E não sobrevive ao panic: um emissor que morre a meio de um move deixa um valor que nem é dele nem é do canal. |
| **o canal tem casas na região do RECETOR e o `send` COPIA para lá** | **ESCOLHIDA.** |

Os quatro argumentos, por ordem de força:

1. **Aguenta o panic, que é o teste que o coordenador impôs.** A cópia termina antes da publicação, e
   a publicação é um passo. Uma thread morta a meio de um `send` deixa uma casa não publicada; uma
   morta depois deixa um veredicto completo em memória que NÃO é a dela. Em nenhum dos dois casos o
   fecho da arena da thread toca no que o recetor vai ler.
2. **Não inventa lei de posse nova.** A lei já existe e é a mesma que a via 1 usa: *o que atravessa
   uma fronteira, atravessa por CÓPIA para memória que o outro lado já possui.* Uma lei, duas vias.
3. **Respeita R6 literalmente.** Nenhuma `ref` atravessa. O que a thread recebe é o canal, cujo
   interior ela nunca nomeia: ela chama `send` e o `send` copia.
4. **Torna a capacidade uma decisão de MEMÓRIA e não de sorte.** `cap` casas × `sizeof(T)` + o texto
   copiado é um limite superior que o pai escolheu.

**O preço, dito em voz alta:** `T` tem de ser COPIÁVEL POR VALOR — escalares, `str`, e structs
planas desses. Fecho, interface, referência e agregado com ponteiro interior são **recusa de
compilação**, não cópia rasa silenciosa. Uma cópia rasa de um fecho aliasaria o ambiente capturado, e
esse ambiente está na arena que vai fechar. É uma paragem honesta na checagem, com a lista de tipos
recusados escrita no diagnóstico.

**Quanto texto se copia:** `message`/`site`/`out_text`/`err_text` são `str`, portanto de comprimento
variável. A cópia é limitada por `TEST_TEXT_CAP` (um `const`, não um número mágico) e o excesso é
truncado com uma marca visível — porque um veredicto que faz o canal crescer sem limite é um harness
que morre de memória a relatar falhas, que é o pior momento possível para morrer.

### 6.4 Bloqueio, capacidade, fecho

- **Capacidade:** `cap = lanes + 1`. Uma casa por thread em voo, mais uma, para que uma thread nunca
  bloqueie no `send` à espera do pai — o pai drena entre lançamentos. `cap` finita é obrigatória
  (§6.1).
- **Bloqueio:** `send` bloqueia com o canal cheio; `recv` bloqueia com o canal vazio e não fechado.
- **Quem fecha:** o **RECETOR**, e só ele, depois de ter feito `join` a todas as threads que lançou.
  Nessa altura, por definição, não pode existir mais nenhum emissor. Um emissor a fechar o canal seria
  a corrida clássica ("quem fecha por último?") e aqui ela nem se põe, porque o recetor é único e a
  barreira é dele.
- **Como o recetor distingue "acabou" de "ainda vem mais":** NÃO é pelo fecho, e esta é a parte que
  interessa. O recetor sabe quantos testes despachou; ele conta. `recv` devolver `null` (canal fechado
  e vazio) é a confirmação, não a fonte. A fonte é o **balanço de esperados contra recebidos** (§6.6).
- **Se o recetor morrer antes de ler:** o processo inteiro morreu, portanto não há ninguém a quem
  responder. É o caso de §6.7, e resolve-se um nível acima, na fronteira de PROCESSO.
- **Se um emissor morrer antes de enviar:** §6.6. É o caso perigoso e tem resposta própria.

### 6.5 A CAPTURA DE `exit` E DE `panic` — as DUAS PRIMITIVAS NOVAS (R5, R7)

#### 6.5.1 Como está hoje, medido — e o `catch` global NÃO serve

```
src/runtime/teko_rt.h:611   _Noreturn void tk_panic(const char *msg);
src/runtime/teko_rt.h:614   _Noreturn void tk_panic_str(tk_str msg);
src/runtime/teko_rt.h:615   _Noreturn void tk_exit(int32_t code);
src/lir/lower.tks (call_symbol)   "panic" -> "tk_panic_str"   ·   "exit" -> "tk_exit"
```

`tk_panic_str` escreve `teko: deliberate panic: <msg>` em stderr, imprime backtrace e chama `abort()`
— SIGABRT, exit 134. `tk_exit` faz `tk_regions_free_all` e sai. **Os dois são `_Noreturn` e os dois
matam o PROCESSO.** É exactamente o que R7 diz por medição do dono — *"exit e panic hoje saem
limpo"* — e é o que `concorrencia-adiantada-s8.md` §4.1 já tinha registado.

**O "catch global" de hoje, localizado e julgado.** Ele existe e é `tk_rt_crash_handler`
(`src/runtime/teko_rt.c`:116), instalado por um `__attribute__((constructor))` sobre `SIGSEGV`,
`SIGBUS`, `SIGILL` e `SIGFPE`:

```c
static void tk_rt_crash_handler(int sig) {
    fputs("\nteko: FATAL signal — a generated program crashed (M.1).\n", stderr);
    tk_backtrace();
    _Exit(128 + sig);   // async-signal-safe
}
```

**Ele TRATA e não INTERROMPE** — imprime, e depois `_Exit`. É a descrição de R7 palavra por palavra,
e é por isso que **não pode ser a base**: observar não é conter. O mecanismo novo nasce **ao lado**
dele (o `crash_handler` continua a ser a última rede, para a falta de hardware que nenhum handler
Teko alcança, §6.6), e nunca por cima.

**Captura de saída não existe de todo.** Não há antecedente nenhum: `tk_exit` só sai.

#### 6.5.2 As duas primitivas — nome, assinatura, semântica

R7 fixa que são **duas primitivas novas**, e o desenho entrega exactamente duas CAPACIDADES:

| # | primitiva | o que contém |
|---|---|---|
| **P-A** | **captura de panic** | um `panic` levantado dentro de um corpo de teste guardado deposita `{mensagem, sítio}` e termina **a thread**, não o processo |
| **P-B** | **captura de saída** | um `exit(code)` levantado dentro de um corpo de teste guardado deposita `{code}` e termina **a thread**, não o processo |

**As duas são ARMADAS PELO MESMO PAR, e essa é uma decisão com argumento.** Um corpo de teste está
guardado ou não está; armá-las separadamente não tem caso de uso, e dois pares dobram as maneiras de
deixar um deles por desarmar — que é precisamente a falha que
`concorrencia-adiantada-s8.md` §4.2 já nomeou para `guard_lane`/`unguard_lane` (*"uma raia que retorna
sem desguardar deixa entrada morta na tabela, o que a próxima varredura leria como veredicto de outra
raia"*) e para a qual já reservou a fixture `thread_lane_unguard_pairs`. **Se o dono quis dizer dois
pontos de armar separados, é uma linha de mudança** — a semântica das duas capacidades não muda.

O par é o `guard_lane`/`unguard_lane` daquele desenho, com o nome ajustado ao que ele agora faz (não
guarda uma raia longa: guarda UM teste, porque R1 manda uma thread por `#test`):

```teko
/**
 * gate_guard_begin — arma, para a thread chamadora, a captura de panic (P-A) e a captura de saída
 * (P-B) do teste `index`.
 *
 * Enquanto armado, um `panic` ou um `exit` dentro deste corpo NÃO mata o processo: deposita o motivo
 * na linha desta thread na TABELA DE GUARDAS e termina a thread. Fora de qualquer guarda, `panic` e
 * `exit` comportam-se EXACTAMENTE como hoje, byte a byte — o exit 134 e a linha `TK_PANIC_MARKER`
 * são contrato afirmado por golden, e trocar o mecanismo sem trocar a saída é a única forma honesta
 * de fazer esta migração.
 *
 * O reconhecimento é por `sys_thread_self()` contra a tabela, e NÃO por armazenamento local de
 * thread: a linguagem não tem TLS, a tabela tem no máximo `lanes` linhas — uma dezena — e uma
 * varredura linear sobre ela custa menos do que introduzir TLS na linguagem para um caso que ainda
 * não mediu precisar. É a mesma escolha, e pelo mesmo motivo, que
 * `concorrencia-adiantada-s8.md` §4.2 já tinha ratificado.
 *
 * @param index  o índice do `#test` que esta thread vai correr — a CASA do veredicto
 * @return       nada
 * @since 0.3.2
 */
fn gate_guard_begin(index: u64) -> void

/**
 * gate_guard_end — desarma a guarda da thread chamadora: o caminho normal de saída de um teste que
 * terminou sem panic e sem `exit`.
 *
 * O par é OBRIGATÓRIO. Uma thread que retorna sem desarmar deixa uma linha morta na tabela, e a
 * varredura seguinte leria essa linha como o veredicto de outra thread. Afirmado por
 * `thread_lane_unguard_pairs`.
 *
 * @return  nada
 * @since 0.3.2
 */
fn gate_guard_end() -> void
```

**O que a guarda deposita, e onde.** A linha da tabela é `{thread_id, index, state, code, message,
site}` de largura FIXA, e a **tabela vive na região do PAI**, alojada antes de a primeira thread
nascer. Não pode viver na região da thread que morre — essa arena vai fechar. A ordem é obrigatória e
está no doc-comment: **copiar para a linha da tabela → fechar a região da thread → terminar a
thread.** Invertida, o `str` da mensagem é lido de memória já libertada.

#### 6.5.3 Onde vivem — Teko, com zero C novo

Três sítios possíveis, e a lei escolhe:

| sítio | veredicto |
|---|---|
| **na `main` gerada** | **IMPOSSÍVEL.** A `main` é quem CHAMA; `panic` não retorna. Não há ponto de retoma. |
| **em `teko_rt.c`, remendando `tk_panic_str`/`tk_exit`** | **REJEITADO**, e não por mim: `concorrencia-adiantada-s8.md` §4.2 já o recusou — *"é a direção errada: o romaneio já condena `teko_rt.c` à morte"*. E R7 diz que estas primitivas *"nascem no caminho nativo"*. |
| **em Teko, `src/runtime/teko_rt.tks`, alcançadas por uma arm de `call_symbol`** | **É AQUI.** É o mecanismo do desenho anterior, inteiro. |

Concretamente, o que `concorrencia-adiantada-s8.md` §4.2 já desenhou e que este documento **adopta
sem alterar**: `call_symbol` (`src/lir/lower.tks`) mapeia builtin → símbolo por tabela literal —

```
if last == "exit"  { return "tk_exit" }
if last == "panic" { return "tk_panic_str" }
```

— e esses dois destinos passam a ser funções **em Teko**, que implementam o comportamento guardado e
só tocam o host, no caminho NÃO-guardado, por `extern fn` para `write` e `abort` (o padrão que
`examples/probes/arena_bottom` provou). Resultado: o handler passa a ser Teko, `tk_panic_str` fica
órfão e sai junto com o resto do C. **Zero C novo, e o romaneio avança em vez de ser contornado.**

Divergência que registo por honestidade: a §6.5.2 da PRIMEIRA redacção deste documento propunha
remendar `teko_rt.c` com `_Thread_local`. **Estava errada e foi substituída** — divergia do desenho
anterior por desconhecimento, não por razão, e R7 (*"nascem no caminho nativo"*) fecha a questão.
O que sobrevive daquela análise é só o achado de §6.7 sobre a CLASSE DE ARMAZENAMENTO da raiz de
arena, que é outro assunto e continua a ser uma medição por fazer.

#### 6.5.4 "Conhecidas somente pelo compilador" — o mecanismo, não a intenção

Esta é a metade de R7 mais fácil de violar por acidente, e por isso é a que leva mais texto.

**O precedente existe e é EXACTAMENTE o oposto do que precisamos — e é preciso dizê-lo.** O
compilador já injecta funções que nenhum `.tks` declara: `builtin_fn` (`src/checker/scope.tks`:503+)
resolve `print`, `cov_enter`, `arena_push`, `intern_get`, … **pelo ÚLTIMO SEGMENTO do caminho**, o
que significa que qualquer `.tks` pode chamá-las por nome nu. Não são secretas; são globais sem
declaração. O doc-comment de `call_symbol` regista o preço que isso já custou: *"a user function whose
bare name happened to match a builtin (`arena_push`, `one_byte`, …) was silently emitted as the
RUNTIME symbol"*. **Portanto seguir esse precedente violaria R7.** Segui-lo seria pôr as duas
primitivas ao alcance de qualquer ficheiro.

O mecanismo que cumpre R7 tem três camadas, e nenhuma é uma convenção:

1. **Não entram em `builtin_fn`.** Um `.tks` que escreva `gate_guard_begin(0)` recebe
   `unknown function` do checker, pela mesma via por que qualquer nome inexistente o recebe. Isto é
   mecânico: a ausência de uma linha numa tabela.
2. **O seu caminho de origem é um NAMESPACE RESERVADO SEM MÓDULO — `teko::__gate`.** O sintetizador
   não passa pela resolução de nomes: constrói o `checker::TCall` já resolvido, com
   `call_ns = "teko::__gate"`, e `call_symbol` ganha uma arm para esse namespace. Um `.tks` não
   consegue produzir esse nó: não existe `src/__gate/`, portanto `use teko::__gate` não resolve.
3. **E uma REGRA DE PREFIXO RESERVADO no checker fecha o buraco que a camada 2 deixaria:** um
   segmento de caminho que comece por `__` é recusado em SOURCE, com diagnóstico próprio. Sem esta
   regra, alguém que criasse `src/__gate/` colidiria com o namespace sintetizado — e colidiria em
   silêncio, que é o modo de falha desta casa.

**O que não pode vazar, item a item:**

| superfície | vaza? | porquê |
|---|---|---|
| mensagens de erro do checker | **não** | o checker nunca as resolve, portanto nunca as nomeia |
| traço de panic (`.tsym`) | **não, POR CONSTRUÇÃO** | são emitidas SEM entrada na tabela `.tsym`; `tk_tsym_resolve` (`teko_rt.c`) só acrescenta um nome Teko quando o conhece, e não conhece estas. Um traço mostra o frame do runtime, nunca um nome Teko que não existe em ficheiro nenhum |
| cobertura | **não, e este é o que quase vaza** | a `main` sintetizada e as guardas NÃO são funções de produção. Têm de ser excluídas do DENOMINADOR (`count_prod_fns`, `src/coverage/coverage.tks`:557) por PROVENIÊNCIA, do mesmo modo que `strip_tests`/`filter_tkt` já excluem os `#test`. Sem isso, a percentagem de cobertura muda no dia em que o gate pousa, sem que uma linha de código de produção tenha mudado — e muda em silêncio |
| `teko --help`, documentação gerada, `.tsym` de release | **não** | só existem sob `GateShape`, isto é, só no binário de gate; o binário de release nunca as contém |

#### 6.5.5 `panic` × asserção falhada — distinguidos NA ORIGEM

Hoje `teko::assert::is_true` chama `panic("assertion failed: is_true")` (`src/assert/assert.tks`:21),
portanto uma asserção falhada É um panic e a única diferença é o texto. **Distinguir por texto é um
trocadilho** e parte no dia em que alguém escrever esse prefixo à mão.

A correcção é de UMA linha por asserção: `teko::assert::*` passa a chamar um choke point próprio.

```teko
/**
 * assert_fail — o ponto único por onde uma asserção falhada termina o teste corrente.
 *
 * Distinto de `panic` DE PROPÓSITO e na origem: uma asserção falhada é `TestState::Failed`, um panic
 * é `TestState::Panicked`, e o pai nunca precisa de inspeccionar a mensagem para saber qual foi.
 *
 * Fora de um teste guardado, o comportamento é byte-idêntico ao `panic` de hoje — a mesma linha
 * `TK_PANIC_MARKER`, o mesmo exit 134 — porque essa saída é contrato afirmado por golden.
 *
 * @param message  a mensagem da asserção que falhou
 * @param site     o ficheiro:linha:coluna da asserção
 * @return         nada; a chamada não retorna nem para o teste nem para o processo
 * @since 0.3.2
 */
pub fn assert_fail(message: str, site: str) -> void
```

#### 6.5.6 `exit` dentro de um teste — o RECORD distingue, a POLÍTICA decide

O dono nomeou a diferença: *"o panic é uma falha do teste, o `exit` pode ser o teste a testar
precisamente que algo sai com um código."* O desenho separa as duas perguntas:

- **O REGISTO distingue sempre:** `state = Exited`, `code = <o código>`. Nunca se perde qual foi.
- **A POLÍTICA, hoje, é VERMELHA.** O contrato de um `#test` é "passa se retornar normalmente"; um
  teste que termina o processo não retornou. Verde por omissão seria transformar um acidente
  (código sob teste que chama `exit`) num teste que passa sem ninguém reparar.
- **O gancho para o inverter fica NOMEADO e não desenhado:** um atributo `#test(expect_exit = N)`
  é a superfície natural para um teste que quer AFIRMAR uma saída. Não é inventado aqui porque
  nenhuma falha medida o exige ainda, e o `MASTER_PLAN` manda não congelar o que não precisa.

### 6.6 A thread que morre sem escrever nada — o caso perigoso

Depois de capturados o panic e o `exit`, sobra o desaparecimento puro: SIGSEGV, SIGBUS, divisão
inteira por zero ao nível da CPU, `abort()` de dentro da libc. Nenhum handler Teko corre antes disso.

**A regra, e ela é estrutural e não depende de temporizador nenhum:**

> O pai sabe, ANTES de lançar seja o que for, o conjunto exacto de índices que vai despachar
> (`gate_test_indices`). Ele aloja `[]TestVerdict` com uma casa por índice, todas inicializadas a
> `TestState::Vanished`. Cada veredicto drenado do canal SUBSTITUI a casa do seu `index`. Quando a
> barreira fecha, **toda a casa que continua `Vanished` é VERMELHA e é impressa como tal, nomeando o
> teste.**

Silêncio não pode contar como verde, e aqui não conta: o verde exige uma escrita positiva. A
contagem esperados-contra-recebidos é a mesma regra dita por outras palavras, e o relatório imprime as
duas (`N testes, M veredictos recebidos`) porque um desencontro é ele próprio um diagnóstico.

**Porque não há temporizador:** um `recv` com timeout transforma um teste lento numa falha
intermitente, que é o formato de falha que este projecto menos consegue depurar. O limite é o `join`:
o recetor não drena para sempre, drena até ter feito `join` a todas as threads que lançou. Uma thread
que trava para o `join` — e isso é um bloqueio real, não um silêncio, com o binário todo parado, o
que é visível. O limite de tempo pertence ao passo de CI que corre o binário, não à primitiva.

**E a recuperação do que se perdeu, que é o que a via 1 traz de volta para dentro da via 2:** antes de
lançar o índice `i`, o `main` gerado escreve a linha *"iniciei `i`"* no **canal de veredicto**
(§5.1/§5.3 — um caminho, um ficheiro, o mesmo mecanismo de R0). Se o processo INTEIRO morrer, o pai —
que é o processo do compilador, um nível acima — lê o ficheiro, sabe exactamente que índices estavam
em voo, e **reexecuta apenas esses, com `lanes = 1`**. Cada um recebe então o seu veredicto próprio.
Custo pago só quando há queda; "relata tudo" preservado; nenhum handler de sinal.

### 6.7 As arenas, sob panic

Três regiões, três donos, e a distinção é o que faz o desenho aguentar:

| região | dono | quando fecha | o que lá vive |
|---|---|---|---|
| a do PAI (o fio principal) | o processo | no fim do gate | o `[]TestVerdict`, as casas do canal, as áreas de estágio |
| a de CADA THREAD | a thread | quando a thread termina, normal OU por panic | tudo o que o corpo do teste alojou |
| — | — | — | — |

Hoje a raiz é uma só e sem alça: `static tk_region *tk_g_root` (`teko_rt.c`:940), e `tk_arena_push`/
`tk_arena_pop` empilham marcas num vector global (`static tk_arena_mark tk_arena_marks[64]`,
`teko_rt.c`:1341). Duas threads a fazer isso concorrentemente rebobinam o ponteiro de bump uma sobre a
outra, **e o sintoma é NENHUM** — o próprio cabeçalho do runtime regista que reuso de arena é
invisível ao ASan. É o perigo silencioso nº1 de `concorrencia-adiantada-s8.md` §8.1.

**Achado desta carga, e ele estreita muito o pré-requisito.** Aquele documento classificou "raiz de
região por tarefa" como crumb caro e bloqueante (C4), presumindo um redesenho de API. A leitura do
código diz outra coisa: `tk_g_root`, `tk_g_regs`, `tk_arena_marks` e o índice de marca são
**variáveis com classe de armazenamento estática**, e as assinaturas de `tk_region_root()`,
`tk_arena_push()` e `tk_arena_pop()` **não têm parâmetro nenhum a acrescentar** — passam a ser por
thread mudando a CLASSE DE ARMAZENAMENTO (`_Thread_local` no POSIX, `__declspec(thread)` no MSVC).
Nenhum chamador muda. **Isto é uma hipótese a MEDIR (migalha 0d), não um facto**, porque a lista de
globais do runtime é maior do que estas quatro (a tabela de internamento, as posições de diagnóstico
de cast, os sinks de cobertura) e cada uma tem de ser julgada por si. Mas se se confirmar, o
pré-requisito mais caro do desenho anterior custa uma linha por variável.

**Quem fecha a arena de uma thread que entrou em panic:** o guarda, na ordem de §6.5.3 — copiar o
motivo para o estágio do PAI, depois `tk_regions_free_all` **daquela thread**, depois terminar a
thread. E o veredicto que já foi para o canal sobrevive porque **nunca esteve nessa região**: está
nas casas do canal, que são do recetor. Era este o teste que a escolha de §6.3 tinha de passar.

### 6.8 As duas vias coexistem — e não, a leitura simples não se aguenta inteira

A leitura do coordenador — *descritores para regressões, `chan<T>` para unitários* — está **certa
como atribuição** e é agora ruling (R5). O que ela não cobre, e é preciso dizer:

**A metade unitária precisa das DUAS.** `chan<T>` transporta o veredicto entre THREADS; mas o binário
de gate é ele próprio um processo filho do compilador, e a fronteira compilador↔binário-de-gate é uma
fronteira de PROCESSO. Por ela atravessam: o registo "iniciei o índice i" que salva a corrida quando o
processo morre (§6.6), a imagem de cobertura (§7) e o veredicto final. Essa travessia é via 1.

Portanto: **`chan<T>` é o transporte de dentro do binário de gate; os descritores são o transporte
entre o compilador e o binário de gate.** Não competem, encaixam. A regressão usa via 1 nas duas
fronteiras porque só tem uma.

### 6.9 O harness nasce na via 1 e migra para a via 2 sem se escrever duas vezes?

**Sim, e é essa a razão de `GateShape` existir (§3.2).**

- **`GateShape::Serial`** (0.3.1.1) não usa canal nenhum: um só fio, os veredictos vão para o mesmo
  `[]TestVerdict` indexado por `index`, e o relatório é impresso pela MESMA função.
- **`GateShape::Threaded`** (0.3.2) troca o sítio onde a casa é preenchida — em vez de o corpo do
  `main` escrever directamente, uma thread envia e o `main` drena — e **mais nada muda**: o mesmo
  `TestVerdict`, o mesmo `[]TestVerdict`, o mesmo relatório, a mesma ordenação, o mesmo balanço de
  esperados.

A condição para isto se aguentar é escrever a metade serial **contra o tipo `TestVerdict` desde o
primeiro dia**, e nunca contra um `print` directo. É a única disciplina que a migalha 4 tem de
respeitar, e é o que impede o harness de ser escrito duas vezes.

Aviso de forma, medido: **não usar uma interface para abstrair o sumidouro.** O caminho nativo tem uma
paragem conhecida e por fechar em `fat-pointer interface-dispatch result not yet lowered`. O
sintetizador escolhe a forma; não há despacho dinâmico nenhum.

### 6.10 A OBJECÇÃO ANTERIOR A `channel<T>` — citada, e respondida

`concorrencia-adiantada-s8.md` §3.3 deixou `channel<T>` deliberadamente DE FORA, e o argumento é
concreto. Literal:

> *"**`channel<T>` merece nota própria, porque a análise mudou o desenho.** Nenhum dos três ganhos que
> o owner nomeou precisa de canal: gate, codegen e regressor são todos **fork-join sobre um intervalo
> de índices, com escrita disjunta e leitura após barreira**. Canal é a primitiva de comunicação
> *durante* a execução, e comunicação durante a execução é precisamente o que introduz ordem
> dependente de tempo. Congelar `channel<T>` agora seria congelar a peça que os casos reais não usam
> — e a única que ameaça o determinismo. Fica reservada com uma razão escrita, não por omissão."*

**O dono decidiu o contrário (R4, R5) e a decisão dele vale.** Mas a objecção não desaparece por
haver ruling: ela nomeia um perigo REAL — *ordem dependente de tempo* — e o documento tem de dizer
como é que ele não morde. A resposta tem três partes e nenhuma é disciplina de programador.

**(a) O canal transporta, não ordena.** A ordem em que `recv` devolve valores É a ordem do
escalonador e é não-determinística. O desenho nunca a usa: o recetor drena para
`verdicts[v.index]` — a CASA — e imprime percorrendo `0..count`. É a regra 4 de §8. **A saída é a
mesma com 1 thread e com 16 porque a ordem de impressão não é a ordem de execução**, exactamente o
mesmo argumento com que §6.1 daquele documento defendeu o fork-join.

**(b) O canal não é "comunicação DURANTE a execução".** A objecção visa o padrão em que raias
conversam entre si e uma decisão depende de quem falou primeiro. Aqui não há nada disso: os
emissores nunca leem do canal, nunca se veem uns aos outros, e o único recetor só age **depois da
barreira de `join`**. É fork-join com um transporte diferente — a topologia é a mesma que a objecção
aprovou; só a casa deixou de ser um endereço cru e passou a ser um valor tipado.

**(c) O que o canal COMPRA e que a casa crua não compra**, e é por isso que a troca não é neutra:
com escrita disjunta em memória partilhada, o veredicto de um teste que entra em `panic` teria de ser
escrito **pela thread moribunda** num bloco do pai, o que obriga a raciocinar sobre uma escrita a
meio de uma morte. Com o canal, a publicação é um passo único e anterior à morte (§6.3): ou o
veredicto foi publicado inteiro, ou não foi publicado — e "não foi publicado" tem resposta própria e
vermelha (§6.6). **A escolha do dono é a que aguenta o panic melhor**, e essa é a prova real, não a
elegância.

**O que continua verdade da objecção, e fica como guarda:** um `chan<T>` cuja ordem de `recv` fosse
usada para QUALQUER decisão — ordenar saída, atribuir cobertura, escolher o primeiro erro a reportar
— partiria o fixpoint e os goldens. Por isso a regra é escrita como proibição e não como conselho:
**a ordem de chegada ao `recv` não pode influenciar nenhum byte de saída.** É afirmada por
`gate_lanes_output_identical` e por `gate_coverage_lanes_identical`.

### 6.11 O NOME — `channel<T>` × `chan<T>`. NÃO escolho.

O desenho anterior escreve **`channel<T>`** (`concorrencia-adiantada-s8.md` §3.3, e o
`TEKO_MASTER_PLAN.md`:262 reserva-o com essa grafia). O dono escreveu hoje **`chan<T>`** (R4).

Este documento usa `chan<T>` por ser a grafia do ruling mais recente, **e regista que a divergência
existe e é do dono para o dono**. As duas grafias não podem coexistir: uma palavra-chave com dois
nomes é a próxima linha de `_ =>` à espera de acontecer.

**PERGUNTA AO DONO, e é a única deste documento:** `chan<T>` substitui `channel<T>` no
`TEKO_MASTER_PLAN.md`, ou `chan` é abreviatura de conversa e a palavra congelada é `channel`? Uma
palavra ou a outra — o desenho não muda em nada além do token.

---

## 7. Cobertura sob paralelismo

Factos: `cov_enter(idx)`/`cov_leave()` marcam "o teste corrente" num sink de PROCESSO
(`tk_cov_ids`/`tk_cov_n`/`tk_cov_cap`, `teko_rt.c`:1977-1979); `cov_mark`, `cov_line_at` e
`cov_branch_at` escrevem tabelas globais. Sob threads paralelas, a atribuição de uma linha ao teste
que a executou passa a ser um sorteio.

**A decisão, e ela é possível porque a pergunta do PISO não é a pergunta da ATRIBUIÇÃO:**

1. As três métricas de piso (funções/linhas/ramos) são a **UNIÃO** sobre a corrida inteira. União é
   independente de ordem e é monótona. Logo: **os sinks passam a ser POR THREAD** (a mesma mudança de
   classe de armazenamento de §6.7) e o pai funde-os depois da barreira, por **ORDEM DE ÍNDICE**, com
   união. `coverage::coverage_pct`/`line_coverage_pct`/`branch_coverage_pct` leem os sinks fundidos
   sem uma linha de mudança.
2. A fusão por ordem de índice não é necessária para a união ser correcta (é comutativa) — é para que
   qualquer consumidor futuro que NÃO seja comutativo não introduza intermitência sem ninguém reparar.
3. **A atribuição POR TESTE melhora, não piora.** O sink de uma thread É a cobertura de um teste, o
   que é exactamente o que `--per-test-cov` hoje obtém reexecutando um binário por teste. Essa
   melhoria não é reclamada nesta carga: `--per-test-cov` fica no caminho actual até a migalha 15.
4. **O depósito para o pai**: `tk_cov_dump(const char *)` continua sem forma chamável honesta de Teko
   (o `char*` × `tk_str`, `gate-sem-c-0.3.0.31.md` §4.2(e)). **A tensão dissolve-se e não se decide:**
   a imagem dos sinks fundidos passa a ser escrita **pelo `main` gerado, em Teko**, no canal de
   veredicto (§5.3) — `teko::io::write_file`, que já existe e já baixa nativamente. Não há `char*`
   nenhum a alcançar. A decisão que `gate-sem-c` elevou ao dono deixa de ser necessária, o que é o
   mesmo desfecho que `concorrencia-adiantada-s8.md` §10.1 previu.
5. **A guarda, e é barata:** o relatório de cobertura com `lanes=1` e com `lanes=N` tem de ser
   BYTE-IDÊNTICO. Qualquer diferença é defeito, não ruído. É fixture.

---

## 8. Determinismo

Quatro regras, todas estruturais — nenhuma é disciplina de quem escreve testes.

1. **Índice estático.** O índice de um teste é a sua posição em `prog.items`. Não há roubo de
   trabalho, não há reordenação por duração, não há afinidade.
2. **Escrita disjunta.** Cada teste escreve numa casa que é só sua. Não há acumulador partilhado, nem
   contador, nem lista com append concorrente.
3. **Quem executa não imprime.** O corpo de um teste escreve para o seu próprio `out_text`/`err_text`
   (capturados) e nunca para o stdout do processo. O pai imprime, depois da barreira, percorrendo
   `0..count`.
4. **O canal é transporte, não ordem.** A ordem de chegada ao `recv` é a ordem do escalonador. O
   recetor drena para `verdicts[v.index]` e ordena pelo índice. **Nunca imprimir na ordem de
   chegada** — é a única forma de a saída ser a mesma com 1 thread e com 16.

Para a via 1 a regra é a que já existe: o relatório é dobrado por índice de linha depois de todos os
filhos serem colhidos, que é o que `run_captured_batch` já faz e que `run_pool` herda.

**Afirmado por:** `gate_lanes_output_identical` — a mesma suíte com `lanes` 1, 4 e 16, stdout
comparado por diff entre as três; exit 0 nas três e diffs vazios.

### 8.1 O FIXPOINT — o que estas peças lhe devem

As duas primitivas novas (§6.5) e o sintetizador (§3) passam a estar **dentro do compilador que se
reconstrói a si próprio**, portanto entram em `gen2 == gen3` byte-idêntico. Três exigências, e a
primeira já apanhou um erro na primeira redacção deste documento:

1. **NENHUM valor lido do ambiente pode influenciar bytes emitidos.** `TEKO_TEST_LANES` e
   `TEKO_REGR_JOBS` são lidos pelo `main` gerado **em tempo de execução** e nunca cozidos como
   literais em `gate_program`. A primeira versão de `GatePlan` levava um campo `lanes: u64`; com ele,
   o mesmo fonte compilado num host de 4 núcleos e noutro de 64 emitiria bytes DIFERENTES, e
   `gen2 == gen3` partiria por uma razão que não tem nada a ver com o compilador — e o diff apontaria
   um número, nunca a causa. É a mesma família dos nomes ordinais de símbolo que
   `concorrencia-adiantada-s8.md` §6.2 catalogou. **Corrigido: `GatePlan` só carrega a FORMA, e a
   forma é política do compilador.**
2. **A tabela de guardas tem largura decidida em execução**, pelo mesmo motivo. Ela é alojada pelo
   `main` gerado com `lanes` linhas; `lanes` vem do ambiente; o CÓDIGO que a aloja é o mesmo em todo
   o host.
3. **O sintetizador é uma função pura da AST tipada.** Não lê relógio, não lê ambiente, não lê
   sistema de ficheiros, e percorre `prog.items` na ordem em que os recebe. Duas execuções sobre a
   mesma árvore produzem o mesmo `main`, item a item.

**E o aviso que o fixpoint não dá:** ele compara dois binários do MESMO compilador, portanto **é cego
a uma miscompilação determinística** — foi exactamente assim que a variante DECLARADA sobreviveu a
todos os portões. Um fixpoint verde não é prova nenhuma de que os veredictos estão certos; quem prova
isso é §10, e só até 0.3.1.4.

---

## 9. `TEKO_REGR_JOBS` e o grau de paralelismo — decidido onde

**Decidido no BINÁRIO, lido do AMBIENTE, nunca de argv.** Não é preferência: um binário do backend
próprio vê `teko::env::args()` vazio (§4), portanto argv não é um canal disponível. O ambiente é
também o que o doc-comment de `REGR_JOBS_ENV` já argumenta (*12-Factor: a concorrência da máquina é
assunto da máquina*).

**Dois botões, porque os dois recursos limitantes são diferentes** — e é este o argumento, não
"dois porque são duas metades":

| botão | metade | omissão | recurso limitante |
|---|---|---|---|
| `TEKO_REGR_JOBS` | processos (regressões) | **4, inalterado** | **MEMÓRIA** — cada filho é um `teko build` inteiro com a sua arena; o vagão mediu 1,3 GB de pico |
| `TEKO_TEST_LANES` | threads (unitários) | `hardware_parallelism()` | **NÚCLEOS** — cada teste é curto e a sua arena é pequena |

`TEKO_REGR_JOBS` **mantém nome, valor de omissão e significado**, porque é um botão publicado que
pernas de CI já definem; renomeá-lo partiria configurações que ninguém está a rever nesta carga. O
que muda é só QUEM o lê: deixa de ser "quantos `&` antes de um `wait`" e passa a ser "quantos filhos o
pool mantém em voo".

```teko
/**
 * PARALLELISM_ENV_REGR — a chave de ambiente do grau de paralelismo da metade de PROCESSOS.
 */
const PARALLELISM_ENV_REGR: str = "TEKO_REGR_JOBS"

/**
 * PARALLELISM_ENV_LANES — a chave de ambiente do grau de paralelismo da metade de THREADS.
 */
const PARALLELISM_ENV_LANES: str = "TEKO_TEST_LANES"

/**
 * parallelism_of — o grau de paralelismo escrito em `raw`, ou `fallback` para tudo o que não seja um
 * decimal positivo.
 *
 * Uma ÚNICA função de leitura para os dois botões, para que um valor malformado tenha exactamente um
 * comportamento em todo o projecto. Um valor malformado é um erro de CONFIGURAÇÃO, e a leitura segura
 * de um é "quem escreveu isto não queria dizer zero".
 *
 * @param raw       o valor tal como está no ambiente
 * @param fallback  o valor a usar quando `raw` é vazio, não-numérico ou zero
 * @return          o grau efectivo, nunca zero
 * @since 0.3.1.2
 */
fn parallelism_of(raw: str, fallback: u64) -> u64

/**
 * hardware_parallelism — quantas threads o host consegue executar de facto em paralelo.
 *
 * @return  o número de processadores em linha, nunca menor que 1
 * @since 0.3.2
 */
pub fn hardware_parallelism() -> u64
```

---

## 10. A PROVA DE EQUIVALÊNCIA — e ela é parte do desenho, não um seguimento

O dono fixou a rota C como referência de comportamento. A prova de que o harness novo dá **exactamente
os mesmos veredictos** tem quatro camadas, e a **ordem entre elas é obrigatória**: cada uma isola uma
variável, e trocá-las torna qualquer diferença ambígua.

| # | camada | o que prova | como |
|---|---|---|---|
| **P1** | **estrutural** | o harness não é uma variável | um só sintetizador (`gate_program`), duas rotas de código. A diferença entre rotas é, por construção, só a geração de código |
| **P2** | **golden, antes×depois, MESMA rota** | o sintetizador não mudou nada | o stdout do binário de gate na rota C, com o `main` gerado, **byte-idêntico** ao do `main` emitido à mão de hoje |
| **P3** | **diferencial, rota×rota, SERIAL** | o nativo concorda com o oráculo | a mesma suíte com `TEKO_BACKEND=c` e sem ele, ambos `lanes=1`; linhas de veredicto byte-idênticas |
| **P4** | **serial×paralelo, MESMA rota** | o paralelismo não mudou veredicto nenhum | `lanes` 1, 4, 16; stdout e relatório de cobertura byte-idênticos |

**A ferramenta:** `scripts/gate_route_equivalence.sh`, que corre os dois lados, normaliza só o que é
legitimamente variável (durações), compara linha a linha e **nomeia o PRIMEIRO teste divergente**.
"O gate falhou" não é um resultado utilizável; "o teste `lvt_x` deu ok em C e Failed em nativo" é.

**A regra de arbitragem já existe e não é inventada aqui** — A REGRA DO ORÁCULO
(`docs/memory/0.3.1.0-linux-native-first-stop.md`): *enquanto a rota C existir, qualquer divergência
de COMPORTAMENTO entre nativo e C é um defeito do nativo até prova em contrário.* E a excepção
medida também fica: o oráculo é um prior forte, não escritura — há já um caso registado em que o
nativo está certo e a rota C recusa (`variant Box | null`).

**A prova é PERECÍVEL.** A rota C morre na 0.3.1.4. P2 e P3 têm de ser executadas e o seu resultado
arquivado antes disso. Depois, P4 é a única que sobrevive — e P4 sozinha não prova equivalência com
coisa nenhuma, só consistência interna.

---

## 11. A SEQUÊNCIA DE MIGALHAS

**Ponto ritual** = onde o gate completo tem de passar: `teko test .` verde + fixpoint `gen2 == gen3`
byte-idêntico + `sh scripts/no_emitted_c.sh`. Migalhas de sonda e de fixture saltam o fixpoint.

| # | migalha | entrega | ritual |
|---|---|---|---|
| **0** | **SONDAR, não construir** | (a) o backend próprio baixa um corpo de `#test`? (`lower.tks`:8186 descarta-os — nunca foi exercitado); (b) um `extern fn` com DOIS parâmetros `[]str` mais quatro `str` baixa nativamente? (o `spawn_redirected` de §5.2 depende disso); (c) `teko::io::write_file` baixa nativamente no binário de gate?; (d) **`_Thread_local` em `tk_g_root`/`tk_g_regs`/`tk_arena_marks` basta, ou a API precisa de alça?** (§6.7) — mede, não presumas | não |
| **1** | **o gate OBEDECE ao backend** | `run_native_gate` → `run_test_gate`, despacho por `Backend`: nativo → `.o` + `link_object`, zero `.c`; `c` → o caminho de hoje, só sob `TEKO_BACKEND=c`. **É a peça que fecha R2 e não depende de nada** | **sim** |
| **2** | **a catraca de zero C dentro do compilador** | o gate regista os caminhos que escreveu; se algum termina em `.c` com backend nativo, a sessão FALHA. `no_emitted_c.sh` fica como rede exterior, com baseline VAZIA sob nativo | **sim** |
| **3** | **builtin `flush_out`** | `tk_flush_out` existe no runtime (`teko_rt.h`:183) e não é builtin do checker; o `main` gerado consome-o (o rótulo tem de sair do buffer antes do corpo) | **sim** |
| **3b** | **o namespace reservado `teko::__gate` e a regra de prefixo `__`** | a arm de `call_symbol` para o namespace sintetizado; a recusa em SOURCE de qualquer segmento começado por `__`, com diagnóstico próprio. **Sem isto, "conhecidas somente pelo compilador" (R7) é intenção e não mecanismo** (§6.5.4) | **sim** |
| **4** | **`src/build/gate.tks` — o `main` SERIAL sintetizado** | `gate_program`/`gate_test_indices`/`GatePlan`/`TestVerdict`/`TestState`; `lower_item_function` deixa de descartar `is_test` sob modo gate; `lower_virtual_main` toma as statements sintetizadas; a `main` sintetizada é excluída do DENOMINADOR de cobertura por proveniência. **Escrito contra `TestVerdict` desde já (§6.9)**; `GatePlan` NÃO carrega `lanes` (§8.1) | **sim** |
| **5** | **a rota C consome o MESMO `main`** | `emit_test_main`/`emit_test_call` APAGADOS; `tk_emit_c_test` = `tk_emit_c_mode(gate_program(...))`. **P2 é o portão desta migalha** | **sim** |
| **6** | **P3 — o diferencial rota×rota, serial** | `scripts/gate_route_equivalence.sh` + o resultado arquivado. Pode FALHAR, e falhar é o produto: cada divergência é um defeito do nativo com endereço | não |
| **7** | **`spawn_redirected` + `wait_one`** | a divisão de `tk_rt_run` em `src/runtime/teko_rt.c`; `ProcHandle`; os três descritores por caminho; POSIX e Win32 | **sim** |
| **8** | **`run_pool` — a metade de regressões** | janela deslizante, colheita por índice, `child_ns` real. `run_captured_batch` reescrita por dentro, mesma assinatura de intenção | **sim** |
| **9** | **a morte do andaime de shell** | os treze símbolos de §5.4 apagados, `.rc` incluído. É o passo que o `parallel-test-harness-0.3.2.md` prometeu | **sim** |
| **10** | **o terceiro canal, com fallback** | `TEKO_VERDICT_CHANNEL`; sem ele, o veredicto vai para stderr (R0). Um registo por linha, com etiqueta de tipo (veredicto · início-de-índice · imagem de cobertura) | **sim** |
| **11** | **`cabi fn(T…) -> R` em parâmetro de `extern fn`** | coerção do nome nu de uma função de topo não-capturante para `LFuncAddr`; recusa de capturante, genérica, método e tipo não representável em ABI C. Já desenhado em `concorrencia-adiantada-s8.md` C1 — **não redesenhar** | **sim** |
| **12** | **o chão de thread** | `pthread_create`/`join`/`exit`/`self` e os gémeos Win32 como `extern fn` sob `#os` | **sim** |
| **13** | **raiz de arena e sinks de cobertura POR THREAD** | o que a migalha 0(d) tiver medido. **BLOQUEANTE:** nenhuma migalha posterior pousa antes desta | **sim** |
| **13b** | **`panic`/`exit` reimplementados em TEKO** | `call_symbol` deixa de apontar a `tk_panic_str`/`tk_exit` e passa a apontar a funções Teko em `src/runtime/teko_rt.tks`, cujo fundo NÃO-guardado é `extern fn` para `write`/`abort`. **Zero C novo.** Saída byte-idêntica, exit 134 preservado — é a pré-condição das duas primitivas e é `concorrencia-adiantada-s8.md` C3 verbatim | **sim** |
| **14** | **as DUAS PRIMITIVAS — captura de `panic` (P-A) e de `exit` (P-B)** | `gate_guard_begin`/`gate_guard_end` no namespace reservado; a tabela de guardas na região do pai, varrida por `sys_thread_self()`; `teko::assert::assert_fail`. Fora de guarda, comportamento de hoje byte a byte | **sim** |
| **15** | **`chan<T>`** | `chan_new`/`send`/`recv`/`chan_close`; a cópia para a região do recetor; a recusa de compilação para `T` não copiável | **sim** |
| **16** | **`GateShape::Threaded`** | o `main` gerado lança uma thread por teste, no máximo `lanes` em voo, drena o canal, ordena por índice, imprime. Balanço de esperados; `Vanished` é vermelho | **sim** |
| **17** | **P4 + a reexecução serial pós-queda** | `lanes` 1/4/16 byte-idênticos, cobertura incluída; e a política de reexecutar com `lanes=1` os índices que o canal diz que estavam em voo quando o processo morreu | **sim** |

**Onde a paragem é dura:** as migalhas 1 a 10 são executáveis HOJE — nenhuma precisa de primitiva de
linguagem que não exista. As migalhas 11 a 17 dependem de `cabi fn`, que não existe. E **correr o
binário de gate do PRÓPRIO compilador pela rota nativa depende do auto-build nativo**, que hoje para
no degrau 8 (`MInst`, variante DECLARADA em `push_box_bytes`). Isso **não bloqueia** as migalhas
1 a 17: todas se provam em projectos de sonda (`examples/probes/`, `examples/regressions/own_native/`),
que é o veículo que os degraus 3 a 7 já validaram. O que bloqueia é só o gate do compilador sobre si
próprio, e esse desbloqueia-se no vagão do backend, não aqui.

---

## 12. Fixtures de regressão

Todas no caminho nativo, um par entrada → código de saída. (Não há segundo motor: o corpus corre no
nativo, e a rota C só aparece nas fixtures de EQUIVALÊNCIA, que a nomeiam explicitamente.)

| fixture | forma | esperado |
|---|---|---|
| `gate_honours_backend_native` | `teko test .` num projecto de sonda com `TEKO_BACKEND` não definido; contar `.c` no fim | 0 **e** zero `.c` |
| `gate_emits_c_only_under_c_backend` | o mesmo com `TEKO_BACKEND=c` | 0 **e** exactamente o `.c` do gate |
| `gate_zero_c_ratchet_bites` | forçar um `.c` a existir sob nativo no fim da sessão | não-zero, nomeando o ficheiro |
| `gate_main_synthesis_is_byte_identical` | stdout do gate com `main` gerado × com o `main` emitido à mão (P2) | 0 e diff vazio |
| `gate_route_equivalence_serial` | a mesma suíte nas duas rotas, `lanes=1` (P3) | 0 e diff vazio nas linhas de veredicto |
| `gate_test_index_is_item_index` | um projecto com `#test` intercalados com funções de produção; a atribuição de cobertura de cada teste tem de casar com o índice do item | 0 |
| `gate_reports_all_failures` | três `#test` a falhar; hoje o gate nomeia **um** | não-zero **e** os três nomeados |
| `gate_panic_is_not_assert` | um teste que falha uma asserção e outro que faz `panic("assertion failed: x")` à mão | não-zero, **e** o primeiro `Failed`, o segundo `Panicked` |
| `gate_exit_inside_test_is_a_verdict` | um teste que chama `exit(3)` | não-zero, **e** os OUTROS testes têm veredicto, **e** o registo diz `Exited code=3` |
| `gate_vanished_is_red` | um teste que faz um acesso inválido (morte sem escrever nada) | não-zero, **e** o teste é nomeado como `Vanished`, **e** os outros são reportados |
| `gate_lanes_output_identical` | a mesma suíte com `lanes` 1, 4, 16 (P4) | 0 nas três **e** diffs vazios |
| `gate_coverage_lanes_identical` | o relatório de cobertura com `lanes` 1 e N | 0 **e** byte-idênticos |
| `gate_unguarded_panic_is_unchanged` | um `panic` FORA de um teste guardado | 134, **e** a linha `TK_PANIC_MARKER` byte-idêntica à de hoje |
| `gate_unguarded_exit_is_unchanged` | um `exit(7)` FORA de um teste guardado | 7, saída byte-idêntica à de hoje |
| `thread_lane_unguard_pairs` | uma thread que retorna sem `gate_guard_end` deixa linha morta na tabela | 1 (detectado, nomeando a linha) |
| `gate_primitive_not_callable_from_source` | um `.tks` que escreve `gate_guard_begin(0)` | 1 (`unknown function`) |
| `gate_reserved_namespace_rejected` | um `.tks` com `use teko::__gate` ou um `src/__gate/` | 1 (segmento reservado, diagnóstico próprio) |
| `gate_main_not_in_coverage_denominator` | percentagem de cobertura de um projecto ANTES e DEPOIS de o gate sintetizado pousar | idênticas |
| `gate_primitives_absent_from_tsym` | traço de panic de um binário de gate | 134 **e** nenhum nome `__gate` no traço |
| `fixpoint_is_lane_count_blind` | construir o mesmo projecto com `TEKO_TEST_LANES=1` e `=64`, comparar os OBJECTOS byte a byte (§8.1) | 0 só se idênticos |
| `chan_send_copies_the_value` | o emissor muda a sua cópia depois do `send`; o recetor não vê a mudança | 0 |
| `chan_survives_sender_panic` | três emissores, o do meio entra em panic depois de enviar; o veredicto enviado chega intacto | 0 |
| `chan_rejects_noncopyable` | `chan<T>` com `T` contendo um fecho | 1 (erro de compilação, nomeando o campo) |
| `chan_close_then_recv_is_null` | canal fechado e vazio | 0 |
| `chan_send_on_closed_is_error` | `send` depois de `chan_close` | 0 (o `error` é tratado, não é silêncio) |
| `arena_per_thread_isolation` | cada thread grava um padrão conhecido, faz `arena_pop` e relê; qualquer cruzamento falha | 0 |
| `spawn_redirected_three_streams` | um filho que lê stdin, escreve em stdout e em stderr, com os três apontados a ficheiros do pai | 0 **e** os três ficheiros com o conteúdo esperado |
| `spawn_redirected_inherits_when_empty` | caminhos vazios herdam os descritores do pai | 0 |
| `spawn_failure_is_negative` | `spawn_redirected` de um executável inexistente | `raw` negativo, **sem** travamento |
| `pool_order_is_row_order` | linhas com durações deliberadamente invertidas | 0 **e** o relatório na ordem das linhas |
| `pool_child_crash_does_not_stop_the_run` | uma linha cujo filho morre por sinal | não-zero **e** as outras linhas com veredicto |
| `verdict_channel_defaults_to_stderr` | binário corrido à mão, sem `TEKO_VERDICT_CHANNEL` | 0 **e** o veredicto em stderr |
| `verdict_channel_survives_child_death` | o filho morre a meio; o que chegou ao canal fica no disco | não-zero **e** os índices em voo legíveis |
| `regr_jobs_env_still_honoured` | `TEKO_REGR_JOBS=1` e `=8` no pool novo | 0 nas duas **e** relatórios idênticos |
| `test_lanes_env_honoured` | `TEKO_TEST_LANES=1` e `=8` | 0 nas duas **e** stdout idêntico |

---

## 13. O que está BLOQUEADO por primitiva em falta — a lista, explícita

| # | primitiva | estado medido | quem bloqueia |
|---|---|---|---|
| 1 | **`cabi fn(T…) -> R`** em parâmetro de `extern fn`, com coerção do nome de função | **NÃO EXISTE.** Um nome de função como valor vira closure `{fn, env}` (`lower_fn_value`), não um endereço. `LFuncAddr` já existe e o isel já o baixa nas duas arquitecturas | migalhas 11-17, ou seja **toda a metade de threads** |
| 2 | **chão de thread** (`pthread_create`/`join`/`exit`/`self`, gémeos Win32) | não existe; `src/` não tem `thread` nem `isolate` | 12-17 |
| 3 | **raiz de região e pilha de marcas POR THREAD** | `tk_g_root`/`tk_g_regs`/`tk_arena_marks` são estáticos de processo. **Hipótese de custo baixo (classe de armazenamento) por MEDIR** | 13-17 |
| 4 | **sinks de cobertura por thread + fusão** | `tk_cov_ids`/`tk_cov_n`/`tk_cov_cap` são de processo | 13, 16 |
| 5 | **captura de `panic` (P-A) e captura de `exit` (P-B)** | `tk_panic_str`/`tk_exit` são `_Noreturn` e matam o processo; o único "catch" que existe (`tk_rt_crash_handler`) trata sem interromper. **DUAS primitivas novas (R7), sem antecedente para P-B** | 14, 16 |
| 5b | **`panic`/`exit` em Teko** (pré-condição de 5) | `call_symbol` aponta hoje a `tk_panic_str`/`tk_exit`; o fundo `write`/`abort` por `extern fn` está desenhado e não escrito | 13b, 14 |
| 5c | **namespace reservado + regra de prefixo `__`** | não existe; `builtin_fn` resolve por ÚLTIMO SEGMENTO, o que torna todo builtin injectado publicamente chamável — o oposto do que R7 exige | 3b, 14 |
| 6 | **`chan<T>`** e as quatro funções | palavra não reservada no lexer; superfície de linguagem nova; `channel<T>` é a grafia reservada no `TEKO_MASTER_PLAN` (§6.11) | 15-16 |
| 7 | **`spawn`/`wait` separados + redirecção por caminho** | `teko::process` só tem `run`/`run_quiet`, síncronos, sem redirecção escolhida pelo chamador | 7-10 |
| 8 | **builtin `flush_out`** | `tk_flush_out` existe no runtime, não é builtin do checker | 3-4 |
| 9 | **`extern fn` com dois parâmetros `[]str`** baixado nativamente | por MEDIR (migalha 0b). Se não baixar, a saída é uma assinatura por caminho em vez de um vector | 7 |
| 10 | **`tk_set_args` no `main` nativo** | `lower_virtual_main` cria `main` com ZERO parâmetros; `args()` é vazio em todo o binário nativo | **não bloqueia este desenho** — nenhuma peça aqui usa argv. Bloqueia o CLI nativo, e é do vagão do backend. **REPORTADO** |
| 11 | **auto-build nativo do compilador** | para no degrau 8 (`MInst` em `push_box_bytes`) | só o gate do COMPILADOR sobre si próprio. As 17 migalhas provam-se em sondas |

---

## 14. O QUE FICOU POR DECIDIR, e porquê

Secção obrigatória. Cada item diz porque não foi decidido aqui — e nenhum deles é "depende".

1. **A política de um `#test` que chama `exit` com um código esperado.** O REGISTO distingue sempre
   (§6.5.5); a POLÍTICA é vermelha hoje. O atributo `#test(expect_exit = N)` é a superfície natural e
   **não é desenhada aqui** porque nenhuma falha medida a exige — o `MASTER_PLAN` manda não congelar o
   que não precisa de ser congelado. Fica NOMEADA para não ser reinventada com outro nome.
2. **`scope { }` e `spawn` como palavras-chave.** Continuam reservadas. R4 nomeou `chan<T>` e só
   `chan<T>`; adiantar as outras duas seria congelar superfície que nenhum ruling pediu.
2b. **A GRAFIA: `chan<T>` ou `channel<T>`** (§6.11). É a ÚNICA pergunta que este documento devolve ao
   dono, porque a palavra congelada é dele e as duas grafias não podem coexistir. O desenho não muda
   com a resposta — só o token.
2c. **Se as duas primitivas se armam com UM par ou com DOIS** (§6.5.2). R7 diz "duas primitivas";
   o desenho entrega duas CAPACIDADES armadas por um par, com o argumento de que dois pares dobram as
   maneiras de deixar um por desarmar. Se o dono quis dois pontos de armar, é uma linha.
3. **Se `chan<T>` deve suportar múltiplos recetores.** O harness tem exactamente um. Um canal
   multi-recetor precisa de uma disciplina de fecho diferente (§6.4) e não há caso que o exija.
   Recusado por ausência de necessidade, não por dificuldade.
4. **Roubo de trabalho entre threads.** Deliberadamente ausente (§8): um índice roubado torna o
   escalonamento dependente do tempo, e um escalonamento dependente do tempo não reproduz a falha que
   causou. Volta à mesa com uma MEDIÇÃO de desequilíbrio, não com uma intuição.
5. **Quanto do runtime tem de passar a ser por thread além das quatro variáveis de §6.7.** A tabela de
   internamento (`tk_intern_*`), as posições de diagnóstico de cast (`teko_rt.h`:613) e os sinks de
   cobertura são, cada um, um julgamento próprio. A migalha 0(d) mede; o desenho não adivinha.
6. **A ordem exacta dos bytes da imagem de cobertura escrita em Teko** (§7.4). Tem de ser idêntica à
   que `tk_cov_dump` produz hoje, e isso é um golden a escrever na migalha 16, não uma decisão.
7. **A segunda metade de `--per-test-cov`.** A atribuição por teste melhora de graça com sinks por
   thread, mas reclamá-la exige mudar o que o relatório afirma. Fica para depois da migalha 16, e é
   ganho, não dívida.

---

## 15. Riscos e tensões de lei

### 15.1 Tensões, resolvidas law-first

| tensão | resolução |
|---|---|
| **"ZERO emissão de C" × "a rota C é a referência de comportamento até 0.3.1.4"** | R2 diz *"não pode emitir C **quando for compilar nativo**"*. Logo: sob nativo (o default) zero `.c`, e a catraca fecha a lane a vermelho; a rota C sobrevive **só** sob `TEKO_BACKEND=c`, o seletor já declarado em retirada, e **só** para servir P2/P3. As duas leis passam sem excepção nenhuma. |
| **Lei Teko-only × a captura de panic** | **RESOLVIDA SEM TOCAR EM C**: as duas primitivas nascem em Teko (`src/runtime/teko_rt.tks`), alcançadas por uma arm de `call_symbol`, com o fundo não-guardado em `extern fn` para `write`/`abort` — o mecanismo de `concorrencia-adiantada-s8.md` §4.2, adoptado verbatim. R7 (*"nascem no caminho nativo"*) confirma-o. A primeira redacção deste documento propunha remendar `teko_rt.c`; estava errada e está substituída (§6.5.3). |
| **R7 "conhecidas somente pelo compilador" × o precedente dos builtins injectados** | O precedente (`builtin_fn`, resolução por último segmento) faz EXACTAMENTE o contrário — torna tudo chamável por nome nu, e o doc-comment de `call_symbol` já regista o preço. Seguir o precedente violaria R7, logo **abre-se mecanismo novo** (namespace reservado + regra de prefixo `__`), e o dono tem de o saber: é peso a mais, e é peso obrigatório (§6.5.4). |
| **`MASTER_PLAN`:262 (não congelar as cinco primitivas sem dado de duplicação) × entregar `chan<T>`** | O dado existe e é R4: o dono nomeou `chan<T>` e nomeou o caso. Congela-se **uma** das cinco, a que foi nomeada; as outras quatro ficam reservadas. |
| **issues-100% × a metade de threads estar bloqueada por `cabi fn`** | Nada fica devendo no DESENHO: as 17 migalhas cobrem a proposta inteira e as fixtures existem para todas. O que está bloqueado é EXECUÇÃO, e está nomeado com endereço (§13). |
| **R1 diz "cada `#test` numa thread isolada" × economia de threads** | Cumprido literalmente: **uma thread por teste**, com no máximo `lanes` em voo. A alternativa (uma thread longa por raia, a varrer `i % lanes`) foi REJEITADA porque um panic mataria a raia e levaria consigo a cauda de testes dela — o oposto do que R1 pede. Uma criação de thread custa dezenas de microssegundos; mil testes pagam dezenas de milissegundos, que é ruído ao lado de uma suíte. |

### 15.2 Riscos, com mitigação

| risco | mitigação |
|---|---|
| Corpos de `#test` **nunca** passaram pelo backend próprio (`lower.tks`:8186 descarta-os) | migalha 0(a) mede ANTES de a migalha 4 depender disso |
| `_Thread_local` pode não bastar para a raiz de arena | migalha 0(d) mede; se não bastar, o custo sobe para o `C4` do desenho anterior e a migalha 13 vira vagão |
| Uma cópia rasa de `TestVerdict` aliasar um `str` da arena que fecha | a recusa de `T` não copiável é de CHECAGEM, não de convenção; `chan_rejects_noncopyable` afirma-a |
| A ordem `copiar → fechar arena → terminar thread` ser invertida por quem implementar | está escrita no doc-comment do guarda e é afirmada por `chan_survives_sender_panic` |
| Um `#test` que imprime interleavar a saída | regra 3 de §8: quem executa não imprime. O corpo escreve para o seu `out_text`, que o pai replica por ordem |
| Dependência entre testes, hoje mascarada pela execução serial | a suíte corre em série desde sempre. **ANTES de ligar o paralelismo**, correr a suíte em ordem INVERTIDA. O que partir ali é dependência real, e teria aparecido como intermitência três meses depois |
| A prova de equivalência expirar com a rota C | §10: P2 e P3 têm data limite (0.3.1.4) e o resultado é ARQUIVADO, não só executado |

### 15.3 Achados adjacentes — REPORTADOS, não convertidos em issue

1. **`teko::env::args()` é vazio em todo o binário do backend próprio** (`lower_virtual_main` cria
   `main` sem parâmetros; `tk_set_args` não é chamado em `src/lir` nem em `src/backend`). Não afecta
   este desenho, e torna impossível qualquer CLI nativo.
2. **`regr_batch_capture` atribui a cada linha uma FATIA do relógio do lote, não uma medição.** O
   próprio doc-comment o diz. Com `spawn`/`wait` a medição por filho passa a ser real — ganho lateral
   da migalha 8.
3. **`tk_cov_dump` recebe `const char *` enquanto o irmão `tk_cov_merge` recebe `tk_str`** — a
   assimetria que gerou a decisão elevada em `gate-sem-c-0.3.0.31.md` §4.2. Este desenho contorna-a
   escrevendo a imagem em Teko; a assimetria fica lá, e alguém há-de tropeçar nela outra vez.
4. **`REGR_GROUP_MIN_MEMBERS` e o colapso em canais** (`regr_group.tks`) reduzem o DENOMINADOR de
   linhas de regressão. `concorrencia-adiantada-s8.md` §7 argumenta que reduzir o denominador vale
   mais do que dividir o numerador. Continua verdade, e é ortogonal a esta carga.

---

## 16. Como este documento se verifica

Toda afirmação de código acima é reproduzível com a árvore em mãos:

```sh
grep -n 'fn run_native_gate' -A 34 src/build/project.tks       # emite C incondicionalmente: §2
grep -n 'TEKO_BACKEND_C_VALUE\|fn c_backend_selected' src/build/project.tks   # o resolvedor que o gate não consulta: §2
grep -n 'if f.is_test' src/lir/lower.tks                       # os corpos de teste são descartados: §3.2
grep -n 'new_func("main", 0' src/lir/lower.tks                 # main sem argv: §4
grep -rn 'tk_set_args' src/lir src/backend                     # zero ocorrências: §4
grep -n 'pub extern fn run' src/process/process.tks            # só síncrono, sem redirecção: §5.2
grep -n 'fn regr_batch_script\|fn sh_squote\|fn to_sh_path' src/build/regression.tks   # o andaime que morre: §5.4
grep -n '_Noreturn void tk_panic\|_Noreturn void tk_exit' src/runtime/teko_rt.h        # matam o processo: §6.5.1
grep -n 'tk_g_root\|tk_g_regs\|tk_arena_marks' src/runtime/teko_rt.c                   # estáticos de processo: §6.7
grep -n 'tk_cov_ids\|tk_cov_n \|tk_cov_cap' src/runtime/teko_rt.c                      # sinks de processo: §7
grep -n 'REGR_JOBS_DEFAULT\|REGR_JOBS_ENV' src/build/regression.tks                    # o botão que fica: §9
grep -n 'panic("assertion failed' src/assert/assert.tks                                # asserção É panic hoje: §6.5.5
sed -n '116,128p' src/runtime/teko_rt.c                                                # o "catch" global que trata e NÃO interrompe: §6.5.1
grep -n 'fn builtin_fn' -A 6 src/checker/scope.tks                                     # builtins resolvidos por ÚLTIMO SEGMENTO: §6.5.4
grep -n 'bare name happened to match a builtin' src/lir/lower.tks                      # o preço já pago por isso: §6.5.4
grep -n 'fn count_prod_fns' src/coverage/coverage.tks                                  # o denominador de cobertura a proteger: §6.5.4
```

Se alguma dessas leituras divergir do que está escrito aqui, **o documento está errado e deve ser
corrigido antes de ser seguido** — não contornado.
