---
section: design
created: 2026-08-04
status: AVALIAÇÃO — arquiteto, encomendada pelo dono ("tirando o cross, dá pra melhorar a
  performance de compilação, ao menos por sistema operacional"). Doc-only; nenhuma linha de
  produto tocada. Teko-only preservado. Escrito para o DONO decidir.
branch: worktree-agent (sem PR)
lê / cita:
  - docs/design/backend-feature-gating-0.3.1.md (origin/cargo/0.3.1.0-backend-feature-gating)
  - docs/design/teko-target-crosslink-0.3.1.md (decisões R1–R5 CLOSED 2026-07-24)
  - src/build/project.tks (NativeTarget:1576, emit_native:2012, native_target:1683, prune_os:116)
  - src/build/discover.tks (dsc_walk — o gancho de descoberta)
  - src/backend/*.tks (27149 linhas), .github/workflows/pr.yml (matriz de produtores)
  - docs/design/tempo-de-build-paridade-por-host.md
---

# Cross-compile vs. target único por OS (0.3.1)

## 0. Recomendação (TL;DR para o dono)

**Não remova o cross. Adote a OPÇÃO DO MEIO: cross disponível mas NÃO default.** Cada OS/host
passa a compilar por omissão só o seu próprio target nativo (perfil `host-only`); o cross
(`teko build --target=<outro>`) fica preservado como opt-in. Isto entrega o ganho de performance
que o dono pediu — **por sistema operacional, exatamente como pedido** — SEM perder o release
multiplataforma, porque o release **já builda cada target no seu próprio runner nativo** (a CI
nunca cross-compila os quatro targets de um runner só).

Os quatro argumentos-chave:

1. **O ganho é real e mensurável, e é ~⅓ do backend.** Um build `host-only` deixa de
   parsear/tipar/baixar entre **8,3k e 9,6k linhas** de código de backend de outros targets — os
   ficheiros mais densos da árvore (encoders/isel). É **31–35 % das 27 149 linhas de `src/backend/`**
   e **~7–9 % das 111 854 linhas do compilador inteiro**, evitadas *no pipeline todo*, não só na
   emissão (§2).
2. **O wild-write NÃO é resolvido por particionamento de target — nem pelo gate, nem pela
   decomposição.** [CORREÇÃO de uma afirmação exagerada da 1ª versão deste doc, após o dono
   apontar, com razão, que era rasa.] O wild-write crasha **dentro de `tk_region_alloc`**
   (`src/runtime/teko_rt.c` + `src/mem/unsafe/arena.tks`) — o alocador de arena PARTILHADO,
   exercido por TODA baixa nativa, seja qual for o target. As repros crasham em itens DIFERENTES
   (2453, virtual-main, 770), inclusive na "última baixa nativa — a emissão ELF do próprio
   virtual-main" (`elf_collect_const_entries`), que é a emissão do target DO HOST. Ou seja: o bug
   vive no NÚCLEO e dispara ao emitir o HOST. Tirar código de OUTROS targets não toca a máquina que
   escreve fora dos limites. A §7.5 prova isto na fonte; **nenhum esquema de partição de target
   conserta este bug — ele precisa de fix próprio no `tk_region_alloc`.**
3. **Remover o cross de vez NÃO é preciso para colher o ganho — e custaria caro.** O ganho vem de
   *não incluir* os outros backends no build por omissão; vem de graça no `host-only`. Apagar o
   cross a mais destruiria capacidade já **legislada e ratificada** (`teko-target-crosslink-0.3.1.md`,
   R1–R5 CLOSED 2026-07-24: emit-not-run, `[extern.libs.<os-arch>]`, `--allow-undef`) e o desenho
   do `teko build --target=` — trabalho fechado, sem ganho de perf adicional por removê-lo (§3).
4. **A CI já É "target por OS".** `pr.yml` roda cada produtor no seu runner nativo
   (`ubuntu-latest`, `ubuntu-24.04-arm`, `macos-latest`, `windows-latest`) e roda o fixpoint em
   cada um. Nada no caminho de release depende de cross-compilar. O default `host-only` só alinha o
   **default do compilador** com o que a CI já faz na prática (§4).

O mecanismo já está desenhado: `docs/design/backend-feature-gating-0.3.1.md`. Este documento é a
**avaliação de política** (manter vs. remover vs. gate) que decide se aquele mecanismo é o caminho.
Recomenda: **implemente aquele desenho, faça `host-only` o default de self-host/CI/dev, e mantenha
`cross` como opt-in.**

**A pergunta funda do dono — decompor cada ISA+OS num PROJETO teko à parte (dep `.tkl`)** — é
respondida na fonte na §6. Resumo: (a) SIM, hoje o self-host monomorfiza/baixa TODOS os backends,
porque o compilador é um projeto monolítico e `discover` não filtra por target (§6.1); (b) a
decomposição é viável em princípio (o `.tkl`/`.tkb` existe) mas exige package-manager PK0–PK3 +
uma interface de backend + história de bootstrap que NÃO existem (§6.3); (c) o ganho de compilação
da decomposição é **IGUAL** ao do gate — os dois deixam um backend no grafo, e o `.tkb` ainda
re-monomorfiza contra uso, então decompor não baixa menos, só é arquitetura mais limpa por muito
mais trabalho (§6.4); (d) **nenhum dos dois toca o wild-write**, que vive na arena partilhada
(`tk_region_alloc`) e dispara na emissão do host (§6.5).

---

## 1. Estado atual do cross

### 1.1 Como o backend suporta múltiplos targets

`NativeTarget` (`src/build/project.tks:1576`) tem hoje as variantes:

```
enum { Arm64Macho; X8664Linux; X8664Windows; Wasm32Wasi; Wasm64Wasi; Wasm32Browser }
```

O dispatch é um `match` exaustivo em `emit_native` (`project.tks:2012–2024`): cada variante seleciona
uma "cauda por-ISA" (isel → regalloc → encode → objfile). O `lir::lower_program` (`:2015`) produz um
`LModule` **independente de target**; cross vs. host diferem SÓ em qual cauda corre e qual driver de
link é escolhido (confirmado em `teko-target-crosslink-0.3.1.md` §3: "nada além de objfile/ABI
selection"). Não há suposição de arch-do-host a montante do dispatch.

Os módulos por target, medidos (linhas `.tks`, sem testes):

| Grupo (target-específico) | Ficheiros | Linhas |
|---|---|---|
| x86-64 / ELF / SysV | `isel_x86_64`, `encode_x86_64`(+`_consts`), `minst_x86`, `regalloc_x86`, `objfile_elf`, `objfile_ar`, `abi_sysv64` | **7 603** |
| arm64 / Mach-O / AAPCS64 | `isel_arm64`, `encode_arm64`(+`_consts`), `abi_aapcs64`, `objfile_macho`, `objfile_ar_macho` | **6 291** |
| Windows / COFF | `objfile_coff`, `objfile_ar_coff`, `abi_win64` | **1 188** |
| WASM | `objfile_wasm` | **826** |
| **Target-agnóstico** (partilhado) | `minst`, `regalloc`, `dwarf`, `stackify`, os `minst_interp`, comuns | resto |
| **`src/backend/` total** | | **27 149** |

Os ficheiros target-específicos são desproporcionalmente os **maiores e mais densos** da árvore:
`encode_x86_64` (1 945), `isel_arm64` (1 793), `encode_arm64` (2 187), `isel_x86_64` (1 250). O
custo de os parsear/tipar/baixar não é marginal — é o custo que importa.

### 1.2 Como o target é escolhido

`native_target()` (`project.tks:1683`): `TEKO_TARGET` unset ⇒ default do host (R1); set ⇒
`target_from_name` (R2, erro honesto em target não-suportado). Cross (`target ≠ host`) emite
normalmente mas **não corre** no host (emit-not-run, R3). O manifesto suporta
`[extern.libs.<os-arch>]` para libs por-target (R4/R5). Todas essas decisões estão **CLOSED**
(dono, 2026-07-24) e implementadas (crumbs C1–C6, feat/0.3.1-target-crosslink). O cross é, hoje,
uma capacidade real e funcional do binário.

### 1.3 O que a CI builda — e como

`pr.yml` (matriz `matrix_artifact`, `scripts/ci_producer_matrix.sh`) declara seis produtores:
`linux-x86_64-glibc`, `linux-x86_64-musl` (ambos `ubuntu-latest`), `linux-arm64-glibc`,
`linux-arm64-musl` (ambos `ubuntu-24.04-arm`), `macos-arm64` (`macos-latest`),
`windows-x86_64` (`windows-latest`). **Ponto central (`pr.yml:401–420`, ruling do dono
2026-07-27):** o fixpoint corre *em cada ambiente*, no runner nativo daquele target —
"pode colocar o fixpoint logo após as compilações nos ambientes". Cada asset de release é buildado
"com a toolchain nativa do próprio target" (`pr.yml:382`, sem container). **Ou seja: o release e o
gate JÁ são native-per-runner. Nenhum runner cross-compila os outros targets.** O cross existe no
binário mas o pipeline de release não o usa.

(Nota lateral, reportada para cima, não uma issue: `NativeTarget` ainda não tem variante
`Arm64Linux`/ELF — o `aarch64-elf-0.3.1.md` está em desenho. A perna linux-arm64 da CI hoje não tem
cauda nativa arm64-ELF; o conjunto de targets ainda está a CRESCER, o que reforça a tese de gate:
cada target novo é mais superfície cross que um build single-target não devia carregar.)

---

## 2. O ganho de performance de tirar o cross do caminho default

### 2.1 Quanto código sai de cada build host-only

O ganho NÃO vem de "remover cross" — vem de **não incluir os backends de outros targets** no build
por omissão. Por host:

- **Host x86_64-linux** (default). Sai: grupo arm64 (6 291) + Windows/COFF (1 188) + WASM (826)
  = **~8 305 linhas**. Fica: grupo x86 + agnóstico.
  → **~31 % de `src/backend/`, ~7,4 % do compilador inteiro**, evitadas.
- **Host arm64-macos** (default, a perna do FIXPOINT). Sai: grupo x86/ELF (7 603) + Windows/COFF
  (1 188) + WASM (826) = **~9 617 linhas**.
  → **~35 % de `src/backend/`, ~8,6 % do compilador**, evitadas.

Estas linhas são evitadas **no pipeline inteiro** — nunca lidas, nunca lexadas, nunca parseadas,
nunca tipadas, nunca baixadas — porque o corte acontece na descoberta de ficheiros
(`discover.tks::dsc_walk`), ANTES do lexer tocar o ficheiro (mecanismo em
`backend-feature-gating-0.3.1.md` §2.3). Contraste com `prune_os` (`project.tks:116`), que só poda
DEPOIS do parse, por item — insuficiente para "este ficheiro inteiro é de outro alvo".

### 2.2 Efeito no self-host / fixpoint nativo (o cenário caro)

O fixpoint nativo (`gen2 == gen3`) compila **o compilador inteiro pelo backend nativo**. Cada
função de `src/backend/*` é baixada como código nativo. Hoje, compilar o compilador para arm64-macOS
baixa `isel_x86_64`/`encode_x86_64`/`objfile_elf`/`objfile_coff` — ~9,6k linhas — que o binário
resultante **nunca vai emitir naquela corrida**. Menos targets = menos itens a lowerar = **build de
self-host mais rápida** e menos memória (o backend "rebaixa TUDO isso dentro de si": mais unidades
de tradução, mais estruturas de isel/regalloc em memória). O efeito no tempo de codegen/lowering é
proporcional às linhas removidas — de primeira ordem, ~⅓ do backend fora do caminho.

### 2.3 Efeito no wild-write — remetido para a §7.5 (o dono estava certo)

A 1ª versão deste doc alegava aqui que `host-only` "reduz a superfície do wild-write". **Isso era
raso e o dono apontou com razão.** A análise na fonte (§7.5) mostra que o wild-write vive no
alocador de arena PARTILHADO (`tk_region_alloc`), disparado ao emitir o target DO HOST — logo
**nem o gate nem a decomposição por-projeto o tocam**. O único efeito de segunda ordem de reduzir
input é mudar o NÚMERO de alocações, o que pode deslocar ONDE o crash aflora — mas ele já aflora na
emissão do host, então não fica inalcançável. Ver §7.5 para a prova completa. Este documento
**não** conta o wild-write como benefício de nenhuma opção de partição.

### 2.4 Tamanho do binário

Secundário, mas real: um `teko` `host-only` não carrega os símbolos dos backends de outros targets.
Não é o motivador (o dono pediu tempo de compilação), mas acompanha de graça.

---

## 3. O que se perde tirando o cross (e por que NÃO se deve removê-lo, só gateá-lo)

Se o cross fosse **removido** (não só posto off-default):

- **`teko build --target=<outro>`** — buildar artefactos de outra plataforma de um runner só.
  Hoje o release não usa isto (§1.3), mas é uma promessa de produto legislada (R3) e usada por
  utilizadores que querem cross-compilar da sua máquina.
- **Testar win/mac a partir de um linux** por emit-not-run + `Then object well-formed`
  (`check_object_wellformed`, regressor T5). Perde-se a verificação de formato cross sem um runner
  daquele OS.
- **Trabalho ratificado desperdiçado.** R1–R5, `[extern.libs.<os-arch>]`, `--allow-undef`, a tabela
  `supported_targets`/`TargetRow` — tudo CLOSED e implementado (C1–C6). Remover viola a postura
  "issues são 100 %, sem regressões" e joga fora decisão do dono de 2026-07-24.
- **Ganho de perf adicional: ZERO.** O custo que dói é o de *incluir/baixar* os backends. `host-only`
  já não os inclui. Remover o cross por cima disso não acelera mais nada — só apaga capacidade.

O que NÃO se perde em nenhum cenário:

- **A matriz da theory** (`theory-generation-decay.yml`): duas pernas do MESMO target não é cross.
  Intocada.
- **O degrau/seed.** O seed é **por-plataforma de qualquer forma** (`seed-linux-fork.yml`, seed no
  runner nativo); a cadeia de bootstrap já é `host-only` por natureza — uma corrida num runner
  arm64-macOS nunca precisa emitir ELF x86_64 (`backend-feature-gating-0.3.1.md` §5.1). O perfil de
  backend **não muda o critério do degrau nem o formato de `bootstrap/DEGRAU`**.

Conclusão: os custos de remover o cross são todos evitáveis mantendo-o como opt-in. Não há razão de
performance para o remover — só para o **gatear off-default**.

---

## 4. A opção do meio: "target por OS" (RECOMENDADA)

Cross disponível mas NÃO default. Cada OS builda o seu próprio target nativo por omissão; a matriz
de release usa runners nativos por-OS (o que a CI **já faz**). Mecanismo (já desenhado, sem código
ainda, em `backend-feature-gating-0.3.1.md`):

- **Dois perfis:** `host-only` (só os módulos de backend do alvo do host) e `cross` (todos). Omisso
  hoje = `cross`; a proposta é flipar o default de self-host/CI/dev para `host-only`.
- **Seleção fora da gramática:** uma tabela de dados `src/backend/BACKEND_TARGETS` (uma linha por
  ficheiro → targets que o usam; ausente = "shared", entra sempre — falha para DENTRO, M.1) lida no
  gancho `discover.tks::dsc_walk` antes do lexer. Um lever `TEKO_BACKEND_PROFILE=host-only|cross`
  (+ `--backend-profile`), com o escopo apertado que `TEKO_FIXPOINT_BACKEND` já disciplina.
- **Dispatch inalterado:** `emit_native`/`NativeTarget` não mudam de texto. Cada `emit_native_*`
  ganha um par real/stub de ficheiro; exatamente um entra no conjunto descoberto por perfil×host; o
  stub responde com honest-stop ("rebuild com `--backend-profile=cross`"), nunca link quebrado.
- **Fixpoint preservado:** `gen2` e `gen3` buildados com o MESMO perfil — a igualdade byte-a-byte
  compara o mesmo programa duas vezes; o perfil é propriedade de QUAL programa, não uma variável que
  difira entre os dois lados (`backend-feature-gating-0.3.1.md` §4).

**Por que é a opção do meio e não "single target only":** o binário `cross` continua a existir
(mesmo código-fonte, perfil diferente) para quem quiser `--target=<outro>`; o release por-runner
pode até usar `host-only` (cada runner builda o seu — que é o que já faz), ou publicar um
`teko-cross` separado. O ganho de perf vem do default; a capacidade cross fica intacta atrás de um
opt-in.

---

## 5. Recomendação, trade-offs e caminho de migração

### 5.1 Recomendação

**Gate, não remova.** Implemente `backend-feature-gating-0.3.1.md`; faça `host-only` o default de
self-host, CI e dev; mantenha `cross` como opt-in (`--backend-profile=cross`/`TEKO_BACKEND_PROFILE`).
Release fica native-per-runner (já é), cada runner podendo buildar `host-only`.

### 5.2 Trade-offs

| | Manter cross default (hoje) | **Gate (host-only default) — RECOMENDADO** | Remover cross de vez |
|---|---|---|---|
| Tempo de build self-host | baseline | **~⅓ menos backend baixado por perna** | igual ao gate (sem ganho extra) |
| Superfície do wild-write | baseline | **~⅓ menos código nativo exercido** | igual ao gate |
| `teko build --target=` | sim | sim (opt-in) | **perdido** |
| Release multiplataforma | sim (runners nativos) | sim (runners nativos, inalterado) | sim (runners nativos) |
| Trabalho R1–R5 ratificado | usado | **preservado** | **desperdiçado** |
| Risco de regressão | — | baixo (corte na descoberta; stub honesto; fixpoint por-perfil provado) | alto (apaga capacidade legislada) |

### 5.3 Caminho de migração (se aprovado)

1. **Mecanismo primeiro** (vagão isolado): `BACKEND_TARGETS` + gancho em `dsc_walk` + par real/stub
   por `emit_native_*` + `TEKO_BACKEND_PROFILE`/`--backend-profile`. Default permanece `cross` neste
   vagão (zero mudança de comportamento) — só o mecanismo entra, gate-able sozinho.
2. **Flip do gate nativo** (vagão seguinte): CI/fixpoint passam a fixar `host-only` em volta das duas
   builds gen2/gen3, exatamente como já fixam `TEKO_FIXPOINT_BACKEND`. Aqui aterra o ganho de perf e
   a redução de superfície do wild-write.
3. **Release:** decidir (decisão do dono §6) se o asset publicado é `host-only` (menor, cross via
   distribuição `teko-cross` separada) ou `cross` (comportamento de hoje). Ortogonal ao ganho — o
   ganho já foi colhido no passo 2.

Sequência de seed: o mecanismo é build-system puro (tabela + driver), não usa feature de linguagem
nova — não força bump de seed. Seguro.

### 5.4 Decisões que ficam para o dono (herdadas de `backend-feature-gating-0.3.1.md` §6)

Estas seis já estão levantadas naquele doc e permanecem as decisões abertas; esta avaliação só
recomenda a POSTURA (gate, não remover). Resumo do que o dono ainda ratifica: (1) dois perfis vs. um
terceiro `custom`; (2) release `host-only` menor + `teko-cross` separado, ou release `cross`; (3) CI
liga `host-only` já; (4) tabela em `src/backend/BACKEND_TARGETS` (recomendado) vs. secção do
manifesto; (5) nome `TEKO_BACKEND_PROFILE`/`--backend-profile`; (6) semântica de cobertura sob
`host-only`.

### 5.5 Tensão de leis

Nenhuma tensão genuína não-resolvida. O gate respeita M.1 (regra de omissão falha para dentro), M.3
(stub honesto, não fingido) e a lei Teko-only (mecanismo é dados + driver, zero C novo). A postura
"gate, não remover" é a única que respeita simultaneamente o pedido de perf do dono E a postura
"issues são 100 %, sem regressões" (o cross ratificado 2026-07-24 não regride). **Não HALTa** — a
decisão de política é clara law-first; as seis ratificações de forma (§5.4) são do dono por serem
escolhas de produto/nomenclatura, não tensões de lei.

---

## 6. Decomposição por-projeto (a pergunta funda do dono)

> O dono achou a 1ª versão rasa e tem razão: "gate a descoberta pula o parse mas não muda a
> monomorfização/lowering do NÚCLEO nem toca o backend do HOST (onde o wild-write vive)". Esta
> secção responde na fonte, com análise ESTÁTICA (nenhum build/teste corrido).

### 6.1 O self-host monomorfiza/lowera TODOS os backends? — SIM. Prova na fonte.

O compilador é **um único projeto monolítico**: `teko.tkp` declara `name = "teko"`,
`source = "src"`, e `[aliases]` diz literalmente *"none yet — the seed has no external
dependencies"*. Não há fronteira de projeto entre os backends hoje.

1. **Descoberta cega ao target.** `discover.tks::dsc_walk` caminha `src/` inteiro e coleta TODO
   `.tks`/`.tkt` num `[]SourceFile`, sem noção de target. O único filtro, `prune_os`
   (`project.tks:116`), é **por-item** (`os_guard`, ex.: `host_write` vs `_write`), pós-parse — NÃO
   por-ficheiro-de-target. Logo `isel_x86_64`, `isel_arm64`, `encode_x86_64`, `encode_arm64`,
   `objfile_elf/macho/coff` — TODOS — entram no `TProgram`.
2. **Monomorfização dirigida por USO.** `monomorph.tks` estampa uma cópia concreta por
   instanciação distinta de fn genérica, e um programa **sem genéricos passa BYTE-IDÊNTICO** (guard
   no-op). Os backends são majoritariamente funções CONCRETAS (`append_minst_x86` etc.) MAIS
   genéricos partilhados (coleções/slices) instanciados sobre os tipos concretos de cada target
   (`MInstX86`, `MInstArm64`, …). Portanto o self-host monomorfiza `push<MInstX86>`,
   `push<MInstArm64>`, slices sobre ambos, etc. — as instâncias por-target dos genéricos do núcleo.
3. **O LModule é target-agnóstico — a baixa é feita UMA vez.** `emit_native` faz
   `lower_program(prog)` produzindo UM `LModule` (LIR independente de target, confirmado em
   `teko-target-crosslink-0.3.1.md` §3) ANTES do `match target`. Mas durante o self-host o `LModule`
   **do próprio compilador** contém a LIR de TODAS as funções de backend — o backend do host então
   faz isel/encode/objfile desse módulo ~⅓ maior.

**Conclusão (a):** SIM. Hoje o self-host baixa as ~27 149 linhas de `src/backend/` — incluindo
~8,3k–9,6k de targets que o binário resultante nem emite naquela corrida — através do backend do
host. Esse é o peso real que o dono nomeou, e é ANTERIOR e maior que o front-half de compilar um
programa de usuário.

### 6.2 A monomorfização carrega especialização por-target que a decomposição cortaria?

Não há monomorfização por-target ESCONDIDA no núcleo que só a decomposição alcançaria. A
especialização que existe é: genéricos partilhados instanciados sobre os tipos concretos de cada
target, e essas instâncias nascem dos **call-sites nos ficheiros do target**. Remover os ficheiros
de um target — pelo gate OU pela decomposição — remove exatamente esses call-sites e, com eles,
essas instâncias. É o MESMO conjunto removido pelos dois mecanismos. O núcleo não tem um eixo de
especialização por-target que sobreviva ao gate mas caia só na decomposição.

### 6.3 A decomposição é viável no sistema de deps do teko? Como — e o que falta.

O mecanismo de dependência EXISTE, e é de um tipo específico que muda a análise:

- **A unidade `.tkl`** = ZIP(`.tkh` + `.tkb` + `.tsym`). O `.tkb` é **AST TIPADA** (pós-checker,
  genéricos INTACTOS), **não** é código de máquina e **não** linka (`package-manager.md` §1,
  decisões CLOSED 2026-07-11).
- **Modelo de consumo = crate-Rust:** o consumidor **monomorfiza o `.tkb` contra o uso real**
  (M.5, por instanciação de fato usada) → nativo → cache por `(AST-hash, instantiation-set,
  target)`. `load_deps_program`/`load_dep_program` (`project.tks:134–238`) já carregam
  `packages/<dep>-*.tkl` e pré-anexam os itens tipados antes do checking; o harness de regressão
  (`regression.tks:1508`) já compila um dep para `.tkl` e consome — **consumidor-compila-dep
  FUNCIONA hoje**.

Mas transformar os backends do compilador em deps `.tkl` exige três coisas que NÃO existem:

1. **Package-manager PK0–PK3** (`{ path = … }`/`git` deps, resolver, versão, lockfile) — hoje
   DESIGN-only (`package-manager.md` é doc-only). O manifesto-raiz tem ZERO deps.
2. **Uma interface de backend.** O dispatch é um `match` MONOLÍTICO (`project.tks:3042`) chamando
   `emit_native_x86`/`emit_native_arm64`/`emit_native_win` definidas no mesmo `project.tks`. Para o
   backend virar dep, esse `match` tem de virar uma INTERFACE que o dep-backend implementa (uma
   costura de plugin), não um `match` fechado no núcleo.
3. **Uma história de bootstrap para a galinha-e-ovo.** Um backend entregue como `.tkb` (AST tipada)
   precisa ser TIPADO/baixado PELO compilador; mas o compilador precisa de um backend para se
   compilar. O backend-do-host tem de estar disponível ao seed/cadeia-de-gerações ANTES de poder
   ser tratado como "dep". Isso é uma complicação de ordem-de-build real (M.4), não um detalhe.

**Conclusão (b):** VIÁVEL em princípio (o `.tkl`/`.tkb` existe e é exercido), mas é um programa
GRANDE — PK0–PK3 + interface de backend + história de bootstrap — muito mais fundo que o gate.

### 6.4 O ganho REAL da decomposição vs. o gate raso — sê honesto.

Este é o ponto onde a intuição do dono ("decomposição tira do grafo inteiro ⇒ ganho maior") precisa
de ser testada na fonte, e a resposta honesta é: **para o self-host/fixpoint, o ganho é O MESMO.**

- O gate DESENHADO corta em `discover.tks::dsc_walk` **antes do lexer** — o ficheiro não-host nunca
  é lido, lexado, parseado, tipado, monomorfizado NEM baixado. A decomposição tira o ficheiro do
  grafo de deps. **Os dois deixam EXATAMENTE UM backend no grafo compilado.** O efeito na
  monomorfização/lowering é idêntico.
- E o `.tkb` **não** é código de máquina: o consumidor **re-monomorfiza e re-baixa** o dep contra o
  uso. Então um backend decomposto em `.tkl` custa o MESMO para baixar que um backend in-tree — não
  há economia de baixa por ele ser um dep. A intuição de que "tirar do grafo" poupa mais só vale
  contra um gate INGÊNUO que cortasse DEPOIS do parse; o gate desenhado corta antes, e iguala.
- **Onde a decomposição PODERIA ganhar (honestamente):** o **cache** de build do `.tkl`
  (`(AST-hash, instantiation-set, target)`) faz um HIT saltar a re-baixa do backend em builds
  REPETIDAS. Mas (i) o self-host do fixpoint tem de compilar **da fonte** para ser um fixpoint
  válido (`gen2 == gen3`) — cache o mina ou é neutro; e (ii) o cache é uma facilidade GERAL, não
  um benefício exclusivo da decomposição.

**Conclusão (c):** ranking de ganho de COMPILAÇÃO: **gate ≈ decomposição ≫ hoje.** A decomposição
NÃO é "muito maior" que o gate para o peso do self-host — ela é MAIS TRABALHO pela MESMA perf,
comprando ARQUITETURA (fronteira de módulo dura — `append_minst_x86` fica fisicamente
inalcançável a partir de código arm64; teste/versão por-backend; os backends cross distribuíveis
como `.tkl` separados — a história `teko-cross`; e a costura de plugin substituindo o `match`
monolítico), não velocidade extra.

### 6.5 O wild-write: nenhum dos dois toca o bug. Prova na fonte.

O wild-write crasha **dentro de `tk_region_alloc`** — o alocador de arena/região em
`src/runtime/teko_rt.c` (+ gêmeo `.tks` + `src/mem/unsafe/arena.tks`), PARTILHADO por toda baixa
nativa. Evidência dos commits do canário:

- `620fe1c9`: *"the wild write clobbered the HANDLE — `r->head` itself, or whatever upstream
  variable/slot fed it — not necessarily a chunk's own cap/next fields"*; três repros crasharam
  dentro de `tk_region_alloc` em **itens diferentes (2453, virtual-main, 770)** com zero trips.
- `3f887208`: *"a full native self-build run reached the very LAST native-lowering item
  (virtual-main's own final ELF emission, `elf_collect_const_entries`)"* — a emissão **ELF**, ou
  seja, o target DO HOST num runner linux.

Portanto o bug (i) vive no NÚCLEO partilhado (a arena + a máquina de baixa nativa que o backend do
HOST dirige), e (ii) DISPARA ao emitir o target do host. Remover os ficheiros de outros targets não
remove a máquina que escreve fora dos limites, nem o caminho de emissão-do-host onde ele reproduz.
Reduzir input muda o NÚMERO de alocações, o que pode deslocar ONDE o crash aflora — mas ele já
aflora na emissão do host, então **não fica inalcançável**.

**Conclusão (d):** NÃO. Nem o gate nem a decomposição tocam o wild-write. Ambos só reduzem a
superfície de OUTROS targets, enquanto o backend do host (buggy) continua idêntico e continua a
disparar o bug ao emitir o host. O wild-write é um defeito SEPARADO — corrupção de handle de arena
no escritor upstream de `tk_region_alloc` — e tem de ser corrigido por seus próprios méritos. Não
deixar o particionamento de target ser vendido como remédio do wild-write: **não é.**

### 6.6 O que isto muda na recomendação

- Para a PERF que o dono pediu: **o gate basta e é o menor caminho** (uma tabela + um gancho em
  `discover`) — não precisa de PK0–PK3, nem de interface de plugin, nem de história de bootstrap. A
  decomposição entrega a MESMA perf por muito mais trabalho.
- A **decomposição é o alvo de ARQUITETURA certo** (fronteiras duras, backends cross distribuíveis,
  costura de plugin) — persiga-a como objetivo de arquitetura/produto, **não** como a alavanca de
  perf, e só depois de PK0–PK3 existirem.
- O **wild-write é ortogonal** aos dois e precisa de fix próprio no caminho de arena/`tk_region_alloc`.
  Trate-o em paralelo; não bloqueie nem justifique a decisão de target por ele.

---

## 7. Sumário de uma linha

O ganho de compilação que o dono quer é REAL (~⅓ do backend — 8,3k–9,6k linhas — fora da
monomorfização/baixa/self-host) e é entregue **igualmente** pelo gate raso OU pela decomposição
por-projeto; a decomposição é arquitetura mais limpa mas **não** dá ganho de perf maior (§7.4), e
**nenhuma das duas conserta o wild-write**, que é um bug do alocador de arena partilhado exercido
na emissão do host (§7.5) e precisa de fix próprio. Recomendação: **gate agora pela perf,
decomposição depois pela arquitetura, wild-write em paralelo como bug independente.**
