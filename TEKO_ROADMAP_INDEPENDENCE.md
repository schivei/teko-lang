# TEKO_ROADMAP_INDEPENDENCE — projeto, testes e independência (pós-primeiro-binário)

> Sucede/!acompanha o `TEKO_ROADMAP_BINARY.md` (o primeiro binário via transpile-para-C, M0/M1).
> Enquanto o BINARY prova a *pipeline* (read→lex→parse→check→emit-C→cc→exec), este roadmap reajusta o
> que falta para a Teko ser uma toolchain **de verdade**: compilação **por projeto** (`.tkp`), um
> **framework de testes** com cobertura mínima de release, e o caminho à **independência** (FFI + backend
> próprio + self-hosting). Ordem = **M.4** (cada fase repousa na anterior). `src/` é canônico.

> **Falha grave que motiva este roadmap.** Hoje `teko <file.tks>` compila **um arquivo isolado** e escolhe
> o parser por *basename* (`src/driver.c`: `tk_compile`/`basename_of`). Isso contradiz o cânone: a Teko
> **compila o projeto inteiro** descrito por um `.tkp`. *(REBOOT_PLAN §2.6, l.758: "O compilador lê o `.tkp`,
> descobre os arquivos e compila o projeto inteiro de uma vez — vê todos os arquivos antes de resolver
> referências. Consequências: sem headers, sem ordem de declaração…".)* Corrigir isso é a **Fase A**, e
> é pré-requisito de quase tudo o que é real (resolução cross-namespace, regra do `main.tks`, dependências).

---

## Eixo A — Compilação por projeto (`.tkp`)

**Cânone:** o `.tkp` é **TOML**, declara `name` (raiz canônica), `artifact` (executable/library), `source`
(raiz do código, invisível nos namespaces), `[dependencies]`, `[aliases]` *(TEKO_LEGISLATION §208–214)*.
Namespace = **diretório**; arquivos no mesmo dir **agregam** numa namespace; `src/` é a raiz `teko::`
*(LEGISLATION §181–188; REBOOT_PLAN §2.6, l.753–756)*. A regra do `main.tks` (executável **exige**,
biblioteca **proíbe**) já existe em Teko: `src/build/tkp_rule.tks` (`check_main_file_rule`).

| # | Entrega | Lei | Estado |
|---|---|---|---|
| A1 | **Leitor `.tkp` (TOML)** — parser TOML mínimo → `Manifest { name, artifact, source, deps, aliases }`. Lido ANTES de qualquer Teko (LEGISLATION: "standalone simple parser"). | M.3 | falta (B1b no BINARY) |
| A2 | **Enumeração do file-set + mapa de namespaces** — varrer a raiz `source`, descobrir `.tks`/`.tkt`, mapear `src/lexer/lexer.tks → teko::lexer`. | M.4 | falta (precisa de `teko::fs` dir-listing — ver Eixo C) |
| A3 | **Modelo de programa multi-arquivo** — agregar TODOS os arquivos num único `tk_program` antes de resolver; visibilidade física (privado=dir, `pub`=projeto, `exp`=outros projetos). | M.1 | parcial (o checker já tipa um `tk_program`; falta a agregação cross-arquivo) |
| A4 | **Regra do `main.tks` fiada** — chamar `check_main_file_rule(artifact, has_main)` a partir do `artifact` do manifesto (NÃO do basename). Aposentar a heurística de basename no driver. | M.3 | falta (a regra existe; falta a fiação) |
| A5 | **Driver por projeto** — `teko build [projdir]` lê o `.tkp` da raiz; sem `.tkp`, erro honesto. (Compilar um `.tks` avulso deixa de ser a entrada normal.) | M.2 | falta |
| A6 | **Pacotes + pré-linker** (deps) — carregar `.tkh`+`.tkb` das dependências e fundir as árvores-tipadas ANTES do codegen (pré-link estático, não FFI — LEGISLATION §215–226). | M.1 | **diferido** (evolução; o codec `.tkb`/`.tkh` já existe) |

> **Resultado do Eixo A:** `teko build` sobre um diretório com `.tkp` compila o projeto inteiro, com
> resolução cross-namespace e a regra do `main.tks` aplicada a partir do artefato. A1–A5 são a semente;
> A6 (deps) é evolução.

---

## Eixo B — Framework de testes + portão de cobertura

**Cânone + legislação do usuário:** `.tkt` = testes **junto** ao `.tks`, mesma namespace (enxergam o
privado), marcados por `#test`, compilados no perfil de teste *(REBOOT_PLAN §2 rev.8, l.129–131;
LEGISLATION §186–187)*. Perfis: **debug=VM**, **release=nativo**, **CI roda release** como portão de
qualidade *(REBOOT_PLAN §2.21, l.964–977)*. "P22 Testing + coverage" está listado como **PULL EARLY**
*(REBOOT_PLAN l.1140)* — mas **não há doutrina de cobertura ainda**.

**Legislado pelo usuário (a registrar na LEGISLATION):**
1. **`assert` NÃO é keyword.** *(Já verdade — não há `Assert` no `token.tks`; `assert` é `Ident`. Zero
   mudança no lexer.)*
2. As ferramentas de teste vivem em **namespace próprio**: `teko::assert::is_true | is_false |
   str_contains | …` (e correlatos), injetadas como stdlib (não-importadas, raiz `teko::`).
3. Testes rodam **isolados** (`teko test`). Para produzir **release**, **havendo `.tkt`**, eles **devem
   ser executados** e a Teko impõe um **threshold mínimo de 80% de cobertura** (portão próprio).

| # | Entrega | Lei | Estado |
|---|---|---|---|
| B1 | **Namespace `teko::assert`** — `is_true(bool)`, `is_false(bool)`, `equals(a,b)`, `str_contains(hay,needle)`, … ; em falha → `teko::panic` (hard-fail na semente; soft-fail = evolução). | M.1/M.3 | **novo** (não existe) |
| B2 | **Migrar os 7 `.tkt`** do `assert` solto → `teko::assert::*`. (`lexer_test`, `parser_test`, `checker_test`, `tkb_test`, `header_test`, `text_test`, `tkp_rule_test`.) | M.3 | falta |
| B3 | **Coletor + runner `#test`** — coletar funções `#test` (já existe ALPHA "o compilador coleta"); rodar, reportar nome/contagem/pass-fail, **exit ≠0** em qualquer falha (compatível com portão). `teko test`. | M.1 | parcial (coletor ALPHA) |
| B4 | **Instrumentação de cobertura (por LINHA)** — contadores por linha; medir % de linhas de produção executadas ao rodar os `#test`. (Métrica = **linha**, decisão do legislador.) `.tkt` não conta para o denominador. | M.3 | **novo** |
| B5 | **Portão de testes + cobertura** — threshold **configurável no `.tkp`, piso absoluto 10%** (abaixo disso não há por que ter `.tkt`), **default 80%**. **Release:** havendo `.tkt`, os testes **executam obrigatoriamente** (não há `--no-test`); falha de teste OU cobertura < threshold ⇒ **build barrado**. **Debug/test:** testes rodam por default, mas `--no-test` é permitido. Os `.tkt` **não vão no binário** — barram o build de release, não o ocupam. | M.1 | **novo (legislar)** |

> **Resultado do Eixo B:** `teko test` roda a suíte isolada (linha-cobertura medida); `teko build --release`
> é barrado por testes verdes + cobertura ≥ threshold (default 80%, configurável, piso 10%); em debug/test
> `--no-test` pula. `teko::assert` (`is_true`/`is_false`/`equals`/`not_equals`/`str_contains`/`is_error`/`is_ok`)
> é a superfície canônica; `assert`-solto sai dos `.tkt`.

---

## Eixo C — Independência + FFI

**Cânone:** FFI = **um opcode** (`OP_CALL_EXTERN`/`OP_SYSCALL`) que os emitters baixam para a convenção da
plataforma — o "fundo OS/FFI" sobre o qual **IO, arena, rede, tempo, threads** viram lib *(REBOOT_PLAN
§7.2, l.1167–1170; `src/core.tks` "a one true intrinsic")*. Teko↔Teko é **pré-link estático de árvore
tipada**, NÃO FFI; **FFI é só para código estrangeiro** na fronteira insegura *(LEGISLATION §215–226)*.
`ptr`/`uptr` são **opacos, só-transporte** *(LEGISLATION §235–240)*. A semente **inclui FFI/syscall** (ler
fonte, escrever saída, `exit`) *(REBOOT_PLAN l.1104–1105)*; a **forma do `extern`** é decisão aberta
*(l.1186–1187)*. Três estágios: **`.tkb` VM → AOT-nativo (LTS, é o que ships) → bare-metal**
*(CONSTITUTION §237–245)*; backend **próprio, sem LLVM** *(REBOOT_PLAN l.974–976)*; **bootstrap em 4
pontos** *(REBOOT_PLAN l.1075–1094)*.

| # | Entrega | Lei | Estado |
|---|---|---|---|
| C1 | **Primitiva FFI/`extern`** — declarar função externa + marshalling na fronteira; `void*`/`ptr` opacos. **Forma sintática = decisão pendente.** | M.0/M.1 | **falta (legislar a forma)** |
| C2 | **Superfícies host sobre FFI** — `teko::env::args`, `teko::exit`, `teko::panic`, `teko::io` (slurp: `read_file`/`write_file`/`write_err`, LEGISLATION §270–282) + **listagem de diretório** (para o Eixo A2) + **process/exec** (invocar `cc` na fase transpile). Destrava `driver.tks`/`main.tks`. | M.4 | parcial (só `read_file` na semente C; resto falta) |
| C3 | **Backend nativo próprio** — emitir direto ao metal + linker próprio, aposentando o `cc` do host (o transpile-para-C é degrau, não o design final). | M.0 | **diferido** (pós-primeiro-binário) |
| C4 | **Self-hosting** — materializar `codegen.tks` + `driver.tks`; rodar o ciclo de 4 pontos (semente-C → compilador-Teko ger.1 → ger.2==ger.3 bit-a-bit + corretude diferencial); **aposentar o C**. | M.4 | **diferido** (trilha longa) |
| C5 | **Capabilities / sandboxing / auditoria de superfície** (`exp`/`extern`/syscall). | M.1 | **evolução** |

### Backlog nomeado (já levantado, alimenta a independência)
genéricos + constraints · ponteiros de função / `use`-capture / `inject` (DI) · métodos/OOP (override pós-genéricos) · `flags` · modo **VM/`.tkb`** (dev-loop) · pacotes/pré-linker (A6) · concorrência (`ref`+escape) · crypto/TLS (libs finas sobre FFI, **sem** TLS/HTTP nativo). *(Citações: REBOOT_PLAN §3–7, l.1104–1170; TEKO_HISTORY backlog.)*

---

## Decisões — ratificadas e abertas

**Ratificadas (legislador):**
- **Sequência:** sem ordem rígida — tudo **segmentado em breadcrumbs** com dependências; agentes pegam crumbs prontos (deps satisfeitas). Ver `## Breadcrumbs`.
- **`teko::assert`:** superfície aprovada — `is_true`, `is_false`, `equals`, `not_equals`, `str_contains`, `is_error`, `is_ok`.
- **Cobertura:** métrica = **linha**; `.tkt` fora do denominador.
- **Portão:** threshold **configurável no `.tkp`, piso 10%, default 80%**; release com `.tkt` **executa obrigatório** (sem `--no-test`); debug/test permite `--no-test`.

**Abertas (a legislar quando o crumb chegar):**
- **F — Forma do `extern`/FFI (C1):** sintaxe da declaração externa + marshalling (`marshall`?) + conversão de `ptr`. *(Cânone em aberto — REBOOT_PLAN l.1186.)* → crumb **C1.0 (legislação)** bloqueia C1.1+.
- **`teko build` por projeto (A5):** compilar um `.tks` avulso vira modo de diagnóstico (`teko check <file>`) ou sai de vez? → resolver no crumb A5.

---

## Caminho crítico
**A1–A5 (projeto/`.tkp`)** → **B1–B3 (assert namespace + runner)** → **C1–C2 (FFI + superfícies host)** →
**B4–B5 (cobertura + portão release)** → **C3 (backend próprio)** → **C4 (self-hosting)**.
A6 (deps/pré-linker) e C5 (capabilities) são evolução. O Eixo A destrava o Eixo C2 (que precisa de `fs`),
que por sua vez destrava `driver.tks`/`main.tks` (self-hosting). F3-pânicos (BINARY) corre em paralelo.

---

## Breadcrumbs (segmentação para agentes)

> Cada crumb = **uma unidade de agente / um sub-PR**. Formato: **[ID] título** — *deps* · *lei* · *par
> (arquivos)* → o quê + **Aceite**. Convenção: todo crumb de código entrega o **par** (`.tks` + semente
> `.c`/`.h`) + `.tkt`. Agentes **rascunham**, eu integro; tensão → **HALT → tribunal**. Um agente pega
> qualquer crumb cujas deps estejam satisfeitas.

### Eixo A — projeto/`.tkp`
- **[A1] Leitor TOML** — deps: — · M.3 · par: `src/build/manifest.{tks,c,h}` + `manifest_test.tkt`
  > Parser TOML mínimo → `Manifest { name, artifact, source, deps[], aliases[] }`, lido antes de qualquer Teko. **Aceite:** parseia `.tkp` exec e lib; TOML inválido → erro honesto; `.tkt` cobre os campos.
- **[A2] File-set + mapa de namespaces** — deps: A1 (fs: semente-C usa `dirent` contido; par Teko = C2c) · M.4 · par: `src/build/discover.{tks,c,h}` + test
  > Varre a raiz `source`, lista `.tks`/`.tkt`, mapeia `src/lexer/lexer.tks → teko::lexer`. **Aceite:** sobre o próprio repo, gera o set+map esperado.
- **[A3] Programa multi-arquivo** — deps: A2 · M.1 · par: `src/build/assemble.{tks,c,h}` + test
  > Lexar+parsear cada arquivo, agregar num `tk_program` cross-namespace; visibilidade (privado=dir/`pub`=projeto/`exp`=outros). **Aceite:** projeto de 2+ namespaces compila como um; referência cross-namespace resolve.
- **[A4] Regra do `main.tks` fiada** — deps: A1, A3 · M.3 · par: `driver` + reuso `tkp_rule.tks`
  > `artifact` do manifesto → `check_main_file_rule`; remove o basename-heurístico. **Aceite:** exec sem main → erro; lib com main → erro; válidos passam.
- **[A5] Driver por projeto** — deps: A1–A4 · M.2 · par: `main.{tks,c}` + `driver`
  > `teko build [projdir]` lê o `.tkp`; sem `.tkp` → erro. **Decidir:** `.tks` avulso → `teko check <file>` ou removido. **Aceite:** `teko build` compila o repo; uso atualizado.
- **[A6] Deps + pré-linker** — deps: A5, codec `.tkb`/`.tkh` (existe) · M.1 · **evolução** · par: `src/build/prelink.{tks,c,h}`
  > Carrega `.tkh`+`.tkb` das deps, funde árvores tipadas antes do codegen. **Aceite (futuro):** projeto+1 dep-lib linka estático e checa junto.

### Eixo B — testes/cobertura
- **[B1] `teko::assert`** — deps: — · M.1/M.3 · par: `src/assert/assert.{tks,c,h}` + `assert_test.tkt`
  > `is_true`/`is_false`/`equals`/`not_equals`/`str_contains`/`is_error`/`is_ok`; falha → `teko::panic`; injetada (raiz `teko::`). **Aceite:** cada assert passa no caso verdadeiro, paniqueia no falso.
- **[B2] Migrar os 7 `.tkt`** — deps: B1 · M.3 · par: os `*_test.tkt`
  > `assert` solto → `teko::assert::*`. **Aceite:** nenhum `assert` solto resta; suíte ainda verde.
- **[B3] Runner `#test`** — deps: B1 · M.1 · par: `src/build/testrun.{tks,c,h}` + `driver` (`teko test`)
  > Coleta `#test`, roda, reporta, exit≠0 em falha. **Aceite:** `teko test` verde no repo; falha injetada → exit≠0 + relatório.
- **[B4] Cobertura por linha** — deps: B3 · M.3 · par: `src/build/coverage.{tks,c,h}` + hook no codegen
  > Contadores por linha; % de linhas de produção (denominador exclui `.tkt`). **Aceite:** relatório determinístico para o repo.
- **[B5] Portão de release** — deps: A1 (threshold no `.tkp`), B3, B4 · M.1 · par: `driver` (`build --release`/`--no-test`)
  > Threshold configurável (piso 10%, default 80%); release+`.tkt` ⇒ testes obrigatórios + cobertura ≥ threshold senão barra; debug/test permite `--no-test`. **Aceite:** abaixo do threshold falha; acima passa; `--no-test` recusado em release.

### Eixo C — independência/FFI
- **[C1.0] LEGISLAR `extern`/FFI** — deps: — · M.0/M.1 · par: `TEKO_LEGISLATION.md`
  > Fixar a forma do `extern`/syscall (declaração, marshalling, `ptr`). **Tribunal.** **Aceite:** cláusula na LEGISLATION; desbloqueia C1.1.
- **[C1.1] Primitiva `extern`** — deps: C1.0 · M.0 · par: `src/codegen` + `runtime`
  > Baixa `extern` para a convenção da plataforma (opcode único). **Aceite:** um `extern` mínimo chama o host e retorna.
- **[C2a] `teko::env` + `exit`** — deps: C1.1 · M.4 · par: `src/env/env.{tks,c,h}` + test → `args()`, `exit(n)`.
- **[C2b] `teko::io` (slurp)** — deps: C1.1 · M.4 · par: `src/io/io.{tks,c,h}` + test → `read_file`/`write_file`/`write_err`.
- **[C2c] `teko::fs` (dir-list)** — deps: C1.1 · M.4 · par: `src/fs/fs.{tks,c,h}` + test → `list_dir` (par Teko de A2).
- **[C2d] `teko::process` (exec)** — deps: C1.1 · M.4 · par: `src/process/process.{tks,c,h}` + test → invocar `cc`.
  > **Aceite (cada C2x):** roda sobre FFI; `.tkt` cobre feliz + erro.
- **[C3] Backend nativo próprio** — deps: C1.1 · M.0 · **diferido** · par: `src/codegen` (emitters + linker)
  > Emite direto ao metal, aposenta o `cc`. **Aceite (futuro):** binário nativo sem host cc.
- **[C4] Self-hosting** — deps: A5, B5, C2*, M1, M2 · M.4 · **diferido**
  > Ciclo 4 pontos (semente-C → Teko ger.1 → ger.2==ger.3 + corretude diferencial); aposenta C. **Aceite (futuro):** ger.2==ger.3 bit-a-bit.
- **[C5] Capabilities/sandboxing** — deps: C1.1 · M.1 · **evolução** — auditoria de superfície `exp`/`extern`/syscall.

### Materialização dos pares C-only (auditoria) — alimenta C4
- **[M1] `codegen.tks`** — deps: — · M.3 · par: `src/codegen/codegen.tks`
  > Original Teko do backend (escrevível já, sem stdlib nova). **Aceite:** espelha `codegen_c.c` 1:1.
- **[M2] `driver.tks`** — deps: A5, C2* · M.4 · **bloqueado** em C2 · par: `src/driver.tks`
  > Original Teko do driver (precisa de `fs`/`process`/`io`/`env`/`exit`). **Aceite:** espelha o driver.

### Cross-ref (BINARY)
- **[F3-pânicos]** — deps: — · M.1 · par: `src/codegen` + `runtime/teko_rt`
  > Guards ÷0 (`tk_panic_div0`), conversão impossível (`tk_panic_cast`), overflow (panic-debug). Ortogonal ao Eixo A → **pronto**.

### ▶ Prontos agora (deps satisfeitas): **A1 · B1 · C1.0 · M1 · F3-pânicos**.
