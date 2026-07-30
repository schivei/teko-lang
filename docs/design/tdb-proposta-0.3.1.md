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
