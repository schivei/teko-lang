# Teko — Decision Log

Registro de decisões tomadas de forma autônoma durante a execução do backlog, para
**revisão posterior do dono** (diretriz 2026-07-05: aplicar o recomendado sem travar o
fluxo; registrar aqui para revisar de uma vez no gate **LTS = v1.0.0.0**).

Cada entrada traz: a **decisão aplicada**, as **alternativas** preteridas (com motivo), a
**base** (constituição / lei / diretriz — lembrando que estamos na **fase de EVOLUÇÃO**,
pós-reboot) e a **reversibilidade**. Decisões marcadas **⚠️ PENDENTE** aguardam a revisão
do dono especificamente.

Constituição: Laws M.0–M.5 (ver `TEKO_MASTER_PLAN.md` / memória `project-structure`).
Lei suprema operacional: **main-integrity** — nunca mergear para main algo potencialmente
corrompido; só CLEAN.

---

## 2026-07-05 — Redesign do release + reorganização do CI (#249, PR #277)

### D1 · Ferramenta de cross-compile Linux no release → **`zig cc`** ✅
- **Aplicada:** um runner emite o `teko.c` uma vez e `zig cc -target <triple>` cross-compila
  os artefatos Linux (glibc dinâmico + musl estático × x86_64/arm64).
- **Alternativas:** (a) runner nativo por arquitetura + `musl-tools`/cross-gcc — musl para
  arm64 é frágil; (b) qemu por-arch — lento e mais
  peças; (c) imagens docker cross — mais dependências externas. `zig` unifica tudo com uma
  ferramenta e sem emulação.
- **Base:** economia + robustez; sem violar nenhuma lei (o binário é o mesmo C determinístico
  que o CI já provou). **Validado localmente** (zig 0.16 + docker): os artefatos compilam com a
  arquitetura correta; arm64 musl/glibc rodam `teko --version`.
- **Reversível:** sim — trocar o job `build-linux` por runners nativos é local ao release.yml.

### D2 · Release NÃO re-valida (1 geração, `--no-verify`) ✅
- **Aplicada:** o release builda uma geração e publica; sem gate `.tkt`, sem gen-2/gen-3,
  sem fixpoint, sem diff motor legado==nativo.
- **Alternativa:** manter a cadeia gen-1→2→3 + asserts por plataforma (desenho antigo).
- **Base:** **ruling do dono** "o CI é quem garante, o release não assera"; o fixpoint e o
  gate já rodam no CI do commit. Codegen determinístico ⇒ 1 geração É o artefato-fixpoint.
- **Reversível:** sim.

### D3 · CI roda **só em `pull_request`** (não em `push: main`) ✅
- **Aplicada:** `native`/`sast`/`sanitizers`/`codeql` perderam o trigger `push: main`.
- **Alternativa:** (a) manter CI no push da main (custo dobrado por merge); (b) merge-queue
  (mais robusto contra merge-skew, porém mais complexo — adiado).
- **Base:** **ruling do dono** "se o merge veio de um PR com CI 100%, não re-rodar o CI, só
  o build release". A proteção de branch já exige os checks verdes p/ mergear ⇒ todo commit
  na main já passou 100%.
- **⚠️ Risco registrado (merge-skew):** dois PRs verdes isoladamente, mergeados em sequência,
  podem combinar em uma main nunca testada junta. **Mitigação recomendada ao dono:** ligar
  "Require branches to be up to date before merging" no ruleset (re-roda o CI no PR rebaseado).
- **Reversível:** sim (readicionar `push:`).

### D4 · Guard do release = confiar na proteção de branch (removido o poll ci-green) ✅
- **Aplicada:** como o CI não roda mais no push da main, o release não tem run de CI na main
  para consultar; ele confia que "estar na main" ⇒ veio de PR verde.
- **Alternativa:** job `ci-green` que faz poll dos runs de CI do commit (desenho intermediário
  desta mesma sessão) — inútil sob D3 (não haveria runs na main para consultar).
- **Base:** consequência direta de D3; a garantia migra para a proteção de branch.
- **Reversível:** sim.

### D6 · Runner do host Linux = `ubuntu-latest` (não `ubuntu-26.04`/kernel-7) ⚠️ PENDENTE
- **Aplicada:** o job zig-host roda em `ubuntu-latest`.
- **Contexto:** o dono pediu **kernel 7 / Ubuntu 26.04 em todo ponto Linux** ("24.04 tem
  falha de segurança no kernel"). Com **zig-cross**, o artefato Linux é **independente do
  kernel do runner** (o zig embute o libc/headers do target), então o binário publicado NÃO
  herda o kernel do host — a preocupação de segurança do 24.04 não alcança o artefato.
- **Alternativa:** pinar `ubuntu-26.04` — **risco:** não confirmei que o label existe como
  runner hospedado; um label inexistente QUEBRA o release (outward-facing). Não arrisquei
  sem confirmar.
- **⚠️ Revisão do dono:** confirmar disponibilidade do runner `ubuntu-26.04`; se existir e
  ainda quiser, é troca de uma linha. (Vale reavaliar também para `native.yml`.)
- **Reversível:** sim (uma linha por job).

### D7 · CodeQL — dashboard de segurança da default branch ⚠️ PENDENTE
- **Aplicada:** ao tirar `push: main` do CodeQL, o dashboard da branch default deixa de
  atualizar por-merge (a análise de PR continua). Nota deixada no `codeql.yml`.
- **Alternativa:** adicionar `schedule:` semanal ao CodeQL para refrescar o dashboard barato.
- **⚠️ Revisão do dono:** quer o `schedule:` semanal? (Trade-off custo × frescor do painel.)
- **Reversível:** sim.

### D8 · Split libc — glibc **dinâmico** / musl **estático**; glibc pinado a **2.28** ✅
- **Aplicada:** artefatos glibc linkam dinâmico (portável para glibc ≥ 2.28); musl linka
  totalmente estático (roda em qualquer lugar, ex.: Alpine).
- **Base:** padrão de mercado; 2.28 (Debian 10) cobre distros correntes sem símbolos faltando
  no runtime (que só usa libc/libm antigos + `__int128` do compilador).
- **Reversível:** sim (parâmetros do target no release.yml).

### D9 · Runtime musl-portável — guardar `execinfo.h`/`backtrace` sob `TK_HAVE_BACKTRACE` ✅
- **Aplicada:** os includes/chamadas de backtrace no `teko_rt.c` passam a exigir
  `TK_HAVE_BACKTRACE` (só `__APPLE__`/`__GLIBC__`). Bug pego na validação local: o 2º include
  (#148 RA2) e as chamadas em `tk_slice_push_fo` não eram guardados → quebravam os 3 musl.
- **Base:** correção de portabilidade necessária para D1; sem mudança de comportamento em
  macOS/glibc/Windows. `dladdr`/dlfcn (existe no musl) fica sob `!_WIN32`.
- **Reversível:** sim, mas reverter re-quebra o musl.

---


Contexto: o release 0.0.1.23 (primeiro de 9 targets) FALHOU no `build-linux` — o zig **0.13.0**
tinha um libc incompleto em seu acervo de targets (faltavam cabeçalhos de compilação).
Causa raiz do meu erro: validei local com zig **0.16.0** mas pinei **0.13.0** no CI (versões
diferentes). O #277 já estava mergeado → correção via novo PR (nunca direto na main).

### D1 (adendo) · Versão do zig = **0.16.0** (a que atende os targets ativos) ✅
- **Aplicada:** `ZIG_VERSION=0.16.0`. **Busca definitiva** (não tentativa-erro): 0.16.0 é o
  stable mais recente com acervo completo de cabeçalhos de compilação para os targets
  (confirmado por análise de tarball; versões anteriores tinham gaps). Compila os artefatos Linux (validado local no 0.16 + download/versão
  conferidos em container linux/amd64). Corrigido também o nome do tarball: 0.14.1+ usa
  `zig-x86_64-linux-<v>` (os/arch trocados vs o formato antigo `zig-linux-x86_64-<v>`).
- **Alternativas:** versões anteriores (acervo incompleto — descartados por evidência).
- **Fallback futuro (diretriz do dono):** se o zig algum dia NÃO atender um target, usar o `cc`
  específico da arquitetura/SO para aquele target (por-arch), mantendo zig para os demais.

### D10 · Smoke de cross-compile no CI de PR (`release-cross-smoke`) ✅
- **Aplicada:** novo job em `native.yml` roda o MESMO `scripts/cross_compile_linux.sh` (extraído
  do release, fonte única — sem drift) em modo `smoke`: emite teko.c e cross-compila os artefatos Linux,
  conferindo a arquitetura de cada um. Bloqueante no `gate`.
- **Por quê:** `release.yml` é disparado por tag, então sua parte Linux não rodava no CI de PR —
  foi por isso que a quebra do 0.13.0 só apareceu PÓS-merge. Agora uma quebra de release
  (versão do zig / target / portabilidade do runtime) gateia o merge. Custa ~1 runner de compile
  por PR relevante; o dono priorizou não publicar release quebrado.
- **Reversível:** sim.

### D11 · Validação por COMPILE + arquitetura, SEM qemu (diretriz do dono) ✅
- **Aplicada:** para validar os binários cross NÃO se roda o binário sob qemu; cross-compile é
  determinístico, então **compilar com sucesso o teko.c (que é o fixpoint byte-idêntico provado
  pelo CI) + conferir a arquitetura via `file`** É o argumento de correção. Removido o smoke por
  qemu que eu vinha usando.
- **Base:** diretriz do dono ("se tem cross-compile, não precisa qemu; verifica por paridade de
  byte") — o C é o mesmo fixpoint; a tradução C→binário por target é determinística.
- **Reversível:** sim (readicionar execução sob qemu se algum dia quisermos runtime-check real).

---

## 2026-07-05 — Fix do miscompile zig + drenagem do backlog validado (#281, #257/#251/#264/#260)

### D12 · Release Linux via zig = `-O0 -fno-sanitize=undefined -DTEKO_VERSION_STRING` ✅
- **Aplicada:** o `cross_compile_linux.sh` casa as flags do build normal do teko (`run_cc`): sem `-O`, define de versão. Bisecção (agente + VPS x86_64 real) provou que o `-O2` do zig explora uma UB no teko.c gerado → miscompila o checker; o compilador está INOCENTE.
- **Alternativas:** manter `-O2` (miscompile); voltar Linux a build nativo por-arch (perde a unificação zig multi-target — descartada por ora).
- **Reversibilidade / follow-up:** re-habilitar `-O1/-O2` exige achar+corrigir a UB → issue **#283**. Por ora `-O0` (como todo release nativo sempre foi).

### D13 · Seed AUTO-CURÁVEL (version-check) ✅
- **Aplicada:** `ci_provision` caminha os releases newest-first e rejeita seed cujo binário reporta versão ≠ tag (o 0.0.1.24 zig reportava `0.0.0.0-dev`) → cai pro próximo bom. Contorna o **release imutável** (não deu pra despublicar o 0.0.1.24 ruim). Validado em x86_64 real.
- **Base:** lei main-integrity (nunca seedar de algo corrompido) + robustez contra recorrência.

### D14 · Smoke de release RODA o artefato (não só compila) ✅
- **Aplicada:** o `release-cross-smoke` executa o binário x86_64-glibc sobre o corpus (nativo ao runner, sem qemu). Um smoke compile-only nunca veria um miscompile. É a regressão que teria pego o bug.

### D15 · Merge-skew da DI: defaultar campos DI nos literais ao re-sincronizar ✅
- **Aplicada:** ao re-sync features onto a main pós-DI, os literais `TypeDecl`/`Function` (que a DI ganhou campos `di_kind`/`has_inject`/…) recebem defaults (`DiKind::None`/`false`/empty). Padrão: sed guardado por `/di_kind/!` só nos que faltam + fechar chaves em concatenações de teste. Semanticamente inerte (codec/testes não usam DI).

### D16 · Gate `teko fmt --check` STAGED (desabilitado até o seed ter o CLI) ✅
- **Aplicada:** o #260 comenta o gate no native.yml — o seed (release anterior) não tinha o CLI `fmt --check`. Liga no PR após o release que o carrega (0.0.1.29) → issue **#282**. Mesmo bootstrap-handling da auto-cura do seed. O `fmt_cli_test.sh` já testa o feature no gen1.

### D17 · `.gitattributes eol=lf` (cross-plataforma) ✅
- **Aplicada:** o Windows fazia checkout dos `.tks` como CRLF → o `fmt` (LF canônico) via o corpus não-idempotente → panic. `* text=auto eol=lf` fixa LF no checkout em toda plataforma. Blobs já eram LF no git; só faltava forçar no working-tree. (CI multi-plataforma pegou o que a validação macOS/Linux não via.)

---

## 2026-07-05 — TR3: traits estruturais Eq/Ord/Hash/Clone/Default sintetizados (#177)

### D18 · `Hashable`≡`Hash` e `Comparable`≡`Ord` como SINÔNIMOS (sem interface paralela) ✅
- **Aplicada:** `is_structural_trait` reconhece `Hashable`/`Comparable` como sinônimos de `Hash`/`Ord`; `structural_trait_canonical` os colapsa. A chave-de-Map `<K: Hashable & Eq>` resolve contra um deriver de `Hash`+`Eq` via `type_conforms_to` (o nome canônico fica no `implements` folded). NÃO se introduziu interface `Hashable`/`Comparable` paralela.
- **Alternativas:** criar interfaces nativas `Hashable`/`Comparable` (duplica capacidade + descasa trait-vs-interface); renomear a ruling de collections agora (fora de escopo do #177).
- **Base:** M.0 (no-reflection) + design de traits §2 (structural derives cobrem encoding/collections sem contrato separado) + §5 (conjunto fechado). O trait estrutural É a capacidade.
- **Reversível:** sim (uma linha em `is_structural_trait`). Reconciliar a memória `teko-collections-rulings` p/ "structural traits" quando #180/collections aterrissar — edição de 1 linha, opcional do dono.

### D19 · `Ord` sintetiza `-1` como `Unary(Minus, 1)`, não `Number{value=-1}` ✅
- **Aplicada:** o builder `mk_neg_int` produz o literal negativo como unário-menos sobre `1` positivo (o shape que o parser emite), porque `codegen::cb_i128` faz `v to u128` no carrier — e o guard F3 rejeita negativo→unsigned ("impossible conversion"). Um `Number{value=-1}` direto é o PRIMEIRO a exercitar esse caminho.
- **Base:** M.1 (fail-loud, não quero corromper) + não tocar o codegen congelado de literais.
- **Reportado (adjacente, NÃO nova issue):** `cb_i128` (codegen.tks:149) tem bug latente: `(v to u128)` num i128 negativo faz panic sob o guard F3; nunca disparou porque literais negativos do source são `Unary(Minus, N)`. Follow-up p/ o integrador sequenciar.

### D20 · str-field Hash/Ord via `tk_str_hash`/`tk_str_cmp` (o seam de runtime C permitido) ✅
- **Aplicada:** adicionados `tk_str_hash` (FNV-1a, casa `di_type_id`) e `tk_str_cmp` (lexicográfico unsigned) a `teko_rt.{c,h}`, com gêmeos puro-Teko em `teko_rt.tks`, registro no checker (`scope.tks`), dispatch em `codegen.tks` (→ `tk_str_*`) e intercept no twin Teko do motor legado. motor legado==nativo garantido pelos dois gêmeos.
- **Base:** ruling no-mirroring — `teko_rt` é o C mantido (não-twin, runtime); o resto é `.tks`.

### Reportados-up (adjacentes, NÃO novas issues)
- `==` em dois structs type-checka e emite C inválido (expr.tks:19 + codegen.tks) — latente, pré-existente; questão de operator-overloading separada. TR3 NÃO auto-baixa `==`→`.eq()`.
- Slice/Optional/enum sob derive estrutural são honest-stop em v1 (M.1); follow-up TR3.1 natural quando #178 (Json) aterrissar.
- `cb_i128` negativo (ver D19).

---

## 2026-07-05 — Reorganização do backlog por dependência (fases "para trás e fora de ordem")

### D21 · Ondas de dependência sobrepostas às fases; keystone da fase-1 destrava a fase-3 ✅
- **Contexto (o que o dono viu):** fase-1-linguagem 25% / fase-2-packaging 22% / **fase-3-stdlib 2%** (33 abertas) / fase-4 16% / fase-5 50% / fase-6 0%. Merges oportunistas (unblocked-first, monomorphic-first) deram aparência de "fora de ordem": tooling e stdlib-roots aterrissaram à frente de fechar a linguagem.
- **Diagnóstico:** as fases NÃO são topo-ordenadas de fato — um **keystone da fase-1** (cluster monomorfização+128-bit) é o PORTÃO de toda a fase-3 genérica. Enquanto não fecha, 33 issues de stdlib ficam represadas. O monomorphic-first foi deliberado (camada genérica parada atrás do cluster), mas os labels não comunicavam.
- **Aplicada:**
  1. Criado label `keystone`; marcado o cluster #254/#290/#294/#296/#299/#301.
  2. Rotuladas 9 issues sem-fase (#158/#159→fase-2; #163→fase-3; #164/#283→fase-1; #167/#168/#282→fase-6; #233→fase-5).
  3. Espinha de **milestones = ondas de dependência** (delegada à teko-docs) sobre os labels de fase.
  4. Ordem corrigida ratificada (abaixo).
- **Ordem corrigida (recomendação, base: dep-DAG + "o VPS/site é DEPOIS" nas palavras do dono → fundação primeiro):**
  - **Onda 2 (fechar):** #300 (#184) — em rework (review HALTou: tee corrompe dados + flat_map/tee-iterator/compress omitidos → issue-100%).
  - **Onda 3 (KEYSTONE):** sub-cluster A `#290→#301→#254→#294` + sub-cluster B `#296∥#299`. Destrava fase-3 genérica + coleções #163 + parte de async #164.
  - **Onda 4:** fase-1 cleanup independente (#171/#172/#173/#174) + cadeia de traits #178→#179.
  - **Onda 5:** fase-3 stdlib flui (coleções #163 primeiro → math/encodings/compress/crypto/net roots).
  - **Trilha paralela (fillers near-term):** higiene de release/dist #267/#159/#282/#283 (servem o pipeline JÁ ativo); packaging pesado #180/#218-220 e site/servidor de pacotes = DEPOIS (palavra do dono).
- **Alternativas registradas:** (B) puxar fase-2 packaging/dist + site teko-lang.cloud para a frente em paralelo à onda-3 — REJEITADA por ora (dono disse "esse VPS DEPOIS"); (C) priorizar amplitude de stdlib visível (net/http/db/web) para demos — adiada (depende da onda-3 para a camada genérica).
- **Base constitucional:** issues-must-be-100% + backlog-deve-convergir + main-integrity; dependência força keystone-antes-de-dependentes (não é escolha de produto, é topo-ordem). A única escolha de produto (fundação vs site-primeiro) resolvida pela palavra do dono ("depois").

### D22 · #294 (struct sob `<T: Contract>`): constraint É gate de monomorfização, não promoção a dispatch dinâmico ✅ (do architect, law-first)
- **Aplicada:** um struct constrangido por `<T: Contract>` despacha DIRETO ao método concreto estampado (precisa do #254 antes); o fat-pointer/vtable segue exclusivo de `class`, casando o design OOP já assentado. NÃO se promove struct-constrangido a vtable dinâmica.
- **Base:** design OOP assentado (vtable = ref-semantics de class) + monomorfização (constraint = prova em tempo de estampagem). Registrado para revisão no gate LTS. Residual (struct-como-VALOR-de-contrato em slot) reportado, não expandido.

---

## 2026-07-05 — #184 (#300): fix do tee + descoberta de que flat_map é bloqueado pelo keystone

### D23 · #184 é vítima do keystone: núcleo monomórfico fecha, `flat_map`/tee-lazy sequenciam com #301 ✅
- **Contexto:** review adversarial do #300 HALTou com (1) `tee_write_fn` corrompendo dados em sinks assimétricos [bug real] e (2) `flat_map`/tee-iterator/compress "omitidos" [issue-100%].
- **Investigação (leituras, sem build):** o PARKED doc do `iter.tks` já documenta com repro empírico que `flat_map` precisa carregar um iterator interno (closure) em estado mutável (`Ref` cell / campo `IntIter?`) — que é EXATAMENTE o **#301** (closure-in-Ref/optional não round-trip; codegen não mangla optional/slice de function-type; motor legado dropa closure re-assentada num Ref). `compress_stream.tks` é construível (byte-state `Ref<MemWriter>` + `write_zip`/`read_zip`), só falta um teste exercitando o round-trip.
- **Aplicada:**
  1. **Fix do tee (bug real):** `tee_write_fn` agora drena 100% da região em CADA sink via `write_all` antes de reportar consumo → nunca re-oferece cauda → sem double-feed em sinks assimétricos. Docstring reescrito. Checkpoint commitado local em `fix/issue-184-resync` (NÃO pushado — pega-leve, sem CI até o batch com #301).
  2. **flat_map / fold genérico / tee-lazy de iterator:** NÃO forçados — bloqueados pelo #301. PARKED doc atualizado p/ citar #301 explicitamente.
  3. **Escopo #184:** entrega o núcleo monomórfico (IO0 streams c/ tee corrigido + ITER0 adapters/terminals + IO1 file copy); o remanescente (`flat_map`, tee-lazy, + teste assimétrico do tee, + teste round-trip do compress) sequencia com #301 na onda-3, validado num único CI, então #184 fecha 100%.
- **Base constitucional:** issues-must-be-100% NÃO exige entregar o que o compilador não compila (bloqueio de capacidade = dependência legítima, reportada+folded, não omissão). main-integrity: o bug do tee é real mas vive num PR aberto (não em main) — corrigido; #184 NÃO mergeia até 100% (pós-#301). Alinha com D21 (keystone antes de dependentes).
- **Correção ao review:** o achado "flat_map omitido silenciosamente" foi sobre-sinalização — é deferral documentado e bloqueado, não narrowing. (O achado do tee estava 100% correto.)
- **Reversível:** o fix do tee é independente e correto por si; o resto é aditivo pós-#301.

---

## 2026-07-06 — Compile-time: CI quickwins + gate nativo (motor legado-out) + plano-mestre do backlog

### D24 · CI 16m→~6m: desabilitar alvos gargalo + un-double do gate (#306) ✅
- **Contexto:** o dono flagou 16m31s inaceitável. Architect achou: o 16m é AUTO-INFLIGIDO — o gate nativo (#265, opt-in) rodava como 2º gate em TODA plataforma → cada uma rodava os 863 `#test` DUAS vezes (motor legado+nativo). Caminho crítico: dois targets emulados/cross consumiam 973s cada.
- **Aplicada (#306, merged):** dois targets comentados da matriz build-test (pendências abertas); gate nativo restrito a linux-x86_64 (un-double). Projeção 16m→~6m. Só `native.yml`, sem bump. (Revogado por D45: os alvos foram removidos completamente, não apenas desabilitados.)
- **Base:** a "All Green" ruleset NÃO exige checks por nome (verificado) → desabilitar jobs não trava merge; o `gate` job trata `skipped` como pass.
- **Alternativas registradas:** smoke de arch em vez de comentar (dono preferiu comentar por hora); nightly.

### D25 · motor legado fora dos testes = destino via #265+#168; até lá motor legado é o gate (phasing) ✅
- **Aplicada:** ruling do dono (motor legado out dos testes, [[teko-native-test-gate]]) é o DESTINO, realizado quando o gate nativo for rápido+completo (#168 compile-once + #265 line/branch cov nativo), então ele SUBSTITUI o motor legado em tudo. O `native.yml:76` já documentava a ruling de 2026-07-05 ("native regresses build time until #168"), então "siga o que disse antes" = essa posição estabelecida. Interim: motor legado é o gate de piso de cobertura.
- **Base:** o gate nativo HOJE é mais lento (emit+cc por gate) + só mede cobertura de função → cortar o motor legado agora regrediria tempo+cobertura. #168+#265-cov consertam antes do corte.

### D26 · Plano-mestre de drain + 5 chamadas autônomas (workflow read-only) ✅
- **Aplicada:** `docs/design/backlog-drain-master-plan.md` (DAG + Batches 0→8 + ready-set de 32 issues + notas). Ordem recomendada: Batch 0 (in-flight) → **K-B (gate nativo, CI mais leve p/ todo o resto)** → K-A (monomorfização #290→#254→#294) → onda-4 → roots stdlib → famílias → qualidade (#234 por último).
- **Chamadas autônomas (law-first, para revisão LTS):** (1) #294 = constraint é gate de monomorfização, não vtable; (2) #265 A5 = `tk_cov_line_at`/`tk_cov_branch_at` no seam `teko_rt` (não-twin, crescimento permitido); (3) K-B antes de K-A (CI mais leve = ganho de todo o backlog); (4) #184 tratado como keystone apesar de um-milestoned (destrava 6+ folhas onda-5).
- **Decisões ABERTAS que preciso da sua régua antes do batch relevante:** #174 regex NFA-vs-backtracking (bloqueia Batch 3.3 — recomendo NFA por segurança/sem backtracking catastrófico); #254 layer-4 `Env.expected_ret` (alta rotatividade); #233 LSP sem gate de início; #182 TCC/#267-item1 diferidos pós-alpha.
- **Correção de ground-truth:** 74 abertas (não 73); o design pai `onda3-monomorphization-cluster.md` SUB-CONTA sites nos 4 roots → confiar em `drain-onda3-subcluster-A.md`.

---

## 2026-07-06 — OOP syntax: `this` / `base` / `static` (pedido do dono, feedback de dev)

### D27 · `this`/`base`/`static` = rename SÓ de front-end (codegen+motor legado byte-idênticos) — design pronto, 1 HALT
- **Contexto:** dono (2026-07-06) pediu trocar o receiver (1º arg solto sem tipo) por `this`, o `class Base(binding)` por `base`, e adicionar `static` explícito. Dev achou a sintaxe atual confusa.
- **Ground truth (verificado):** receiver = `params[0]` com `has_type=false`, NOME escolhido pelo autor (`self` hoje); codegen/motor legado leem `params[0]` POSICIONALMENTE (nunca casam a string) → renomear é fixpoint-neutro. Base-binding já é `let <binding>: <Base> = <this upcast>` sintético no typer (`typer.tks:3110-3128`). Static/instance é `params.len==0 || params[0].has_type` em TODO lugar (`di.tks:119`, `typer.tks:745`, `collect.tks:721`).
- **Keystone de implementação:** o parser INJETA um `Param{name="this"; has_type=false}` sintético p/ método não-`static` → preserva o invariante inteiro do checker, então **#254 (métodos genéricos) + #294 (constraint dispatch), que leem o modelo de receiver atual, precisam de ZERO mudança**.
- **Autônomo (law-first, p/ revisão LTS):** (1) `static` = RESERVADA (sem colisão; M.2); `this`/`base` = CONTEXTUAIS — `base` é nome de local VIVO em `driver.tks`/`resolve.tks`/`zlib.tks`, reservar quebraria produção; (2) add `"this"` ao `cg_is_c_keyword` (kw de C++, não C — fixpoint-neutro); (3) tamanho = **L** (não XL): produção tem 0 classes/4 interfaces/0 traits; massa OOP está nos `.tkt` + `synth.tks` (~89 sites de receiver); codegen/motor legado intocados.
- **Base constitucional:** M.2 explícito + M.3 honesto = GANHO (torna receiver/staticness visíveis vs convenção implícita do 1º arg solto). Consistente com no-`ref`-keyword, modelo no-GC/arena/Ref-por-lowering (`this` = receiver pointer-lowered arena-backed), e `teko-default-args-named-call` (receiver ainda anda em args[0] sem nome). Sem tensão de Lei.
- **HALT (precisa da régua do dono):** **hard-cut (A) vs transição dual-syntax (B).** Recomendo **(A) hard-cut ANTES da enxurrada fase-3 de coleções**: migração é pequena+codegen-neutra, gramática dual mantém viva a implicitness que o dono quer remover (cheiro M.2/M.3), e #163 (coleções, ABERTO, sem árvore `src/collections` ainda) deve ser escrito já na sintaxe nova, não duas vezes. (B) só p/ proteger #163 em voo.
- **Doc:** `docs/design/oop-this-base-static.md`. Memória: `teko-oop-this-base-static-design`.

### D27-owner · RATIFICAÇÃO (dono 2026-07-06): OOP this/base/static = HARD-CUT ✅
- **Decisão do dono:** aprovada a opção A (hard-cut) da revisão de arquitetura (D27 / docs/design/oop-this-base-static.md). **Justificativa do dono:** "ainda não temos a LTS e nem código em produção, logo, há coisas que podemos remodelar se assim for melhor" — pré-LTS + zero código em produção = SEM dívida de backward-compat → o hard-cut limpo vence a transição dual-syntax (que só carregaria complexidade de parser duplo sem necessidade).
- **Timing vs #163:** deixar o #163 (coleções, in-flight) fechar na sintaxe ANTIGA; o PR do hard-cut reescreve todo o corpus (incl. #163) atomicamente via codemod mecânico (rename self→this + drop base-binding + add static). Sem desperdício, sem re-escrita humana.
- **Base:** é renomeação PURA de front-end (receiver=params[0] posicional; base já é `let <bind> = <this upcast>` sintético) → codegen/motor legado idênticos → **fixpoint-safe** (#254/#294 zero mudança). Tamanho L. Risco = codemod perturbar codegen → mitigado por rename-só + gate gen2==gen3 (crumb C4). `static`=reservada, `this`/`base`=contextuais (`base` é nome local vivo).
- **Sequência:** #163 fecha → hard-cut OOP (próximo keystone, verificação independente do fixpoint) → resto da fase-3 na sintaxe nova.

---

## 2026-07-06 — Dívida de memória: fix real via K-B Track C (gate nativo default, motor legado out)

### D28 · Flip do gate default p/ nativo = o fix da dívida de memória (pedido "corrija a dívida de memória") ✅
- **Contexto:** dono pediu explicitamente "corrija a dívida de memória então" — o gate de PR estava em ~1,5 GB (o balão in-process do motor legado executando os testes com env funcional), causando o OOM da lane ASan (7 GB ÷ ASan-3x ÷ corpus crescente). O #323 (cap ASan) foi só STOPGAP; o dono quer a raiz.
- **Diagnóstico (ground truth, drain-265-168):** o ganho de memória é CONSEQUÊNCIA do flip do CI (Track C), não uma reescrita — o gate NATIVO já roda os testes num processo FILHO (`teko::process::run`, project.tks:748), então o pico do compilador cai pro codegen-only (~366 MB) no instante em que o gate motor legado deixa de ser default. Track A (#265) já fez o gate nativo enforçar os 3 floors (function/line/branch), CI-verificado → o flip é low-risk.
- **Aplicada (Track C, agente Opus, worktree isolado):** (1) `run_gate`/`native_gate_of` (project.tks) default → `native_gate=true`, com opt-out via a flag alternativa de gate (removida); (2) native.yml dropa o step do antigo gate interpretado das 5 plataformas + mantém UMA lane motor legado nightly (regressão); (3) sanitizers.yml roda o gate NATIVO (mais leve → conserta o OOM de vez + ASan cobre o path de PRODUÇÃO, mais valioso que o motor legado). `diff-motor legado-nativo` intocado (paridade motor legado==nativo segue provada).
- **Base constitucional:** realiza a ruling STANDING do dono "motor legado out of tests" ([[teko-no-gc]]: nativo é autoritativo, motor legado = dev/WASM); respeita o teto ≤300 MB de design (o balão motor legado era a violação); main-integrity preservada (gate nativo enforça TODOS os floors, não enfraquece). Fixpoint-neutro (muda QUAL gate roda, não o C emitido) — verificado gen2==gen3.
- **Autônomo (law-first, p/ revisão LTS):** (1) ASan passa a sanitizar o NATIVO (não o motor legado) — decisão minha: o path nativo é produção (autoritativo), então ASan-checá-lo > motor legado; (2) manter #323 (cap) como defensivo mesmo com o flip (cinto+suspensório); (3) motor legado regride via UMA lane nightly, não per-PR (governança separada, não bloqueia PR).
- **Secundário (não neste PR):** codegen puro em ~366 MB ainda > 300 (lei de design) → tuning de arena (right-size do first-rung, lição #148 R3b — os novos módulos stdlib re-introduziram sobre-capacidade). Follow-up separado.

### D28b · Desacoplar a lane ASan do #324 (defer native-ASan flip) — landar o fix de memória, sequenciar a UB ✅
- **Contexto:** o #324 flipou a lane ASan pro gate NATIVO, que sob `-fsanitize=function` **expôs uma UB pré-existente** (do #291): método de trait chamado via `R(*)(void*)` sendo definido com receiver concreto (`tk_t_Sq*`) — UB de C (CFI/LTO-frágil) que o gate-motor legado nunca compilava. #324 vermelho.
- **Decisão (law-first):** o ganho de memória (motor legado 1566→937MB) é o flip do **build-gate** (`teko . -o bin`), **independente** da lane ASan. Reverti só o step da lane ASan pro gate-motor legado (+ caps do #323), landando o fix de memória (PR #324 merged, `af223c2`). A UB da vtable é pré-existente + separável → **task_0ab18d5e / branch fix/trait-vtable-ub**: fix na raiz (thunks tipados) que re-flipa a lane ASan e valida sob o próprio UBSan. **NÃO é mascarar** — status quo da lane (nunca pegou essa UB) + fix sequenciado; a alternativa (fix keystone dentro do PR de memória) bloquearia a prioridade do dono.

### D29 · Achados empíricos corrigem a análise + 3 bugs de codegen sequenciados ✅
- **Empírico (worktree descartável, motor legado+nativo):** (1) grafo cíclico/DLL **compila nativo hoje** via campo de classe (só `Ref`-como-campo é rejeitado) → cliff-2 estava mal-enquadrado; (2) o UAF do `mem::free` aliased é **invisível ao ASan** (free nunca chama libc free) — só `TEKO_MEM_PARANOID=1` pega → corrige "pego pelo ASan" repetido nos round-1/2. Ver [[teko-mem-model-empirical]].
- **Torneio (round-2, 3 juízes):** arena = **default certo mas não suficiente** — vence pragmatista+purista (híbrido), future-architect prefere ownership+move p/ concorrência; consenso = família arena é a fundação, ninguém defende GC/RC/gen-ref como fundação. Fronteira teórica: pilotar **second-class values** (piso de soundness inferível); nada fecha cliff duro barato.
- **3 bugs de codegen (dono ratificou "eu conduzo, serial"):** (A) assert-dispatch nativo + (B) slice-elem region-drop → BUNDLE `fix/native-codegen-parity` (1 ciclo de fixpoint, PEGA LEVE); (C) UB de vtable → `fix/trait-vtable-ub` (keystone, depois). Um build pesado por vez; drenar cada CLEAN.

### D30 · Vtable-fix (#326) mergeado decoupled + a lane ASan nativa = auditoria rolante de UB ✅
- **#325** (bundle A+B): slice-drop consertado (predicado `cg_binding_is_slice_element_borrow` + suprime o drop de view emprestada), assert já resolvido pelo #324 → só fixture. Reviewer adversarial CLEAN (caçou vazamento largo-demais, não achou). **Mergeado** (`b093d71`). Lição: o drain auto-mergeou ANTES do reviewer fechar (CI ficou verde rápido) → pro #326 (keystone) gatear explícito: reviewer PRIMEIRO, drain depois.
- **#326** (C): UB de vtable consertada na raiz via **thunks tipados** (per-(class,contract), reconstrói receiver leaf/base), cobre trait/interface/base; fixpoint gen1==gen2==gen3 (`f2f1b663`); de brinde consertou um vtable-drop latente de método de base. Reviewer adversarial CLEAN (só 1 site de dispatch, cobertura total, sem colisão nova). Mergeável.
- **Descoberta (a lane ASan nativa = auditoria rolante):** o re-flip da lane ASan pro gate nativo é uma auditoria de UB do path nativo — cada UB pré-existente exposta tem que ser consertada na raiz antes do flip aterrissar. Sequência exposta: vtable #291 (fixed no #326) → depois `tk_mul_u16` signed-overflow → depois **alinhamento sistêmico do arena** (`tk_region_alloc` arredonda por `max_align_t`=8 em arm64 mas Expr/TExpr têm `__int128` que exige 16 → todo Expr desalinhado; latente no CI x86_64 onde max_align_t=16). Ver [[teko-arena-int128-alignment]].
- **Decisão (law-first, recomendação do implementer que HALTou no meu threshold "cauda longa / decisão semântica"):** DECOUPLAR o re-flip do #326 — mergear só o vtable-fix (reviewer-clean, lane volta pro motor legado, verde). O cleanup de runtime (`tk_mul_u16` + alinhamento do arena, arquitetural) + o re-flip = pass dedicada `fix/arena-alignment` off post-#326 main, com re-verificação de fixpoint completa (arena é pervasivo). NÃO mascara — o vtable-fix é ganho puro; o alinhamento é bug pré-existente sequenciado com o rigor que merece.
- **Fila de produção restante:** trap do `run`/`args`/`cwd` (reserved-name-guard, crasha codegen nativo); fixture de self-dispatch de base (#326 reviewer); alinhamento do arena (dedicado); #184 DIRTY (re-sync); contagem stale no header do diff_vm_native.

---

## 2026-07-06 — RATIFICAÇÃO: remodelagem de memória + `unsafe` + backend próprio

Doc de base completo: `docs/design/memory-unsafe-backend-remodel.md`. Fecha a discussão de vários turnos; é a fundação da branch paralela.

### D31 · Modelo de memória híbrido + `unsafe`-por-tipo + direção motor legado/backend (dono, aprovado 2026-07-06) ✅
- **Enquadramento honesto (o que mudou o entendimento):** o 1,5GB era o **motor legado** (env funcional interpretando o corpus in-process), NÃO o arena — `#324` (motor legado→nativo) já matou. O arena do compilador é ~366MB, leak-to-root **batch seguro**. O ganho 8,5GB→293MB veio de free-list+right-sizing+free-old-on-grow, NÃO de reclaim de escopo. ⇒ **o modelo de memória é pras APPS de usuário** (funcionais, vida-longa), não pro compilador batch. Ver [[teko-mem-model-empirical]].
- **Híbrido recomendado:** `arena` (default invisível, BUILT) + **spine** (fato de points-to/unicidade inferido, bounded, sobre `escape.tks` — a aposta de segurança; UNBUILT, contingente ao audit) + `adopt` (opt-in, fecha C1 por bulk-drop) + `unsafe` (piso). **SEM GC** (descartado: barreiras program-wide, sem stack-maps portáveis em C, e o único modo sound colapsa no `adopt`). RC/gen-ref/borrow-checker também descartados. **Teto honesto: 0 cliffs fechados hoje; tudo pende do audit do spine** (uma dívida → 4 garantias, ou 0).
- **`unsafe` = MODIFICADOR de tipo/fn-de-namespace, NÃO bloco (decisão do dono):** *"se o dev usa, assume o risco por completo, não de uma partezinha isolada 'bloco'"*. Contágio no DADO (tipo unsafe só em fn/tipo unsafe; propaga por composição — struct que embute unsafe é unsafe); chamadas NÃO-coloridas (só tipo safe cruza); métodos herdam o unsafe do tipo. **Ganho grande:** contenção é check NOMINAL → `unsafe` DESACOPLA do spine → é o keystone que shippa PRIMEIRO. Casa com M.3 (honesto).
- **Superfície:** lexer ~0 tokens (keywords contextuais, precedente `from`/`base`); parser: `use path::[A,B]` (lista=`[]`+`,`; corpo=`{}`+`;` — invariante confirmada no `src/`), `unsafe` como modificador (junta `pub`/`static`/`extern`), `adopt{}` (molde `defer`), `#must_free` decorator em declaração. `Owned<T>`/`RawBuf`=tipos stdlib; spine=inferência no checker.
- **motor legado/backend:** construir **backend próprio AOT + linker** (sair do C+cc/linker externos; north-star toolchain-independence/velocidade/bare-metal). motor legado aposenta — papéis: `run` (não-usado), REPL (o único a decidir), diferencial (MIGRA pra C-backend-vs-próprio, oráculo melhor), LSan-com-rewind (só significativo no gate motor legado → nightly). **SEM dependência de comptime/const-eval** (verificado — de-risca a aposentadoria). O gate nativo-ASan é **auditoria rolante de UB** (pegou vtable/tk_mul_u16/alinhamento).
- **Gate pré-branch (sequência aprovada):** (1) `#327` caiu → main limpa + release `0.0.1.49`; (2) 2 fixes de de-risk na main (doc-honesty do `mem::free` + guard de reserved-name `run`); (3) abrir a branch paralela. **Rastreado, não-pré-branch:** `#301` (Func-in-Ref, pré-APPS não pré-branch, parka `#184`); `#283` (UB de `-O2`, obsoletado pelo backend próprio); `#184` parkado (bloqueado por `#301`).
- **Base constitucional:** consistente com no-GC/arena ([[teko-no-gc]]), M.3-honesto (unsafe total/visível), small-language (spine inferido, superfície mínima), e a ruling motor legado-out ([[teko-native-test-gate]]). Primeiro movimento da branch: **auditar `escape.tks`** (sobrepõe ou substitui?) — trava o spine; `unsafe` (nominal) e `adopt` (arena-tree) andam em paralelo, independentes do spine.

---

## 2026-07-06 — Execução da onda 0.1.0.0-beta

### D31b · Recon #330 (`escape.tks`): veredito **LAYER** — a spine é query aditiva ✅
- **Veredito (mergeado em #329 via #345, `docs/design/spine-layer-or-replace.md`, doc-only):** LAYER, não REPLACE. Uma lattice bounded (uma-função + um-hop) de points-to/unicidade entra como query NOVA (`fn_spine`/`ref_target_outlives`) ao lado do `fn_escaping_vars` intocado — o name-set continua o piso sound (over-approximação monótona), a spine só *relaxa* pontualmente. REPLACE é estritamente mais perigoso (poderia baixar o escaping-set abaixo do piso → leak vira UAF, M.1/M.5) por zero poder extra.
- **Escopo honesto e estreito (a spine NÃO shippa as 4 garantias barato):** RELAXA ref-para-local (`typer.tks:2450`), `mem::free` afim (`us=unique`), stored-borrow one-hop unique frame-local (`typer.tks:2512`). **REJECT eterno:** ref retornado (`typer.tks:3169` — o recon ACHOU esse gate que faltava no §2b da issue; o bound de uma-função torna o frame do caller invisível), ref em coleção/variant/genérico-arg/nullable (`resolve.tks:1168/1185/1213/1113/1233` — sem âncora de referente).
- **Classificação do build #331:** *additive-query-mas-gate-touching* — a query é aditiva, mas as relaxações de gate que ela habilita são shared-checker → **herda full gate (C+self-host+nativo) + FIXPOINT + `diff_vm_native` + review independente**, NÃO o fast-path aditivo. 5 fixtures nomeadas (padrão `mem_free/`): `stored_borrow_outlives_referent`+`ref_returned_rejected`+`free_aliased_rejected`+`ref_in_collection_rejected` (REJECT) e `stored_borrow_sound` (ACCEPT). Ver [[teko-remodel-memory-unsafe-backend]].

### D32 · 100% de cobertura no código NOVO/ALTERADO — padrão desde já (dono, 2026-07-06) ✅
- **Ruling do dono:** *"tudo que for criado novo ou alterado deve ter cobertura de 100%, para minimizar o retrabalho final (última W15 antes do LTS)."* Cobertura 100% do **delta** (linhas/branches novos/alterados) shippa JUNTO com a mudança — **definition-of-done** de toda issue, não passe posterior.
- **Enforcement:** medido no **gate NATIVO** (`teko test .` `--coverage` Cobertura, [[teko-coverage-cobertura]]/[[teko-native-test-gate]], nunca motor legado); o **reviewer** checa o delta como checa um teste falhando; o **integrador** não mergeia em #329 sem o número reportado. Degrau ACIMA do floor histórico de branch-cov (49% = mínimo do corpus legado; a lei é 100% no delta).
- **Exceção honesta (M.3):** arm genuinamente inalcançável (`HALT`/`exit`/`panic` pós-match-exaustivo) pode ser excluído SÓ se **listado com justificativa de uma linha no PR** — zero gap silencioso. 100% forçando teste feio/código morto = sinal p/ extrair/remover, não maquiar.
- **Recorrente por onda até o LTS**, ao lado do W15-sweep e do doc-sync (dev-model da epic #340). Torna o W15 final uma VERIFICAÇÃO, não retrabalho de cobertura em massa. Ver [[teko-100-percent-coverage-on-new-code]] · [[teko-w15-style-from-now]] · [[teko-issues-must-be-100-percent]].

### D33 · Metaprogramação FORA da LTS v1 — adiada pro pós-1.0 (dono, 2026-07-06) ✅
- **Ruling do dono:** *"não vamos incluir na LTS (vai ficar para um futuro, se tiver issue, pode remover e reestruturar as referências) metaprogramação. É muita coisa para uma LTS v1."*
- **Escopo do que SAI:** metaprogramação = **comptime geral / meta-code execution / macros** (execução de código em compile-time, geração/manipulação de AST pelo usuário, quasi-quote). Fica pra versão **futura pós-`1.0.0.0`**, fora de TODAS as ondas 0.X que compõem a LTS.
- **NÃO confundir — FICA na LTS (não é metaprog):** traits estruturais TR0–TR5 (#177/#298, derive de `Eq`/`Ord`/`Hash`/`Clone`/`Default`/`Json` via **compile-time field-view**, ZERO reflexão em runtime — lei M.0); `#`-decorators (`#inject`/`#singleton`/…/`#must_free`, wiring compile-time); genéricos+mono. O `#derive` como atributo já fora REJEITADO em favor do trait estrutural.
- **Estado factual:** varredura (todas as issues + docs) → **NÃO existe issue de metaprog** em nenhum milestone; só aparecia como *"General comptime ('meta-code execution') stays a DEFERRED separate proposal"* (`TEKO_MASTER_PLAN.md:621`). O remodel já é **sem dependência de comptime/const-eval** (D31, `memory-unsafe-backend-remodel.md:146`) — motor legado-retirement e backend próprio não precisam de metaprog. ⇒ nada a remover; ação = **endurecer a referência** (L621 → "post-1.0 / out of LTS v1"), dobrado no escopo do **#341** (doc-sync da onda 0.1).
- **Enforcement:** qualquer issue de metaprog que aparecer num milestone de onda (0.X) ou apontando pra LTS = **removida do milestone → bucket futuro pós-1.0**, referências reestruturadas. Metaprog nunca é dependência de nada da LTS. Ver [[teko-metaprogramming-out-of-lts]].

### D34 · Rulesets que ENFORÇAM + lição de merge + coverage deferido (dono, 2026-07-06) ✅
*(o D34 original foi perdido — o push direto no umbrella foi bloqueado pela própria Merge gate recém-criada; esta é a versão final, pós-reestruturação.)*
- **Gatilho (erro meu):** mergeei #346 (#336) via `gh pr merge` olhando um snapshot PARCIAL de checks — as lanes de sanitizer ainda rodavam e depois falharam (causa = harness, não UB: o fixture negativo `must_free_leak/` quebrava o loop build-all do sanitizer; fix = marker `EXPECT_COMPILE_FAIL`). Violou main-integrity ([[teko-main-integrity-absolute]]).
- **Descoberta:** a ruleset "All Green" (`~ALL`) NÃO tinha `required_status_checks` — nada exigia CI verde pra mergear (nome aspiracional). E `required_status_checks`/`pull_request` em `~ALL` **BLOQUEIA push de WIP** em feature branch (trava os agentes).
- **DESIGN FINAL de 3 rulesets:** (1) **Merge gate** (`main`+`remodel/**`) = `required_status_checks` **CI gate + Sanitizer gate + SAST gate** (os 3 agregadores `if:always()`); (2) **All Green** (`~ALL` EXCLUDE fix/**,docs/**,recon/**,agent-**,worktree-**,chore/**) = `pull_request`+`non_fast_forward`+`code_quality` (baseline, não trava WIP); (3) **Feature branch guardrail** (fix/**,docs/**,recon/**) = só `non_fast_forward` (garantia sem travar WIP). + push-CI em `remodel/**`. **Push direto no umbrella agora bloqueado → mudanças em #329 (DECISION_LOG etc.) via docs-PR.**
- **Processo (permanente):** nunca mergear em snapshot; confirmar TODOS os checks do commit-HEAD `completed`+`success` (`gh api .../commits/SHA/check-runs`); sempre via `gh pr merge`.
- **COVERAGE DEFERIDO:** a regra nativa "Restrict code coverage" (GitHub Code Quality) **só existe em plano Team/Enterprise** — indisponível em conta pessoal (API 404). Além disso a COLETA de coverage estoura memória (motor legado in-process, o balão do remodel) → OOM no ubuntu 7GB; só passa em macos + caminho nativo `-o bin --coverage`. Dono **desabilitou a ruleset `code_coverage` + tirou dos required checks; PR #357 draft**. Caminho futuro: o **Coverage gate custom (#355)**, independente de plano. Bridge: verificação MANUAL de delta. Ver [[teko-coverage-ci-findings]].

### D35 · `unsafe #must_free type Arena` — arena manual dev-controlada (dono, 2026-07-06) ✅ (=#358, 0.1)
- **Decisão:** adicionar uma **arena MANUAL, não-lexical, só-unsafe** — o dev cria a região, aloca ponteiros ATRELADOS a ela, e faz **bulk-free** num ponto à escolha. É o complemento **unsafe** e **não-lexical** do `adopt` (que é lexical/safe).
- **A elegância — compõe as 2 features da onda, cada uma guardando a metade que sabe:** **`#must_free` (S2)** barra o **leak da região** (largar a Arena sem `mem::free(a)` = erro de compilação, dataflow do #336); **`unsafe` (U1/U2)** contém o **resto** — pós-free os ponteiros viram dangling (aliased-UAF que a spine não rastreia), risco assumido POR COMPLETO pelo dev. Encaixe exato de M.3.
- **Zero gramática:** tipo stdlib (`is_unsafe`+`must_free` coexistem no `TypeDecl`); `Arena::new/alloc<T>/mem::free` mapeiam na árvore `tk_region_new/alloc/free` que **já existe** (a mesma do `adopt`) — expor, não construir alocador novo.
- **Hierarquia de memória (do auto ao cru):** arena-default(invisível) → `#must_free`/`mem::free`/`defer`/`adopt` (= o "C# `using`/`IDisposable`", SAFE e **mais forte** — `#must_free` OBRIGA o free, C# só avisa) → **`unsafe #must_free Arena`** (região dev-controlada) → `RawBuf`/`Owned<T>` (malloc/free cru).
- **Sequência:** =#358, follow-on do #334 (U3, precisa de `RawBuf`/`Owned`/`ptr`), na onda **0.1**. Independente da spine. Ver [[teko-remodel-memory-unsafe-backend]].
- **Implementação (#358, entregue):** handle = `struct { region: uptr }` (o `tk_region *` cru como word opaco). Três builtins especiais no checker+codegen (SEM gramática nova, sem alocador novo): `teko::mem::region_new(): uptr` (→ `tk_region_new(tk_region_root())`), `teko::mem::region_alloc(region: uptr, init: T): ptr<T>` (T inferido de `init`, → `tk_region_alloc` + init, `ptr<T>` atrelado; **não** é fn genérica com corpo Teko, então zero maquinaria de genéricos), e `teko::mem::free(a)` estendido pra aceitar o handle e rotear pro **bulk-drop** `tk_region_drop_subtree` (a árvore inteira, NÃO a free-list). Reconhecimento por **ESTRUTURA** (`is_region_handle_name` = `#must_free` struct de campo único `uptr`), nunca por nome `Arena` fixo — o módulo stdlib e a cópia local do fixture roteiam idênticos.
- **Staging (mesmo padrão do D16/rawbuf):** o seed liberado (0.0.1.50) precede U1/S2 **E** os builtins novos deste PR, então `src/mem/unsafe/arena.tks` embarca só o TIPO `Arena` cru (sem `#must_free`/`unsafe`, sem `new()`/`alloc` que chamariam os builtins) — self-host hoje. A superfície `#must_free unsafe` COMPLETA (`Arena::new` → `region_alloc` → `mem::free` em todos os caminhos, incl. `defer` + ambos os braços do `if`) é provada por `examples/regressions/arena_manual_ok` (exit 62, NATIVE_ONLY, PARANOID limpo) e o reject por `arena_manual_leak` (COMPILE_FAIL), ambos compilados por gen1. Re-tag = mecânico quando um seed carregar U1/S2 + os builtins.
- **GAP reportado (fora de escopo — checker/parser):** a grafia `a.alloc<T>(...)` do AC#3 NÃO é expressável: `parser::MethodCall` não tem `type_args` (o parser não aceita args de tipo em chamada de método), E um type-param de MÉTODO próprio (`fn alloc<T>` num struct não-genérico) não monomorfiza ("unknown type" no receiver, tanto inferido quanto explícito — só o `Owned<T>::make` estático funciona, pois lá o T é do TIPO). Entregue o **thin-wrapper** que o issue abençoa (hazard #2): a alocação é `teko::mem::region_alloc(a.region, init)` (T inferido), semanticamente idêntica; a grafia-método fica pra quando genéricos-de-método + type-args-em-método-call existirem (surface de checker/parser, fora deste issue).

### D36 · Seeds intermediários `*-beta` por-merge — a umbrella se auto-hospeda ao longo da onda (dono, 2026-07-07) ✅
- **Problema:** o seed liberado da main (`0.0.1.50-alpha`) NÃO parseia a sintaxe nova da onda (`unsafe`/`#must_free`/`use ::[…]`/`adopt`). Enquanto as features são só ADITIVAS (o `src/` ainda não as usa), o seed constrói o gen1 e tudo funciona. MAS quando o `src/` do compilador COMEÇAR a usá-las (dogfooding, re-tag do stdlib staged, spine), o seed da main não constrói nem o gen1 → trava. **Decisão do dono:** publicar um **seed intermediário do compilador (`0.1.0.N-beta`) a CADA merge na umbrella**, E **backfill retroativo de UM seed por CADA merge histórico** (dono, plural explícito: "para cada merge que houve" / "as versões retroativas … todas"). Implementação: **eu (integrador), inline + validado** (não-agent).
- **Mecanismo (4 peças, este PR):** (1) **`tag-on-version-bump.yml`** passa a observar `remodel/**` além de `main` (o bump de `teko.tkp` na umbrella cria a tag); (2) **`release.yml`** dispara em tags `*-beta` (além de `*-alpha`/`*-bootstrap`); (3) **`ci_provision_teko.sh`** vira **channel-aware** — canal `beta` (base/branch `remodel/**` OU tag `*-beta`) prefere o `*-beta` mais novo e cai pro `*-alpha` no 1º bootstrap; canal `stable` (main, tags `*-alpha`) EXCLUI `*-beta` (a main NUNCA semeia de uma umbrella em curso); (4) **bump `teko.tkp` BUILD `0.1.0.0→0.1.0.11`** — o tip atual (todas as features + a própria infra) publica como `v0.1.0.11-beta`, o TOPO da cadeia retroativa (os 10 abaixo = os 10 PR merges históricos, ver backfill).
- **A cadeia de dogfooding:** cada sub-PR mergeado na umbrella bumpa o BUILD → tag `0.1.0.N-beta` → `release.yml` publica um seed do tip → o CI do próximo sub-PR semeia DESSE beta (`ci_provision` canal-beta). No build da própria tag `0.1.0.N-beta`, como ela ainda não foi publicada, "beta mais novo" resolve pro seed ANTERIOR (`N-1`) — exatamente a corrente de auto-hospedagem. Bootstrap-friendly: enquanto não houver nenhum `-beta`, o canal-beta cai pro `-alpha` da main (features aditivas → gen1 constrói).
- **Convenção reforçada:** toda umbrella de onda DEVE se chamar `remodel/<slug>` (senão o filtro `remodel/**` não pega) — casa com a convenção já ratificada dos 4 workflows de CI. Quando a onda fecha, a umbrella→main leva o `teko.tkp` em `0.1.0.<final>-beta`, e a main tag-a a release da onda (alpha→beta na main = a virada de estágio da onda).
- **Validação (inline):** YAML dos 2 workflows parseia OK (triggers conferidos); shellcheck limpo nas linhas novas (só o SC2012 info pré-existente na l.84); **7 cenários unit-testados** do seletor de canal (main-PR/remodel-PR/push-remodel/beta-tag-build/1º-bootstrap/alpha-tag/push-main) — todos PASS, incluindo as invariantes críticas (main nunca semeia de beta; remodel dogfooda o beta mais novo). `derive_version.sh` → `v0.1.0.1-beta`. Ver [[teko-remodel-memory-unsafe-backend]].
- **Fix do trigger de release (sem PAT):** o repo NÃO tem `RELEASE_TAG_PAT`, então `tag-on-version-bump` cai no caminho DISPATCH (`gh workflow run release.yml`), que antes disparava no branch default (main) → construiria o compilador da **main** (sem as features novas), não o tip da umbrella que a tag aponta. Corrigido: dispatch **na própria tag** (`--ref "$DERIVED"`) → todo checkout do `release.yml` fixa no commit taggeado (tip da umbrella). Estritamente melhor também pra linha stable (sem corrida se a main andar após taggear). `release.yml` existe na main (registrado p/ o dispatch-by-name) E na tag (tem o `workflow_dispatch`), então `--ref tag` roda a versão da tag. (PAT continua sendo o caminho preferido/auto-trigger, mas agora é opcional.)
- **Backfill retroativo EXECUTADO (10 seeds, cadeia real — dono ratificou "todos os PR merges" + "primeira do alpha, demais da anterior"):** publiquei um seed beta por CADA um dos 10 PR merges na umbrella, na ordem do branch, cada um construído a partir do seed anterior (self-hosting real): `v0.1.0.1` (#345, do `0.0.1.50-alpha`) → `.2` (#351 U1) → `.3` (#346 S2) → `.4` (#347 S1) → `.5` (#359 docs) → `.6` (#343 U2) → `.7` (#344 U3) → `.8` (#348 adopt) → `.9` (#360 docs) → `.10` (#361 Arena). Depois `v0.1.0.11` = tip+infra. **Mecanismo (Option C):** cada tag fica num commit destacado = `C_i` + **bump manifest-only** de `teko.tkp` p/ `0.1.0.i` (o manifesto NÃO é fonte do compilador → binário funcionalmente idêntico ao do merge, mas reporta a versão certa; sem isso `cross_compile`/`derive_version` embutiria `0.1.0.0` em TODOS e o version-sanity do `ci_provision` rejeitaria a cadeia). Disparei `release.yml --ref V_i` sequencialmente (o `ci_provision` ANTIGO em cada commit histórico escolhe naturalmente a maior versão publicada = o predecessor). Todos os 11 assets/plataforma por seed; cada binário verificado reportando `0.1.0.i-beta`. **Correção honesta:** minha 1ª grafia deste D36 colapsou pra "um catch-up só" — ERRADO; o dono queria a cadeia por-merge (registrado aqui pra não repetir).
---

## 2026-07-12 — Consolidação native-only (Crumbs 1–5): native torna-se o único engine

### D37 · Consolidação native-only (Crumbs 1–5) — native torna-se o único engine (dono, 2026-07-12/13) ✅
- **Decisão aplicada — os 5 crumbs, na ordem, cada um com seed intermediário:** crumb 1 = CI-first, retirar as lanes de execução legada de falso-negativo (#531) · crumb 2 = driver repoint (`teko test`/`teko run` → nativo; a flag alternativa de gate REMOVIDA outright; `src/driver.tks` morto deletado) (#533) · crumb 3 = aposentar o REPL (#538) · crumb 4 = coverage → `teko::coverage` + deletar o diretório do engine legado + **adendo do dono: aposentar o ESPELHO C inteiro** (92 twins + CMakeLists.txt; sobrevive só a família runtime) (#548) · crumb 5 = varredura de prosa/docs + este registro + o pointer §7 devido (#551). Nativo é o único engine; o bootstrap C está arquivado na tag `0.0.1.3-bootstrap` + histórico do git.
- **Micro-decisões RATIFICADAS:** (M1) REPL aposentado — o dev-loop é `teko run` (build debug nativo + exec, molde `cargo run`); `teko repl` cai no caminho genérico "não é projeto" (exit 1 — desvio do exit-2 do design, aceito). (M2) exe C-bootstrap aposentado — o seed é SEMPRE o binário do release (`ci_provision`/`fetch_teko.sh`). (M3) a flag alternativa de gate removida OUTRIGHT — um resíduo dela cai como positional desconhecido (o sinal honesto de que o engine legado se foi); a variável de ambiente do gate legado não é mais lida. (M4) `teko test .` = o gate NATIVO com os 3 floors (function/line/branch, #265). (M5) `teko run` = build debug para `target/debug` + execução em PROCESSO-FILHO, propagando o exit code do programa (o contrato exato que o `run` legado tinha).
- **Adendo do crumb 4 (dono 2026-07-13):** os 92 pares `.c`/`.h` congelados + `CMakeLists.txt` deletados por inteiro; a família runtime (`teko_rt.{c,h}`, `assert.{c,h}`, `win32_compat.h`) sobrevive com data de morte marcada — **#549: a versão seguinte ao linker próprio (zero C na árvore)**.
- **Evidência ritual:** por-crumb nos corpos dos PRs (#533/#538/#548) — fixpoint byte-idêntico em cada crumb de src/; prova de byte-identidade do Cobertura pré/pós-move (sha256 `7aab136b…b92d8`); o gate 21/21 do #548 é a primeira auto-hospedagem sem C de compilador.

---

## 2026-07-15 — Módulo-level `const` + convenção W15 "no magic values" (#594)

### D38 · `const NAME: Type = <const-expr>` no nível de módulo — INLINE escalar, rodata p/ agregado (dono 2026-07-15) ✅ RATIFICADA (com 2 rulings; plano `docs/design/const-module-level-plan.md`)
- **Decisão aplicada (design ratificado, law-first):** introduzir `const` de nível de módulo como feature real (parse→check→uso-como-valor→`pub const` cross-módulo) e migrar os ~50 `fn X(): T { <const> }`. O feature é **front-end + lowering apenas**: os backends e os C twins ficam intocados.
- **Rationale central (dono):** o custo real das fns zero-arg NÃO é CALL/RET — é que **cada chamada abre uma ARENA (região lexical, R11)**. Const é comp-time → **zero arena**. Escalar → **INLINA o literal no ponto de uso** (zero arena, zero global, zero reloc, um `mov imm`). Agregado imutável → **rodata** (o caminho read-only que os literais string já usam ponta-a-ponta em TODOS os backends + a motor legado).
- **D2 (agregados) — RULING 1 do dono: rodata JÁ NO BASELINE (não 2-passos).** O dono REJEITOU o baseline-inline; quer o end-state direto. Consequência que a investigação revelou: **reloc data→data NÃO EXISTE** (todo o modelo de reloc é `.text`-relativo — `RelocX86.offset` é text-base; o ELF só emite `.rela.text`, sem `.rela.rodata`; COFF dobra o offset rodata no patch-site em `.text`; wasm coloca rodata num data-segment ativo com offset conhecido em emit-time). Logo **TIER os agregados:** **Tier A** (flat-POD escalar/enum/bool, e `[]byte`/`str` com header montado no uso como os literais string) = blob rodata autocontido, SEM ponteiro interno → **ZERO backend**, entra na fase-feature (crumb 6: serializador de layout rodata + load tipado via `LGlobalAddr`/`LFieldAddr`/`LLoad`). Cobre os ~50 inteiros. **Tier B** (campo slice/ponteiro, ex.: descritores ABI `sysv64`/`aapcs64` com 8 campos `[]u32`) exige ponteiro DENTRO da rodata → reloc data→data inexistente → **QUEBRA "zero backend"**: vira fase de crumbs T-B1–T-B6 (3 encoders nativos ganham tag de seção no patch-site + writers ELF/Mach-O/COFF emitem reloc em seção de dados + wasm calcula/escreve offsets intra-data + motor legado resolve ponteiro interno). **Veredito "backend intocado?": SIM p/ Tier A / os ~50; NÃO p/ Tier B.** Nenhum dos ~50 anêmicos é Tier B.
- **D1 (gramática) — Tiers 0–5:** literal / cast `to` / unário-binário-bitwise / ref a outra const|enum|flags / literal agregado / **chamada a construtor puro allowlisted** (conjunto FECHADO: `teko::f64_from_bits`, `teko::f32_from_bits`, `preg`). **Alternativa preterida:** analisador de pureza transitivo (`const fn`) — maior superfície e risco, fora do escopo; o allowlist é minúsculo, determinístico e REVERSÍVEL (um `const fn` futuro o supersede).
- **D3–D5:** ordem por DAG de dependência + detecção de ciclo (DFS visiting-set, sem eval numérico — a substituição é AST, o backend computa `~(0 to u64)` como o fn antigo fazia); `pub const` serializa o **initializer typed** no `.tkb` (C7.16) e o consumidor **re-inlina** (sem símbolo de dado cross-módulo); const-eval vive em **novo `src/checker/consteval.tks`**.
- **Escopo enum/flags (dono #3):** famílias inteiras relacionadas viram `enum` (`block_type_*`, `wasm_scope_kind_*`, `elide_kind_*`, `edge_result_*`, `wasm_vt_*`, `zip_method_*`); bitmasks viram `flags` (ELF `SHF_*`/`ST_INFO`, Mach-O section attrs); file-magic vira `const`. **Invariante:** os BYTES emitidos ficam idênticos (wire via um único helper `_wire` dirigido por `match`); prova = goldens dos object-writers + fixpoint.
- **EXCEÇÃO justificada (permanece fn):** factories `*_empty()` que semeiam estado mutável fresco por-chamada (`empty_module`, `env_empty`, `memstore_new`, …) — NÃO são constantes; virar const compartilhada aliasa estado e quebra semântica de valor.
- **Reversibilidade:** alta — feature aditiva; o allowlist Tier-5 e o baseline-inline são trocáveis sem migração.

### D39 · Convenção W15 "no magic values" — const/enum/flags conforme o tier (dono 2026-07-15) ✅ RATIFICADA (com RULING 2)
- **Decisão:** todo literal com significado de domínio vira nomeado. Escalar → `const`; família de tags inteiros fechada → `enum`; bitmask OR de bits independentes → `flags`; agregado imutável relido → `const` agregado (rodata). Exceção: `0`/`1` identidade/passo e byte de opcode one-off numa tabela-encoder ISA documentada. Heurística de gate: literal não-trivial que aparece ≥2× OU codifica constante de formato externo (file magic, número de ABI, section flag) DEVE ser nomeado.
- **RULING 2 do dono — a varredura ENTRA em #594 por inteiro ("em meio ao código também, feito por inteiro aqui").** Retifica a proposta original (que reportava a varredura de encoders para cima). #594 entrega: (1) a feature; (2) os ~50 fns + famílias dos object-writers (file-magic/section-flags); (3) a **varredura file-by-file dos encoders ISA** `encode_x86_64.tks` (90) / `encode_arm64.tks` (66) / `stackify.tks` (109) + writers (crumbs S1–S6), CADA arquivo com **golden de bytes congelados + fixpoint gen2==gen3** como barra de aceitação (nenhum byte emitido muda). Sequenciada DEPOIS de a feature entrar no seed (crumbs 1–7).
- **Texto da regra (p/ colar em `.claude/agents/teko-canonicalizer.md` + `.claude/skills/w15-retrofit/SKILL.md`):** ver §7.3 do plano.
- **Reversibilidade:** a regra é convenção (skill/agent), não código; ajustável.

### D40 · `const` em TRÊS posicionamentos + membro-const estático + nomenclatura + bumps múltiplos (dono 2026-07-15, leu #594) ✅ RATIFICADA
- **Três posicionamentos (esclarecimento do dono a partir do corpo da issue):** `const` é polimórfico por posição — **local** (JÁ existe, `BindKind::Const` em `parse_stmt.tks:166`, INTOCADO: binding imutável de bloco, pode conter valor de runtime, tem escopo/arena), **módulo** (o que já estava desenhado), e **membro de tipo** (NOVO). Os três aceitam `pub`/`exp`/privado (`Visibility { Private; Pub; Exp }`). Módulo + membro compartilham **o mesmo `consteval.tks`** (`is_const_expr`, allowlist, ciclo/ordem) **e o mesmo inliner**; só mudam o SÍTIO de parse e o PATH de resolução. Reconciliação do local: mesmo keyword, distinguido puramente pela posição de parse (top-level/type-body → `ConstDecl` novo; statement → `Binding` local existente).
- **Membro-const (D6 do plano) — estático/nível-de-tipo, RATIFICADO law-first:** parse no corpo do `struct`/`class`/`trait` (peer de fields/methods, via `consts: []ConstDecl` novo em `StructBody`/`ClassBody`/`TraitBody`), reusando `parse_const_decl`. **NÃO ocupa slot no layout** (`LStructLayout` inalterado). Acesso por **`TypeName::NAME`** (qualificado, como membro de enum), ancorado em `type_path_expr` (`typer.tks:2283`) via `find_member_const` (caminha a cadeia de bases). **Herança:** um `pub`/`exp` membro-const É alcançável por `Sub::NAME` (lookup por nome sobe a cadeia de bases, como `effective_class_methods`) — mas NÃO é virtual/vtable (não há `this`). **Sombreamento (static hiding) PERMITIDO:** subclasse pode redeclarar `const NAME`; cada `T::NAME` resolve para a declaração mais próxima subindo de `T`, sem ambiguidade de dispatch (resolução é por path explícito + inline em comp-time). NÃO é `override` (nada dinâmico p/ sobrescrever; `override` continua só-método). **Interface REJEITA** membro-const (contratos puros de assinatura, sem statics); **classe abstrata ACEITA**; **trait DOBRA** no deriver como field. É exatamente a semântica de static-const de C#/Java, com ZERO máquina de dispatch nova. Visibilidade = igual a métodos.
- **Nomenclatura (dono): `UPPER_SNAKE_CASE` para constantes — CONVENÇÃO DE ESTILO, NÃO gramática.** A LINGUAGEM não impõe caixa: parser/checker aceitam qualquer identificador válido (minúsculo inclusive). `UPPER_SNAKE` (`ZIP_METHOD_STORE`, `ADLER32_MODULUS`, `SHF_ALLOC`) vale para const de módulo, membro e local-quando-é-constante-real, aplicado pelo canonicalizer/skill W15, **nunca pelo compilador** (restrição dura de implementação: NÃO adicionar checagem de caixa). Enums = `PascalCase` tipo + `PascalCase` membros conforme a convenção EXISTENTE do repo (`Visibility { Private; Pub; Exp }`, `MRegClass { GPR; FPR }`) → `enum BlockType { Stored; Fixed; Dynamic }` etc. Flags: membros seguem o nome do spec externo (C-ABI `SHF_ALLOC`, já UPPER_SNAKE).
- **Bumps de seed MÚLTIPLOS (dono):** não forçar um único bump ao fim da fase feature — o seed (`teko.tkp`) avança VÁRIAS vezes (bootstrap incremental). Pontos de bump capability-gating: **BUMP #1 após crumb 8** (3 posicionamentos + enum/flags + rodata Tier-A + codec no seed → migração 9–11 + varredura S* podem usar); **BUMP #2 rolling** (cada merge de migração/varredura taga `-beta` via D36); **BUMP #3 após T-B5** (capability data-reloc no seed → T-B6 migra agregados-com-ponteiro). Tier-B pode exigir >1 bump intermediário. Promoção lane→umbrella continua "o quanto antes"; os bumps internos disponibilizam cada incremento ao corpus.
- **Impacto na sequência:** +1 crumb (membro-const = crumb 7; codec vira 8; migração 9–11). Reversibilidade: alta (aditivo).

## 2026-07-16 — Conclusão do crumb 8 (validação e2e) + governança + SEED BUMP #1 (#594)

### D41 · Sub-sequência de conclusão do crumb 8 pré-SEED-BUMP + NO-DEFERRAL reforçado + memória=só-regras (dono 2026-07-16) ✅ RATIFICADA
- **Contexto:** um teko-reviewer achou 4 falhas no crumb 8 (codec `.tkb` + cross-module). Um teko-architect desenhou a sub-sequência de conclusão (`docs/design/const-crumb8-preseed-subsequence.md`); resolvidas TODAS in-wave antes do bump.
  - **c8b** — colisão silenciosa de const dep↔projeto: o inliner keava por NOME (first-match) enquanto o typer resolvia por last-match. Fix: keyar o inliner E o grafo de ordenação (`const_dep_order`) por `(name, namespace)` casando `TVar.func_ns` (via novo `ConstKey`) — fecha drop-silencioso + falso-ciclo por conflation cross-módulo. **Bug raiz mais fundo achado in-wave:** `collect_with_seed` (o pass-1 do caminho `with_deps`, que `checked_program_of` SEMPRE usa) não tinha o arm de `ConstDecl` → um `const` de PROJETO nunca era bindado → `func_ns` errado. Adicionado.
  - **c8c** — `type_path_expr` não resolvia valor qualificado por namespace (`m1::P`). Fix: novo `lookup_value_in_ns` + arm de namespace-valor (match ends-with, pois a ns real é `<projeto>::<dir>` e o qualificador é curto), generalizado p/ const E fn-value. Fecha #613.
  - **c8e** — nenhuma leg de CI asseria `EXPECT_COMPILE_FAIL` (o `diff_vm_native.sh` foi aposentado em #524). Novo `scripts/compile_fail_regressions.sh` como leg required. Fecha #610.
  - **c8d + c8d-fase2** — fixture E2E cross-module both-engine (bare + qualificado `m1::Q`) com provisionamento de dep `.tkl` real no harness (`crossmodule_regressions.sh`, sob `mktemp`/`rm`, nunca commitado).
  - **c8f (adição no-deferral) — runner de fixtures POSITIVAS (`scripts/positive_regressions.sh`, leg required):** expôs que **member-const NUNCA funcionou no build C real** (`find_member_const` carimbava `func_ns=""`; sob o keying `(name,ns)` do c8b não casava o hoist `type_type_member_consts` que usa `namespace=item_ns` → o símbolo `ns::Type::NAME` com `::` vazava como identificador C inválido — fix `func_ns=owner_ns`); + bug de codegen de vtable de `abstract class` latente (**#291**: `cg_pair_is_base_vtable` incluía o self-par de base abstrata → thunk de método abstrato sem stamp; fix `cg_class_is_abstract`); + 9 fixtures de const estruturalmente inválidas (declaravam module-level em `main.tks`, violando R-main → movidas p/ module files). Nada disso era pego pelo CI antigo.
- **Const provado e2e:** unit tests + positive-runner **9/9** + negativas assertadas + cross-module both-engine (bare+qualificado) + **fixpoint gen2==gen3 byte-idêntico**.
- **NO-DEFERRAL reforçado (dono, verbatim):** *"toda falha encontrada (mesmo antiga) deve ser resolvida imediatamente e nada pode ser deferido, mesmo que para corrigir tenha que implementar algo (planejado para o futuro) agora."* "Não bloqueia" NÃO é desculpa (é falha de design/desleixo). Se o fix precisa de peça futura (0.4/pós-1.0), a peça é ADIANTADA. Supersede "follow-up / workaround-em-vez-de-fix / completar-pós-alpha" em toda memória/skill/agente (aplicado no #614: dispatch skill + teko-implementer + digest `docs/memory`).
- **MEMÓRIA = SÓ REGRAS (governança, dono):** memória = conjuntos de regras/design-rulings/convenções; agente = regras + como-agir; skill = superpoderes (+regras); qualquer ACHADO a resolver NÃO vive em memória → valida se já feito → senão vira ISSUE. Gaps histórico-enterrados migrados p/ issues `bug`: **#616** (bare-name builtin shadowing), **#617** (`mem::free` 3 gaps), **#618** (generics `>=`), **#171** (mangle typedef, rotulada `bug`) — para a rodada de bugs pós-const. motor legado-`TAssign`-diverging-match e `#88`-field-assign-aninhado eram MOOT (motor legado aposentada #524; #88 implementado PR #130).
- **SEED BUMP #1:** `teko.tkp` `0.3.0.21 → 0.3.0.22-beta` (`v0.3.0.22-beta`) publica o seed compilador COM const → habilita a migração c9–c11 a USAR const no `src/`.
- **Reversibilidade:** alta (feature aditiva + fixes de correção; governança é convenção).

---

## 2026-07-24 — Drop the 128-bit family (`i128`/`u128`) + `f16`, R2+R3 (rejeição de superfície + deleção da topologia + tidies)

### D42 · Drop-128 R2 (rejeição de superfície + deleção da topologia PrimKind/LType) + R3 (tidies + B.38) ✅
- **Achado in-wave (não documentado no design doc, bloqueava a compilação — corrigido agora, sem deferral):** `scope.tks::builtin_fn` injetava os builtins reservados `div`/`rem`/`int_to_float` tipados sobre o carrier largo `PrimKind::I128` (FFI interna para `tk_div`/`tk_rem`/`tk_int_to_float` do runtime, herança da motor legado aposentada — #524). Nenhum call site do corpus usa esses builtins bare/sem-namespace (confirmado por varredura); native codegen nunca roteia `/`,`%` por eles (usa os helpers per-width `tk_div_<tag>`/`tk_mod_<tag>`). Deletados como código morto pós-motor legado-retirement (o guard `cg_is_arith_builtin_call` em codegen.tks também caiu, junto dos 3 arms de dispatch).
- **Sweep do corpus:** `examples/regressions/{repr_box,inline_attr_parse}` usavam `u128`/`i128` como veículo de teste ("payload ≥16 bytes sem niche") — como NENHUM escalar builtin sobra ≥16 bytes pós-drop, `repr_box` perdeu de vez o sub-caso "boxed SCALAR" (a cobertura "boxed STRUCT" via `Vec2` já prova o mesmo mecanismo; EXPECT_EXIT 116→19) e `inline_attr_parse` trocou o veículo `u128` por um struct `Pair` local de 16 bytes (mesma semântica de parse, EXPECT_EXIT inalterado, 47). `src/codegen/codegen_test.tkt` tinha 6 asserts diretos sobre `PrimKind::U128`/`"i128"` como builtin-scalar — corrigidos/invertidos. Duas dessas (`cgt_union_repr_class_dial`/`cgt_inline_attr_eligible_classes`) usavam o `u128 | null` como o veículo ≥16-byte-sem-niche do classificador de box-in-arena; a primeira troca tentada (`checker::Func`, tamanho fixo 16 sem precisar de decl registrado) **quebrou** `cgt_inline_attr_eligible_classes` (achado in-wave, corrigido sem deferral): `Func` não é mangleável (`cg_opt_mangle`'s `_ => error` — nem struct nem prim), então `cg_union_inline_recursive`'s fallback "os dois erraram → são iguais" (`cg_mangle_eq`) confundia o `Func` membro com a própria união (também não-mangleável), disparando um falso-positivo de auto-recursão. Corrigido trocando por um struct `Pair16` REAL (dois campos `i64`, registrado num `TProgram` de teste dedicado, `cgt_prog_with_pair16`) — mangleável, mesma semântica de tamanho/niche que `Vec2` já usa em `repr_box`. Nasceram 3 fixtures de rejeição: `reject_i128`, `reject_u128`, `reject_f16` (compile-fail, `EXPECT_STDERR` pino a mensagem honesta) — a primeira tentativa delas TAMBÉM falhou o pin: `resolve.tks::resolve_named` descartava silenciosamente QUALQUER erro de `builtin_type` e caía no "unknown type" genérico; corrigido para só cair no fallback quando a mensagem for exatamente o sentinel genérico ("not a built-in type"), propagando o honest-stop nos demais casos.
- **B.38 emendado:** `TEKO_LEGISLATION.md` (Redefinitions Index + a seção "native numeric type set") e novo `TEKO_HISTORY.md §B.40` registrando a decisão completa (was/is/why/agent-rule). `tooling/shared/src/spec_json.tks` NÃO carrega a lista de tipos (verificado — só keywords/operators/comment-delimiters), então não há nada a emendar lá; nenhum `MASTER_PLAN.md` existe no repo com uma entrada "drop-128" a marcar (verificado, `TEKO_MASTER_PLAN.md`'s WAVE 0.3 ROADMAP não cita a issue).
- **AR hygiene (escopo aditivo, mesmo vagão):** `objfile_ar.tks`'s writer GNU corrompia silenciosamente um member name >15 bytes (o campo `Name` de 16 bytes overflowava, desalinhando cada `ar_hdr` subsequente) — `ar_member_name_field`/`emit_ar_archive`/`emit_static_archive` agora retornam `| error` com honest-stop; `objfile_ar_coff.tks` JÁ tinha suporte a longnames (`"//"` extended table) — nenhuma mudança lá além do unwrap de tipo. `scripts/check_ar_elf.sh` (existia mas nunca rodava em CI) agora está wired em `native.yml`'s `ar-elf-macho-coff-validation` (renomeado de `ar-macho-coff-validation`), leg `linux-x86_64`/`format: elf`, espelhando os legs macho/coff já wired.
- **Ritual:** ver corpo do PR — GATE-G (seed build + `teko test .` + fixpoint gen1→gen2→gen3 byte-idêntico) após R2 e após R3, `compile_fail_regressions.sh`/`positive_regressions.sh`, differential `diff_c_own.sh` sem os KNOWN-STOP de i128.
- **Reversibilidade:** baixa por design — a issue existe para REMOVER a superfície permanentemente; reverter exigiria reintroduzir `PrimKind`/`LType` membros + toda a topologia deletada.
- **Adendo (ruling do owner, 2026-07-24) — wire `.tkb` FALHA ALTO, com bump de formato:** a deleção dos membros renumerou os ordinais de `PrimKind` no wire `.tkb`; um artefato pré-0.3.1 lido pelo compilador novo deserializaria TIPOS ERRADOS em silêncio. Ruling: um `.tkb` antigo deve **falhar ao compilar** contra o compilador atual (M.3) — estamos em beta, sem retroatividade para formatos que nunca saíram em STS/LTS. Implementação: `TKB_EXPR_VERSION` 1→2 e `TKB_PROGRAM_VERSION` 3→4 (consts nomeadas em `tkb_frame.tks`, D39), leitores em `tkb_read.tks` rejeitam versão diferente com mensagem honesta apontando a causa (ordinais aposentados) e a ação (rebuildar o artefato).

---


### D43 · CI light/full + fail-open do regressor + linkagem do seed de assert ✅
- **Contexto (proposta do dono 2026-07-24):** *"fazer com que o CI atual execute apenas em PRs que apontam para main e criar então outro CI para esse padrão mais leve que roda só o necessário"*. O modelo de entrega é um TREM EMPILHADO — cada vagão é um PR cuja base é a branch do vagão anterior, e o trem inteiro aterrissa numa integração única no vagão do topo, retargetado para `main`. O único run completo que importa é o da aterrissagem.
- **O eixo:** `full ⇔ github.base_ref == 'main'`; `light` para qualquer outra base. **Nunca `github.ref`** — num evento `pull_request` ele é `refs/pull/N/merge` e não casa nome de branch nenhum.
- **J1 — matriz dinâmica (`native.yml`):** o job `changes` passa a exportar `full` + `matrix_build`/`matrix_gen1` em JSON, consumidos por `fromJSON`; light = `linux-x86_64` (build-test) e `ubuntu-latest` (gen1-checks). Pré-requisito cumprido: o `install_deps` multi-linha saiu da matriz (shell dentro de JSON dentro de YAML) e virou um passo condicionado a `runner.os`.
- **J2 — dois jobs MORTOS, ressuscitados:** `mem-paranoid` e `asan-default` gateavam em `github.ref == 'refs/heads/main'` num workflow cujo único gatilho é `pull_request`. A condição **nunca casou**: ASan/UBSan/LSan e o oráculo `TEKO_MEM_PARANOID` **nunca rodaram em CI**, e o `Heavy sanitizer gate` que os agrega era vacuamente verde (confirmado ao vivo no PR #91: ambos `skipped`, gate `success`). Trocado para `base_ref`. O split aqui AUMENTA cobertura.
- **J3 — os gates afirmam o MODO, não a ausência de falha:** os quatro agregadores (`CI gate`, `Sanitizer gate`, `Heavy sanitizer gate`, `Test suite gate`) tratavam `skipped` como aprovado. Agora: job de caminho light exige `success` (`skipped` é ERRO — condição quebrada, não "nada a fazer"); job full-only exige `success` em full e `skipped` em light (um `success` inesperado avisa, não bloqueia); e o gate imprime `mode=light|full` sempre. Sem isto o split trocaria custo por cegueira.
- **Execução de alvo cross RESTAURADA (regressão de cobertura aberta pela dobra do regressor):** um job de execução emulada existia só para dirigir `scripts/diff_c_own.sh`, culado com os side-cars — a execução tinha ido a ZERO (o `release-cross-smoke` só cross-COMPILA). Restaurado sobre o EXECUTOR ÚNICO: os 15 cenários diferenciais de `regressor.tkr` viraram um cruzamento `Examples` de duas colunas `| backend | target |` (`c`/`own` × `host`/`<alvo-cross>`) — a gramática `.tkr` já é agnóstica a número de colunas (`tkr_table_cells` + `tkr_subst_row`; a Feature F6 já usa 5 colunas em produção). O cross-linker não precisou de código novo. (Revogado por D45: o alvo foi removido completamente.)
- **Sentinel `host` no `Given target` (código de produto):** para a linha HOST de uma tabela backend×target, `tkr_target_env` passa a NÃO exportar `TEKO_TARGET` (o default R1 derivado do host É o que uma variável não-setada significa; exportar `TEKO_TARGET=host` renderia o honest-stop R2 de target desconhecido em todo host). `tkr_effective_target` dobra o sentinel para `teko::os()`.
- **Probe de capacidade ANTES do build (código de produto):** um alvo cross sem toolchain no host surgia como falha de `cc` — indistinguível de regressão real de compilador — porque a resolução do run-wrapper só acontecia DEPOIS do build. `target_toolchain_skip_reason` (novo) probeia o cross-linker (via `cross_cc_for_target`, que delega às MESMAS `target_from_name`/`default_cc_for_target` que o build usa, então probe e build não podem divergir) e o run-wrapper antes de compilar.
- **Fail-open do runner de regressivo (M.3):** `regr_skip` devolvia `ok = true`, toda a contabilidade era `if !ok && !skipped`, e o resumo era `regressions {N} run, {F} failed` — **uma corrida inteiramente pulada imprimia "N run, 0 failed" e saía 0**. Agora `RegrOutcome` carrega `skips`, agregado por `regr_add_skips` na cadeia linha→cenário→feature→arquivo→run, e o resumo tem TRÊS colunas (`regr_summary_line`). `check_module_valid` passa a honrar `REGRESSION_REQUIRE_TOOLS` (o const documentava cobrir os leaf-tools e não cobria).
- **`REGRESSION_REQUIRE_TOOLS=1` numa lane de alvo cross, e SÓ nela:** o flag é fail-closed para TODA capacidade que o corpus declara, então só pode viver numa lane que provisiona todas elas. Era uma lane de cross-compilation (cross-gcc + emulador + **wasmtime**, que NENHUMA lane provisionava — o cenário `wasm32-wasi` da F7 nunca havia rodado). Ligá-lo em `tests.yml`, onde a toolchain legitimamente não existe, produziria vermelho falso; lá os pulos ficam honestos e agora VISÍVEIS na coluna nova. (Revogado por D45: a lane foi removida completamente.)
- **Diagnóstico cego de build dentro de cenário (M.3, achado no PR #91):** a saída do `cc` É capturada (o filho herda os streams redirecionados), mas a mensagem de falha citava só `last_line` do stderr — que é a linha do próprio compilador dizendo que o `cc` falhou. O log de CI não trazia uma linha do compilador C. `compile_failure_message` reproduz o TAIL capturado (`COMPILE_FAIL_TAIL_LINES`), e a falha agora passa pelo wrapper que carimba o NOME do cenário (antes só havia índices de path de scratch).
- **Seed de assert: WEAK não funciona em PE/COFF (a falha só-Windows do PR #91, raiz achada e corrigida):** GCC baixa uma DEFINIÇÃO weak para o símbolo real renomeado `.weak.<name>.` mais um weak external INDEFINIDO `<name>`, e o GNU ld para PE não resolve a referência forte de outro objeto contra isso. Reproduzido com `x86_64-w64-mingw32-gcc`, independente de ordem: `undefined reference to 'teko__assert__is_true'`. Ou seja: no Windows o seed estava MORTO e qualquer programa que CHAMASSE `teko::assert` sem definí-lo não linkava — o cenário `assert_native`, que a dobra do regressor fez rodar no Windows pela primeira vez na história do projeto. **Correção:** o PAPEL do seed passa a ser escolhido pelo BUILD — `build_cc_argv` define `TK_ASSERT_SEED_STRONG` exatamente quando o programa não declara `teko::assert` (`program_declares_assert_seed`), e aí as definições são fortes e resolvem em ELF, Mach-O e PE/COFF. **Weak segue o DEFAULT de propósito:** um seed já RELEASADO compila esta árvore sem passar o flag e linka `teko.c` + `assert.c` juntos — forte-por-default quebraria o bootstrap de todo seed existente com símbolo duplicado. Como o build do próprio compilador é o caso sem-flag, sua linha de link fica byte-inalterada e o FIXPOINT intacto. Os quatro casos (ordinário/corpus × ELF/PE) foram validados com `gcc` e `x86_64-w64-mingw32-gcc`.
- **Patches do mirror (`mirror-pr-to-org.yml`):** job `drain-cleanup` (fecha os PRs dos vagões e apaga as branches após a aterrissagem, guardado pelo SHA de embarque do bloco `<!-- train-manifest -->`; `continue-on-error: true` por ruling do dono) + strip do manifesto na fronteira de saída, para não vazar branches/SHAs internos no corpo do PR público da org.
- **`codeql.yml` NÃO foi gateado:** ele alimenta a regra de ruleset `code_scanning`, que exige uma análise concluída para o head do PR — pular deixaria o PR travado em "waiting for CodeQL". `sast.yml` já é filtrado por caminho a `src/runtime/**`/`src/assert/**` (clang-tidy sobre dois arquivos C) e ficou como está.
- **Reversibilidade:** alta para o split e os gates (só YAML); média para as mudanças de produto no runner de regressivo e no seed de assert (cobertas por testes `.tkt` novos).

---

## 2026-07-25 — `TEKO_TARGET` + deps por target no manifesto: crumbs C2, C4, C5, C6 (C3 fica para a .32)

### D44 · Cross-link honesto: `teko::arch()` landed-unused, chaves os-arch no `[extern.libs]`, relato de cross-emit, `--allow-undef` ✅
- **Contexto:** `docs/design/teko-target-crosslink-0.3.1.md` (decisões FECHADAS 2026-07-24: R1-R5 + o spec-fill §4.2). O C1 (default de host os-only) já entrou no vagão anterior e fechou o bug reproduzido (linux x86_64 emitindo Mach-O arm64). Este vagão entrega C2+C4+C5+C6.
- **C2 — builtin landed, chamado por NINGUÉM:** `tk_rt_arch()` em `src/runtime/teko_rt.{c,h}` (exceção maintained-C), espelhando a forma plain-`str` do `tk_rt_os` (sem lift, para o codegen congelado do seed conseguir baixar), tokens canônicos `x86_64`/`arm64`/`unknown` — a grafia que concatena direto com `tk_rt_os()` na chave `<arch>-<os>`. Wiring: `scope.tks::builtin_fn` (assinatura `(): str`) + o mapa de builtins do `codegen.tks`. Zero call sites no corpus (nem `.tkt`): é isso que mantém a escada em dois degraus. Verificado à mão num projeto scratch compilado pelo gen1 (`teko::arch()` → `"x86_64"`).
- **C4 — o §4.1 é RATIFICADO, não redesenhado:** a chave de seção `[extern.libs.<os-arch>]` já era capturada VERBATIM pelo parser (`sec_os = slice_from(nm, 12)`); o delta real é no LINK — `os_lib_key_matches` sobre `LinkTargetKeys` (`build_os`/`emit_os`/`os_arch`), a UMA regra de aplicabilidade agora compartilhada pela linha de link (`link_target_keys`) e pelos validadores M.3 (`target_link_keys`). **Segundo defeito achado e corrigido no crumb:** uma seção de OS puro era casada SÓ contra `target_os`, que responde o OS do HOST quando não há triple `[extern] target` — então um cross-link para `x86_64-windows` em linux descartava silenciosamente `[extern.libs.windows]`, contrariando o próprio "any arch of that OS" do §4.2. A regra passa a casar também o OS para o qual se LINKA; aditivo por construção (tudo que casava antes continua casando), e `target_os`/o pruning de `#os()` ficaram intocados de propósito (mexer neles muda a semântica de compilação condicional em builds cross — decisão separada). O modo `static:`/`shared:` deixou de ser descartado: `mf_extern_spec` devolve `ExternLibSpec { flag; mode }` para as colunas novas `link_mode`/`os_lib_mode`. **Bug achado e corrigido no crumb (sem deferral):** `mf_extern_flag` testava PATH antes de tirar o prefixo de modo, então a grafia ratificada `static:vendor/x86_64-linux/libfoo.a` vazava o literal `static:` na linha do `cc`. R4 implementado em `validate_static_libs_for_target`: para um target CROSS uma lib estática tem que apontar um arquivo LOCAL (o linker do host não sabe procurar nos caminhos do target), e um path explicitamente nomeado e ausente é erro em qualquer target (é a regra já ratificada da LEGISLATION:423, aplicada por target).
- **C5 — uma fonte de verdade, dois consumidores:** `CrossNote`/`cross_note` (+ `cross_note_for_name`, a porta por NOME, + `resolved_cross_note`). `teko run` e o gate de teste nativo imprimem a linha "emitido, não executado" e pulam o processo filho quando cross (antes: um `teko build` cross MORRIA no gate ao tentar exec de um binário de formato estrangeiro). O runner de regressivo consulta a MESMA `cross_note_for_name` em `tkr_row_run_verdict` — não uma segunda regra — e os leaf-checks (`Then object well-formed`) continuam rodando sobre a linha pulada, que é justamente o ponto de um build cross. Buraco pré-build do vagão 17 fechado: `host_cc_cannot_link_cross_reason` — um target cross SEM driver cross dedicado (`x86_64-linux` num host mac) chegava ao `ld` do host e falhava no formato do objeto, indistinguível de regressão de compilador; agora é skip honesto antes do build (e um `TEKO_CC` pinado levanta o skip).
- **C6 — o default FALHA, o blind é opt-in:** `--allow-undef` (nossa flag: `ALLOW_UNDEF_FLAG`, `allow_undef_of`/`allow_undef_selected`, pulada por `project_arg_of`, listada no `--help`), tradução transitória por formato (`allow_undef_cc_flag`), `omit_flag_for_blind_link` (um `shared:` que APONTA um path ausente é retirado da linha de link sob o opt-in — sem isso o `ld` falha no arquivo que falta e a flag é no-op), fail-loud por default (`validate_shared_libs_for_target`) e honest-stop NOMEADO para import-lib COFF (mesmo com a flag: PE/COFF não referencia DLL sem import lib, e o linker transitório não sintetiza uma — E1 fecha). **A linha ELF do §5.2 estava incompleta e foi corrigida no crumb:** `--allow-shlib-undefined` sozinho só governa símbolos indefinidos DENTRO de uma dependência compartilhada — um EXECUTÁVEL com símbolo indefinido continua falhando o `ld` (verificado com `cc` em linux/x86_64). A tradução ELF é o PAR `-Wl,--allow-shlib-undefined,--unresolved-symbols=ignore-all`; emitir só a flag ratificada teria entregue um opt-in no-op, que é capacidade fabricada — M.1/M.3 pesam mais que a lista de flags. Mach-O (`-undefined dynamic_lookup`) não precisou de correção.
- **Fixtures (corpus novo):** 5 cenários novos no `regressor.tkr` (247+16 → 252+16): dois no F5 (cross windows emitindo COFF com run pulado honestamente; o os-arch do host escrito explicitamente — o caminho reproduced-good) e uma Feature F8 nova com 3 compile-fail pinando o erro R2 (a frase, o valor ofensivo, um nome canônico da lista). Nenhum diretório novo em `examples/regressions/` (alvo do dono ≤10 `.tkr`). Os casos T6/T7/T8 do §7 do design NÃO são expressáveis como cenário `.tkr` — o manifesto de um snippet é SINTETIZADO e a gramática não tem um noun `Given manifest` — então viraram `#test` sobre os validadores, que é o que o próprio doc de formato prefere para diagnóstico. **Reportado (não é código faltando):** um noun `Given manifest` levaria T6/T7/T8 a cenário end-to-end; é mudança de FORMATO do regressor, fora do escopo deste design.
- **Higiene de diagnóstico:** `unsupported_target_error` (C1) trazia um `teko: ` próprio e toda saída passa por `fail`, que já prefixa `teko: <dir>: ` — a mensagem lia com prefixo duplo. Removido; as mensagens novas (R4/R5) nascem sem prefixo por essa razão.
- **Ritual:** gen1 pelo seed, `teko test .` (suíte + os 10 regressivos), fixpoint gen1→gen2→gen3 byte-idêntico (binário E `teko.c`), `TEKO_MEM_PARANOID=1` exit 0, auditoria W15 (`git diff … | grep '^+.*//'` vazio).
- **Reversibilidade:** média — C4/C5/C6 são aditivos e cobertos por `#test` + cenários; o C2 é uma adição de runtime que entra em TODO binário gerado (fixpoint reverificado byte-a-byte).

---

## 2026-08-19 — RT-L6 R3 ratificação: `capture_panic` backend intrinsic como lei-primeira da mitigação

### D45 · RT-L6 R3 CLOSED: `capture_panic(body: func<null>): str | null` ratificado como intrinsic de backend ✅
- **Contexto:** a migração C→Teko da camada L6 (harness de testes + assert + crash-handler, `migracao-runtime-c-para-teko-0.3.1.md`) tinha uma tensão genuína: `setjmp`/`longjmp` no harness não tem superfície Teko (R3). A recomendação lei-primeira era um intrinsic de backend (o próprio backend já pilota a stack no crash-handler; deixa zero resíduos C; inventa zero superfícies harness-only). A alternativa fallback era um mini-shim C de `setjmp` como último resíduo mensurável (aposentado quando o intrinsic ratificasse). **Sem ratificação, R3 era um HALT-parcial para o dono.**
- **Ratificação do dono (2026-08-19):** o intrinsic `capture_panic(body: func<null>): str | null` É a lei-primeira. O shim `setjmp` fallback NÃO é tomado. Implicações: (1) o backend ganha UMA invasão de intrinsic = `capture_panic` (stack unwind + PC restore) como primitivo do próprio compilador; (2) L6 migra 100% para Teko; (3) zero C novo (a manutenção C vai ao lixo em M3 via clean expurgo); (4) nenhuma superfície `:` rota em linguagem por harness.
- **Crumb 0064 (RT-L6) ratificado:** a assinatura e javadoc `capture_panic` mudaram de DRAFT para RATIFICADO; o título do crumb não carrega mais "owner gate"; a framing descarta o fallback setjmp; R3 marca CLOSED neste log.
- **Base constitucional:** lei-primeira (nenhuma lei violada; nenhuma alternativa sobrevive todas as 5 leis M.1–M.5); transparência (o backend já é autoridade de stack); S16-SYNC já entrou em produção, L6 é o last-measured-residue de C in-tree (RM-C9, M3); M.1 fail-loud (a captura de panic no harness é MANDATÓRIA pra não matar a suite; o intrinsic garante).
- **Reversibilidade:** baixa — uma invasão de backend é concreto. Se rejeitado, o crumb volta pra draft + fallback setjmp é tomado (diferença de magnitude de risco neste gate, não de tempo).
- **Timing / onda-relativa:** o crumb 0064 (RT-L6) bloqueia atrás de RT-L5 (task/names/coverage); a ratificação neste log desbloqueia M2 final para integração L6 (que chega com fixpoint-rebuild, não-ensinante).

---

## 2026-08-19 — Refined comment convention: export-gate + doc-comment size bound

### D46 · Comment convention refined: `/** */` only on `exp`, no `//`/`/* */`, size bound (owner 2026-08-19) ✅
- **Ruling:** o dono refinou a convenção de comentários a ser aplicada na próxima limpeza W15. A regra é uma CONVENÇÃO (W15 canonicalizer + reviewer executam; NÃO é lexer/parser/checker) com dois meio:
  1. **Export-gate:** `/** */` doc-comments são permitidos APENAS em declarações marcadas `exp` (export). Qualquer outro acessor (`pub` method, private/unmarked) NÃO carrega doc-comment — o canonicalizer DEMOTE (delete) qualquer `/** */` em site não-`exp`.
  2. **Sem `//` nem `/* */`:** inline `//` (mid-body/trailing) e block `/* */` são ambos deletados (proibidos 0%); se uma linha "precisa" comentário, extrair função bem-nomeada.
  3. **Size bound (novo):** um `/** */` nunca pode ser MAIOR que o código que documenta (judgment de reviewer, nenhuma fórmula line/char; the deciding factor is readability + ratio documento:assinatura).
- **Escopo da limpeza:** ~24 inline/block comments + ~881 doc-comments em sites não-`exp` demarcados para deletar em `src/`.
- **Base constitucional:** M.2 (PURO-TEKO) — a superfície exportada é exatamente `exp`; apenas ela leva narrativa assinada. CONVENTION (W15, reviewer+canonicalizer, não compiled):  o lexer/parser/checker NÃO muda; nenhum diagnóstico novo.
- **Ratificação:** a regra export-gate foi registrada em `docs/design/estado-doc2-campanha-limpeza-0.3.1.md:24`; o size-bound é novo (owner 2026-08-19). Ambas ratificadas aqui. REGRA law-first: W15 skill + canonicalizer agent documentam; crumbs herdam via TEMPLATE.md standing-law line.
- **Reversibilidade:** extremamente alta — é remoção de comentários (sem mudança de comportamento) + enfoque de reviewer.
- **Timing:** a canonicalization roda como W15 retrofit em lane-close (antes de bump); sem impacto no CI (documento-only, convention-enforcement). Reseed quando o canonicalizer processar o batch (`fix/retirement` ou lane posterior).
