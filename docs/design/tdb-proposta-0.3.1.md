# `tdb` — a proposta do depurador de Teko (0.3.1)

> **Esta é a peça de leitura do `tdb`.** Substitui, como documento a ler,
> `docs/design/debugger-orcamento-0.3.1.md` (1.ª passagem — orçamento e quatro experimentos medidos) e
> `docs/design/debugger-superficie-e-contramedida-0.3.1.md` (2.ª passagem — PoC executável, superfície,
> `kind = "tool"`, e a refutação medida da red-flag 3). **Nenhum dos dois foi reescrito ou editado:**
> ficam onde estão, e esta proposta refere-os pelo detalhe medido, byte a byte, que não vale repetir.
>
> **É uma PROPOSTA.** Propõe o que se constrói, em que ordem, com que superfície, e com que número.
> Obedece à lei de forma do dono (`docs/memory/teko-laws-digest.md:1156`):
>
> > **Um risco NÃO MEDIDO é um PASSO do plano, não um alarme.**
>
> Portanto: onde não se sabe se algo funciona, isso aparece aqui como **um crumb de medição nomeado que
> abre a fase**, e não como aviso. Alarmes entram **provados**, com a medição à vista. Números entram
> como **orçamento dentro do plano**, nunca como argumento contra.
>
> **Nada disto se implementa nesta versão nem na seguinte** (§7 — o portão).

## As decisões que esta proposta obedece, e não reabre

| decisão do dono | consequência aqui |
| --- | --- |
| *"precisaremos de um debugger próprio, como em Go"* | o `tdb` **é o alvo**. Não há alternativa apresentada, nem ponderada |
| *"não me importo (por ter ele) com os demais debuggers de mercado"* | **interop não é objectivo.** gdb, lldb, `cppdbg` e CodeLLDB estão fora do caminho crítico; nenhum crumb desta proposta os serve |
| *"quero que considere tudo que possa auxiliar o dev e dar a melhor experiência"* | o alvo é **a melhor experiência**, não o mínimo viável. Variáveis, tipos, formatação legível e frames fiáveis entram **todos** |
| *"SEM C LANG, somente Teko nativo"* | nenhuma linha de C nossa, nenhuma compilação pela rota C. **§5** mostra como se alcança o SO sob esta ordem |
| *"gastar energia marcando `#line` em C é desnecessário"* | **a Camada 0 não existe.** Não está orçada, não é opcional |
| *"primeiro precisamos do teko 100% nativo (emissão e linhagem)"* | **o PORTÃO, §7**, com as duas metades e o número de hoje |
| `/tdb` na raiz; `kind = "tool"` emite um `.tkl`, compila na máquina do dev, não entra em `[deps]`; um `tool` em `[deps]` é **ignorado**, não recusado | **§4.6** — a forma do projeto |

---

# 1. A tese

> **O `tdb` transforma a informação que o compilador JÁ CALCULA e HOJE DESCARTA numa sessão de
> depuração que fala Teko: pára numa linha de `.tks`, nomeia as funções pelo nome qualificado Teko,
> desenrola a pilha pelo descritor de frame que o nosso próprio backend produziu, e mostra os valores
> com a forma dos nossos tipos.**

O que o `tdb` dá ao dev, e que **hoje não existe de forma nenhuma**:

| hoje | com o `tdb` |
| --- | --- |
| um programa Teko que falha dá **um código de saída** e, no melhor caso, um stack trace **por função** (o `.tsym` v1 resolve `<símbolo>` → `<nome-teko>`, sem linha) | pára-se **na linha**, vê-se a linha do `.tks`, e o `bt` diz `teko::demo::add at src/main.tks:41` |
| para saber quanto vale `x`, escreve-se `println` e recompila-se — **o ciclo é uma recompilação por pergunta** | `print x` responde sem recompilar; `locals` responde a todas de uma vez |
| a pilha, quando existe, é a do runtime e tem os nomes **manglados** do símbolo | os nomes qualificados Teko, que o `.tsym` já carrega nos dois lados (medido, §4.2) |
| não há forma de observar um `defer` a correr, nem qual membro de um `T \| error` está activo | o `step` **entra** nas linhas do `defer` (é código que corre), e o formatador lê a tag e escolhe o membro |
| o editor não sabe pôr um breakpoint num `.tks` | o `tdb` registra a linguagem, e o breakpoint no gutter funciona sem o dev ligar uma opção global do editor |

**E a razão de ser NOSSO, e não um adaptador para o de outra pessoa,** é a que o Go escreveu primeiro e
que §8 verifica na fonte: um depurador de terceiros modela o *seu* modelo de execução, não o nosso.
As três coisas em que o nosso já difere — o nome qualificado, o descritor de frame que **nós** temos
exacto em vez de inferido, e a forma dos nossos rails de união — são exactamente as três em que um
depurador nosso responde melhor **por construção**, não por afinação.

---

# 2. A experiência, do lado do dev

**Isto vem primeiro de propósito.** É uma proposta de ferramenta, não de formato: o formato (§4) existe
para servir o que está nesta secção, e não o contrário.

## 2.1 O que ele escreve

Duas linhas. A primeira constrói com linhagem, a segunda abre a sessão.

```sh
teko build . --debug=vars -o bin
tdb run .
```

Ou, sobre um binário que já existe:

```sh
tdb exec ./bin/hello
```

Ou anexando-se a um processo vivo:

```sh
tdb attach 41207
```

**A flag de construção é nivelada, com valores nomeados**, na forma que o `--opt=<n>` já pratica na
nossa CLI:

| forma | efeito |
| --- | --- |
| `--debug=none` | **o default, em todos os perfis, `--release` incluído.** Informação de depuração nunca é imposta |
| `--debug=lines` | a tabela endereço→linha + os descritores de frame. Responde *"onde estou e como cheguei aqui"* |
| `--debug=vars` | tudo o de `lines`, mais os locais com nome, slot e tipo. Responde *"quanto vale `x`"* |
| `-g` | **alias legal de `--debug=vars`**, porque é isso que `-g` significa em gcc, clang e rustc — e quando `--debug=vars` existir, prometê-lo passa a ser verdade |
| valor desconhecido | **erro duro**, com a lista dos aceites. Nunca degradação silenciosa: um nível de depuração adivinhado custa uma sessão inteira a perseguir um breakpoint que nunca resolveu |

`--release --debug=vars` é **legal e não é recusado**: é como se depura um crash de release, e recusá-lo
obrigaria a reconstruir noutro perfil — o que muda o código e apaga o defeito.

## 2.2 O que ele vê

```
$ tdb run .
tdb 0.3.1 — teko debugger
built bin/hello (--debug=vars)
loaded bin/hello.tsym (v2): 412 functions, 5108 line rows, 412 frame descriptors, 1877 locals
(tdb) break src/main.tks:41
Breakpoint 1 at 0x1133: src/main.tks:41 (in teko::demo::add)
(tdb) run
Breakpoint 1, teko::demo::add at src/main.tks:41
   39 fn add(a: i32, b: i32) -> i32 {
   40     let doubled = a * 2
-> 41     let s = doubled + b
   42     s
   43 }
(tdb) print s
s: i32 = <not yet assigned>
(tdb) next
Breakpoint 1, teko::demo::add at src/main.tks:42
(tdb) print s
s: i32 = 47
(tdb) locals
a:       i32 = 12
b:       i32 = 23
doubled: i32 = 24
s:       i32 = 47
(tdb) bt
#0  teko::demo::add   at src/main.tks:42
#1  teko::demo::main  at src/main.tks:52
(tdb) frame 1
#1  teko::demo::main at src/main.tks:52
(tdb) print label
label: str = "total" (5 chars, 5 bytes)
(tdb) print res
res: i64 | error = i64(47)
(tdb) continue
process exited with status 0
(tdb) quit
```

**Cinco coisas nesta transcrição são a proposta, e nenhuma delas é o que um depurador genérico daria:**

1. **`teko::demo::add`, não `tk_demo_add`.** O nome qualificado Teko, que o `.tsym` já carrega ao lado
   do símbolo manglado — medido hoje em §4.2. Um depurador de terceiros mostra o segundo.
2. **`-> 41` com contexto de duas linhas acima e abaixo**, listado do `.tks` real. A listagem é o
   depurador a ler o ficheiro, e não depende de nada dentro do processo.
3. **`s: i32 = <not yet assigned>`** antes da atribuição, e o valor depois. **Um local que ainda não foi
   escrito não mostra lixo com confiança** — é a única resposta honesta, e §4.5 é o mecanismo que a
   torna possível.
4. **`label: str = "total" (5 chars, 5 bytes)`** — os **dois** contadores, porque `str` em Teko conta
   caracteres em `.len` e leva os dois. Formatar um `str` **é** o layout de `str`, e é por isso que a
   fase que o formata (§3.5) abre com a medição do layout, e não com um palpite.
5. **`res: i64 | error = i64(47)`** — o **membro activo** da união, escolhido pelo rail (`niche`,
   `InlineTag`, ou box-em-arena). O `tdb` lê o rail do formato; não o adivinha.

## 2.3 Os comandos

Grafia do gdb, por memória muscular de quem depura — a mesma razão pela qual o delve a escolheu.
**Aliases de CLI não são sobrecarga de função:** são um nome com abreviatura, e é assim que todo
depurador se escreve.

```
break <ficheiro.tks>:<linha>   b     põe um breakpoint
break <ns::fn>                 b     põe na primeira linha executável da função
tbreak <...>                         breakpoint de uma vez só
delete <n>                     d     remove
disable <n> / enable <n>             desliga sem perder
info breakpoints                     estado, com contagem de disparos

run [-- args…]                 r     arranca
continue                       c     continua
next                           n     próxima linha, sem entrar
step                           s     próxima linha, entrando
finish                               corre até ao retorno da moldura corrente
until <linha>                        corre até uma linha desta função

bt                                   a pilha, com nomes Teko
frame <n>                      f     selecciona a moldura
up / down                            navega a pilha
list [<linha>]                 l     lista o .tks em volta

print <nome>                   p     o valor de um local ou parâmetro
locals                               todos os locais da moldura selecionada
args                                 só os parâmetros
whatis <nome>                        o tipo Teko, sem avaliar

quit                           q
```

**O que o `tdb` NÃO faz, dito aqui e não depois de prometido:** avaliar expressões (só nomes simples e
acesso a membro por `.`); watchpoints; alterar valores; ler core dumps; e entrar dentro de código de
biblioteca da plataforma — uma chamada a `write` aparece como uma moldura de fronteira nomeada, não
como um passeio pela libc.

## 2.4 No editor

O `tdb` fala DAP **como modo do próprio binário** (`tdb dap`), na forma verificada do delve — não há um
segundo executável adaptador para manter (§8.1).

`.vscode/launch.json`, e é tudo:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "teko: depurar",
      "type": "tdb",
      "request": "launch",
      "program": "${workspaceFolder}/bin/hello",
      "args": [],
      "cwd": "${workspaceFolder}",
      "stopOnEntry": false,
      "preLaunchTask": "teko: build --debug=vars"
    }
  ]
}
```

**Não há `MIMode`, `miDebuggerPath`, `sourceFileMap`, `sourceMap`, nem
`debug.allowBreakpointsEverywhere`.** Os quatro primeiros não existem porque o adaptador é nosso e
resolve os caminhos com o nosso formato. O quinto não é preciso porque a extensão que registra o tipo
`tdb` **também registra a linguagem `teko`** — e é essa a diferença concreta e mensurável de ter
depurador próprio: as vias de terceiros exigem que o utilizador ligue uma opção global do editor para
poder clicar no gutter de um `.tks`; a nossa não.

E a extensão que registra o tipo é **um `package.json` com `contributes.debuggers`, zero JavaScript** —
o que também é virtude de lei: o roadmap de tooling já ratificou que nenhuma invocação de processo a
partir de um editor usa concatenação de string para shell, por causa de um achado real de injeção. Um
adaptador registado por manifesto **não tem código onde essa falha caiba**.

## 2.5 A frase que a documentação leva

> `--debug=lines` responde **"onde estou e como cheguei aqui"**. `--debug=vars` responde
> **"quanto vale `x`"**. O default é `none`, e um binário construído sem a flag **diz** que não tem
> linhagem em vez de parar em silêncio no sítio errado.

Recusa nomeada, nunca omissão — a regra que o expurgo já fixou para o MinGW.

---

# 3. O plano — oito fases, cada uma utilizável por si

**Unidade: crumb** — passo pequeno, prova própria, reversível.

**A regra de ordenação, e é o que faz esta secção ser um plano e não uma lista de desejos:** cada fase
entrega **algo que se usa sem a fase seguinte existir**. Uma fase que só valha quando a próxima aterrar
não é uma fase, é meio crumb grande.

**E onde há incerteza, a fase ABRE com um crumb de medição nomeado**, marcado **`M`**. Um `M` não
produz produto: produz **um número que decide os crumbs a jusante**, e o seu custo é sempre uma fracção
do que decide. A prova histórica de que isto vale está na própria lane: a red-flag 3 foi levantada por
raciocínio, custou a rejeição de um arco inteiro, e quando foi medida **era falsa**. Custo por medir:
nenhum. Custo por não medir: uma decisão errada.

## 3.0 Fase 0 — o arnês e o oráculo · **3 crumbs** · entrega: um activo de teste que não expira

**O que se usa no fim da fase:** uma suíte que afirma factos sobre tabelas de depuração **sem precisar
de um `tdb`, sem precisar de um compilador Teko correcto, e sem precisar de um depurador de terceiros**.
É a fase que se pode fazer antes do portão sem desperdício (§7.2), e a razão está em §6.

| crumb | conteúdo | prova |
| --- | --- | --- |
| **F0.1** | os dois objetos escritos à mão do PoC da 2.ª passagem entram como fixtures do arnês: `docs/design/debugger-poc/mini.s` (tabelas de correcção conhecida, byte a byte) e `adv.s` (cinco molduras através de **todas** as formas de frame do nosso encoder) | `reproduce.sh` já corre e sai 0. O crumb é a promoção de "documentação que executa" a **fixture citada por um teste** |
| **F0.2** | o arnês de asserção: dado um objeto e uma tabela esperada, afirmar **profundidade de pilha através de uma função frameless**, e não `>= N`. Uma afirmação de `>=` passa com uma cadeia truncada | o ramo negativo é metade do crumb: uma tabela deliberadamente errada tem de **falhar** |
| **F0.3** | um terceiro fixture, novo: uma pilha de **seis** molduras que atravessa uma fronteira de biblioteca da plataforma, para fixar o que o `bt` faz nessa fronteira (moldura nomeada, não passeio) | o teste que afirma que o desenrolar **pára** na fronteira em vez de inventar molduras |

**Porque três e não um:** F0.1 é promoção de activo existente, F0.2 é a máquina de asserção, F0.3 é o
único caso de fronteira que os dois fixtures actuais não cobrem e que o `bt` do `tdb` vai encontrar em
todo programa real que escreva no stdout.

## 3.1 Fase 1 — a linhagem no compilador (`.tsym` v2) · **11 crumbs** · entrega: stack traces de produção com LINHA EXACTA

**O que se usa no fim da fase, e nada disto precisa do `tdb`:** o stack trace nativo que hoje resolve
**por função** passa a resolver **por linha**. Mesma informação, melhor granularidade, mesmo ficheiro,
e todo programa Teko em produção beneficia. **É a fase de maior valor por crumb da proposta**, e é a
única que rende antes de existir uma linha de depurador.

| crumb | conteúdo | prova |
| --- | --- | --- |
| **M1.0 — MEDIR PRIMEIRO** | **quanto custa o `.tsym` v2, de verdade?** A estimativa de mesa é ~1,2 MB no corpus do compilador (≈50 k linhas × ≈24 bytes) e "tempo de build desprezável". Nenhum dos dois foi medido. Este crumb gera o ficheiro contra o corpus real e mede **bytes e segundos**. Decide, a jusante, se o `L` fica em texto ou passa a binário compacto | o número, num relatório. Se o texto passar de um limiar que o próprio crumb fixa, o formato binário entra como crumb; se não passar, poupam-se os crumbs de codificador e descodificador |
| **C1.1** | `LFunc` ganha `file` e `decl_line`. **Medido hoje: não os tem** — `LFunc` é `{ symbol, n_params, param_types, ret_type, blocks, next_vreg }`, e o `file`/`line` existem só a montante, em `checker::TFunction` | `lower_test.tkt`: o `file`/`decl_line` de uma função lowered casa com o do `TFunction` de origem |
| **C1.2** | `MLineMark` — a pseudo-instrução de **zero bytes** que carrega `(linha, coluna)` pela fila de instruções de máquina, no lado **x86-64** | golden do encoder: o objeto com marcas é **byte-idêntico** ao sem marcas. Zero bytes quer dizer zero bytes |
| **C1.3** | o mesmo, no lado **arm64** | idem, na perna aarch64 |
| **C1.4** | **o produtor de `.tsym` MUDA DE CASA.** Medido hoje, e é o achado desta passagem: `tk_emit_tsym` vive em `src/codegen/codegen.tks:12187` — **o emissor de C** — e é chamado pela rota **nativa** (`src/build/project.tks:1845` e `:2642`, em `finish_native_object`). A fatia 6 do expurgo manda *"REMOVER DO FONTE … a emissão de C"*. Sem este crumb, o dia do expurgo leva o produtor de linhagem com ele | o `.tsym` v1 emitido depois da mudança é **byte-idêntico** ao de hoje, e nenhuma chamada atravessa `codegen::` |
| **C1.5** | a linha **`L`** — endereço→linha, uma por `MLineMark` sobrevivente ao alocador | golden: uma função com um derramamento produz linhas `L` **na ordem dos endereços finais**, não na ordem de lowering |
| **C1.6** | a linha **`F`** — o descritor de frame, serializando o que **já é calculado**: `compute_frame_layout_x86` → `FrameLayoutX86` (`src/backend/encode_x86_64.tks:1501`) e `compute_frame_layout` → `FrameLayout` (`src/backend/encode_arm64.tks:1066`) | golden das duas arquiteturas, e o teste que afirma que a linha `F` de uma função **frameless** diz frameless |
| **C1.7** | a linha **`V`** — locais: o JOIN por `vreg_id` sobre `assign_lookup`, e a **fixação a slot** sob o perfil `vars` | `lower_test.tkt`: sob o perfil, duas `let` + um shadow dão **três** entradas na ordem de ligação; **sem** o perfil, o LIR é **byte-idêntico** ao de hoje. A segunda metade é o que torna o crumb seguro |
| **C1.8** | o **registo de tipos de layout congelado**, explícito e enumerado — nunca "tudo o que não sabemos que muda" | um tipo **fora** do registo produz **nenhuma** descrição; um dentro produz a esperada. O ramo negativo é metade do crumb |
| **C1.9** | a superfície: `--debug=none\|lines\|vars`, `-g` como alias de `vars`, e **a linha em `project_arg_of`** | `help_test.tkt` + o teste que prova que `--debug=lines` **não** é lido como o caminho do projeto (§4.7) |
| **C1.10** | **a especificação escrita do `.tsym` v2**, em `docs/` | é entregável de documentação, e é o contrato: emissor e leitor viverão em repos diferentes (§4.6), e um formato definido pela implementação não sobrevive à separação |

**Porque `M1.0` abre a fase:** o formato ser texto é uma escolha que se paga em bytes e se cobra em
simplicidade de leitor (um separador feito à mão, no molde do parser TOML de `manifest.tks` — medido: não
há `teko::str::split` no corpus). A escolha é **boa se o número for pequeno**, e o número não foi medido.
Medir custa um crumb; escolher errado custa o leitor inteiro reescrito na fase 4.

## 3.2 Fase 2 — o chão de controlo · **7 crumbs** · entrega: `tdb exec` corre um programa sob controlo

**O que se usa no fim da fase:** `tdb exec ./bin/hello` arranca o programa **parado**, deixa-o correr,
recolhe o estado de saída **decodificado** e reporta-o. Não pára em linhas ainda — mas o chão está
provado, e um programa que sai com sinal deixa de ser um número opaco.

| crumb | conteúdo | prova |
| --- | --- | --- |
| **M2.0 — MEDIR PRIMEIRO, e é o crumb mais importante desta proposta** | **um `extern` de libc implícito, com parâmetros de largura de ponteiro, liga e chama pela rota nativa?** Medido hoje: o checker tem `ptr` e `uptr` (`src/checker/scope.tks:387-388`, *"opaque FFI pointer (transport-only)"*), e o `TExternDecl` tem `from_lib: str` com o comentário **`"" = implicit libc`** (`src/checker/tast.tks:172`). Medido também: **os 24 `pub extern fn` da árvore levam TODOS `from "teko_rt"`** — nenhum exercita o caminho de libc implícito. O mecanismo está declarado; o caminho está **por andar**. Este crumb declara `extern fn getpid() -> i32 = "getpid"`, constrói pela rota nativa, e corre | o binário nativo imprime o pid correcto. Se falhar, o crumb reporta **onde** falha (checker, símbolo indefinido, ou linha de linker), e o conserto é um crumb nomeado em vez de uma descoberta a meio da fase 3 |
| **T2.1** | `ptrace` declarado como `extern` de libc implícito, com os pedidos que interessam como constantes nomeadas | um teste que anexa e desanexa de um processo filho trivial |
| **T2.2** | `fork` + `execvp` + `PTRACE_TRACEME`: nasce um filho **parado no `execvp`** | o filho existe, está parado, e o pai sabe o pid |
| **T2.3** | `waitpid` + a **decodificação do estado**, com a regra 128+N que `tk_rt_run` já pratica na árvore | a matriz: saída normal, saída por sinal, e paragem — três resultados distinguíveis, nenhum colapsado |
| **T2.4** | os registos: `PTRACE_GETREGS`/`SETREGS` para uma vista de bytes, e **acesso nomeado** ao contador de programa, ao topo de pilha e à base de frame, por arquitetura | ler o contador de programa de um filho parado devolve um endereço **dentro** do mapa do executável |
| **T2.5** | a memória: ler e escrever uma palavra no espaço do filho | escreve-se e lê-se de volta, e o byte original recupera-se |
| **T2.6** | o esqueleto: `tdb exec`, `tdb version`, e o laço de despacho de comandos | `tdb exec` sobre um programa que sai 5 reporta 5; sobre um que aborta, reporta o sinal pelo nome |

**Nota de forma:** as declarações desta fase são o **único** sítio da proposta onde a superfície toca o
SO, e §5 mostra que a forma obedece a *"SEM C LANG"* sem nenhuma linha de C nossa.

## 3.3 Fase 3 — breakpoints · **3 crumbs** · entrega: parar num endereço e continuar, fiavelmente

**O que se usa no fim da fase:** põe-se um breakpoint por **endereço**, o programa pára lá, continua, e
**pára lá outra vez na segunda passagem**. Esse "outra vez" é a fase inteira: o defeito clássico de um
depurador novo é o breakpoint que dispara uma vez e desaparece.

| crumb | conteúdo | prova |
| --- | --- | --- |
| **T3.1** | x86-64: guardar o byte original, escrever `0xCC`, e **no sinal recuar o contador de programa um byte** antes de restaurar | um breakpoint num laço de 3 iterações dispara **3 vezes**, e o programa termina correctamente. O ciclo restaurar → single-step → **re-armar** é o crumb |
| **T3.2** | arm64: `BRK #0` é uma palavra de 4 bytes e **o contador de programa NÃO recua**. A assimetria é real e é a razão de ser um crumb próprio | o mesmo teste de 3 iterações, na perna aarch64 |
| **T3.3** | a tabela endereço→byte-original, com `delete`/`disable`/`enable` e contagem de disparos | remover um breakpoint restaura o byte; `info breakpoints` conta certo; e **sair do `tdb` deixa o processo íntegro** |

## 3.4 Fase 4 — a posição · **4 crumbs** · entrega: **o depurador de terminal completo**

**O que se usa no fim da fase, e é o marco grande:** `break src/main.tks:41`, `run`, `bt` com nomes
Teko, `next`, `step`, `finish`, `list`, `frame`. **Toda a transcrição de §2.2 excepto as três linhas de
`print`/`locals`.**

| crumb | conteúdo | prova |
| --- | --- | --- |
| **T4.1** | o leitor de `.tsym` v2, escrito **contra a especificação de C1.10**, não contra o emissor. Separador de linhas e campos à mão, no molde do parser TOML de `manifest.tks` | os fixtures de F0.1: um `.tsym` de correcção conhecida lê-se para as tabelas esperadas; um `.tsym` v1 lê-se **degradando** (sem linhas, sem frames, sem locais) em vez de falhar; um cabeçalho desconhecido **para honestamente** |
| **T4.2** | as duas resoluções: `ficheiro:linha → endereço` (para o `break`) e `endereço → ficheiro:linha` (para o `bt`). A segunda é a que precisa de cuidado: um endereço **entre** duas linhas resolve para a **anterior**, nunca para a seguinte | a matriz de fronteiras: primeiro endereço de uma função, último, e um endereço no meio de uma linha |
| **T4.3** | o desenrolar **pela linha `F`** — o descritor que o compilador emitiu, não análise de prólogo. É a clamação mais forte do arco, porque é a única que não depende da inferência de ninguém | os três fixtures de F0.*, incluindo a pilha de seis molduras que atravessa a fronteira de biblioteca |
| **T4.4** | os comandos de navegação: `break`/`tbreak` por linha e por `ns::fn`, `bt`, `frame`/`up`/`down`, `next`, `step`, `finish`, `until`, `list` | `next` sobre uma linha com `defer` **executa** as linhas do `defer` e volta — o comportamento certo, e §8.2 explica porquê |

## 3.5 Fase 5 — variáveis e legibilidade · **5 crumbs** · entrega: `print`, `locals`, `args`, `whatis`

**O que se usa no fim da fase:** as três linhas que faltavam em §2.2, e a proposta está cumprida do lado
do terminal.

| crumb | conteúdo | prova |
| --- | --- | --- |
| **M5.0 — MEDIR PRIMEIRO** | **o layout de `str` está congelado?** Medido hoje: `src/runtime/teko_rt.h:44-48` define `tk_str` com **duas** palavras e o comentário `length in BYTES`; a decisão registada do dono é que `.len` conta **CARACTERES** e que `str` leva **os dois** contadores — uma terceira palavra. **A decisão existe, o layout não mudou.** Este crumb lê o layout no dia em que a fase abrir e diz qual é | o registo de tipos congelados de C1.8 é actualizado pelo número medido, não pela intenção. Se `str` ainda for duas palavras, `str` fica **fora** do registo e os locais de tipo `str` não recebem descrição — o que é honesto — e o resto avança |
| **T5.1** | `print <nome>` e `locals`/`args`: ler o slot pelo offset da linha `V`, e formatar pelo tipo | um escalar de cada largura, um `bool`, um `char`, e um local **antes** da atribuição a dar `<not yet assigned>` em vez de lixo |
| **T5.2** | o formatador de agregados: `struct` por campo, `[]T` por elementos com comprimento, e acesso a membro por `.` | um struct aninhado imprime aninhado; um slice vazio imprime vazio e não falha |
| **T5.3** | o formatador de **uniões**, lendo o **rail** do formato: `niche` (uma palavra, o padrão do ponteiro discrimina), `InlineTag` (palavra de tag + payload), e box-em-arena | os três rails, um teste cada. E o teste que importa: um `T \| null` no rail `niche` com o ponteiro nulo imprime `null`, e com ponteiro válido imprime o membro |
| **T5.4** | `whatis` e o formatador de `str` com **os dois contadores**, condicionado ao veredicto de M5.0 | `"total" (5 chars, 5 bytes)`, e um `str` com um caractere multi-byte a mostrar contadores **diferentes** — que é o teste que prova que os dois contadores são lidos e não calculados |

**Porque o rail vai no formato e não no formatador:** a regra de `niche` é uma **optimização**, logo vai
mudar. Um formatador que codifique *"ponteiro nulo significa `null`"* replica uma decisão interna do
gerador de código; no dia em que a regra mudar, ele mente em silêncio. **Um campo por tipo no `.tsym` v2
custa um campo e poupa uma mentira silenciosa** — e o formatador **lê** o rail em vez de o adivinhar.

## 3.6 Fase 6 — o editor · **5 crumbs** · entrega: depurar Teko no VSCode

| crumb | conteúdo | prova |
| --- | --- | --- |
| **T6.1** | o enquadramento `Content-Length: <n>\r\n\r\n<json>` e o laço de pedidos. **A camada JSON é grátis** — `src/encoding/json/json.tks` já expõe codificação e descodificação | golden de mensagens: um pedido partido em dois pacotes lê-se inteiro; um `Content-Length` que mente **para honestamente** |
| **T6.2** | o ciclo de arranque: `initialize`, `launch`, `attach`, `setBreakpoints`, `configurationDone`, e os eventos `initialized`/`stopped`/`continued`/`exited`/`terminated` | uma sessão simulada, dirigida por um ficheiro de pedidos, produz a sequência de eventos esperada — **sem VSCode** |
| **T6.3** | os pedidos de navegação: `threads`, `stackTrace`, `scopes`, `continue`, `next`, `stepIn`, `stepOut`, `pause` | idem, por golden |
| **T6.4** | os pedidos de dados: `variables`, `evaluate` (nomes simples), `source` | idem |
| **T6.5** | a extensão que registra o tipo `tdb` **e a linguagem `teko`** — um `package.json`, zero JavaScript | o breakpoint no gutter de um `.tks` aceita o clique **sem** `debug.allowBreakpointsEverywhere` |

## 3.7 Fase 7 — os portes · **8 crumbs** · entrega: `tdb` em macOS e em Windows

**Uma plataforma de cada vez, e a ordem é a do uso.** O aviso de escopo é medido e vem do precedente:
o delve, com uma década de manutenção e patrocínio corporativo, cobre **cinco** pares de plataforma —
e não cobre `darwin/arm64`. **Isto não é argumento contra o `tdb`; é o que ordena esta fase por último
e a divide em duas metades independentes.**

| crumb | conteúdo | prova |
| --- | --- | --- |
| **M7.0 — MEDIR PRIMEIRO (macOS)** | **o entitlement.** Depurar em macOS exige que o binário do depurador seja assinado com `com.apple.security.cs.debugger`. Este crumb mede o que isso custa **no nosso pipeline de release**, não em crumbs de código | o `tdb` assinado anexa-se a um processo próprio numa máquina Darwin. É custo de engenharia de release, e é o menos agradável do lote — por isso é medido antes de qualquer código de porte |
| **P7.1 · P7.2 · P7.3** | macOS: `task_for_pid` + `mach_vm_read_overwrite`/`mach_vm_write`, os registos por `thread_get_state`, e as excepções de depuração | a matriz das fases 2 e 3, repetida na perna Darwin |
| **M7.4 — MEDIR PRIMEIRO (Windows)** | o modelo de eventos de Windows é **empurrado** (`WaitForDebugEvent`), não **puxado** (`waitpid`). Este crumb mede se o laço de sessão da fase 2 absorve a inversão ou se precisa de uma camada de adaptação | o número de sítios do laço que mudam. Decide se P7.5 é um crumb ou três |
| **P7.5 · P7.6 · P7.7** | Windows: `DebugActiveProcess` + `WaitForDebugEvent`, `ReadProcessMemory`/`WriteProcessMemory`, `GetThreadContext`/`SetThreadContext` | idem, na perna Windows |

**Windows não precisa de contentor de depuração nenhum.** O `tdb` lê o `.tsym` v2, um ficheiro ao lado
do `.exe`. PE, COFF, CodeView e PDB **não entram nesta proposta** — nem como item, nem como custo.

## 3.8 O total

| fase | entrega utilizável por si | crumbs |
| --- | --- | --- |
| **0 — arnês e oráculo** | um activo de teste que não expira, sem depender de `tdb` nem de compilador correcto | **3** |
| **1 — linhagem no compilador** | **stack traces de produção com linha exacta**, sem depurador nenhum | **11** |
| **2 — chão de controlo** | `tdb exec` corre sob controlo e decodifica a saída | **7** |
| **3 — breakpoints** | parar num endereço e voltar a parar | **3** |
| **4 — posição** | **o depurador de terminal completo** (tudo excepto valores) | **4** |
| **5 — variáveis e legibilidade** | `print`, `locals`, `args`, `whatis`, com os nossos tipos | **5** |
| **6 — editor** | depurar Teko no VSCode, sem opção global e sem JavaScript | **5** |
| **7 — portes** | macOS e Windows | **8** |

> **Linux completo (fases 0–6): 38 crumbs. Três plataformas: 46.**
> **Dos 38, seis são de medição** (`M1.0`, `M2.0`, `M5.0` e os três dos portes contam 2 fora dos 38) —
> e são o que impede os outros de serem escritos contra um palpite.

**Duas leituras honestas do número, e as duas pertencem ao plano:**

**(a) Onde o número SUBIU em relação à 2.ª passagem, e porquê.** Ela orçava 18 crumbs para o `tdb` em
Linux e 8 para o piso do compilador — 26. Este plano diz 38. A diferença **não** é pessimismo novo: é
o alvo, que mudou por ordem do dono. *"Quero que considere tudo que possa auxiliar o dev e dar a melhor
experiência"* põe variáveis, tipos, formatação de uniões e `str` legível **dentro** do arco em vez de
os deixar como fase opcional; e acrescenta os crumbs que a 2.ª passagem não tinha (a mudança de casa do
produtor de `.tsym`, a especificação escrita, e os seis de medição).

**(b) Onde o número DESCEU, e é maior do que subiu.** Saem, por *"não me importo com os demais
debuggers de mercado"*: as três seções DWARF, os tipos DWARF, a CFI DWARF, o CodeView, o DWARF-em-PE, e
os pretty-printers de gdb e de lldb — **17 crumbs que a 2.ª passagem orçava e que esta proposta não
tem.** O interop sair do caminho crítico é a maior economia do plano, e é uma decisão do dono, não uma
optimização minha.

**E o número que interessa mais do que o total:** **a fase 1, com 11 crumbs, rende sozinha** — stack
traces por linha em produção, para todo programa Teko, sem uma linha de depurador escrita.
