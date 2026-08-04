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
2. **Menos superfície nativa = menos superfície para o wild-write.** O bug de wild-write do
   fixpoint nativo gen2 (bissetado por `TEKO_NATIVE_CHUNK_CANARY`, commits `da8bfb2f`/`c2ee1091`)
   vive no código nativo *exercido* durante o self-host. `host-only` remove ~⅓ desse código
   exercido de cada perna — tornando *inalcançável* qualquer stop que hoje vem de baixar código de
   um target que aquela perna nem emite (§2.3). É a mesma filosofia "tornar o defeito inalcançável
   é melhor que corrigi-lo" já na árvore.
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

### 2.3 Efeito no wild-write (menos superfície nativa exercida)

O wild-write do fixpoint gen2 (bissetado por `TEKO_NATIVE_CHUNK_CANARY` via snapshots de
chunk-header, commits `da8bfb2f`/`c2ee1091`) é um bug do **código nativo exercido durante o
self-host**. Dois efeitos de `host-only`:

- **Reduz a superfície onde o bug pode disparar.** ~⅓ menos código nativo é baixado por perna →
  ~⅓ menos alocações de arena/estruturas de codegen onde um wild-write pode nascer ou aterrar.
- **Torna inalcançável uma classe inteira de stops.** Um tropeço ao baixar `append_minst_x86`
  (`minst_x86.tks:1219`, código 100 % x86_64) hoje pode travar o self-host **arm64**, sem relação
  com arm64 (`backend-feature-gating-0.3.1.md` §0). Sob `host-only` esse ficheiro nem está no
  programa comparado — o stop deixa de existir, em vez de precisar ser corrigido. Mesma filosofia
  "tornar o defeito inalcançável é melhor que corrigi-lo" (`teko-laws-digest.md`).

Isto não *conserta* o wild-write — reduz a probabilidade e a área de busca, e destrava pernas que
hoje ficam reféns de bugs de um backend que nem lhes compete.

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

## 6. Sumário de uma linha

Manter o cross como capacidade, mas **flipar o default para target-único-por-OS** (`host-only`)
colhe todo o ganho de performance que o dono descreveu (~⅓ do backend fora do caminho, menos
superfície para o wild-write, self-host mais rápido) **sem perder nada** — porque o release já é
native-per-runner e o cross ratificado continua a um opt-in de distância.
