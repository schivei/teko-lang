# TEKO_ROADMAP_TOOLING — ferramental de editor (cores, intellisense, build) para VS Code, Vim/Neovim, Emacs, Nano

> **Escopo.** Este roadmap cobre o ferramental de **editor** para Teko: quatro alvos (**VS Code**,
> **Vi/Vim + Neovim**, **Emacs**, **Nano**) em três capacidades (**cores** = realce de sintaxe,
> **intellisense** = language server, **ferramental de compilação** = build/run/test integrados ao
> editor). É um roadmap de **consumo**, não de linguagem: nada aqui altera `src/`.

> **Não-escopo / relação com a SUPREME RULE.** A SUPREME RULE (zero desalinhamento `.c`/`.h` ↔ `.tks`)
> não se aplica a este documento — o ferramental vive fora do compilador, num diretório `tooling/` na
> raiz do repo, e consome apenas **interfaces estáveis** já existentes ou a existir: a CLI
> (`teko build|run|test <dir>` — `main.c:119-127`), o formato de diagnóstico
> `teko: <arquivo>:<linha>:<coluna>: <mensagem>` (já emitido — `src/checker/check_modules.c:18`,
> `src/driver.c:198-213`), e — quando existir (Eixo C) — o protocolo **LSP** de um servidor `teko-lsp`.

> **Cross-ref.** Depende do Eixo E do `TEKO_ROADMAP_INDEPENDENCE.md` (posição `file:line` — já
> parcialmente fiada no checker, ver acima) para diagnósticos ricos, e do Eixo A do mesmo documento
> (compilação por `.tkp`) para os comandos de build que o editor invoca. Não altera o `TEKO_MASTER_PLAN.md`
> (não é uma fase do compilador); pode ser referenciado lá como uma trilha paralela quando o Eixo C aqui
> amadurecer.

> **Princípio DRY aplicado ao próprio ferramental.** Palavras-chave, operadores, delimitadores de
> comentário e regras de string do Teko vivem em **um lugar** (`src/lexer/token.tks` + `lexer.tks`).
> É proibido manter 4 cópias manuais divergentes (uma por editor) dessa mesma informação — o Eixo A
> gera as quatro saídas a partir de **uma** fonte.

---

## Eixo A — Fonte única de léxico para realce (evita 4 cópias divergentes)

**Cânone:** a lista de keywords/operadores/literais/comentários é definida **uma vez**, lida do lexer
real, e cada editor consome uma **saída gerada**, nunca uma cópia digitada à mão.

| # | Entrega | Estado |
|---|---|---|
| A1 | **Extrator de especificação** (`tooling/shared/grammar-spec.json`) — lê `src/lexer/token.tks` (+ `lexer.tks`) e emite um JSON canônico: `{keywords, operators, literals, line_comment, block_comment, string_escapes}`. | falta |
| A2 | **Gerador TextMate** (`tooling/vscode/syntaxes/teko.tmLanguage.json`) a partir do A1. | falta (dep A1) |
| A3 | **Gerador de sintaxe Vim** (`tooling/vim/syntax/teko.vim`, `syntax match`/`syntax keyword`) a partir do A1. | falta (dep A1) |
| A4 | **Gerador de font-lock Emacs** (`tooling/emacs/teko-mode.el` — lista `teko-font-lock-keywords`) a partir do A1. | falta (dep A1) |
| A5 | **Gerador de regras Nano** (`tooling/nano/teko.nanorc`) a partir do A1 — sem aninhamento/contexto (limitação do próprio Nano), cobertura best-effort. | falta (dep A1) |

> **Resultado do Eixo A:** uma mudança de sintaxe da linguagem (nova keyword, novo operador) se propaga
> para os 4 editores regerando as saídas de A2–A5, nunca editando 4 arquivos à mão.

---

## Eixo B — Cores (realce de sintaxe) por editor

| # | Entrega | Estado |
|---|---|---|
| B1 | **VS Code** — gramática TextMate embutida (consome A2) + `language-configuration.json` (comentários `//`/`/* */`, pares de bracket/aspas, indentação por bloco). | falta (dep A2) |
| B2 | **Vim/Neovim** — `syntax/teko.vim` (consome A3), `ftdetect/teko.vim` mapeando `.tks`/`.tkt` → filetype `teko` e `.tkp` → `toml` (o manifesto já É TOML — `TEKO_ROADMAP_INDEPENDENCE.md` Eixo A). *Opcional/futuro:* gramática Tree-sitter (`queries/teko/highlights.scm`) para Neovim ≥0.9, mais precisa que regex. | falta (dep A3) |
| B3 | **Emacs** — `teko-mode.el`, modo maior derivado de `prog-mode`, `teko-font-lock-keywords` (consome A4) + `syntax-table` para comentários/strings + `.tkp` associado a `toml-mode` se instalado. | falta (dep A4) |
| B4 | **Nano** — `teko.nanorc` (consome A5): keywords, tipos primitivos, comentários de linha/bloco, strings/interpolação. Sem contexto/aninhamento — aceita falso-positivo ocasional (limite conhecido do Nano). | falta (dep A5) |

---

## Eixo C — Intellisense (`teko-lsp`, Language Server Protocol)

**Cânone:** o intellisense **reaproveita** o front-end real de Teko (lexer/parser/checker), nunca
reimplementa análise léxica/semântica em paralelo — mesmo princípio DRY/SUPREME RULE do compilador,
aplicado ao servidor. `teko-lsp` é um **processo separado** que embute esse front-end como biblioteca.

| # | Entrega | Estado |
|---|---|---|
| C1 | **Esqueleto `teko-lsp`** — processo stdio, handshake LSP (`initialize`/`initialized`/`shutdown`/`exit`), linka `src/lexer`+`src/parser`+`src/checker` como biblioteca (não duplica). | falta |
| C2 | **Diagnostics** — reanálise por arquivo ao editar/salvar (debounce), publica `textDocument/publishDiagnostics` a partir dos `tk_error` do checker — mesmo `arquivo:linha:coluna` que a CLI já emite (ver Eixo E do ROADMAP_INDEPENDENCE para a doutrina de posição). Erro de parse → diagnóstico honesto, nunca trava o editor. | falta (dep C1) |
| C3 | **Hover** — tipo inferido da expressão sob o cursor, lido direto da árvore tipada (`tast`) já produzida pelo checker. | falta (dep C1) |
| C4 | **Completion** — símbolos em escopo (vars/fns), membros de `struct`/`interface`, namespaces alcançáveis via `use`, e keywords da linguagem. | falta (dep C1) |
| C5 | **Go-to-definition / find-references** — usa a mesma tabela de símbolos/escopo do checker (`resolve.c`/`scope.c`), não uma reconstrução própria. | falta (dep C1) |
| C6 | **Document symbols / outline** — lista de `fn`/`struct`/`variant`/`interface` de um arquivo. | falta (dep C1) |
| C7 | *(avançado, opcional)* **Semantic tokens** — realce preciso baseado no checker (distingue tipo vs valor vs namespace), superior ao regex do Eixo B quando o cliente suporta. | falta (dep C1–C6) |

> **Nano fica de fora do Eixo C por design, não por atraso** — não tem API de plugin/processo externo
> para falar LSP. Recebe só o Eixo B (cores) e a documentação do Eixo D5 (fluxo manual de build).

---

## Eixo D — Clientes LSP + integração de build/run/test por editor

**Ferramental de compilação já existe hoje** (`teko build|run|test <dir> [-o <out>] [--coverage]
[--no-test]` — `main.c:119-127`, `src/driver.c`); D2 não tem dependência do Eixo C e pode ser entregue
imediatamente.

| # | Entrega | Estado |
|---|---|---|
| D1 | **VS Code — client LSP** — extensão contribui `vscode-languageclient`, conecta a `teko-lsp` via stdio, ativa para linguagem `teko` (`.tks`/`.tkt`) e `toml`+schema para `.tkp`. | falta (dep C1) |
| D2 | **VS Code — tasks + problemMatcher** — comandos "Teko: Build" / "Teko: Run" / "Teko: Test" chamando a CLI; `problemMatcher` `^teko: (.*):(\d+):(\d+): (.*)$` casa o formato hoje emitido. | falta (sem dependência — CLI já existe) |
| D3 | **Vim/Neovim — client LSP** — `nvim-lspconfig` (Neovim nativo) ou `vim-lsp`/`ale` (Vim 8+), apontando para o binário `teko-lsp`. `:make`: `makeprg=teko\ build\ %:h`, `errorformat=teko\\:\\ %f:%l:%c:\\ %m`. | client: falta (dep C1) · `:make`: falta (sem dependência) |
| D4 | **Emacs — client LSP** — `eglot` (nativo desde Emacs 29) ou `lsp-mode`, apontando para `teko-lsp`. `compile-command="teko build ."` + `compilation-error-regexp-alist` casando o mesmo formato. | client: falta (dep C1) · `compile`: falta (sem dependência) |
| D5 | **Nano — fluxo manual** — sem client LSP nem task runner nativos; documentar rodar `teko build|run|test .` no terminal ao lado do editor. Só documentação, sem código. | falta |

---

## Eixo E — Empacotamento e distribuição dos plugins

**Layout de diretório proposto** (paralelo a `src/` — não mistura com o compilador):

```
tooling/
├── shared/     # A1: grammar-spec.json (fonte única)
├── teko-lsp/   # C1-C7: o servidor LSP
├── vscode/     # B1, D1, D2: a extensão
├── vim/        # B2, D3: plugin Vim/Neovim
├── emacs/      # B3, D4: teko-mode.el
└── nano/       # B4, D5: teko.nanorc + doc
```

| # | Entrega | Estado |
|---|---|---|
| E1 | Criar a árvore `tooling/` acima (esqueletos vazios, sem lógica). | falta |
| E2 | **VS Code** — empacotar `.vsix` (`vsce package`); instalação local sempre suportada; publicação no Marketplace é decisão aberta. | falta (dep B1, D1, D2) |
| E3 | **Vim/Neovim** — plugin instalável por gerenciador padrão (`vim-plug`/`packer.nvim`/`lazy.nvim`) apontando para `tooling/vim/`; sem gerenciador, `:set rtp+=` manual. | falta (dep B2, D3) |
| E4 | **Emacs** — pacote via `use-package :load-path` apontando para `tooling/emacs/`; publicação em MELPA é decisão aberta (futuro). | falta (dep B3, D4) |
| E5 | **Nano** — arquivo único; instalação = `include "~/.nano/teko.nanorc"` em `~/.nanorc`, documentado, sem instalador. | falta (dep B4) |

---

## Decisões — ratificadas e abertas

**Ratificadas (Law-first — DRY/SUPREME RULE aplicados ao ferramental):**
- Fonte única de léxico (Eixo A) — proibido manter 4 cópias manuais de keywords/operadores.
- `teko-lsp` reaproveita `src/lexer`/`src/parser`/`src/checker` como biblioteca — proibido reimplementar
  análise léxica/semântica em paralelo dentro do servidor.
- Nano recebe só cores (Eixo B) + doc de build manual (D5) — sem API de plugin, não é uma lacuna a fechar.

**Abertas (a decidir quando o crumb chegar):**
- **Linguagem de implementação do `teko-lsp`:** o gate nativo self-host ainda não fecha (`TEKO_MASTER_PLAN.md`
  Fase 6 🔶) — recomendo começar em **C** sobre `src/checker` já existente e migrar para Teko quando o
  self-host fechar, mesmo caminho já trilhado pelo resto do compilador (`codegen.tks` espelhando
  `codegen_c.c`).
- **Binário separado vs subcomando:** `teko-lsp` autônomo ou `teko lsp` como subcomando da própria CLI
  (ao lado de `build`/`run`/`test` em `main.c`)? Recomendo **subcomando** — evita distribuir/instalar um
  segundo binário e reaproveita o dispatch já existente.
- **Tree-sitter para Neovim (B2):** nice-to-have, não bloqueia o `syntax/teko.vim` via regex.
- **Publicação em marketplaces** (VS Code Marketplace, MELPA): decisão de distribuição, diferida até o
  ferramental estar funcional localmente (E2/E4 cobrem instalação local sempre).

---

## Caminho crítico

**A (fonte única)** → **B (cores, 4 editores em paralelo)** ‖ **C (LSP)** → **D (clientes + build)** →
**E (empacotamento)**. Exceção: **D2 (VS Code tasks + problemMatcher)** não depende de C nem de A — a
CLI já existe hoje e pode ser entregue imediatamente, em paralelo a tudo o resto.

---

## Breadcrumbs (segmentação para agentes)

> Cada crumb = uma unidade de agente / um sub-PR, dentro de `tooling/` (nunca toca `src/`). Convenção:
> todo crumb do Eixo A entrega gerador + saída regenerada; todo crumb do Eixo C entrega o par
> `teko-lsp` (código) + smoke-test manual documentado.

### Eixo A — fonte única
- **[A1] Extrator de grammar-spec** — deps: — · par: `tooling/shared/` → lê `src/lexer/token.tks`, emite `grammar-spec.json`. **Aceite:** JSON contém todas as keywords/operadores atuais do lexer, sem hardcode paralelo.
- **[A2]–[A5] Geradores por editor** — deps: A1 · um crumb por editor (TextMate/Vim/Emacs/Nano). **Aceite:** cada saída gerada abre corretamente no editor-alvo sem edição manual pós-geração.

### Eixo B — cores
- **[B1]–[B4] Realce por editor** — deps: A2/A3/A4/A5 respectivamente. **Aceite:** um arquivo `.tks` de exemplo (`examples/`) mostra keywords, tipos, strings, comentários e literais numéricos coloridos distintamente no editor.

### Eixo C — intellisense
- **[C1] Esqueleto `teko-lsp`** — deps: — · par: `tooling/teko-lsp/`. **Aceite:** responde a `initialize` e mantém o processo vivo sobre stdio, sem travar.
- **[C2] Diagnostics** — deps: C1. **Aceite:** abrir um `.tks` com erro de tipo mostra o erro sublinhado no editor, mesma mensagem da CLI.
- **[C3]–[C6] Hover/Completion/Definition/Symbols** — deps: C1 (podem paralelizar entre si). **Aceite:** cada recurso responde corretamente sobre um projeto de exemplo do corpus.
- **[C7] Semantic tokens** — deps: C2–C6. **Aceite:** realce via LSP substitui visualmente o regex do Eixo B quando ativo, sem regressão de cobertura de tokens.

### Eixo D — clientes + build
- **[D1] Client VS Code** — deps: C1. **Aceite:** diagnostics/hover/completion aparecem na aba correta do VS Code.
- **[D2] Tasks + problemMatcher VS Code** — deps: — (CLI já existe). **Aceite:** "Teko: Build" roda `teko build .` e popula a aba Problems a partir de um erro real.
- **[D3] Client + `:make` Vim/Neovim** — deps: C1 (client) / — (`:make`). **Aceite:** `:make` navega para `arquivo:linha:coluna` de um erro real via quickfix.
- **[D4] Client + `compile` Emacs** — deps: C1 (client) / — (`compile`). **Aceite:** `M-x compile` navega para o erro via `next-error`.
- **[D5] Doc Nano** — deps: —. **Aceite:** um parágrafo no `tooling/nano/README` explica o fluxo de terminal-ao-lado.

### Eixo E — empacotamento
- **[E1]–[E5]** — deps: conforme tabela do Eixo E. **Aceite (cada um):** instalação local funcional documentada num README por editor.

---

## Estado
- **✓ Feitos:** nenhum — documento novo, define o roadmap.
- **▶ Prontos agora (sem dependência):** **A1** (extrator de spec) · **D2** (tasks/problemMatcher VS Code — a CLI já existe e já emite `arquivo:linha:coluna`) · **D5** (doc Nano).
- **Em seguida:** A1 → {A2, A3, A4, A5} → {B1, B2, B3, B4}; C1 → {C2…C7} → {D1, D3, D4}; tudo → Eixo E.
