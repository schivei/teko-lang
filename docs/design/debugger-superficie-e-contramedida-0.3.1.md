# A superfície do debugger, os quatro debuggers, a fronteira honesta, e a contra-medida (arquiteto, 2026-07-30)

> **Companheiro, não substituto.** `docs/design/debugger-orcamento-0.3.1.md` fica como está. Este
> documento **não o reescreve**: acrescenta as cinco peças que o dono cobrou (prova que se corre,
> superfície até ao texto, uso em cada debugger, camadas restantes com número, contra-medida
> orçada), acrescenta a **sexta** que o ruling seguinte exigiu (a fronteira redesenhada por
> honestidade), responde à pergunta sobre Rust/Zig/Go **verificada por fonte**, e aponta em §10 os
> erros factuais que encontrei nele.
>
> **Prova executável:** `docs/design/debugger-poc/reproduce.sh` — corre e sai 0.

---

## 0. Sumário — os oito veredictos, todos com medição

| # | Veredicto | Onde |
|---|---|---|
| 1 | **RED-FLAG 3 REFUTADA.** Cinco molduras de profundidade, **todas** as formas de frame do nosso encoder x86-64, **zero CFI** no objeto: gdb **e** lldb recuperam a cadeia inteira. O `bt` pode ficar na lista de entregas do Piso — em x86-64. | §4.1 |
| 2 | **A razão é estrutural, não sorte:** `frame_is_framed_x86` garante que **RSP só se move dentro de uma função que pôs `rbp`**. Uma função sem prólogo devolve RSP intacto. Não há forma de frame que derrote a heurística. | §4.1 |
| 3 | **arm64 fica por medir** — não há toolchain aarch64 nem qemu neste host. Vira crumb de sondagem próprio, com orçamento condicional nos dois ramos. | §4.2, §5.1 |
| 4 | **RED-FLAG 2 REDIMENSIONADA:** o nome **não precisa de atravessar** o alocador. `assign_lookup(sr, vreg_id)` é **público** e a cadeia inteira é indexada por `vreg_id` — é um **JOIN**, não plumbing. O perigo real é outro e é pior: um vreg em registo físico só é válido no seu intervalo, logo `print x` **mentiria**. A resolução honesta já existe no código: `lenv_bind_scalar_slot`. | §4.3 |
| 5 | **RED-FLAG 1 CONFINADA:** `str` não toca o Piso. Medido: a listagem de fonte é o debugger a ler o ficheiro do disco — zero envolvimento de `str`. Bloqueia o **tipo** `str` na Camada 2 e o pretty-printer na Camada 3, e a regra honesta é *emitir tipo DWARF só para layout CONGELADO*. | §4.4 |
| 6 | **O Windows colapsa de "segundo formato do zero" para "o mesmo DWARF, noutro contentor"** — verificado no `cmd/link/internal/ld/pe.go` do Go. **3 crumbs**, um deles sondagem. | §7.3 |
| 7 | **A contra-medida NÃO se paga: 13 crumbs para uma ferramenta, uma plataforma de cada vez, e ainda dependente das tabelas da Camada 2** — contra 14 crumbs que dão gdb + lldb + VSCode em três plataformas sem uma linha de servidor nosso. A recomendação de não fazer tem agora **prova**, não asserção. | §8 |
| 8 | **A condição de reabertura está NOMEADA e medida como NÃO satisfeita:** o gatilho do Go é escalonador/pilhas próprias. `concorrencia-adiantada-s8.md` desenha **threads 1:1 (`pthread_create`)**, que é exactamente o que gdb/lldb modelam nativamente. | §8.5 |

**A resposta directa à objeção do dono** (*"não faz sentido ter suporte de debugger sem as coisas
das outras camadas"*) está em §4.6 e §6: das três red-flags que o levaram a rejeitar desde a
Camada 0, **uma foi refutada por medição** (o `bt`), **duas nunca foram problemas do Piso** (`str`
e nomes-no-regalloc pertencem às Camadas 2 e 3). O que a objeção corrige de verdade — e corrige
bem — é que a Camada 1 antiga **vendia** o `bt` sem prova. Agora tem prova, fixture, e uma
sondagem nomeada para a perna que não medi.

---

# PEÇA 1 — A prova de conceito, que se corre

Tudo em `docs/design/debugger-poc/`. **Refiz o Experimento D do documento anterior de ponta a
ponta** e confirmo o número 4 — com a razão que lá faltava (§10.1).

## 1.1 Os ficheiros

| Ficheiro | O que é |
|---|---|
| `docs/design/debugger-poc/hello.tks` | o `.tks` de referência: `main` chama `add`, duas molduras para o `bt`. **Nunca é compilado** — é a fonte que o DWARF escrito à mão APONTA, e essa isolação é o ponto: o debugger lista texto Teko enquanto passa por código de máquina que nenhum compilador Teko produziu. Prova-se o DWARF, não o compilador. |
| `docs/design/debugger-poc/mini.s` | o objeto mínimo: `.text` com `add`+`main`, e as três seções de depuração escritas **byte a byte com `.byte`** — não geradas por ferramenta nenhuma. É literalmente o golden. |
| `docs/design/debugger-poc/adv.s` | a medição de desenrolar: cinco molduras através de **todas** as formas de frame do nosso encoder, sem CFI. |
| `docs/design/debugger-poc/reproduce.sh` | corre as duas provas e sai 0. Vive em `docs/`, não em `scripts/`: é documentação que executa, não portão. |

## 1.2 As linhas do `.tks` que o resto do documento cita

`add` declarada em **29**, primeiro statement em **30**, expressão de retorno em **31**, `}` em 32.
`main` declarada em **40**, chamada em **41**, retorno em **42**.

## 1.3 `.debug_abbrev` — 37 bytes (0x25), anotado campo a campo

```
01              ULEB  código de abreviatura 1
11              ULEB  DW_TAG_compile_unit
01                    DW_CHILDREN_yes
25 08                 DW_AT_producer      DW_FORM_string
13 05                 DW_AT_language      DW_FORM_data2
03 08                 DW_AT_name          DW_FORM_string
1b 08                 DW_AT_comp_dir      DW_FORM_string
11 01                 DW_AT_low_pc        DW_FORM_addr
12 07                 DW_AT_high_pc       DW_FORM_data8
10 17                 DW_AT_stmt_list     DW_FORM_sec_offset
00 00                 fim da lista de atributos da abreviatura 1
02              ULEB  código de abreviatura 2
2e              ULEB  DW_TAG_subprogram
00                    DW_CHILDREN_no
03 08                 DW_AT_name          DW_FORM_string
3a 0d                 DW_AT_decl_file     DW_FORM_udata
3b 05                 DW_AT_decl_line     DW_FORM_data2
11 01                 DW_AT_low_pc        DW_FORM_addr
12 07                 DW_AT_high_pc       DW_FORM_data8
3f 19                 DW_AT_external      DW_FORM_flag_present
00 00                 fim da lista de atributos da abreviatura 2
00                    fim da tabela de abreviaturas
```

**Duas escolhas de forma que o documento anterior não fixou e que a produção precisa:**
`DW_AT_decl_line` é **`DW_FORM_data2`**, não `data1` — o próprio orçamento anterior deu como
exemplo um breakpoint em `main.tks:410`, que **não cabe** em `data1`. E `DW_AT_decl_file` é
**`DW_FORM_udata`** (ULEB) porque Teko emite **um objeto por programa** com funções vindas de
**muitos** `.tks`, e a tabela de ficheiros do programa de linha passa de 255 entradas num corpus
do tamanho do nosso.

## 1.4 `.debug_info` — 107 bytes (0x6b), anotado campo a campo

```
67 00 00 00                       unit_length = 0x67 (103) = tamanho da seção menos estes 4 bytes
04 00                             version = 4
00 00 00 00                       debug_abbrev_offset = 0  (UMA CU por objeto -> literal, sem reloc)
08                                address_size = 8
                                  --- DIE da unidade de compilação ---
01                                código de abreviatura 1
74 65 6b 6f 20 30 2e 33 2e 31 00  DW_AT_producer  = "teko 0.3.1\0"
0c 00                             DW_AT_language  = 0x000c (DW_LANG_C99)  [ver §1.7]
68 65 6c 6c 6f 2e 74 6b 73 00     DW_AT_name      = "hello.tks\0"
2e 00                             DW_AT_comp_dir  = ".\0"                 [ver §1.7]
00 00 00 00 00 00 00 00           DW_AT_low_pc    -> RELOCAÇÃO 1: Abs64 contra `add`
3b 00 00 00 00 00 00 00           DW_AT_high_pc   = 0x3b (59) — um COMPRIMENTO (data8), sem reloc
00 00 00 00                       DW_AT_stmt_list = 0
                                  --- DW_TAG_subprogram `add` ---
02                                código de abreviatura 2
61 64 64 00                       DW_AT_name      = "add\0"
01                                DW_AT_decl_file = 1
1d 00                             DW_AT_decl_line = 29
00 00 00 00 00 00 00 00           DW_AT_low_pc    -> RELOCAÇÃO 2: Abs64 contra `add`
18 00 00 00 00 00 00 00           DW_AT_high_pc   = 0x18 (24) comprimento
                                  --- DW_TAG_subprogram `main` ---
02                                código de abreviatura 2
6d 61 69 6e 00                    DW_AT_name      = "main\0"
01                                DW_AT_decl_file = 1
28 00                             DW_AT_decl_line = 40
00 00 00 00 00 00 00 00           DW_AT_low_pc    -> RELOCAÇÃO 3: Abs64 contra `main`
23 00 00 00 00 00 00 00           DW_AT_high_pc   = 0x23 (35) comprimento
00                                fim dos filhos do DIE da CU
```

## 1.5 `.debug_line` — 89 bytes (0x59), anotado campo a campo

```
55 00 00 00                       unit_length = 0x55 (85)
04 00                             version = 4
21 00 00 00                       header_length = 0x21 (33) — daqui até ao primeiro opcode
01                                minimum_instruction_length = 1
01                                maximum_operations_per_instruction = 1   (só DWARF 4)
01                                default_is_stmt = 1     <- é isto que faz cada linha ser breakpoint
fb                                line_base  = -5
0e                                line_range = 14
0d                                opcode_base = 13
00 01 01 01 01 00 00 00 01 00 00 01   standard_opcode_lengths[1..12]
00                                include_directories: terminador da lista vazia
68 65 6c 6c 6f 2e 74 6b 73 00     file_names[1].name            = "hello.tks\0"
00                                file_names[1].directory_index = 0
00                                file_names[1].mtime           = 0
00                                file_names[1].length          = 0
00                                file_names: terminador da lista
                                  --- o programa de números de linha ---
00 09 02                          DW_LNE_set_address (op 0 estendido, ULEB comprimento 9, sub-op 2)
00 00 00 00 00 00 00 00           o seu operando de 8 bytes -> RELOCAÇÃO 4: Abs64 contra `add`
                                  (a ÚNICA relocação desta seção — ver §10.1)
03 1c                             DW_LNS_advance_line, SLEB +28  -> linha 29
05 01                             DW_LNS_set_column,   ULEB 1
01                                DW_LNS_copy                    => LINHA (offset 0x00, linha 29)
02 0a                             DW_LNS_advance_pc,   ULEB +10
03 01                             DW_LNS_advance_line, SLEB +1   -> linha 30
01                                DW_LNS_copy                    => LINHA (offset 0x0a, linha 30)
02 09                             DW_LNS_advance_pc,   ULEB +9
03 01                             DW_LNS_advance_line, SLEB +1   -> linha 31
01                                DW_LNS_copy                    => LINHA (offset 0x13, linha 31)
02 05                             DW_LNS_advance_pc,   ULEB +5   (entra em `main`, em 0x18)
03 09                             DW_LNS_advance_line, SLEB +9   -> linha 40
01                                DW_LNS_copy                    => LINHA (offset 0x18, linha 40)
02 08                             DW_LNS_advance_pc,   ULEB +8
03 01                             DW_LNS_advance_line, SLEB +1   -> linha 41
01                                DW_LNS_copy                    => LINHA (offset 0x20, linha 41)
02 12                             DW_LNS_advance_pc,   ULEB +18
03 01                             DW_LNS_advance_line, SLEB +1   -> linha 42
01                                DW_LNS_copy                    => LINHA (offset 0x32, linha 42)
02 09                             DW_LNS_advance_pc,   ULEB +9   (até 0x3b, um byte além do fim)
00 01 01                          DW_LNE_end_sequence
```

**Estes três blocos são o golden do `dwarf_test.tkt` do crumb D1.3.** Não são uma leitura da
especificação: são os bytes de um objeto que gdb 15.1 **e** lldb 18.1.3 aceitaram sem alteração.

## 1.6 O comando de ponta a ponta

```sh
cd docs/design/debugger-poc && ./reproduce.sh
```

E, minimamente, o que o dono cola para ver o breakpoint parar:

```sh
cd docs/design/debugger-poc
as -o mini.o mini.s && cc -o mini mini.o
gdb -batch -nx -ex "break hello.tks:30" -ex run -ex bt ./mini
```

Saída medida (gdb 15.1):

```
Breakpoint 1 at 0x1133: file hello.tks, line 30.
Breakpoint 1, add () at hello.tks:30
30	    let s = a + b
#0  add () at hello.tks:30
#1  0x0000555555555158 in main () at hello.tks:41
```

E o **mesmo objeto**, sem uma alteração, no lldb 18.1.3:

```
$ lldb -b -o "breakpoint set --file hello.tks --line 30" -o run -o bt ./mini
Breakpoint 1: where = mini`add + 10 at hello.tks:30:1, address = 0x0000000000001133
* thread #1, name = 'mini', stop reason = breakpoint 1.1
    frame #0: 0x0000555555555133 mini`add at hello.tks:30:1
   29  	fn add(a: i32, b: i32) -> i32 {
-> 30  	    let s = a + b
   31  	    s
  * frame #0: 0x0000555555555133 mini`add at hello.tks:30:1
    frame #1: 0x0000555555555158 mini`main at hello.tks:41:1
```

Nos dois: `print a` / `frame variable` falham — `No symbol "a" in current context` /
`no variable information is available in debug info for this compile unit`. **É a fronteira, e é
exactamente esse texto que a superfície tem de prometer e nada mais.**

## 1.7 Duas anotações honestas sobre o golden

**`DW_AT_comp_dir = "."`.** No PoC é `"."` de propósito, para que **todo** byte de `mini.s` seja
literal e o golden não dependa do host. Medido: gdb e lldb resolvem `./hello.tks` a partir do cwd,
logo o PoC funciona corrido de dentro do seu directório. **A produção tem de emitir a raiz de
projeto ABSOLUTA**, porque é o que dispensa `sourceFileMap`/`sourceMap` no VSCode (§3.3, §3.4).
Um `comp_dir` errado dá o meio-falho confuso: o breakpoint resolve e a **listagem** falha.

**`DW_AT_language = DW_LANG_C99`.** Não existe código DWARF atribuído a Teko. Escolhi C99 e não um
`lo_user`, e a razão é medível: com C99 o gdb diz `source language c` e o **seu analisador de
expressões fica ligado** — o que é a pré-condição de `print x` na Camada 2. Um `lo_user` daria
"linguagem desconhecida" e desligaria o analisador. Recomendo registar isto como decisão, não como
acidente: **C99 é o código de transporte** até haver um atribuído.

---

# PEÇA 2 — A superfície, desenhada até ao texto que o dono escreve

## 2.1 O que a CLI já aceita — medido, não suposto

`src/build/help.tks` e `src/build/project.tks`:

| forma | flags que a usam | onde |
|---|---|---|
| booleana longa | `--coverage`, `--no-verify`, `--cov-validation`, `--no-tty`, `--allow-undef`, `--release`, `--analyzer`, `--arith-cast-gate` | `coverage_of`, `no_verify_of`, … |
| valor separado | `-o <dir>`, `--per-commit-test <base>` | `out_dir_of` |
| **nivelada com `=`** | **`--opt=<0\|1\|2>`** | `opt_arg_has_prefix` (prefixo de 6 bytes) + `opt_level_of_value` |
| curta | `-o`, `-h`, `-v` | — |
| **rejeição honesta** | `--backend` / `--backend=…` — retirada, e **nomeada** em vez de silenciosamente ignorada | `is_backend_flag` + `has_backend_flag` |

**A restrição que decide o desenho, e que é uma armadilha silenciosa:** `project_arg_of` devolve o
**primeiro positional que não reconhece como flag**. Uma flag nova que não seja acrescentada à sua
lista de saltos passa a ser lida como **o caminho do projeto**. Isso não dá erro de flag
desconhecida — dá "projeto não encontrado", ou pior, constrói o projeto errado. **Qualquer crumb
que acrescente flag TEM de tocar `project_arg_of`, e o teste que o prova é obrigatório.**

**E há precedente vivo de que `-g` já é falado internamente:** `build_cc_argv(input, binary, m,
prog, debug, opt)` acrescenta `-g` quando `debug` é verdadeiro, e `run_cc_debug` /
`build_debug_binary` (o caminho de `teko run`) passam `true`. Ou seja **o perfil de depuração da
rota C existe e está ligado a `teko run`** — só não tem superfície de utilizador em `teko build`.
Isto muda o custo da Camada 0 para perto de zero **na rota C**: não é acrescentar `-g` ao `cc`, é
**expor** o booleano que já lá está.

## 2.2 A flag — `--debug=lines`, e `-g` REJEITADO com mensagem

**A referência de superfície é Rust**, e verifiquei-a antes de a invocar
(`doc.rust-lang.org/rustc/codegen-options/index.html`, citação literal):

> * `0` ou `none`: no debug info at all (the default).
> * `line-tables-only`: line tables only. **Generates the minimal amount of debug info for
>   backtraces with filename/line number info, but not anything else, i.e. no variable or function
>   parameter info.**
> * `1` ou `limited`: debug info without type or variable-level information.
> * `2` ou `full`: full debug info.
> * Note: The `-g` flag is an alias for `-C debuginfo=2`.

**Isto é o achado de superfície do documento.** A fronteira que o Experimento C mediu — breakpoint
e `bt` sim, variáveis não — **não é uma invenção nossa: é um nível de primeira classe, documentado
e nomeado, da referência que o dono atribuiu.** O nosso Piso **é** `line-tables-only`. E a mesma
citação diz que **`-g` significa `full`** em Rust, como em gcc e clang.

**Decisão, e é law-first:** uma flag `-g` que prometesse `full` e entregasse `line-tables-only`
seria exactamente o defeito que o dono acabou de rejeitar — uma superfície que promete demais. Logo:

| forma | efeito |
|---|---|
| **`--debug=lines`** | emite as três seções do Piso (tabela de linha + CU + um DIE por função). Nome tirado do vocabulário verificado do Rust. |
| **`--debug=none`** | sem informação de depuração. **O DEFAULT**, incluindo sob `--release`. Grafia explícita aceita para scripts que querem ser inequívocos. |
| **`-g`** | **REJEITADO, com mensagem que diz o que escrever.** O precedente é exacto e está no mesmo ficheiro: `has_backend_flag` rejeita `--backend` nomeando o motivo em vez de o ignorar. |
| valor desconhecido em `--debug=` | **ERRO DURO**, nunca uma degradação silenciosa para `none` (M.3). Note-se que isto é o **oposto** de `opt_level_of_value`, que resolve o malformado para 2; ali um palpite errado custa velocidade, aqui custa uma sessão de depuração inteira a perseguir um breakpoint que nunca resolveu. |

Quando a Camada 2 aterrar, acrescenta-se **`--debug=vars`** e **só então** `-g` passa a ser alias
legal de `vars` — a promessa de `-g` cumprida no dia em que for verdade. **Não há duas formas da
mesma operação**: `--debug=` é a única, com valores nomeados; `-g` é um alias que hoje não existe.

## 2.3 Nada no `teko.tkp` — medido, com precedente exacto

Não há precedente de perfil de construção no manifesto. `Manifest` tem `[artifact]`,
`[platforms]`, `[aliases]`, `[coverage]`, `[tests]`, `[extern]`, `[extern.libs.*]` — e
**otimização, o eixo mais próximo, é exclusivamente de CLI**. O doc-comment de `build_cc_argv`
legisla a razão em cinco palavras: *"Optimization is an AXIS of the build, not a global"*.
Informação de depuração é da mesma classe: dois `teko build` da mesma árvore têm de poder diferir
nela sem editar um ficheiro versionado. **Nenhuma chave nova no `.tkp`.**

## 2.4 O texto exacto do `--help`

Em `print_help_general` (depois da linha do `--release`) e em `print_help_build`, verbatim:

```
       --debug=lines          emit DWARF line info: breakpoint by .tks line, step, and backtrace
                              in gdb/lldb/VSCode. Does NOT include variables (see BUILDING.md)
       --debug=none           no debug information (the default, including under --release)
```

E a rejeição, em `main.tks`, antes de `project_arg_of` correr (o mesmo sítio onde `--backend` é
rejeitado hoje):

```
teko: `-g` is not a Teko flag.
      In gcc, clang and rustc `-g` means FULL debug information (types and variables), and this
      compiler cannot emit that yet — promising it would be a lie.
      Use `--debug=lines` for the line table (breakpoint by .tks line, step, backtrace).
```

## 2.5 Onde vivem os bytes — medido, e a resposta é "não no `.tkl`"

O `.tkl` é um ZIP-STORE com **exactamente três entradas**: `<name>.tkh`, `<name>.tkb`,
`<name>.tsym` (`project.tks`, o bloco `C7.12`). **Não contém objeto nenhum.** O `.tsym` também é
escrito solto ao lado do binário como `<binário>.tsym`, em quatro sítios distintos.

Portanto: **DWARF vai dentro do objeto/binário e em mais lado nenhum.** Não entra no `.tkl`,
porque não há lá objeto a que se agarrar; e não é um ficheiro ao lado, porque a nossa rota liga com
`cc` que já traz as seções do `.o` para o executável (medido — foi assim que o PoC funcionou).
**O `.tkl` fica intocado por este desenho**, o que é uma boa notícia de reversibilidade.

`.tsym` e `.debug_line` **não são rivais**: `.tsym` é por-função (`file:line` da declaração),
`.debug_line` é por-endereço. §9.3 diz o que o `.tsym` se torna depois do Piso.

## 2.6 A regra escrita sobre `--release`

Três frases, para entrarem em `docs/BUILDING.md` e no doc-comment da função de resolução:

1. **A informação de depuração nunca é imposta.** O default é `--debug=none` em **todos** os
   comandos e perfis, `--release` incluído. Ela cresce o objeto e um release não a paga sem pedido.
2. **`--release --debug=lines` é LEGAL e não é recusado.** É exactamente como se depura um crash de
   release, e recusá-lo obrigaria a reconstruir com outro perfil — que muda o código e apaga o
   defeito. Os dois eixos são ortogonais **por medição**: na rota nativa não temos otimizador, logo
   não há reordenação que faça a tabela de linha mentir; na rota C o `-g -O2` do host é o par que
   toda a gente já usa.
3. **`teko run` continua com o seu perfil de depuração** (é o que faz hoje via `run_cc_debug`), e
   **não** passa a exigir a flag — a flag é para `build`/bare, onde hoje não há como pedir.

## 2.7 As formas em Teko que o implementador acrescenta

`void` é banido (nenhuma destas devolve nada sem valor) e não há sobrecarga (cada operação, um nome).

```teko
/**
 * DebugInfo — how much debug information a build emits, the `--debug=<value>` axis.
 *
 * An ENUM and not a bool because the axis is LEVELLED at the reference this surface mirrors:
 * rustc documents `line-tables-only` ("the minimal amount of debug info for backtraces with
 * filename/line number info, but not anything else") as a first-class level distinct from
 * `full`. `Lines` IS that level. When variables land, a `Vars` member joins here and no
 * existing spelling changes meaning — which is the whole reason this is not a bool.
 *
 * @since 0.3.1 debugger camada 1
 * @see opt_level_of  the sibling build AXIS, likewise CLI-only and absent from `teko.tkp`
 */
pub type DebugInfo = enum { None; Lines }

/**
 * debug_arg_has_prefix — does `a` begin with the `--debug=` selector prefix, so its remainder is
 * the requested level?
 *
 * An 8-byte prefix, guarded on length so the slice never runs past the string — the exact shape
 * of `opt_arg_has_prefix`, whose 6-byte guard this mirrors deliberately: two selector flags that
 * parse differently is how one of them ends up wrong.
 *
 * @param a  a single CLI argument token
 * @return   true iff `a` is a `--debug=<value>` form
 */
fn debug_arg_has_prefix(a: str) -> bool {
    a.len > 8 && teko::str::slice_to(a, 8) == "--debug="
}

/**
 * debug_info_of_value — map the text after `--debug=` to a level, or FAIL.
 *
 * DELIBERATELY UNLIKE `opt_level_of_value`, which resolves a malformed suffix to level 2. An
 * unrecognised optimization level costs speed; an unrecognised DEBUG level costs a whole
 * debugging session spent chasing a breakpoint that silently never resolved, which is the
 * "teaches wrong" failure this surface exists to avoid. M.3: no silent coercion.
 *
 * @param v  the substring after `--debug=`
 * @return   the level for "none"/"lines", else an honest error naming both accepted values
 * @throws   when `v` is neither "none" nor "lines"
 */
fn debug_info_of_value(v: str) -> DebugInfo | error {
    if v == "none" { return DebugInfo::None }
    if v == "lines" { return DebugInfo::Lines }
    error { message = teko::str::concat("teko: --debug=", teko::str::concat(v, ": unknown level — accepted: none, lines")) }
}

/**
 * debug_info_of — resolve the requested debug level from the build flags.
 *
 * With no `--debug=` flag the level is `None` — the DEFAULT AT EVERY PROFILE, `--release`
 * included: debug information grows the object and is never imposed. The LAST matching flag on
 * the line wins, matching `opt_level_of`'s rule so the two axes behave the same way under
 * repetition.
 *
 * `--release --debug=lines` is a LEGAL, SUPPORTED pair and is not refused: it is how a release
 * crash gets debugged, and refusing it would force a rebuild at another profile — which changes
 * the code and erases the defect.
 *
 * @param args  the full CLI argument vector
 * @return      the resolved level (`None` by default), or the honest error from a bad value
 * @throws      when a `--debug=` value is not a recognised level
 */
fn debug_info_of(args: []str) -> DebugInfo | error {
    mut i: u64 = 0
    mut level = DebugInfo::None
    loop {
        if i >= args.len { break }
        if debug_arg_has_prefix(args[i]) {
            level = match debug_info_of_value(teko::str::slice_from(args[i], 8)) {
                DebugInfo as d => d
                error as e => return e
            }
        }
        i = i + 1
    }
    level
}

/**
 * has_bare_g_flag — does a bare `-g` appear anywhere in `args` from `start`?
 *
 * Consulted at CLI dispatch BEFORE `project_arg_of` runs, so a `-g` gets the honest rejection
 * that names `--debug=lines` instead of being read as the project path. `-g` is refused rather
 * than aliased because in gcc, clang and rustc it means FULL debug information (rustc documents
 * it as an alias for `-C debuginfo=2`), and this compiler cannot emit that yet — an alias would
 * promise types and variables and deliver a line table.
 *
 * The precedent is exact and lives in this same file: `has_backend_flag` refuses the retired
 * `--backend` by NAMING the reason rather than silently ignoring it.
 *
 * @param args   the full CLI argument vector
 * @param start  the index to begin scanning from (2 for a subcommand, 1 for a bare project)
 * @return       true iff a bare `-g` token appears anywhere from `start` onward
 */
fn has_bare_g_flag(args: []str, start: u64) -> bool {
    mut i = start
    loop {
        if i >= args.len { break }
        if args[i] == "-g" { return true }
        i = i + 1
    }
    false
}
```

**E a linha que não pode faltar**, dentro de `project_arg_of`, sob pena da armadilha de §2.1:

```teko
        else if debug_arg_has_prefix(args[i]) { i = i + 1 }
```

---

# PEÇA 3 — Uso em cada debugger: quatro, completos, coláveis

Pressuposto único: `teko build . --debug=lines -o bin`, com o `DW_AT_comp_dir` absoluto de §1.7.
O exemplo assume um projeto cujo `main` vive em `src/main.tks` e cujo binário sai em `bin/hello`.

## 3.1 gdb no terminal

```sh
teko build . --debug=lines -o bin
gdb ./bin/hello
```

```
(gdb) break src/main.tks:41
Breakpoint 1 at 0x1133: file src/main.tks, line 41.
(gdb) run
Breakpoint 1, add () at src/main.tks:41
41	    let s = a + b
(gdb) next
42	    s
(gdb) bt
#0  add () at src/main.tks:42
#1  0x0000555555555158 in main () at src/main.tks:52
(gdb) info line
Line 42 of "src/main.tks" starts at address 0x555555555146 <add+19> ...
(gdb) continue
```

Numa linha só, para script ou para colar:

```sh
gdb -batch -ex "break src/main.tks:41" -ex run -ex bt ./bin/hello
```

**O que NÃO funciona:** `print s` → `No symbol "s" in current context`. `info locals` → `No symbol
table info available.` `ptype` de qualquer coisa nossa → nada. Isto é literal, medido em §1.6, e é
a fronteira do Piso.

## 3.2 lldb no terminal — sintaxe diferente, mesmo objeto

```sh
teko build . --debug=lines -o bin
lldb ./bin/hello
```

```
(lldb) breakpoint set --file src/main.tks --line 41
Breakpoint 1: where = hello`add + 10 at src/main.tks:41:1, address = 0x0000000000001133
(lldb) run
(lldb) next
(lldb) bt
(lldb) continue
```

A forma curta (`b src/main.tks:41`) também resolve. Numa linha:

```sh
lldb -b -o "breakpoint set --file src/main.tks --line 41" -o run -o bt ./bin/hello
```

**O que NÃO funciona:** `frame variable` → `no variable information is available in debug info for
this compile unit`. `expression s` falha. `v` (o alias) idem.

**Duas diferenças de forma face ao gdb, que valem entrar na documentação porque custam tempo a
descobrir:** o lldb reporta `ficheiro:linha:coluna` (usa o `DW_LNS_set_column` que emitimos, o gdb
ignora-o); e o lldb desenrola **para dentro da libc** por omissão, logo o `bt` do Piso tem 8
molduras, não 2 — as 5 extra são da libc e do `_start` e são **corretas**, não ruído nosso.

## 3.3 VSCode via `cppdbg` (Linux) — `launch.json` completo

**A peça que falta em qualquer tutorial e sem a qual nada disto funciona:** o VSCode **recusa pôr
breakpoint num ficheiro cuja linguagem não conhece**. `.tks` não tem extensão de linguagem
registada, logo o gutter não aceita o clique. Sem a linha abaixo, o resto do ficheiro é inútil.

`.vscode/settings.json`

```json
{
  "debug.allowBreakpointsEverywhere": true,
  "files.associations": { "*.tks": "plaintext", "*.tkt": "plaintext" }
}
```

`.vscode/tasks.json`

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "teko: build --debug=lines",
      "type": "shell",
      "command": "teko",
      "args": ["build", ".", "--debug=lines", "-o", "bin"],
      "options": { "cwd": "${workspaceFolder}" },
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": []
    }
  ]
}
```

`.vscode/launch.json`

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "teko: depurar (gdb / cppdbg)",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/bin/hello",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "preLaunchTask": "teko: build --debug=lines",
      "sourceFileMap": {},
      "setupCommands": [
        {
          "description": "onde procurar os .tks quando o comp_dir nao bastar",
          "text": "-gdb-set directories ${workspaceFolder}",
          "ignoreFailures": true
        },
        {
          "description": "nao parar no primeiro SIGTRAP do arranque dinamico",
          "text": "-gdb-set breakpoint pending on",
          "ignoreFailures": true
        }
      ]
    }
  ]
}
```

Requer a extensão `ms-vscode.cpptools`. `sourceFileMap` fica **vazio de propósito**: com o
`DW_AT_comp_dir` absoluto de §1.7 não há nada para remapear — e é esse o argumento de fundo para a
decisão de §1.7.

**O que NÃO funciona na Camada 1:** o painel **Variables** fica vazio (nem `Locals` nem `Registers`
úteis); **Watch** responde `-var-create: unable to create variable object` a qualquer expressão;
`Debug Console` com `-exec print x` dá o mesmo `No symbol` de §3.1; **hover** sobre um identificador
não mostra valor. **Funciona:** breakpoints no gutter, Continue/StepOver/StepInto/StepOut, o painel
**Call Stack** com nomes Teko, e clicar numa moldura para saltar para o `.tks`.

## 3.4 VSCode via CodeLLDB (macOS) — `launch.json` completo

`.vscode/launch.json`

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "teko: depurar (lldb / CodeLLDB)",
      "type": "lldb",
      "request": "launch",
      "program": "${workspaceFolder}/bin/hello",
      "args": [],
      "cwd": "${workspaceFolder}",
      "env": {},
      "stopOnEntry": false,
      "terminal": "integrated",
      "preLaunchTask": "teko: build --debug=lines",
      "sourceMap": {},
      "initCommands": [
        "settings set target.inline-breakpoint-strategy always"
      ],
      "sourceLanguages": ["c"]
    }
  ]
}
```

O `settings.json` de §3.3 (`debug.allowBreakpointsEverywhere`) é **igualmente obrigatório**.
Requer a extensão `vadimcn.vscode-lldb`.

**Onde difere do `cppdbg`, campo a campo** — não é o mesmo ficheiro com o `type` trocado:

| `cppdbg` | CodeLLDB | porquê |
|---|---|---|
| `"type": "cppdbg"` + `"MIMode": "gdb"` + `"miDebuggerPath"` | `"type": "lldb"`, **sem** `MIMode` nem caminho | o `cppdbg` **conduz** o gdb por MI; o CodeLLDB **embute** o lldb via API. Não há binário externo a apontar. |
| `"stopAtEntry"` | `"stopOnEntry"` | grafias diferentes; a errada é ignorada em silêncio |
| `"environment": [{"name":…,"value":…}]` | `"env": { "K": "V" }` | array de registos vs. objeto |
| `"sourceFileMap"` | `"sourceMap"` | mesma função, nome diferente |
| `"setupCommands"` com `"text": "-gdb-set …"` (MI) | `"initCommands"` / `"preRunCommands"` com comandos **de lldb** | o vocabulário é outro: `-gdb-set` não existe no lldb |
| — | **`"sourceLanguages": ["c"]`** | só o CodeLLDB tem isto, e **importa para nós**: casa com o `DW_AT_language = DW_LANG_C99` de §1.7 e é o que liga o realce e o analisador de expressões no painel |
| — | `"terminal"` | `"externalConsole"` é do `cppdbg` |

**O que NÃO funciona na Camada 1:** o painel **Variables** mostra a mensagem
`no variable information is available in debug info for this compile unit` — o CodeLLDB é mais
honesto que o `cppdbg` aqui, que mostra vazio. **Watch** e **hover** falham igualmente. **Funciona:**
o mesmo conjunto de §3.3.

## 3.5 A frase que os quatro têm de levar na documentação

> `--debug=lines` responde **"onde estou e como cheguei aqui"**. Não responde **"quanto vale `x`"**.
> Se o painel Variables está vazio, não é defeito — é este nível. O nível que responde a isso ainda
> não existe, e quando existir chamar-se-á `--debug=vars`.

Recusa nomeada, não omissão — a regra que o expurgo já fixou para o MinGW.

---

# PEÇA 6 — A fronteira redesenhada por HONESTIDADE (o ruling do dono)

**Regra nova, aplicada a todas as camadas:** *uma camada só pode CLAMAR o que consegue GARANTIR;
se a garantia depende de outra camada, ou a garantia sai da lista, ou a dependência entra na
camada.* O dono está certo sobre a causa: as camadas antigas foram cortadas por custo. Abaixo,
cada red-flag foi **medida**, e o corte refeito.

## 4.1 RED-FLAG 3 — REFUTADA, com medição, em x86-64

A afirmação a derrubar era: *"as nossas funções têm duas formas de frame e uma função sem `rbp` em
pilha profunda pode derrotar a heurística de análise de prólogo"*.

**Primeiro, o argumento estrutural, lido no código.** `encode_x86_64.tks:1532`:

```teko
fn frame_is_framed_x86(layout: FrameLayoutX86) -> bool {
    let n_saved = (layout.saved_gpr.len + layout.saved_fpr.len) to u32
    (layout.size > (0 to u32)) || (n_saved > (0 to u32)) || layout.call_align
}
```

e `needs_call_align_x86(f) = func_makes_call_x86(f) && func_any_ret_x86(f)`. Logo:

* `layout.size > 0` (há slots ou região de argumentos) ⇒ **framed** ⇒ `push rbp; mov rbp,rsp`.
* há registo callee-saved ⇒ **framed**.
* a função **faz uma chamada** e retorna ⇒ `call_align` ⇒ **framed**.
* **uma função frameless emite prólogo e epílogo VAZIOS** (`emit_prologue_x86` /
  `emit_epilogue_x86` devolvem lista vazia).

**A consequência é um invariante, não uma esperança: RSP só se move dentro de uma função que já
pôs `rbp`. Uma função frameless devolve RSP exactamente como o recebeu.** É o caso mais fácil que
existe para um analisador de prólogo, não o mais difícil.

**Segundo, a medição.** `docs/design/debugger-poc/adv.s` constrói uma cadeia de cinco molduras que
percorre **todas** as formas, **incluindo o único furo teórico** (a função frameless que *chama* e
nunca retorna — `func_any_ret_x86` falso), e confirma que o objeto **não tem `.eh_frame`**:

| moldura | forma | bytes |
|---|---|---|
| `#0 lvl4` | **frameless leaf** — zero bytes de prólogo | `mov $7,%eax; ret` |
| `#1 lvl3` | **framed com callee-saved DEPOIS de `mov rbp,rsp`, e `sub rsp`** — a nossa ordem exacta | `push %rbp; mov %rsp,%rbp; push %rbx; push %r12; sub $0x20,%rsp` |
| `#2 lvl2` | **frameless CALLER** — chama e nunca retorna, o furo | `call lvl3; …; hlt` |
| `#3 lvl1` | **framed só por alinhamento** (`size` 0, zero callee-saved) | `push %rbp; mov %rsp,%rbp; sub $0,%rsp` |
| `#4 main` | framed | — |

gdb 15.1, sem uma linha de CFI:

```
Breakpoint 1, lvl4 () at hello.tks:30
#0  lvl4 () at hello.tks:30
#1  0x000055555555514f in lvl3 () at hello.tks:42
#2  0x0000555555555163 in lvl2 () at hello.tks:20
#3  0x0000555555555181 in lvl1 () at hello.tks:17
#4  0x000055555555518c in main () at hello.tks:41
```

lldb 18.1.3, **o mesmo objeto**, as mesmas cinco molduras (mais as 4 da libc). E os **dois pontos
de fronteira de prólogo**, que são o modo de falha real de um analisador de prólogo:

* breakpoint no **primeiro byte** de `lvl3`, **antes** do `push %rbp` — 4 molduras corretas;
* breakpoint **entre** o `push %rbp` e o `mov %rsp,%rbp` (`lvl3+1`) — 4 molduras corretas.

**Veredicto: o `bt` FICA na lista de entregas do Piso em x86-64, e agora com prova e fixture — não
com asserção. A CFI NÃO desce.** A red-flag 3 estava errada, e o que a tornava plausível era não
ter sido medida.

## 4.2 O que sobra por medir, e é honesto dizê-lo: arm64

**Não medi arm64 e não posso**: não há `aarch64-linux-gnu-as`, `qemu-aarch64` nem toolchain cruzado
neste host. O que **posso** afirmar é o invariante, lido em `encode_arm64.tks:1514`:

```
sub sp, sp, #size
stp x29, x30, [sp, #size-16]
add x29, sp, #size-16
```

`x29` fica a apontar **para** o par salvo, logo `[x29]` = `x29` do chamador e `[x29+8]` = endereço
de retorno — **exactamente o invariante de cadeia de molduras do AAPCS64**, mesmo que a nossa
colocação do par (no topo do frame) difira do idiomático `stp …,[sp,#-16]!` (na base). O andar da
cadeia é idêntico.

**Isso é um argumento, e um argumento não é uma medição.** Vira o **crumb D1.7**, primeiro da
perna arm64, com dois ramos orçados em §5.1.

## 4.3 RED-FLAG 2 — redimensionada, e o perigo verdadeiro é outro

A red-flag dizia: *"a cadeia é `LEnv(nome→vreg)` → `regalloc(vreg→registo ou slot)` →
`compute_frame_layout(slot→deslocamento)`; cada elo existe, nenhum carrega o nome"*. Verdade — e
**irrelevante**, porque medi a chave:

```teko
pub fn assign_lookup(sr: ScanResult, vreg_id: u32) -> AssignLookup   // regalloc.tks:1475
pub type InReg   = struct { vreg_id: u32; phys: u32 }
pub type Spilled = struct { vreg_id: u32; slot: u64 }
```

`ScanResult` é **indexado por `vreg_id`** e `assign_lookup` é **pública**. O nome não tem de
*viajar* por nenhum elo: uma tabela lateral `(nome, vreg_id)` capturada no lowering **junta-se** ao
`ScanResult` depois do scan, pela chave que já existe. **É um JOIN, não plumbing** — e nenhum
`match inst` do backend é tocado. O orçamento da Camada 2 desce por causa disto.

**Mas há um perigo que a red-flag não nomeou e que é pior.** Uma alocação `InReg` é válida **apenas
dentro do intervalo de vida do vreg**. Fora dele o registo físico já tem outra coisa. Um
`DW_AT_location` que dissesse "`x` está em `rbx`" faria `print x` imprimir, **em silêncio e com
confiança**, o valor de outra variável. Isso não é uma camada em falta: **é uma camada que ensina
errado**, e é precisamente o defeito que o dono rejeitou.

**A resolução honesta já está no código.** `src/lir/lower.tks:113`:

```teko
pub fn lenv_bind_scalar_slot(env: LEnv, name: str, slot: u32, ty: LType) -> LEnv
```

Um local nomeado **pode já ser ligado a um slot de frame** com o seu `LType`. Sob
`--debug=vars`, o perfil de depuração **fixa todo local nomeado a um slot**, e a localização
DWARF passa a ser um único `DW_OP_fbreg <offset>` válido em **todo** o escopo. É literalmente o que
`cc -O0` faz, e é por isso que `print a` funcionou no Experimento A do documento anterior. **A
Camada 2 honesta é mais barata que a Camada 2 rápida, e mais correta.**

## 4.4 RED-FLAG 1 — confinada, e a regra que a torna inofensiva

Estado medido de `str`: `src/runtime/teko_rt.h:44-48` define `tk_str` como **duas** words, com o
comentário `length in BYTES`. `docs/memory/raiz-comum-dos-degraus-0.3.1.0.md:29` registra a decisão
do dono de que `.len` conta **CARACTERES** e que `str` leva **os dois contadores** — terceira word.
**Decidido, não aterrado.**

Três perguntas do ruling, três respostas medidas:

1. **Afeta a listagem de fonte?** **NÃO.** A listagem é o debugger a ler o `.tks` do disco. Medido
   em §1.6: o lldb imprimiu as linhas 27–33 de `hello.tks` num processo onde não existe um `str`
   Teko. Zero acoplamento.
2. **Afeta a tabela de linha?** **NÃO.** A tabela é `(offset, linha, coluna)` — inteiros.
3. **Afeta o painel Variables da Camada 2?** **SIM, e só nos locais de tipo `str`.** Um
   `DW_TAG_structure_type` de duas words escrito hoje erra **duas vezes** quando a terceira word
   aterrar: no tamanho e no significado de `len`. Um local `u64` é indiferente.

**A regra que resolve, e que é generalizável em vez de ser um remendo para `str`:**

> **Só se emite tipo DWARF para tipo cujo layout esteja CONGELADO.** Um local cujo tipo não está
> congelado **não recebe `DW_TAG_variable` nenhum** — o debugger diz `No symbol "s" in current
> context`, que é honesto, em vez de mostrar lixo com confiança.

Com essa regra, `str` **deixa de bloquear a Camada 2**: bloqueia os locais de tipo `str`, e o resto
avança. A Camada 3 (pretty-printer) continua bloqueada, porque um pretty-printer de `str` **é** o
layout de `str`. **Isto é uma correção ao orçamento anterior**, que fazia `str` parecer
pré-condição de mais do que é.

## 4.5 E a CFI, se algum dia for preciso — o número, porque o ruling pediu

Se a medição de §4.2 correr mal em arm64, ou se algum dia se quiser desenrolar sem heurística:

| crumb | conteúdo | prova |
|---|---|---|
| **F1** | `dwarf.tks` ganha o escritor de `.debug_frame`: uma CIE (fixa, ~32 bytes) + uma FDE por função | golden de bytes da CIE + de uma FDE de cada forma |
| **F2** | `FrameLayoutX86` → programa de CFA: framed = `DW_CFA_advance_loc` + `DW_CFA_def_cfa_offset 16` + `DW_CFA_offset rbp,-16` + `DW_CFA_def_cfa_register rbp`; frameless = **nada além dos defaults da CIE** | `encode_x86_64_test.tkt` sobre as quatro formas de `adv.s` |
| **F3** | `FrameLayout` (arm64) → o mesmo, com `x29`/`x30` | idem na perna arm64 |
| **F4** | routing da seção nos escritores ELF e Mach-O | golden + `readelf --debug-dump=frames` |

**4 crumbs. E muda um número do Experimento D:** cada FDE precisa de um `initial_location`, logo
**+1 `Abs64` por função** — o total deixa de ser 4 e passa a **4 + N**. Continua a ser um único
kind, o `Abs64` que já emitimos; nenhum kind novo.

**Nota de escolha, para não a fazer por acidente:** `.debug_frame` (não alocada, formato simples)
vs. `.eh_frame` (`SHF_ALLOC`, augmentation `zR`, codificações de ponteiro, e **consumida em tempo
de execução**). Recomendo `.debug_frame` para depuração. `.eh_frame` é um item **maior e separado**,
e compra outra coisa: **stack traces nativos corretos em produção**, hoje servidos pelo `.tsym`.
Não os misturar.

## 4.6 A fronteira nova

| Camada | Clama | Garante? | Veredicto |
|---|---|---|---|
| **Piso** (Camada 1 redefinida) | breakpoint por linha `.tks`, listagem, `step`/`next` | **sim** — medido §1.6 | fica |
| **Piso** | `bt` com nomes Teko, x86-64 | **sim** — medido §4.1, 5 molduras, todas as formas, zero CFI | **fica, agora com prova** |
| **Piso** | `bt` com nomes Teko, arm64 | **por medir** | fica **condicionado ao D1.7**; até lá a documentação diz "medido em x86-64" |
| **Piso** | `print x` | **não** | **sai da lista, e a saída é NOMEADA** no `--help`, nos quatro `launch.json` e em §3.5 |
| **Camada 2** | `print x`, membros de struct | sim **se** locais fixados a slot (§4.3) | a fixação a slot **entra na camada**; não é opcional |
| **Camada 2** | tipos | sim **se** só para layout congelado (§4.4) | a regra do congelamento **entra na camada**; `str` fica de fora até aterrar |
| **Camada 3** | `str` legível, união pelo membro ativo | **não hoje** | bloqueada por `str`; e ver §9.4 — a via do `variant_part` **não se aplica** |

---

# PEÇA 4 — As camadas restantes, orçadas com número

Unidade: **crumb** (passo pequeno, prova própria, reversível). Cada linha tem ficheiro e prova.

## 5.1 O Piso — 8 crumbs (era 6)

D0.1…D1.6 mantêm-se como no orçamento anterior (§5 de lá) com **duas alterações** e **um crumb
novo**:

* **D0.1 (arnês) ganha uma afirmação a mais, e é a que defende §4.1:** o arnês afirma a
  profundidade do `bt` **através de uma função frameless**, não só ">= N". O fixture é `adv.s`.
  Sem isso a refutação de §4.1 é verdadeira hoje e indefesa amanhã.
* **D1.3 (`src/backend/dwarf.tks`) tem o golden já fixado por este documento** — §1.3, §1.4, §1.5,
  bytes, não prosa. Este crumb **não colide com nada** e **não está bloqueado por nada**: pode
  começar hoje.
* **D1.6 (superfície)** passa a ser o que a Peça 2 e a Peça 3 desenham: `DebugInfo`,
  `debug_info_of`, a rejeição de `-g`, a linha em `project_arg_of`, os dois blocos de `--help`, e
  os **quatro** `launch.json`/`settings.json`/`tasks.json` de §3.3 e §3.4 em `docs/`.

**D1.7 — CRUMB NOVO: a sondagem de desenrolar em arm64**

* **Mexe em:** nada em `src/`. É um `.s` de sondagem irmão de `adv.s`, corrido na perna aarch64
  (`cargo/0.3.1-aarch64-elf` já tem lane).
* **O que mede:** as cinco molduras de `adv.s` traduzidas para a nossa forma de prólogo arm64
  (`sub sp; stp x29,x30,[sp,#top]; add x29,sp,#top`), com lldb e gdb, sem CFI.
* **Ramos orçados:**
  * **se a cadeia de `x29` recuperar (o esperado, §4.2): +0 crumbs**, e o Piso fica em 8.
  * **se não recuperar: +4 crumbs** (F1…F4 de §4.5) **ou** — e esta é a alternativa honesta e
    barata — **retirar a clamação de `bt` da perna arm64** na documentação até alguém comprar a CFI.
    A escolha é do dono; as duas são honestas, a de hoje (clamar sem medir) não é.
* **Porquê primeiro na perna arm64:** porque é a única clamação do Piso sem prova.

## 5.2 Camada 2 — 6 crumbs, e a sondagem já foi feita (por leitura)

O orçamento anterior disse "5+ crumbs, um deles perigoso" e pôs a sondagem como primeiro crumb.
**A sondagem está feita em §4.3**: a chave é `vreg_id`, `assign_lookup` é pública, é um JOIN. O que
resta é orçável **directamente**, e o item perigoso mudou de identidade — não é o plumbing, é a
**validade da localização**.

| crumb | mexe em | prova |
|---|---|---|
| **D2.1** | `src/lir/lower.tks` — sob o perfil de depuração, todo `let` nomeado passa por `lenv_bind_scalar_slot` em vez de `lenv_bind`. O mecanismo **já existe**; o crumb é a bifurcação por perfil. | `lower_test.tkt`: sob o perfil, o `LEnv` de um `let` escalar tem `is_scalar_slot = true`; sem o perfil, o LIR é **byte-idêntico** ao de hoje. A segunda metade é o que torna o crumb seguro. |
| **D2.2** | `src/lir/lir.tks` — `LFunc` ganha `local_names: []str`, `local_slots: []u32`, `local_types: []LType`, `local_decl_lines: []u32`, populados de `LEnv` no fim de `lower_function`. | `lower_test.tkt`: uma função com dois `let` e um shadow produz **três** entradas, na ordem de ligação. O shadow é o teste que importa. |
| **D2.3** | `src/backend/dwarf.tks` — `DW_TAG_variable` / `DW_TAG_formal_parameter` com `DW_AT_location` = `DW_OP_fbreg <sleb>`, e `DW_TAG_lexical_block` a delimitar shadows. O offset vem de `compute_frame_layout`; a base é `DW_AT_frame_base` = `DW_OP_call_frame_cfa` (sem CFI, usa-se `DW_OP_reg6`/`rbp` — decisão do crumb, com a nota de §4.5). | golden de bytes + o arnês estendido: `gdb -ex "print s"` devolve o **valor**, não `No symbol`. |
| **D2.4** | `src/backend/dwarf.tks` — o **registo de tipos congelados** e `DW_TAG_base_type` para os escalares (`i8…i64`, `u8…u64`, `f32`, `f64`, `bool`, `byte`). A lista de congelados é **explícita e enumerada**, nunca "tudo o que não sabemos que muda". | `dwarf_test.tkt`: um `LType` **não** congelado produz **nenhum** `DW_AT_type` e **nenhum** `DW_TAG_variable`; um congelado produz o DIE esperado. O ramo negativo é metade do crumb. |
| **D2.5** | `src/backend/dwarf.tks` — `DW_TAG_structure_type` + `DW_TAG_member` para agregados **congelados**. `str` **excluído por regra** (§4.4), não por esquecimento. | golden; e `gdb -ex "print p.x"` sobre um struct de dois `i64`. |
| **D2.6** | superfície: `--debug=vars`, e **só aqui** `-g` passa a ser alias legal (§2.2). `project_arg_of` já cobre `--debug=`. | `help_test.tkt` + o teste que prova que `-g` deixou de ser rejeitado. |

**Total: 6 crumbs.** Nenhum deles é o penhasco que o orçamento anterior desenhou, e a razão é
inteiramente §4.3 + §4.4: a fixação a slot mata os intervalos de vida, e o registo de congelados
mata a re-tipificação do LIR.

**O que a Camada 2 ainda NÃO dá, dito antes de ser prometido:** genéricos monomorfizados (o DIE
leva o nome resolvido, não o genérico), uniões (§9.4), e `str` (§4.4).

## 5.3 Camada 3 — 3 crumbs, e é 3, não 1

**Bloqueio nomeado como pré-condição:** `str` de três words aterrado. M.4 resolve por sequenciamento
— não é tensão para o dono, é ordem que a lei determina.

| crumb | conteúdo |
|---|---|
| **D3.1** | o ficheiro de pretty-printers **do gdb** (Python) + a seção `.debug_gdb_scripts` que o auto-carrega do binário. O `cppdbg` carrega printers do gdb, logo o VSCode Linux vem de graça. |
| **D3.2** | os **três** rails de união (§9.4): tagged inline, niche, box-em-arena. Um branch cada. |
| **D3.3** | **o ficheiro de synthetic providers do lldb** — API **diferente**, ficheiro **diferente**. |

**D3.3 é uma correção ao orçamento anterior, e vem da referência verificada:** Rust ship**a os
dois** — `gdb_load_rust_pretty_printers.py` **e** `lldb_lookup.py`/`lldb_providers.py`. Não há um
ficheiro que sirva gdb e lldb. O "1 crumb" de lá subestimava a **paridade de plataforma**, que é
exactamente onde o Piso tem a sua maior virtude (um escritor, dois debuggers) e a Camada 3 não tem.

**Custo depois de desbloqueado: 3 crumbs.** Antes: zero — e **o código escrito antes nasce errado**,
não meio-certo.

## 5.4 Windows — 3 crumbs, e o formato é DWARF, não CodeView

**Este é o número que mais mudou, e mudou por verificação de referência.** O `pe.go` do Go
(`cmd/link/internal/ld/pe.go`, lido) põe **DWARF dentro do PE**:

* `pefile.addDWARF()` é chamado na montagem do PE;
* *"DWARF section names are longer than 8 characters. PE format requires such names to be stored
  in string table"* → *"section names replaced with slash (/) followed by correspondent string
  table index"*;
* `h.characteristics = IMAGE_SCN_ALIGN_1BYTES | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_DISCARDABLE |
  IMAGE_SCN_CNT_INITIALIZED_DATA`;
* **nenhum CodeView, nenhum PDB.**

**Portanto CodeView só é necessário para debuggers de TERCEIROS que não leem DWARF** — WinDbg e o
depurador do Visual Studio. gdb e lldb em Windows leem DWARF-em-PE. Isto **corrige o §7 do
orçamento anterior**: Windows deixa de ser "um segundo formato de informação de depuração, do zero,
sem reaproveitar nada da Camada 1" e passa a ser **os mesmos bytes, noutro contentor**.

Medi o nosso lado. `src/backend/objfile_coff.tks`:

* **existe** máquina de string table: `CoffStrtab`, `build_coff_strtab`, com prefixo de 4 bytes —
  **mas só para nomes de SÍMBOLO**;
* nomes de **seção** usam `emit_coff_name8_str` — pad cru a 8 bytes, **sem** a forma `/N`. É
  exactamente o mecanismo que o Go teve de implementar.

| crumb | conteúdo | prova |
|---|---|---|
| **W0 — SONDAGEM, e é PRIMEIRO** | O nosso link em Windows é `clang --target=x86_64-pc-windows-msvc` (`resolve_cc_choice`). **A seção `.debug_*` do nosso `.obj` COFF sobrevive até ao `.exe`, ou o linker descarta-a?** `lld-link` preserva; `link.exe` é a incógnita. **Não posso medir aqui — não há host Windows.** | um `.obj` com as três seções, ligado na perna Windows do CI, e `llvm-dwarfdump` sobre o `.exe` |
| **W1** | generalizar `build_coff_strtab` para colocar também nomes de **seção** longos e emitir a forma `/N` em `emit_coff_name8_str` | golden: um objeto sem seções de depuração fica **byte-idêntico** ao de hoje |
| **W2** | routing das três seções + `.debug_*` relocações, com as características **exactas** do Go acima | golden + `llvm-dwarfdump` + o arnês na perna Windows |

**Orçamento condicional, honesto:**

* **se W0 disser que o linker preserva: 3 crumbs**, e Windows fica depurável em gdb/lldb/VSCode com
  **os mesmos bytes** do Piso. Contra "um segundo escritor completo" do orçamento anterior.
* **se W0 disser que descarta: +6 crumbs** para CodeView de verdade — `.debug$S` com as
  subseções `DEBUG_S_SYMBOLS` (`S_GPROC32_ID`, `S_OBJNAME`, `S_COMPILE3`) e `DEBUG_S_LINES`
  (`CV_Line_t`/`CV_Column_t`), `.debug$T` com o *type stream* mínimo, e o `LF_FUNC_ID`/`LF_PROCEDURE`
  que o `S_GPROC32_ID` referencia. Segundo formato, do zero, como o orçamento anterior temia — mas
  **só neste ramo**, e o ramo é decidido por uma sondagem de um dia.

**A recomendação muda:** o orçamento anterior recomendou **adiar** Windows citando o Zig. Recomendo
agora **correr W0 já** — é uma sondagem sem produto, e o seu resultado positivo torna Windows a
plataforma **mais barata** das três (W1+W2 = 2 crumbs sobre um escritor DWARF que já existe).
Adiar uma decisão que uma sondagem de um dia resolve é o oposto de barato.

## 5.5 O total

| bloco | crumbs | bloqueado por |
|---|---|---|
| **Piso** (D0.1 + D1.1…D1.7) | **8** | nada. D0.1 e D1.3 começam hoje. |
| **Camada 2 honesta** (D2.1…D2.6) | **6** | nada (a regra de §4.4 tira `str` do caminho crítico) |
| **Windows** (W0…W2) | **3** (ou 9 no ramo mau) | uma sondagem de um dia |
| **Camada 3** (D3.1…D3.3) | **3** | `str` de três words |
| **CFI**, se D1.7 correr mal | +4 | D1.7 |

---

# PEÇA 5 — A contra-medida: um debugger nosso, orçada

O dono não pediu um. **Mas uma recomendação de não fazer sem o custo do que se recusa não é
decidível.** Aqui está o custo.

## 6.1 Como se para um processo — medido, e o número depende de uma escolha de lei

**Funciona neste host:** escrevi um `PTRACE_TRACEME` + `fork`/`execl` + `waitpid` + `PTRACE_CONT` em
C e corri: `stopped=1`, `PTRACE_CONT rc=0`, filho continua e sai. Não há Yama a bloquear. Portanto
o mecanismo está disponível; a pergunta é se **Teko** o alcança.

**Medi o que a nossa stdlib tem:** `src/process/process.tks` expõe `run`, `run_quiet`,
`spawn_redirected`, `wait_one` — todos por cima de `tk_rt_*`. **Não há** `ptrace`, **não há**
`waitpid` com status cru, **não há** manipulação de sinais, **não há** leitura/escrita de memória de
outro processo. E `extern fn` existe e é usado (`teko::crypto::rand::secure_bytes`).

**O bloqueio real não é `ptrace`; é o que `ptrace` precisa de passar.** `PTRACE_GETREGS` quer um
ponteiro para `struct user_regs_struct`; `waitpid` quer um `int*` de saída. Medi o estado da
superfície de ponteiros:

* `uptr` e `ptr<byte>` existem no checker (`scope.tks:388`, `:393`);
* **`unsafe` NÃO é palavra reservada** — `parse_decl.tks:197` trata-a como `Ident` comum;
* `docs/design/c-types-and-marshalling-0.3.1.md` está marcado, na sua primeira linha,
  **"DESIGN-AHEAD, doc-only. NOT implemented."**

Logo: **um debugger nosso escrito em Teko puro está BLOQUEADO em `unsafe`/`ptr<T>`/`c_types`, que
não aterraram.** Não é orçável por essa via.

**Há uma via legal que o desbloqueia, e é ela que dá o número:** `src/runtime/teko_rt.{c,h}` é a
**exceção mantida** à lei Teko-only. Um `tk_rt_ptrace_*` — funções de fachada com assinaturas de
palavras inteiras, o `struct user_regs_struct` copiado para um `[]byte` — cabe lá dentro **sem
violar nada**. É C novo, deliberado, num ficheiro onde C novo é legal. Todo o orçamento abaixo
pressupõe essa via.

| crumb | conteúdo |
|---|---|
| **O1.1** | `tk_rt_dbg_spawn_traced(argv) -> i64` — `fork` + `PTRACE_TRACEME` + `execvp`, devolve o pid |
| **O1.2** | `tk_rt_dbg_wait(pid) -> i64` — `waitpid` com o status **decodificado** num inteiro Teko (parado/saiu/sinalizado + qual), pela mesma regra 128+N que `tk_rt_run` já usa |
| **O1.3** | `tk_rt_dbg_cont(pid, sig)`, `tk_rt_dbg_step(pid)`, `tk_rt_dbg_detach(pid)` |
| **O1.4** | `tk_rt_dbg_getregs(pid) -> []byte` + `tk_rt_dbg_setreg(pid, idx, val)` — os registos como bytes, os offsets como constantes Teko por arquitetura |
| **O1.5** | `tk_rt_dbg_peek(pid, addr, n) -> []byte` / `tk_rt_dbg_poke(pid, addr, bytes)` — `PTRACE_PEEKDATA`/`POKEDATA`, ou `process_vm_readv` para blocos |

**5 crumbs**, e **três** portes de plataforma: macOS troca `PTRACE_*` por `mach_vm_read_overwrite` /
`task_for_pid` (que exige **assinatura de código com o entitlement `com.apple.security.cs.debugger`**
— um custo de *release engineering*, não de crumbs, e é o mais desagradável do lote); Windows troca
tudo por `DebugActiveProcess` + `WaitForDebugEvent` + `ReadProcessMemory`/`WriteProcessMemory` +
`GetThreadContext`. **+3 crumbs por plataforma extra** = **11 crumbs só para parar um processo nas
três plataformas.**

## 6.2 Breakpoints — 3 crumbs

| crumb | conteúdo |
|---|---|
| **O2.1** | x86-64: `peek` de 1 byte → guardar → `poke` `0xCC`. No `SIGTRAP`: `rip` está **um byte depois** do `0xCC`, logo há que **recuar `rip`**, restaurar o byte, e re-armar **depois** de um single-step. O ciclo restaurar/step/re-armar é o crumb — esquecê-lo é o defeito clássico "o breakpoint dispara uma vez". |
| **O2.2** | arm64: `BRK #0` = `0xD4200000`, palavra de 4 bytes, e **o PC NÃO recua** — a diferença de arquitetura que um `if` esconde e um teste separa. |
| **O2.3** | a tabela de breakpoints: endereço → byte/palavra original, contagem, activo, e o re-arme na continuação |

## 6.3 A tabela de linha — **e é aqui que o orçamento muda de verdade**

**A pergunta que o dono fez, respondida com medição:** o Piso **já constrói** a tabela de linha
interna (`LineRow{offset, line, col}`, §5 do orçamento anterior, tipo já desenhado). Um debugger
nosso pode lê-la **sem DWARF nenhum**.

| via | crumbs | o que dá |
|---|---|---|
| **um sidecar nosso** (`.tkdbg`: as `LineRow` + a tabela nome→slot, serializadas cruas) | **2** (1 escrever, 1 ler) | posição e nomes **só** em binários nossos, **só** com `--debug=lines`; nada em C ligado ao lado |
| **um leitor de DWARF de verdade** | **4** (header + máquina de estados do programa de linha + abbrev + travessia de DIEs) | tudo o que acima, **mais** funcionar sobre o C do host, sobre `libc` com símbolos, e sobre binários que não são nossos |

**Quanto muda o orçamento?** Muda **2 crumbs num total de 13** — o sidecar poupa 2 face ao leitor
de DWARF. **Isso é ruído.** E a referência verificada diz para **não** fazer o sidecar: o Zig, que é
o precedente exacto de "ler a própria informação de depuração no próprio runtime", lê **DWARF e PDB
de verdade** — `lib/std/debug.zig` importa `debug/Dwarf.zig`, `debug/Pdb.zig`, `debug/ElfFile.zig`,
`debug/MachOFile.zig` e escolhe por `ObjectFormat`. Não inventou formato privado.

**Conclusão desta secção, e é a que desmonta o argumento mais atraente a favor da contra-medida:**
"nós já temos a tabela, logo o debugger próprio é barato" **é falso**. A tabela é 2 dos 13 crumbs.
Os outros 11 são controlo de processo em três plataformas, e nenhum deles é dispensado por termos a
tabela.

## 6.4 O adaptador DAP — 4 crumbs, e mais barato do que eu esperava

**Medição que baixa o número:** `src/encoding/json/json.tks` (631 linhas) já expõe
`decode(text) -> JsonValue | error` e `encode(value) -> str`. **A camada JSON do DAP é grátis.**

| crumb | conteúdo |
|---|---|
| **O4.1** | o enquadramento `Content-Length: <n>\r\n\r\n<json>` sobre stdio + o laço de pedidos |
| **O4.2** | `initialize` (com as *capabilities*), `launch`, `setBreakpoints`, `configurationDone` |
| **O4.3** | os eventos `stopped`/`continued`/`exited`/`terminated`/`thread` e os pedidos `threads`/`stackTrace`/`continue`/`next`/`stepIn`/`stepOut` |
| **O4.4** | `scopes`/`variables`/`evaluate` — **e este depende das tabelas da Camada 2**, não do adaptador |

## 6.5 A pergunta que decide — e a resposta é NÃO, com o critério do Go aplicado

O critério não é meu: é o do Go, e **verifiquei-o na fonte** (`go.dev/doc/gdb`, citação literal):

> *"GDB does not understand Go programs well. The **stack management, threading, and runtime**
> contain aspects that differ enough from the execution model GDB expects that they can confuse the
> debugger and cause **incorrect results** even when the program is compiled with gccgo. As a
> consequence, although GDB can be useful in some situations …, it is not a reliable debugger for Go
> programs, **particularly heavily concurrent ones**."*

**O critério, extraído:** constrói-se um debugger próprio quando o *runtime* torna o debugger geral
**errado** — não quando o torna incompleto. Aplicado a Teko, candidato por candidato, medido:

| propriedade do nosso runtime | torna gdb/lldb ERRADOS? | veredicto |
|---|---|---|
| **arena** (região raiz do processo, bump, `tk_arena_push`/`pop`) | **Não.** O gdb vê ponteiros crus e não sabe o que está vivo — isso é **incompleto**, não errado. E um pretty-printer resolve-o. | não dispara |
| **erros como valores** (`T \| error`) | **Não.** O gdb vê a caixa e não o membro ativo — de novo incompleto. §9.4 diz **como** o printer o resolve e o que é preciso. | não dispara |
| **monomorfização** | **Não**, e medi: `tk_emit_tsym` emite `<símbolo-C>\t<nome-teko>\t<file>:<line>`, e o Piso põe o **nome Teko qualificado** em `DW_AT_name`. O `bt` mostra `ns::fn`, não o mangled. O DIE de um genérico leva o nome **resolvido** — menos informativo que o genérico, mas **correto**. | não dispara |
| **`defer`** (dispara em toda saída de escopo) | **Não, e é bom que não.** Um `next` sobre um `return` executa o código do `defer`. Com granularidade de statement (que é o que o `MLineMark` dá), o `step` **entra** nas linhas do `defer` — que é o comportamento **certo**: esse código corre. O que seria errado é esconder-se. | não dispara |
| **concorrência** | **Não, e este é o que eu esperava que disparasse.** Medi `docs/design/concorrencia-adiantada-s8.md` na branch `cargo/20-concorrencia-adiantada`: o chão é `pthread_create`/`pthread_join`/`pthread_self` (`§3.1 teko::thread::sys`), **threads 1:1 do SO**. Não há escalonador nosso, não há pilhas geridas, não há crescimento de pilha. **Threads 1:1 são exactamente o que gdb e lldb modelam nativamente** (`info threads`, `thread apply all bt`). | **não dispara** |
| **wasm** | **Não.** `objfile_wasm.tks` existe, mas o caminho de depuração de wasm da indústria é DWARF numa seção customizada + a extensão do DevTools/lldb — **outro contentor para os mesmos bytes**, não um debugger nosso. | não dispara |
| **depurar comptime/monomorfização do próprio compilador** | **Não.** O compilador é um binário nativo; depura-se com o Piso, como qualquer outro. | não dispara |

**Nenhum gatilho dispara. A resposta honesta à pergunta do dono é: um debugger nosso não daria nada
que gdb/lldb não dão.** E agora isso é uma **prova**, não a asserção que estava no documento
anterior.

## 6.6 A conta, lado a lado

| | um debugger nosso | o Piso + Camada 2 |
|---|---|---|
| crumbs | **13** (5 controlo + 3 breakpoints + 2 tabela + 4 DAP), **por plataforma** para o bloco de controlo (+3 por plataforma extra ⇒ **19** para as três) | **14** (8 + 6) |
| C novo | **sim** — um bloco em `teko_rt.c` | **nenhum** |
| plataformas | uma de cada vez; macOS exige assinatura com entitlement de debugger | **três**, com **os mesmos bytes** (medido: um objeto, gdb e lldb, §1.6) |
| debuggers servidos | **um** — o nosso | **quatro** — gdb, lldb, cppdbg, CodeLLDB |
| `print x` | **ainda precisa das tabelas da Camada 2** — não as dispensa | é a Camada 2 |
| manutenção | nossa, para sempre, em três ABIs de depuração de SO | zero |

**13–19 crumbs para uma ferramenta pior. A recomendação é NÃO FAZER, e o número é a prova.**

## 6.7 A condição de reabertura — nomeada, e medida como não satisfeita

> **A recomendação de não fazer vale enquanto Teko executar em threads 1:1 do SO.** No dia em que
> Teko ganhar **escalonador próprio, tarefas verdes ou pilhas geridas/crescíveis**, o precedente do
> Go morde: gdb passa a dar resultados **incorretos** (não meramente incompletos) sobre a nossa
> pilha, e o debugger próprio deixa de ser luxo. **Reabrir neste documento, nesta secção.**

**Estado medido hoje: a condição NÃO está satisfeita.** `concorrencia-adiantada-s8.md` — a peça que
o dono mandou adiantar — desenha `pthread_create`. A palavra "corrotina" aparece lá como *unidade
de isolamento por `#test`*, e o seu chão (`§3.1`) é uma thread de SO. **Enquanto o chão for
`pthread`, a contra-medida continua a não se pagar.**

**Segunda condição, menor, e vale registá-la:** se W0 (§5.4) disser que `link.exe` descarta as
seções DWARF **e** o dono recusar CodeView, então Windows fica sem debugger nenhum, e aí um leitor
nosso passa a ser a **única** via. Isso não é razão para o construir hoje; é razão para **correr W0
já**.

---

# 7. As quatro referências — nomeadas, verificadas por fonte, e a nossa superfície medida contra cada uma

A lei desta lane: nomear **qual** referência e verificar que a **nossa** superfície suporta o que
ela oferece. As quatro atribuições do dono: superfície → Rust, controlo → Zig, addins → C#,
comportamentos → Go.

## 7.1 A tabela que me foi passada, verificada linha a linha

| | Rust | Zig | Go |
|---|---|---|---|
| **formato** | ✅ **confirmado.** `split-debuginfo` documenta: `off` para ELF ("DWARF debug information can be found in the final artifact in sections"), `packed` **default em Windows MSVC** = `*.pdb`, e em macOS = `*.dSYM`. | ✅ **confirmado** para DWARF; **e mais forte no leitor** (ver linha abaixo) | ✅ **confirmado, e é o achado da §5.4.** `pe.go` chama `pefile.addDWARF()`; nomes longos pela string table na forma `/N`; **nenhum CodeView** |
| **debugger próprio** | ✅ **nenhum**; `rust-gdb`/`rust-lldb` são wrappers que carregam printers | ✅ **nenhum**, **e o leitor de DWARF/PDB no runtime confirma-se**: `lib/std/debug.zig` importa `debug/Dwarf.zig`, `debug/Pdb.zig`, `debug/ElfFile.zig`, `debug/MachOFile.zig`, e escolhe `SelfInfo` por `ObjectFormat.default(...)` | ✅ **delve, próprio.** `go.dev/doc/gdb`: *"Delve is a better alternative to GDB … It understands the Go runtime, data structures, and expressions better than GDB"* |
| **tipos** | ✅ printers; **e ver §9.4** sobre `DW_TAG_variant_part` | ✅ DWARF padrão | ✅ `runtime-gdb.py` existe, **e a doc avisa** — a citação de §6.5 |
| **Windows** | ✅ PDB real, e `packed` é o **default** lá | ✅ fraco na emissão | ⚠️ **CORRIJO A NUANCE:** `go.dev/doc/gdb` diz DWARFv4 em *"Linux, macOS, FreeBSD or NetBSD"* e **não menciona Windows** — mas isso é sobre **usar o gdb**, não sobre **emitir**. O `pe.go` **emite** DWARF, e o delve suporta `windows/amd64` e lê-o. Emissão ✅, gdb-em-Windows não documentado. |

**Duas correções, e mais nada:** (a) a linha "Windows / Go: funciona" é verdade pelo **delve**, não
pelo gdb — a doc do Go não lista Windows entre os alvos de DWARF-para-gdb; (b) a linha do Zig é
**mais forte** do que estava: não é só um leitor "para stack traces", é uma abstração `SelfInfo`
por formato de objeto, com leitor de **PDB** incluído.

**Escopo do delve, medido** (FAQ oficial): `linux/amd64`, `linux/arm64`, `linux/386`,
`windows/amd64`, `darwin/amd64`. **Nem `darwin/arm64`.** Um debugger próprio, com uma década de
manutenção e patrocínio corporativo, cobre **cinco** pares — e não cobre o Mac de hoje. O nosso
Piso cobre ELF+Mach-O × x86-64+arm64 **com os mesmos bytes**. Isto é, por si só, um argumento
quantitativo contra a contra-medida.

## 7.2 Rust → superfície: **aplica-se, e é a peça que eu adotei**

`line-tables-only` é o nosso Piso, nomeado por eles (citação em §2.2). Adotei o **vocabulário**
(`--debug=lines`) e o **nível**. Não adotei `split-debuginfo`: `dsymutil`/`dwp`/`pdb` são
otimizações de tamanho que só fazem sentido depois de haver DWARF que valha a pena separar.
**A nossa superfície suporta:** sim — `--opt=<n>` é o precedente exacto de flag nivelada com `=`.

## 7.3 Zig → controlo: **aplica-se, e nós já o seguimos sem lhe dar o nome** (§9.3)

## 7.4 Go → comportamentos: **aplica-se em dois sítios, e num deles inverte a decisão anterior**

(a) o **critério** de quando um debugger próprio se justifica — §6.5; (b) o **mecanismo** de
DWARF-em-PE — §5.4, que inverte a recomendação de adiar Windows.

## 7.5 C# → addins: **NÃO se aplica, e digo porquê em vez de o invocar**

A atribuição do dono era addins. O modelo de depuração do .NET é um **runtime gerido** com ICorDebug
e um debugger *in-process*: pressupõe CLR, metadados, e um contrato de depuração dentro do runtime.
Teko compila para código nativo sem runtime gerido. **A referência não tem superfície onde encaixar
aqui, e invocá-la seria o erro que esta lane já pagou.** Onde ela **se aplicará** é noutro assunto:
o dia em que Teko tiver um modelo de *plugins/addins de compilador*, o `IDbgAddin`/analyzer do C# é
o precedente. Não é este documento.

---

# 8. Correções e achados sobre o orçamento anterior

**Não editei `debugger-orcamento-0.3.1.md`.** O que segue são os pontos onde ele diverge do que
medi.

## 8.1 As 4 relocações estão CERTAS — e a razão que faltava é o que torna o número reproduzível

O documento diz *"4 relocações, todas `Abs64`"* e não diz **de onde vem o 4**. Refiz e obtive 4,
mas só depois de descobrir que **duas sequências de programa de linha — uma por função — dão 5**
(dois `DW_LNE_set_address`). O 4 exige **UMA sequência por unidade de compilação**, atravessando as
funções com `DW_LNS_advance_pc`:

```
.rela.debug_info  contains 3 entries:  R_X86_64_64 add, R_X86_64_64 add, R_X86_64_64 main
.rela.debug_line  contains 1 entry:    R_X86_64_64 add
```

Sem essa nota, um implementador que emita uma sequência por função obtém 5, 6, N+3 — e conclui, com
razão, que o golden do documento está errado. **A regra a fixar no D1.3: uma sequência por CU, uma
única `DW_LNE_set_address`, no primeiro endereço do módulo.** É o que o `gcc` faz e o que o Go faz.

## 8.2 `DW_FORM_data1` para `decl_line` transborda no nosso próprio corpus

O orçamento não fixa formas. A escolha ingénua (`data1`, que é o que o `gcc -g1` usa em programas
pequenos) **não serve**: o próprio documento anterior usa `main.tks:410` como exemplo de sessão.
`data2` ou `udata`. §1.3 fixa `data2` para a linha e `udata` para o ficheiro.

## 8.3 RED-FLAG 3 estava errada — §4.1

E estava errada por não ter sido medida. Registo-o sem sarcasmo: **a red-flag foi a coisa certa a
levantar**, e levantá-la é o que fez esta medição existir. O defeito não foi levantá-la; foi
**vender o `bt` na tabela de camadas ao mesmo tempo**.

## 8.4 "1 crumb" para pretty-printers subestima a paridade — §5.3

Rust ship**a dois** ficheiros de printers (gdb e lldb), com APIs diferentes. São 3 crumbs, não 1.

## 8.5 O §7 (Windows) parte de uma premissa que a fonte do Go desmente — §5.4

*"nenhum dos crumbs D1.3–D1.5 rende um byte de valor no Windows"* é **falso**: rende **todos** os
bytes, se o contentor os aceitar. E *"o Zig não resolveu a assimetria"* está **certo** e continua
certo — mas o Zig deixou de ser a única referência: o **Go resolveu-a**, e resolveu-a **sem
CodeView**. A recomendação passa de "adiar" para "correr a sondagem W0 já".

## 8.6 A via do `variant_part` para as uniões **não se aplica** — a correção grande

O orçamento recomenda pretty-printers como a resposta certa à Camada 3, e a referência do Rust é
invocada por trás disso. Medi duas coisas que o obrigam a mudar de forma:

1. **`DW_TAG_variant_part` é DWARF 5.** O nosso PoC, verificado, é DWARF **4**. A via do Rust exige
   subir a versão do escritor — o que não é impossível, mas **é um custo que ninguém orçou**.
2. **As nossas uniões têm TRÊS rails, não um** (`codegen.tks:2048` e vizinhança):
   * **niche** — `T | null` de uma word: `{ptr = NULL}` **significa** `null`, e **não há palavra de
     tag nenhuma**. Um `variant_part` **exige** um discriminante; um rail sem tag não tem o membro
     que o `DW_AT_discr` aponta.
   * **InlineTag** — `{ uint8_t tag; payload }`, em frame.
   * **box-em-arena** — o valor não tem `.tag`; o padrão do próprio ponteiro é a discriminação.

**A resolução, e é mais barata que subir para DWARF 5:** emitir a união pela sua **forma C literal**
(`DW_TAG_structure_type` com um membro de tag quando existe, e um `DW_TAG_union_type` para o
payload) e deixar o **pretty-printer** ler a tag e escolher. Isso funciona em DWARF 4 e é o D3.2.

**E é aqui que levanto a red-flag nova.**

> **RED-FLAG 4 (nova) — o printer do rail *niche* codifica um invariante interno do codegen.**
> Um printer Python que diga "se o ponteiro é NULL, o valor é `null`" está a replicar, num ficheiro
> Python fora da árvore de tipos, uma decisão de `cg_union_niche_member`. Se a regra de niche
> mudar — e ela é uma otimização, logo **vai** mudar — o printer passa a mentir, **em silêncio**,
> e nenhum teste de compilador o apanha. **Mitigação a exigir do D3.2:** o printer não decide o
> rail; o **DWARF** di-lo, num `DW_AT_producer` estendido ou num atributo `lo_user` por tipo, e o
> printer **lê** o rail em vez de o adivinhar. Custa um atributo. Poupa uma mentira silenciosa.

---

# 9. O que fazer, e por onde começar

## 9.1 O menor conjunto que não ensina mal

Chamo-lhe **o Piso**, e é **8 crumbs**: D0.1, D1.1, D1.2, D1.3, D1.4, D1.5, D1.6, D1.7.

Clama, e garante: **breakpoint por linha de `.tks`, listagem do texto Teko, `step`/`next`, e `bt`
com nomes Teko** — no terminal (gdb **e** lldb) e no VSCode (`cppdbg` **e** CodeLLDB), **sem uma
linha de servidor nosso**. Não clama `print x`, e a não-clamação está **escrita** no `--help`, nos
quatro `launch.json` e em §3.5.

**Isto ensina mal?** Respondo de frente: **não, desde que diga o que não faz** — e o desenho força
que diga, em três sítios. O que ensinaria mal é o que o dono rejeitou: vender `bt` sem prova (agora
tem, §4.1), ou um painel Variables a mostrar um `str` com o comprimento errado (a regra de §4.4
impede-o).

**"E se o dono quiser `print x`?"** Então o conjunto é **14 crumbs** (Piso + Camada 2 honesta), e as
duas pré-condições que o orçamento anterior temia **caíram por medição**: a sondagem do regalloc
está feita (§4.3, é um JOIN) e `str` saiu do caminho crítico (§4.4, a regra do congelamento). **14
crumbs, sem bloqueio nenhum a montante.**

## 9.2 Vale a pena começar agora? Sim — e nada precisa de esperar, excepto a Camada 3

| bloco | pode começar? |
|---|---|
| **D0.1** (arnês) + **D1.3** (`dwarf.tks`) | **HOJE.** Zero colisão (`scripts/` novo e ficheiro novo), zero bloqueio, e o golden do D1.3 está **fixado em §1.3–§1.5 deste documento**. |
| **W0** (sondagem Windows) | **HOJE.** Sem produto, e o resultado decide 3 vs. 9 crumbs. |
| **D1.7** (sondagem arm64) | **HOJE**, na lane aarch64. Sem produto. |
| D1.1, D1.2, D1.4, D1.5 | assim que os agentes vivos saírem de `lower.tks`, `isel_x86_64`, `encode_x86_64`. O desenho aditivo do orçamento anterior (§3, `MLineMark`) é o que mantém isto aplicável — e **mantenho a recomendação (b) dele**, com a medição de §4.1 a reforçá-la. |
| D2.* | depois do Piso. **Não espera pelo `str`.** |
| **D3.*** | **ESPERA** pelo `str` de três words. É o único bloco bloqueado, e a lei (M.4) decide, não o dono. |

**A peça que não expira, se o dono quiser gastar o mínimo:** **D0.1, o arnês.** É activo de teste,
agnóstico de camada, e o seu fixture negativo (`adv.s` + `mini.s`) já existe neste commit. Se nada
mais avançar, o arnês continua a valer.

## 9.3 O `.tsym` com o nome do padrão: o precedente do Zig

O orçamento anterior mediu que *"`.tsym` já existe"* e tratou-o como achado solto. **Com a
referência do Zig verificada ao lado, é precedente de desenho** — e um precedente que já
adotámos: *informação de posição própria, lida pelo nosso runtime, para stack traces sem depender
de DWARF*. É exactamente o que `lib/std/debug.zig` faz com `SelfInfo`.

| | `.tsym` hoje | o `SelfInfo` do Zig |
|---|---|---|
| granularidade | **por função** (`file:line` da declaração) | **por endereço** |
| fonte | um ficheiro de texto ao lado / dentro do `.tkl` | **as seções de depuração do próprio binário** |
| o que resolve | *que função* estava na pilha | *que linha* estava na pilha |
| o que lhe falta | mapear **endereço → linha** | — |

**O que isto diz, e é uma recomendação nova:** depois do Piso, `.tsym` fica **redundante e pior**.
As `LineRow` que o Piso produz são a mesma informação com melhor granularidade, no binário. A
evolução law-first é o próprio padrão que o roadmap manda seguir — *"mesma informação, dois
consumidores"* — mas com **a origem certa**: o runtime passa a ler as **nossas** seções
`.debug_line`/`.debug_info` para o stack trace nativo, como o Zig faz, e `.tsym` torna-se o
*fallback* de um binário construído com `--debug=none`.

**Isso não é um crumb deste arco** e não o orço aqui — é um **achado reportado para cima**, e a
razão de o nomear é que ele muda o valor do Piso: o Piso não paga só depuração interactiva, paga
também a qualidade dos stack traces de produção. Registo-o como REPORTE, não como issue nova.

---

# 10. Riscos, red-flags, e a ausência de HALT

**Não cunho KNOWN-STOP.** Levanto o que merece atenção.

* **RED-FLAG 1 (`str`), CONFINADA** — §4.4. Não bloqueia o Piso nem a Camada 2 (com a regra do
  congelamento). Bloqueia a Camada 3. Sequenciamento, não tensão.
* **RED-FLAG 2 (nomes no regalloc), REDIMENSIONADA** — §4.3. É um JOIN. O perigo real é a validade
  da localização, e a mitigação (`lenv_bind_scalar_slot`) já está no código.
* **RED-FLAG 3 (desenrolar), REFUTADA em x86-64** — §4.1, medida. **Aberta em arm64** — §4.2, com
  crumb de sondagem próprio (D1.7) e orçamento condicional nos dois ramos.
* **RED-FLAG 4 (NOVA) — o printer do rail *niche* replica um invariante interno do codegen** —
  §8.6. Mitigação: o rail vai no DWARF, não no Python.
* **RED-FLAG 5 (NOVA) — a armadilha de `project_arg_of`** — §2.1. Uma flag nova que não entre na
  sua lista de saltos torna-se **o caminho do projeto**, sem erro de flag desconhecida. Todo crumb
  que acrescente flag paga um teste que o prova.
* **Sondagem W0 é bloqueante para o número do Windows**, não para o Piso — §5.4. 3 vs. 9 crumbs.
* **Semente de bootstrap:** reli o desenho à procura de funcionalidade nova. `struct`, `enum`,
  `variant`, `[]T`, `match`, `teko::list::push`, `teko::str::slice_to`/`slice_from`,
  `teko::encoding::json` — **tudo já na semente**. Nada a sequenciar por causa dela. O único bloco
  que **precisaria** de funcionalidade não aterrada é a contra-medida escrita em Teko puro
  (`unsafe`/`ptr<T>`/`c_types` — §6.1), e é por isso que ela é orçada pela via do `teko_rt.c`.
* **`wasm` fora**, por decisão do dono; `objfile_wasm.tks` intocado.

**Sem HALT.** Todas as tensões deste documento resolveram-se por lei ou por medição:
`.tsym` vs. DWARF por *"uma origem, dois consumidores"*; `str` por M.4 (sequenciamento);
`-g` vs. `--debug=lines` pelo precedente `has_backend_flag` no mesmo ficheiro + a citação
verificada do rustc; nada no `.tkp` pelo *"an AXIS of the build, not a global"* do próprio
`build_cc_argv`; e a contra-medida pelo critério do Go aplicado com medição em vez de gosto.

**As duas coisas que são do dono, e são escolhas, não tensões:**

1. **Se o D1.7 correr mal em arm64:** pagar a CFI (+4 crumbs) ou retirar a clamação de `bt` da perna
   arm64 até alguém a comprar. As duas são honestas.
2. **Se o W0 disser que `link.exe` descarta as seções:** pagar CodeView (+6) ou declarar Windows
   não-depurável, com recusa **nomeada** (a regra do MinGW).
