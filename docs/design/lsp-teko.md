# Tooling 0.3.1.0 — LSP nativo, navegacao rica, build/run, e o trilho do debugger

> Estado: DESENHO (arquiteto). Nao implementa produto. Este documento e o plano executavel
> que o implementador segue crumb-a-crumb. `bootstrap/teko.c` e SAIDA; o C dos twins esta
> CONGELADO (excecao: `src/runtime/teko_rt.{c,h}` + semente de assert — C mantida, e o piso
> de syscall).
>
> Regra da proposta (dono): *"uma proposta, nao contra-argumentos para desencorajar.
> Alarmes so se puder prova-los."* Todo alarme abaixo cita `arquivo:linha`.

Este doc funde o desenho de LSP que ja circulava (o subcomando `teko lsp`, ratificado sem
tensao em `docs/design/debugger-superficie-e-contramedida-0.3.1.md:37`: *"quem precisa do
FRONT-END vive em `src/`; quem precisa so de um FORMATO vive fora"*) com quatro direcoes
novas do dono (2026-08-02) e amarra o trilho do debugger, que a mesma data foi
RE-ARQUITETado (servidor proprio, motor proprio, Teko-aware).

---

## 1. As decisoes do dono (2026-08-02) e a consequencia aqui

| # | Ruling do dono | Consequencia neste desenho |
|---|---|---|
| **N1** | *"O LSP sera feito SOMENTE EM MODO NATIVO."* | O servidor LSP e um programa Teko compilado pelo **backend nativo** (`src/backend/`, `src/codegen/`), nunca emite C e nao depende de `teko_rt.c` alem do piso mantido. §2. |
| **N2** | *"Servidor NOSSO, NATIVO ... um debugger NOSSO, feito EM TEKO e que SABE TEKO."* | O debugger deixa de ser "registrar cppdbg/CodeLLDB". Passa a ser servidor **DAP proprio**, **motor proprio** (ptrace/mach), **Teko-aware**, escrito em Teko. §8. Descarta a variante "dirigir gdb por baixo". |
| **N3** | *"navegacao rica: find references, goto implementation, confirme goto definition."* | Tres provedores novos/confirmados, cada um mapeado a uma fonte do front-end. §5. |
| **N4** | *"suporte a build e run ... reusa o CLI que ja existe (`src/build/`)."* | Build/run como **capacidade do SERVIDOR** via `workspace/executeCommand`, mais tasks/launch do lado cliente. §7. |
| **N5** | *"VSCode e o primeiro, tem que considerar ser AGNOSTICO de editor/IDE."* | **O servidor e o produto; o cliente de editor e cola.** Nada de VSCode entra no servidor. VSCode e o PRIMEIRO cliente, nao o unico. §3. Cada crumb no roteiro e rotulado `servidor (agnostico)` ou `cliente VSCode (glue)`. |
| **N6** | *"o `.tsym` (ja legislado) pode ser a semente ... pese DWARF vs formato Teko-nativo."* | O substrato de debug-info e reconsiderado: DWARF-4 minimo vira **interop opcional**; o formato de primeira classe do NOSSO motor e `.tsym` v2 Teko-aware (tipos, tags de variante, layout de arena). §8.4. |

---

## 2. O constraint NATIVE-ONLY — o que muda (e o que NAO muda)

O servidor LSP e um binario Teko produzido por `teko build` no **backend nativo**. Isso e
consistente com a rota nativa e nao com a rota C: ele nao pode importar nada que so exista
como semente C, nem emitir C, nem linkar `vm.c`.

**O que o native-only PROIBE:**

- Depender do interpretador (VM) — a VM esta em retirada
  (`docs/design/vm-retirement.md:6`: *"native becomes the sole engine"*).
- Qualquer caminho de codegen que so exista no twin C congelado.

**O que o native-only NAO muda (o front-end ja e Teko puro e ja produz o que o LSP le):**

- O checker/resolvedor sao `.tks` e correm no binario nativo. Um LSP e, no essencial, o
  front-end parado numa posicao do cursor.
- O AST **ja carrega posicao de origem** — nao ha crumb de "adicionar spans":
  - `src/parser/ast.tks:259` — `pub type Expr = struct { kind: ExprKind; line: u32; col: u32 }`;
  - `src/parser/ast.tks:408-410` — `Function` carrega `line`/`col` do nome (1-based);
  - `src/parser/ast.tks:557-559` — `TypeDecl` idem;
  - `src/lexer/lexer.tks:20-49` — `Loc { line; col }`, o piso da stamping.
- O substrato de I/O ja existe e e nativo:
  - `src/encoding/json/json.tks` — parser **e** encoder RFC 8259 (`json.tks:4,9,51`:
    *"a full RFC 8259 DOM parser + encoder"*; `JsonValue` variant em `:73`; `encode` total,
    `decode` falivel). **Nao ha crumb de serializer** — ja esta pronto.
  - `src/runtime/teko_rt.h:766` `tk_rt_read_line`, `:772` `tk_rt_read_stdin`, `:768`
    `tk_rt_stdin_eof` — leitura de stdin sem bloqueio-cego.
  - `src/process/process.tks` — spawn/pipe/read para o servidor lancar `teko build`
    (`spawn_redirected` :288, `read_to_eof` :555, `fd_wait_readable` :435).

**Consequencia nos crumbs:** o native-only **nao adiciona** crumbs de LSP; ele apenas
**fixa o alvo de build** (todo binario de tooling e `kind = "binary"` sob o backend nativo)
e **proibe** um atalho VM. O custo real do LSP e o indexador de simbolos e os provedores,
nao o transporte.

---

## 3. Editor-agnostico — o servidor e o produto, o cliente e cola

Principio de arquitetura de primeira classe (dono N5):

1. **O servidor e um binario Teko standalone** falando o **protocolo padrao** (LSP; e, no
   trilho irmao, DAP) sobre **stdio**. NADA especifico de VSCode entra no servidor — nem
   caminhos, nem comandos de editor, nem suposicoes de UI.
2. **VSCode e o PRIMEIRO alvo, nao o unico.** A extensao VSCode e um cliente fino (glue),
   com o **mesmo estatuto** dos plugins ja em `tooling/` (`tooling/vim/`, `tooling/emacs/`,
   `tooling/nano/`, hoje so realce de sintaxe).
3. **Mesmo servidor, todo cliente:** Neovim (LSP embutido + `nvim-dap`), Emacs
   (`eglot`/`lsp-mode` + `dap-mode`), Zed, Sublime (LSP), Helix — todos conectam ao MESMO
   binario **sem mudar uma linha do servidor**. O que e por-editor (o `launch.json`, o
   `tasks.json`, o `package.json` da extensao) fica **do lado do cliente**, isolado.
4. **Build/run sao capacidades do SERVIDOR** (via `workspace/executeCommand`) sempre que
   possivel, para que todo cliente as ganhe de graca — em vez de codadas so na extensao
   VSCode. §7.

Layout proposto (sitio segue a regra `debugger-superficie...:37` — quem precisa do
front-end vive em `src/`):

```
src/lsp/                 # o SERVIDOR (agnostico) — .tks, backend nativo
  jsonrpc.tks            #   enquadramento Content-Length + laco stdio
  server.tks            #   dispatch de metodos LSP (initialize, textDocument/*, ...)
  index.tks             #   indexador de simbolos (declaracoes + referencias)
  providers/            #   um provedor por feature (definition, references, impl, hover)
  commands.tks          #   workspace/executeCommand -> teko::build (§7)
tooling/vscode/          # cliente VSCode (glue, PRIMEIRO) — ja existe como realce
tooling/vim/ emacs/ ...  # clientes finos (mesmo estatuto)
```

---

## 4. Substrato compartilhado — JSON-RPC sobre stdio (LSP e DAP sao irmaos)

LSP e DAP sao **o mesmo transporte**: mensagens JSON com cabecalho `Content-Length:`,
lidas de stdin, escritas em stdout. Um unico modulo serve os dois trilhos — e a primeira
economia do roteiro (a peca de transporte do debugger e a MESMA do LSP).

Formas fixadas para o implementador (copiar verbatim, ja em Javadoc):

```teko
/**
 * A single decoded JSON-RPC / LSP / DAP message: the raw `Content-Length`-framed body
 * already parsed into a `JsonValue` DOM (see `src/encoding/json/json.tks:73`).
 *
 * @since 0.3.1.0
 * @see read_message
 */
pub type RpcMessage = struct { body: teko::encoding::json::JsonValue }

/**
 * Reads ONE `Content-Length`-framed message from stdin, blocking until a full body is
 * available. Frames per the LSP/DAP base protocol: ASCII header lines terminated by CRLF,
 * a blank CRLF, then exactly `Content-Length` bytes of UTF-8 JSON.
 *
 * @return the decoded message, or `error` on a malformed frame / decode failure / EOF
 * @throws error when the header is absent, the length is unparseable, or stdin hits EOF
 *         mid-body (a truncated frame is never silently accepted)
 * @see teko::encoding::json::decode
 * @since 0.3.1.0
 */
pub fn read_message() -> RpcMessage | error { /* crumb L1 */ }

/**
 * Writes ONE `Content-Length`-framed message to stdout. Total: encoding a `JsonValue` is
 * infallible (`src/encoding/json/json.tks:51`), so only an I/O fault could fail — and the
 * write path panics rather than returning it, matching the runtime's `tk_write` contract.
 *
 * @param body the response / notification payload to frame and emit
 * @see teko::encoding::json::encode
 * @since 0.3.1.0
 */
pub fn write_message(body: teko::encoding::json::JsonValue) -> void { /* crumb L1 */ }
```

**Alarme, provado:** o transporte NAO pode usar `println`/`tk_println` para responder,
porque essas rotas nao contam bytes nem emitem o cabecalho `Content-Length`. O laco tem
de escrever o corpo com um `write` cru (`src/runtime/teko_rt.tks:78`
`exp extern fn write(s: str) -> void = "tk_write" from "teko_rt"`) apos o cabecalho. Um
`println` intercalado (log) em **stdout** corrompe o stream — logs vao para **stderr**
(`tk_ewrite`, `teko_rt.tks:85`). Isto e um invariante do protocolo, nao uma preferencia.

---

## 5. Navegacao rica — o mapa feature -> fonte do front-end

Todas as tres features (mais o hover ja desenhado) sao **consultas ao mesmo indice**. O
indice e a unica peca nova de estado; os provedores sao finos.

### 5.1 A peca de estado: o indice de simbolos

```teko
/**
 * A single indexed symbol occurrence: a name at a source position, classified as a
 * DECLARATION or a REFERENCE, keyed by its resolved canonical name so definition and
 * references join by equality.
 *
 * @since 0.3.1.0
 * @see build_index
 */
pub type SymOcc = struct {
    canon: str      // resolved canonical name (namespace-qualified), the join key
    file: str       // source file path
    line: u32       // 1-based line (from the AST node)
    col: u32        // 1-based column
    is_decl: bool   // true = defining occurrence; false = use/reference
    kind: SymKind   // Type | Func | Method | Field | Local | Const
}

/**
 * The whole-workspace symbol index: every declaration and every reference, flat, plus the
 * type table the resolver already builds so implementation queries can walk the trait/class
 * graph without re-collecting.
 *
 * @since 0.3.1.0
 */
pub type SymIndex = struct {
    occs: []SymOcc
    types: teko::checker::TypeTable   // src/checker/resolve.tks:24
}

/**
 * Builds the workspace index by running the EXISTING front-end collect+resolve passes over
 * the parsed program and recording every name occurrence with its resolved canonical name.
 * Reuses `type_table_of` (src/checker/collect.tks:467) and `resolved_name_ns`
 * (src/checker/resolve.tks:498) — the LSP invents NO new name resolution.
 *
 * @param prog the checked program (the same TProgram the compiler builds)
 * @return the populated index
 * @since 0.3.1.0
 */
pub fn build_index(prog: teko::checker::TProgram) -> SymIndex { /* crumb L3 */ }
```

### 5.2 goto-definition — CONFIRMAR (a base ja existe)

Fonte: o resolvedor. Dada a posicao do cursor, achar a ocorrencia (`SymOcc`) que a cobre,
tomar seu `canon`, e devolver a ocorrencia `is_decl = true` com o mesmo `canon`.

- Resolucao de nome ja existe: `src/checker/resolve.tks:491`
  `pub fn resolve_name_ref(name, table, ref_ns) -> parser::TypeDecl | error`;
  `:97` `type_table_find`; `:139` `type_table_find_path`; `:498` `resolved_name_ns`.
- A definicao devolve `parser::TypeDecl`, que carrega `line`/`col` (`ast.tks:557-559`) —
  o `Location` LSP sai direto dai.

```teko
/**
 * Answers `textDocument/definition`: maps a cursor position to the defining occurrence of
 * the name under it.
 *
 * @param ix  the workspace index
 * @param file the document URI's path
 * @param line 1-based cursor line
 * @param col  1-based cursor column
 * @return the declaration occurrence, or `null` when the cursor is not on a resolvable name
 * @since 0.3.1.0
 */
pub fn goto_definition(ix: SymIndex, file: str, line: u32, col: u32) -> SymOcc | null { /* crumb L4 */ }
```

### 5.3 find-references — NOVO

Fonte: o **mesmo indice**, filtro invertido. Achar o `canon` sob o cursor, devolver TODAS
as ocorrencias com aquele `canon` (declaracao inclusa se `includeDeclaration = true`, per
a spec LSP). A precisao vem de reusar `resolved_name_ns` para o `canon`, e nao um match
textual — dois `push` de namespaces diferentes nao colidem.

```teko
/**
 * Answers `textDocument/references`: every occurrence (decl + uses) sharing the canonical
 * name under the cursor. Purely an index scan; correctness rides on `canon` being the
 * resolver's namespace-qualified key, not raw text.
 *
 * @param ix the workspace index
 * @param file the document path
 * @param line 1-based cursor line
 * @param col 1-based cursor column
 * @param include_decl whether the defining occurrence is included (LSP `context.includeDeclaration`)
 * @return the matching occurrences (possibly empty)
 * @since 0.3.1.0
 */
pub fn find_references(ix: SymIndex, file: str, line: u32, col: u32, include_decl: bool) -> []SymOcc { /* crumb L5 */ }
```

**Alarme, provado (o unico risco real da feature):** find-references so e completo se o
indexador registrar ocorrencias em **posicao de uso**, nao so em declaracoes. As
declaracoes ja tem `line`/`col`; os **usos** vivem em `Expr` (`ast.tks:259`, que carrega
`line`/`col`) — logo a informacao existe, mas o crumb L3 tem de percorrer os corpos das
funcoes (nao so as assinaturas). Isso e trabalho de walk, nao de novo dado. Fixado como a
metade cara de L3.

### 5.4 goto-implementation — NOVO

Fonte: o **grafo de tipos/traits** que o collector ja constroi. Semanticamente distinto de
definition: parte de uma interface/trait/metodo-de-contrato e devolve os **tipos concretos
que o implementam** (e o inverso — de um metodo concreto para o contrato que ele satisfaz).

O grafo ja esta escrito em `src/checker/collect.tks`:

- `:404` `pub fn register_instance_methods(env, table) -> SigOutcome` — quem implementa o que;
- `:737` `find_method_owner(class_name, table, method_name) -> MemberOwner | error`;
- `:629` `effective_class_methods(cb, table)`; `:455` `instance_methods_of(decl)`;
- `:708` `is_subclass_of(class_name, ancestor_name, table)`;
- `:883` `find_interface_decl(name, table, ref_ns) -> IfaceHit | error`;
- e a superficie de constraints em `resolve.tks:599` `constraint_surface`,
  `:738` `method_owner_interface(ifaces, method, table)`.

```teko
/**
 * Answers `textDocument/implementation`: from an interface / trait / contract-method name
 * under the cursor, the concrete types (and their method sites) that implement it; and the
 * inverse, from a concrete method to the contract it satisfies.
 *
 * Walks the trait/class graph the collector already builds — it registers no new edges,
 * only reads `register_instance_methods` / `find_method_owner` / `find_interface_decl`.
 *
 * @param ix the workspace index (carries the TypeTable)
 * @param file the document path
 * @param line 1-based cursor line
 * @param col 1-based cursor column
 * @return the implementing occurrences (empty when the cursor is not on a contract/impl name)
 * @since 0.3.1.0
 */
pub fn goto_implementation(ix: SymIndex, file: str, line: u32, col: u32) -> []SymOcc { /* crumb L6 */ }
```

### 5.5 hover — CONFIRMAR (ja no desenho)

Fonte: o typer. Achar o `canon`, devolver a assinatura/tipo formatado. Reusa a formatacao
de tipo existente do checker (`src/checker/type.tks`, `tast.tks`). Sem grafo novo.

```teko
/**
 * Answers `textDocument/hover`: the type/signature of the name under the cursor, rendered
 * as Markdown, from the checker's own type printer.
 *
 * @return the hover contents, or `null` when nothing resolves under the cursor
 * @since 0.3.1.0
 */
pub fn hover(ix: SymIndex, file: str, line: u32, col: u32) -> str | null { /* crumb L7 */ }
```

---

## 6. Crumbs do LSP — sequencia ordenada, cada um gate-avel

| Crumb | Titulo | Toca | Tipo | Depende de | Colisao |
|---|---|---|---|---|---|
| **L1** | Transporte JSON-RPC/stdio (`src/lsp/jsonrpc.tks`) — `read_message`/`write_message`, framing Content-Length | novo | servidor | json (pronto), teko_rt read/write | nenhuma |
| **L2** | `server.tks` — laco `initialize`/`initialized`/`shutdown`/`exit` + capabilities honestas (so o que L4-L7 entregam) | novo | servidor | L1 | nenhuma |
| **L3** | `index.tks` — `build_index` (declaracoes **+ referencias em corpos**) sobre TProgram | novo | servidor | collect/resolve (pronto) | **le** collect/resolve (nao escreve) |
| **L4** | `providers/definition.tks` — `textDocument/definition` | novo | servidor | L3 | nenhuma |
| **L5** | `providers/references.tks` — `textDocument/references` | novo | servidor | L3 | nenhuma |
| **L6** | `providers/implementation.tks` — `textDocument/implementation` (grafo de traits) | novo | servidor | L3 | nenhuma |
| **L7** | `providers/hover.tks` — `textDocument/hover` | novo | servidor | L3 | nenhuma |
| **L8** | `textDocument/didOpen`/`didChange`/`didSave` — reindex incremental + `publishDiagnostics` (reusa `src/checker/diagnostics.tks`) | novo | servidor | L2,L3 | nenhuma |
| **L9** | `commands.tks` — `workspace/executeCommand` `teko.build`/`teko.run` (§7) | novo | servidor | L2, `src/build/` (pronto) | nenhuma |
| **L10** | Registro do subcomando `teko lsp` no dispatch (`main.tks`/`src/build/help.tks`) | edita | servidor | L2 | **`main.tks`** — arquivo pequeno, sequenciar |
| **L11** | Cliente VSCode: `package.json` ganha `languageClient` apontando `teko lsp`; `tasks.json`/comandos | edita | cliente VSCode (glue) | L10 | nenhuma (so cliente) |
| **L12** | Notas de cliente agnostico (Neovim/Emacs/Helix): um paragrafo de config por editor em `docs/` | novo | cliente (docs) | L10 | nenhuma |

**Ritual (gate cheio obrigatorio):** apos **L2** (o servidor responde `initialize` — primeiro
verde vivo), apos **L4/L5/L6** juntos (a triade de navegacao — o coracao de N3), e apos
**L11** (o primeiro cliente real conecta). Entre esses, gate por-crumb basta.

**Semente:** L1-L10 nao usam feature de linguagem alem do que o corpus ja compila com a
semente atual (json/process/checker ja estao no corpus). Nao ha inversao de bootstrap.

---

## 7. Build e run — a integracao mais fina que reusa o CLI

O CLI ja existe e ja e o dono da verdade (`main.tks:11-13`, `src/build/help.tks:169-171`,
entrada `teko::build::init_run` em `main.tks:66`). O LSP **nunca reimplementa** build; ele
**invoca** o mesmo caminho. Tres camadas, da mais agnostica para a mais especifica:

1. **Servidor (agnostico) — `workspace/executeCommand`.** Comandos `teko.build` e
   `teko.run` registrados nas capabilities. O servidor executa `teko build <proj>` /
   `teko run <proj>` via `src/process/process.tks` (`spawn_redirected` :288,
   `read_to_eof` :555), transmite stdout/stderr como `$/progress` + `window/logMessage`, e
   devolve o codigo de saida. **Todo cliente LSP ganha isso de graca** (N5).
2. **Cliente VSCode (glue, primeiro) — `tasks.json`.** Para quem quer o botao de build
   nativo do editor, uma task declarativa. A forma ja esta esbocada em
   `debugger-superficie...:865-882` (`type: "shell"`, `command: "teko"`,
   `args: ["build", ".", ...]`). **Declarativa e sem string de shell** — ver o alarme.
3. **Cliente — atalho/comando de extensao** que dispara (1) ou (2).

**Alarme, provado (lei ratificada, nao preferencia):** nenhuma invocacao de processo a
partir de um editor pode usar **concatenacao de string para shell**. A regra e citada em
`debugger-superficie...:1012-1014` — *"nenhuma invocacao de processo externo a partir de um
editor usa concatenacao de string para shell ... por causa de um achado real de injecao em
`extensions/vscode/src/extension.js`."* Consequencia de desenho: o servidor spawna com
**argv-vector** (`spawn_redirected(argv: []str, ...)`, ja e a assinatura em
`process.tks:288`), nunca uma linha `sh -c "..."`; a task VSCode usa `args: [...]`
(vetor), nunca `command` interpolado. A forma segura ja e a forma default das duas APIs —
o desenho so a torna obrigatoria.

```teko
/**
 * Runs `teko build`/`teko run` on behalf of an LSP `workspace/executeCommand`, spawning the
 * existing CLI with an ARGV VECTOR (never a shell string — see the ratified anti-injection
 * rule, docs/design/debugger-superficie-e-contramedida-0.3.1.md:1012). Streams child output
 * back as progress/log notifications and returns the child's exit code.
 *
 * @param subcmd "build" or "run"
 * @param projdir the project directory the client sent as the command argument
 * @return the child process exit code, or `error` when the child could not be spawned
 * @throws error when spawning fails (never on a nonzero child exit — that is returned)
 * @see teko::process::spawn_redirected
 * @since 0.3.1.0
 */
pub fn execute_build_command(subcmd: str, projdir: str) -> i32 | error { /* crumb L9 */ }
```

Nota de estado (nao um alarme, so honestidade): `teko run` hoje aponta para a VM
(`main.tks:12`), mas a VM esta em retirada e `run` sera **repontado para build-and-execute
nativo** (`docs/design/vm-retirement.md:13-14`: *"`teko run` is REPOINTED to a native debug
build-and-execute"*). O comando `teko.run` do LSP herda esse repoint sem mudar — ele so
chama `teko run`.

---

## 8. Trilho debugger — servidor DAP proprio, motor proprio, Teko-aware (REDESENHO)

> Isto **substitui** a recomendacao original de `debugger-superficie...` de "registrar
> cppdbg/CodeLLDB e nunca ter DAP proprio". O dono virou a decisao (N2): *"um debugger
> NOSSO, feito EM TEKO e que SABE TEKO."* Os 6 crumbs DWARF (D0.1/D1.1-D1.6) do relatorio
> **viram substrato**, nao produto final. O produto e o servidor Teko.

### 8.1 Por que um DAP proprio NAO e redundante — o que so o nosso mostra

O relatorio original media que "um DAP proprio nao da nada que o cppdbg nao de". Isso e
**falso para um debugger Teko-aware**, e a prova esta no formato dos valores:

- **`str` e `[]T` sao fat-pointers** (ptr+len). O cppdbg ve dois campos crus; o nosso
  imprime o **texto** e a **lista**. (O layout fat e o que o codegen assume em toda parte.)
- **variantes/unions tem a TAG resolvida por NOME Teko.** As unioes tem tres rails, nao um
  (`src/codegen/codegen.tks:2048` e vizinhanca: **niche**, **tag**, ...). O cppdbg ve um
  inteiro de tag e bytes; o nosso ve `Some(...)` / `null` / `error { message = ... }`.
- **erros-como-valor desenrolados** — a uniao `T | error` mostrada como o valor ou a
  mensagem, nao como bytes.
- **a ARENA** — qual regiao, quanto vivo. Isso e informacao que **nenhum** debugger externo
  tem como saber, porque e um invariante do NOSSO runtime, nao do sistema.

Isso justifica o custo com **valor real**, nao pureza. E o eixo que o roteiro tem de
proteger.

### 8.2 A forma escolhida — UM programa, 100% Teko nativo (o dono ja decidiu)

**O motor E o servidor DAP sao UMA coisa, nao duas camadas** (dono, reforco 2026-08-02:
*"O motor E o servidor DAP, tambem precisa ser 100% teko nativo"*). Nao ha um "motor"
separado de um "adaptador DAP" por cima; o **mesmo binario Teko** faz o ptrace/mach
(int3, registradores, memoria, unwind, single-step) **E** fala DAP na fronteira. Um
binario, uma logica. A variante-adaptador (nosso processo dirigindo gdb-mi por baixo) foi
**descartada** por N2: seria usar o gdb, nao ter o nosso.

O debugger **nao e uma excecao** a diretriz nativa — ele e **mais uma prova dela**. A MESMA
regra da rota nativa vale:

- **Escrito em Teko, compilado pelo backend NATIVO** — `src/debugger/` (pela regra `:37`,
  ele precisa do front-end para os nomes/tipos Teko-aware).
- **NAO depende de `teko_rt.c` alem do piso de syscall irredutivel.** ptrace (Linux) / mach
  exceptions (macOS) sao syscalls — `extern` em `teko_rt.{c,h}`, a excecao mantida por
  design, **exatamente como `write`/`abort`/`mmap`**. Esse e o **unico** C que ele toca.
- **NAO emite C.** Toda a logica — parsear DAP, gerir breakpoints, decodificar valores Teko
  (`str`/`[]T`/variantes/arena), montar respostas, desenrolar a pilha — e **Teko puro**,
  baixada pelo backend nativo. O unico C sao as syscalls que **nenhum** programa nativo
  evita.
- **Duas frentes de consumo do MESMO binario:** `teko debug <proj>` (CLI de terminal) e
  `teko debug --dap` (fala DAP sobre stdio; VSCode e todo cliente DAP conecta). O DAP e so
  o **modo de fronteira** do mesmo programa, nao um segundo processo. Transporte = o
  **mesmo** modulo do §4 (LSP e DAP sao irmaos).

### 8.3 O piso de syscall — os externs novos (a excecao C mantida)

ptrace/mach **sao syscalls** — logo sao `extern` novos em `teko_rt.{c,h}` (a excecao C
mantida; o resto do debugger e Teko). A forma segue o padrao ja usado
(`src/runtime/teko_rt.tks:62` `exp extern fn print(s: str) -> void = "tk_print" from "teko_rt"`):

```teko
/**
 * PTRACE floor (Linux) — one thin extern per ptrace request the engine needs. Each is a
 * direct syscall wrapper in the MAINTAINED C runtime (teko_rt.c), the sanctioned exception
 * to Teko-only: a syscall has no Teko form. ALL breakpoint/step/unwind LOGIC lives above
 * these, in Teko.
 *
 * @param pid the traced child's process id
 * @param addr the target address (for PEEK/POKE)
 * @param data the datum to write (for POKE) / request-specific
 * @return the raw ptrace return (register value, peeked word, or status), engine-decoded
 * @throws error when the syscall reports failure (errno surfaced as an error value)
 * @since 0.3.1.0
 */
exp extern fn dbg_ptrace(req: i32, pid: i32, addr: uptr, data: uptr) -> i64 = "tk_rt_ptrace" from "teko_rt"

/**
 * waitpid floor — blocks the debugger until the traced child stops (breakpoint hit, signal,
 * exit). Returns the raw wait status the engine decodes into stop-reason. macOS uses the
 * mach-exception twin declared alongside (a build-selected symbol, same signature shape).
 *
 * @param pid the traced child
 * @return the raw wait status
 * @throws error on syscall failure
 * @since 0.3.1.0
 */
exp extern fn dbg_waitpid(pid: i32) -> i64 = "tk_rt_waitpid" from "teko_rt"

/**
 * fork+exec-under-trace floor — starts the debuggee stopped at entry (PTRACE_TRACEME in the
 * child, exec, first stop). One extern; the engine drives everything after the first stop
 * in Teko.
 *
 * @param argv the debuggee command vector (NEVER a shell string — anti-injection rule)
 * @param n the argv length
 * @return the traced child's pid
 * @throws error when fork/exec fails
 * @since 0.3.1.0
 */
exp extern fn dbg_spawn_traced(argv: ptr, n: u64) -> i32 = "tk_rt_spawn_traced" from "teko_rt"
```

Alarme, provado (custo honesto, nao desencorajamento): esta e a maior superficie C nova do
0.3.1.0 — mas e **estritamente o piso**. Cada extern e uma syscall sem forma Teko, e a lei
ja abre essa excecao para `teko_rt.{c,h}`. macOS troca ptrace por mach exceptions (o
mesmo formato de extern, simbolo selecionado no build); **Windows fica adiado** (coerente
com o relatorio, que ja adiava Windows — `debugger-orcamento-0.3.1.md:747-748`).

### 8.4 O formato de debug-info — DWARF vira interop, `.tsym` v2 vira primeira classe

O DWARF-4 minimo do relatorio existia porque o **consumidor era o gdb**. Agora o consumidor
e **nosso**, e o DWARF-4 minimo **nao carrega** o que o motor Teko-aware precisa (tipos
Teko, layout de arena, tags de variante). Reponderacao:

- **`.tsym` v2 (Teko-aware) e a semente certa e ja e lei.** `TEKO_LEGISLATION.md:350`:
  *"`.tsym` — Teko Symbols (debug symbols: file:line + names for the debugger + stack
  traces)"*. O emissor v1 ja existe (`src/codegen/codegen.tks:12299` `tk_emit_tsym`, formato
  `:12301`). Estende-lo para carregar **tipos Teko, tags de variante e layout de arena** e
  **obedecer** a legislacao; inventar formato novo abriria um segundo. O cabecalho ja leva
  versao (`.tsym v1`).
- **DWARF-4 minimo continua valido — como interop OPCIONAL.** Ele deixa de ser o piso do
  NOSSO motor e passa a ser o que um gdb/lldb de terceiros consome quando o dev quiser
  (`--debug=dwarf`). Os crumbs D1.3-D1.5 (escritor DWARF + drains ELF/Mach-O) **rendem esse
  interop**, nao o produto.

Consequencia: o **substrato** (mapeamento endereco->linha) e compartilhado; o motor
consome `.tsym` v2; o interop externo consome DWARF. Um produtor, dois escoadouros — que e
exatamente a forma que D1.2 ja fixou (`debugger-superficie...:1099-1100`: *"D1.2
(`MLineMark`) passa a ter DOIS escoadouros declarados — DWARF e `.tsym` v2"*).

### 8.5 Crumbs do debugger (substrato + motor + superficie)

Os 6 crumbs de substrato ficam como no relatorio (nomes preservados para nao divergir do
artefato do dono); os crumbs de MOTOR e SUPERFICIE sao a parte nova que N2 exige.

| Crumb | Titulo | Toca | Tipo | Colisao |
|---|---|---|---|---|
| **D0.1** | Arnes de prova (endereco->linha positivo/negativo; fixture `adv.s`) | novo | substrato | nenhuma — **comeca hoje** |
| **D1.1** | `LFunc` ganha `file`/`decl_line` | `src/lir/lower.tks` + `src/lir/lir.tks` | substrato | **AGENTE VIVO em `lower.tks`** — sequenciar (§9) |
| **D1.2** | Posicao chega as `MInst` (`MLineMark`), DOIS escoadouros (DWARF + `.tsym` v2) | `src/backend/minst*.tks`, codegen | substrato | **maior superficie; gate exige `.text` byte-identico; AGENTE em codegen** |
| **D1.3** | `src/backend/dwarf.tks` — escritor DWARF (interop), golden fixado | novo | substrato/interop | nenhuma — **comeca hoje** |
| **D1.4** | Escoadouro ELF | `src/backend/objfile_elf.tks` | substrato/interop | baixa |
| **D1.5** | Escoadouro Mach-O | `src/backend/objfile_macho.tks` | substrato/interop | baixa |
| **D1.6** | `.tsym` v2 (linhas L/F/V, **+ tipos Teko / tags / arena**) — o que o motor le | `src/codegen/codegen.tks` (`tk_emit_tsym`) | substrato Teko-aware | **AGENTE em codegen** |
| **DE.1** | Piso de syscall: `dbg_ptrace`/`dbg_waitpid`/`dbg_spawn_traced` (+ twin mach) | `teko_rt.{c,h}` (C mantida) | motor | nenhuma (C do piso, isolada) |
| **DE.2** | Motor: insercao int3 / continue / single-step / leitura de registradores+memoria | `src/debugger/engine.tks` | motor | depende DE.1 |
| **DE.3** | Desenrolar de pilha (usa `FrameLayout`/`FrameLayoutX86` ja calculados — `debugger-superficie...:264`) | `src/debugger/unwind.tks` | motor | depende DE.2, D1.1 |
| **DE.4** | Leitor `.tsym` v2 + **renderer Teko-aware** (fat-str/lista, tag de variante, erro-valor, arena) | `src/debugger/render.tks` | motor Teko-aware | depende D1.6, DE.2 |
| **DE.5** | CLI de terminal `teko debug` (break/step/bt/print) | `src/debugger/cli.tks` + `main.tks` | superficie | `main.tks` pequeno |
| **DE.6** | Servidor DAP `teko debug --dap` (reusa transporte §4) | `src/debugger/dap.tks` | superficie (agnostico) | reusa L1 |
| **DE.7** | Cliente VSCode: `contributes.debuggers` apontando nosso adaptador (forma ja em `debugger-superficie...:1017-1034`) | `tooling/vscode/package.json` | cliente VSCode (glue) | so cliente |

Camada 1 (breakpoint/step/bt com nomes Teko) = D0.1 + D1.1-D1.3 + DE.1-DE.3 + DE.5.
Renderer Teko-aware de valores (Camada 2) = DE.4. Windows adiado (§8.3).

---

## 9. Tabela-roteiro — ordena os DOIS trilhos, colisoes nomeadas, cores, vsix

Colisoes de agente **provadas por sitio** (voce so escreve docs; os agentes vivos sao):
`lower.tks`/`teko_rt.tks` (rota nativa), codegen/escape (arena), process/journal. Logo:

- **D1.1** toca `src/lir/lower.tks` — **agente vivo**. Nao pode entrar em paralelo cego;
  entra **depois** do agente de `lower.tks` fechar, ou coordenado.
- **D1.2/D1.6** tocam `src/codegen/codegen.tks` — **agente vivo (arena/escape)**. Mesmo
  gate; D1.2 ainda exige `.text` byte-identico, o que so e verificavel com o codegen estavel.
- **Tudo do LSP (L1-L12)** e **arquivos novos** (`src/lsp/**`) + duas edicoes pequenas
  (`main.tks`, `tooling/vscode/package.json`) — **zero colisao com os agentes vivos**. Logo
  o trilho LSP roda **em paralelo total** com a rota nativa.
- **DE.1** toca `teko_rt.{c,h}` — **agente vivo (nativo)**. Coordenar a adicao dos externs.

| Fase | Pode comecar | Trilho / Crumb | Tipo | Bloqueio / colisao |
|---|---|---|---|---|
| **P0 (hoje, paralelo)** | ja | **L1** transporte + **L2** initialize | servidor | nenhum |
| | ja | **D0.1** arnes + **D1.3** escritor DWARF | substrato | nenhum (comecam hoje) |
| **P1** | apos L2 | **L3** indexador | servidor | le collect/resolve (nao escreve) |
| | apos agente `lower.tks` | **D1.1** LFunc file/decl_line | substrato | **colide `lower.tks` — sequenciar** |
| **P2 (nucleo N3)** | apos L3 | **L4** definition ∥ **L5** references ∥ **L6** implementation ∥ **L7** hover | servidor | nenhum — paralelizavel entre si |
| | apos agente codegen | **D1.2** MLineMark (`.text` byte-identico) | substrato | **colide codegen — gate estrito** |
| | apos D1.3 | **D1.4** ELF ∥ **D1.5** Mach-O | interop | baixa |
| | apos agente codegen | **D1.6** `.tsym` v2 Teko-aware | substrato | **colide codegen** |
| **P3 (motor)** | apos agente `teko_rt` | **DE.1** externs syscall | motor | **colide `teko_rt` — coordenar** |
| | apos DE.1 | **DE.2** motor int3/step ⟶ **DE.3** unwind (usa D1.1) | motor | interno |
| **P4** | apos L3 | **L8** diagnostics ∥ **L9** executeCommand build/run | servidor | nenhum |
| | apos D1.6+DE.2 | **DE.4** renderer Teko-aware | motor | interno |
| **P5 (superficie)** | apos L2 | **L10** `teko lsp` no dispatch | servidor | `main.tks` pequeno |
| | apos DE.3 | **DE.5** `teko debug` CLI ∥ **DE.6** `teko debug --dap` (reusa L1) | superficie | `main.tks` pequeno |
| **P6 (cliente, VSCode primeiro)** | apos L10 | **L11** cliente LSP VSCode ∥ **L12** notas Neovim/Emacs/Helix | cliente | so cliente |
| | apos DE.6 | **DE.7** `contributes.debuggers` VSCode | cliente | so cliente |
| **P7 (cores + release)** | apos P6 | vsix (empacotar `tooling/vscode`), tag/release do tooling | cores/release | ritual final |

**Ritual (gate cheio):** fim de P2 (a triade de navegacao — nucleo de N3), apos D1.2 (o
crumb de maior colisao, `.text` byte-identico), fim de P3 (o motor para no primeiro
breakpoint com nome Teko), e P7 (empacotamento/release).

**Paralelismo maximo:** todo o trilho LSP (arquivos novos) roda ao lado dos agentes vivos
sem tocar `lower.tks`/`codegen`/`teko_rt`. Os pontos de serializacao sao **exatamente
tres**: D1.1 (`lower.tks`), D1.2/D1.6 (`codegen`), DE.1 (`teko_rt`). Cores (integracao de
release, vsix, tags) fecham em P7, depois dos dois trilhos entregarem superficie.

---

## 10. Regressoes — fixtures (entrada -> codigo de saida nativo esperado)

Estilo de arnes: um harness `.tkt`/`.tkr` que **repete** um roteiro de requisicoes
JSON-RPC contra o binario e compara as respostas; saida `0` = match, `!=0` = divergencia.
Isto e o mesmo padrao de arnes que o debugger ja fixou como "o ativo e o teste, o codigo e
descartavel".

| Fixture | Entrada | Saida esperada |
|---|---|---|
| `lsp_initialize.tkr` | `initialize` -> capabilities anunciam SO definition/references/implementation/hover que existem | exit 0 |
| `lsp_framing_truncated.tkr` | frame com `Content-Length` maior que o corpo (EOF no meio) | exit != 0 (erro honesto, nunca aceite silenciosa) |
| `lsp_goto_def.tkr` | cursor sobre um uso de tipo em ns A -> Location da decl em ns A | exit 0 |
| `lsp_goto_def_ns_collision.tkr` | dois `push` homonimos em ns A/B; cursor no de A -> so a decl de A (prova `canon`, nao texto) | exit 0 |
| `lsp_references.tkr` | um metodo com 3 usos em corpos + 1 decl; `includeDeclaration=true` -> 4 locais | exit 0 |
| `lsp_references_bodies.tkr` | uso dentro de um corpo de funcao (nao so assinatura) aparece | exit 0 (prova a metade cara de L3) |
| `lsp_goto_impl.tkr` | cursor num metodo de interface -> os N tipos concretos que implementam | exit 0 |
| `lsp_goto_impl_inverse.tkr` | cursor num metodo concreto -> o contrato que ele satisfaz | exit 0 |
| `lsp_hover_sig.tkr` | cursor numa fn -> assinatura renderizada | exit 0 |
| `lsp_execcmd_build.tkr` | `workspace/executeCommand teko.build <proj-ok>` -> exit 0 do filho | exit 0 |
| `lsp_execcmd_build_fail.tkr` | idem sobre projeto com erro de tipo -> codigo != 0 do filho propagado, sem panico do servidor | exit 0 (o teste verifica a PROPAGACAO) |
| `lsp_execcmd_no_shell.tkr` | projdir com metacaracteres de shell (`; rm -rf`) -> argv-vector, nenhum efeito de shell | exit 0 (prova a lei anti-injecao) |
| `dbg_break_line.tkr` | breakpoint numa linha `.tks` -> para com nome Teko da funcao e file:line | exit 0 |
| `dbg_bt_frameless.tkr` | `bt` atraves de uma funcao frameless mantem a profundidade (fixture `adv.s` do D0.1) | exit 0 |
| `dbg_render_str.tkr` | `print` de um `str` -> texto, nao ptr+len crus | exit 0 (prova Teko-aware, DE.4) |
| `dbg_render_variant.tkr` | `print` de `T | null` = `null` e de `T | error` -> tag/valor por nome Teko | exit 0 |

---

## 11. Riscos, tensoes de lei, e o que fica bloqueado (design-ahead)

| Risco / tensao | Prova | Resolucao recomendada (lei-primeiro) |
|---|---|---|
| find-references incompleto se L3 so indexar assinaturas | usos vivem em `Expr` (`ast.tks:259`), corpos precisam de walk | L3 percorre corpos; fixado como a metade cara (§5.3, fixture `lsp_references_bodies`) |
| Log em stdout corrompe o stream JSON-RPC | protocolo exige framing puro; `tk_println` nao enquadra | logs em **stderr** (`teko_rt.tks:85` `tk_ewrite`); corpo por `tk_write` cru apos header (§4) |
| Injecao de shell a partir do editor | lei ratificada `debugger-superficie...:1012-1014`, achado real | argv-vector sempre (`process.tks:288`), task `args:[...]`; fixture `lsp_execcmd_no_shell` |
| D1.1 colide com agente vivo em `lower.tks` | `debugger-superficie...:1124` (`lower.tks` no caminho) + agente vivo | sequenciar apos o agente fechar; nao entra em P0 |
| D1.2/D1.6 colidem com agente de codegen/arena | codegen tem agente; D1.2 exige `.text` byte-identico | gate estrito apos codegen estavel; nao paralelizar cego |
| DE.1 adiciona superficie C nova | ptrace/mach sao syscalls sem forma Teko | excecao ja aberta para `teko_rt.{c,h}`; coordenar com agente `teko_rt`; **honesto: e a maior superficie C nova do ciclo, e e so o piso** |
| Windows sem debug-info | `debugger-orcamento-0.3.1.md:747-748` | adiado explicitamente (coerente com o relatorio) |
| DWARF vs `.tsym` v2 | DWARF-4 min nao carrega tipos/arena/tags | `.tsym` v2 primeira classe (motor), DWARF interop opcional (§8.4) — sem tensao, um produtor dois escoadouros |

**Nada aqui HALTa.** Todas as tensoes se resolvem lei-primeiro (Teko-only + a excecao do
piso; a legislacao do `.tsym`; a lei anti-injecao; a regra do sitio `:37`). As unicas
dependencias externas sao os **tres pontos de serializacao** (`lower.tks`, `codegen`,
`teko_rt`) — e o trilho LSP inteiro **nao depende de nenhum deles**, logo comeca ja
(design-ahead cumprido: L1-L9 e D0.1/D1.3 sao arquivos novos que compilam hoje).

**O que fica bloqueado (e so isto):** D1.1 (ate `lower.tks` liberar), D1.2/D1.6 (ate
codegen estabilizar), DE.1 (ate coordenar `teko_rt`). O resto do dossie — todo o servidor
LSP, o transporte compartilhado, os provedores, o build/run, o escritor DWARF, o arnes, e
todos os crumbs de motor/superficie **acima** do piso — esta desenhado e destravado.
