---
section: design
created: 2026-08-13
status: DELIBERATION-PREP — §12 (libc-direct / compilação condicional / macro) AINDA NÃO deliberada com o
        dono. Este documento NÃO é um plano de crumbs — é o mapa do estado atual + o espaço de decisão +
        as FORKS que o dono precisa resolver, para que a deliberação aconteça parte-a-parte. Nenhuma
        decisão aqui está ratificada; nenhum código de produto foi tocado.
source: brief do dono ("§12 precisa de ajuda do arquiteto ANTES de deliberar"); leitura de src/ + docs.
companions: star-ref-and-ffi-0.3.1.md (§4 FFI own-backend), c-types-and-marshalling-0.3.1.md (§2.1 macro),
            mudancas-superficie-0.3.1.md (§5 marshall, §6 unsafe), nativo-sem-teko-rt.md +
            docs/memory/nativo-sem-teko-rt-mapa.md (retirada do teko_rt FFI).
---

# §12 — libc-direct / compilação condicional (`#if`/`#os`/`#arch`) / macro — PREP DE DELIBERAÇÃO

> **Para o dono.** Este documento existe para você **argumentar e definir parte-a-parte**. Ele NÃO
> propõe uma sequência de execução — propõe o **mapa** (o que já existe, com `arquivo:linha`), o
> **espaço de desenho**, e as **FORKS reais** (cada uma com opções + uma recomendação law-first que você
> pode aceitar, rejeitar ou emendar). Onde há tensão genuína, ela está marcada como OPEN QUESTION, não
> resolvida por mim. Depois da sua deliberação, viro isto num plano-secao12 executável (crumbs, tipos,
> fixtures, gates).

O cluster §12 tem **três eixos que se tocam mas são separáveis**:

1. **libc-direct** — chamar libc/OS diretamente (a costura FFI). Hoje toda chamada de sistema passa pela
   semente C `teko_rt.{c,h}`; o roadmap quer eventualmente **retirar essa costura**.
2. **compilação condicional** — `#os` (existe, end-to-end, com ZERO usos in-tree), `#arch` (**não
   existe**), `#if` geral (**não existe**).
3. **macro** — hoje **não há macro nenhuma**; a única "macro" desenhada no roadmap é a `extern macro fn`
   (o RESOLVEDOR de macros C do FFI), **não** metaprogramação de usuário (essa é pós-1.0, fora da LTS).

---

## 1. MAPA DO ESTADO ATUAL (com `arquivo:linha`)

### 1.1 libc-direct — a costura FFI como está hoje

**Fato central:** hoje **não há** "libc-direct". Toda superfície de sistema atravessa a semente C
`teko_rt.{c,h}`, alcançada por **nome-nu que o backend nativo resolve para um símbolo `tk_*`**.

| fato | onde | cite |
|---|---|---|
| A superfície FFI inteira mora em UM arquivo do backend nativo | 109 símbolos `tk_*` distintos, todos literais | `src/lir/lower.tks` (`native_builtin_symbol` ~`lower.tks:4235`; `mangle_fn_symbol` `lower.tks:885`) |
| `isel_*`/`objfile_*` são transporte agnóstico de símbolo | `isel_x86_64`/`objfile_coff` só citam `tk_` em doc-comment; `isel_arm64`/`objfile_elf`/`objfile_macho` = zero | `docs/memory/nativo-sem-teko-rt-mapa.md:8-12` |
| Os gêmeos Teko da lógica pura JÁ EXISTEM mas estão bypassados | `str_eq`, `str_contains`, `u64_to_str`, `fmt_*`, guardas de pânico | `src/runtime/teko_rt.tks` (`str_eq:496`, `str_contains:580`, `u64_to_str:133`) |
| `extern fn` já existe como binding estrangeiro (mas a resolução real de símbolo é do own-linker .33–.34) | `is_extern`/`c_symbol`/`from_lib` no `Function` | `src/parser/ast.tks:534-536` |
| Marshall (`ptr`/`uptr` opacos, `__wrap`/`__unwrap`) é a costura de valor desenhada (§5), ainda não implementada | ponteiro opaco por construção; sem aritmética | `mudancas-superficie-0.3.1.md:151-222`; `star-ref-and-ffi-0.3.1.md:141-145` |
| O checker não define cast `uptr<->u64` e não há aritmética de ponteiro | medido | `c-types-and-marshalling-0.3.1.md:102` |
| Alvos nativos | `Arm64Macho`, `Arm64Linux`, `X8664Linux`, `X8664Windows` | `src/build/project.tks` |

**Taxonomia do que a costura carrega hoje** (`docs/memory/nativo-sem-teko-rt-mapa.md:20-49`):
- **Camada 1 — piso syscall/libc (irredutível):** `write`/`fwrite`, `abort`/`exit`, `malloc`,
  `snprintf`/`strtod` + host surface (io/env/fs/process/time/crypto). `teko_rt.c:2311-2410`, `1507`,
  `416`. **É a semente C mantida — a Lei a mantém** (exceção Teko-only para `teko_rt.{c,h}`).
- **Camada 2 — memória crua (bloqueada por 2 lacunas de linguagem):** `tk_region_*`/`tk_arena_*`/
  `tk_slice_*`/`tk_mem_copy` — só viram Teko quando existir `teko::mem::load_u64`/`store_u64` + cast
  `u64->ptr` (`docs/design/arena-em-teko.md:62,67`).
- **Camada 3 — lógica pura com gêmeo Teko pronto:** comparação/hash/format de bytes — religar é fiação,
  não reescrita.

**O que "libc-direct" ADICIONARIA/SUBSTITUIRIA** (o desenho own-backend-first já existe em
`star-ref-and-ffi-0.3.1.md` §4, mas NÃO ratificado): `extern fn`/`extern from lib` resolvidos pelo
**own-linker** contra `.a`/`.o`/`.so` **sem `cc`** (§4.5, `star-ref-and-ffi:228-243`); varargs por uma
**ABI own-backend por-alvo** (§4.3); layout `#repr("c")` próprio (§4.4); `errno` via `teko_rt` TLS
(`star-ref-and-ffi:226`).

**Intenção de roadmap — REMOVER a costura `teko_rt` FFI.** `docs/design/nativo-sem-teko-rt.md` +
`docs/memory/nativo-sem-teko-rt-mapa.md`: mover a fronteira FFI **para baixo** (religar nome-nu ao
mangle do gêmeo Teko, Camada 3 já) até restar só o piso syscall nomeado (Camada 1). **Trava explícita:
nada se implementa até `teko` ser 100% nativo e o gate nativo estar de pé** (`nativo-sem-teko-rt-mapa.md:51-57`).

**O "bug nativo #1" tagueado a esse passo.** O bug de miscompilação nativo standing é o **#112** (o
rep-flip do fixpoint nativo `gen2==gen3`) — `docs/design/n112.0-crash-diagnosis-0.3.1.md`,
`native-slice-str-rep-separation-0.3.1.md:204-215`, `f3-array-cap-len-plan-0.3.1.md:117`. A família de
paragens honestas nativas usa a convenção `native backend N1: … not yet lowered` (não é "bug #1", é o
prefixo `N1` de honest-stop; ex. `docs/memory/0.3.1.0-linux-native-first-stop.md`). **OPEN QUESTION Q7:
confirmar com o dono se "bug #1" = #112 (fixpoint rep) ou uma outra entrada.** Eu não localizei um
artefato numerado literalmente "bug #1"; tratei-o como #112 abaixo e sinalizo.

### 1.2 Compilação condicional — `#os` (existe), `#arch` (não), `#if` (não)

**`#os("…")` — implementado end-to-end, com ZERO usos in-tree.**

| etapa | onde |
|---|---|
| parse do atributo `#os("…")` (só string entre parênteses) | `src/parser/parse_decl.tks:1204-1209` |
| rejeição: `#os` só precede **função** (nunca `type`) | `src/parser/parse_decl.tks:1531` ("`#os(\"…\")` may only precede a function") |
| mensagem de atributo desconhecido (lista o que é reconhecido) | `src/parser/parse_decl.tks:1219` |
| campo `os_guard: str` no `Function` (AST) | `src/parser/ast.tks:536` |
| campo `os_guard` no `ParsedAttributes` | `src/parser/result.tks:31` |
| **poda**: `prune_os` remove toda função cujo guard ≠ target OS; `""` sempre passa | `src/build/project.tks:118-133` |
| seleção do target OS (triple `[extern] target`, senão `teko::os()`) | `src/build/project.tks:107-114` (`target_os`) |
| chamada da poda no front-end | `src/build/project.tks:465-466` |
| **serialização .tkb** (o guard atravessa o formato de dep) | `src/emit/tkb_write.tks:537`, `tkb_frame.tks:519`, `src/emit/tkb_read.tks` |
| threading por checker/synth/collect/resolve | `src/checker/{synth,collect,resolve}.tks` (todos referenciam `os_guard`) |

**Usos in-tree de `#os`: ZERO.** A única citação em `.tks` fora do compilador é uma probe
(`examples/probes/chan_dgram/src/chan_dgram.tks`) e o staged `staged/c_types/c_types.tks`. **Ou seja: a
maquinaria está completa e inteiramente inexercitada por produção.** Isso é uma FORK em si (§2.5).

**`#arch` — NÃO EXISTE.** Nenhum parse, nenhum campo, nenhuma poda. Os alvos já distinguem arquitetura
(`Arm64Linux` vs `X8664Linux`, `src/build/project.tks`) e há `target_name`/`target_os_name`
(`project.tks:2977-2988`) que separam o eixo os×arch, mas **não há guard de arquitetura na superfície**.

**`#if` geral — NÃO EXISTE.** Não há atributo condicional com predicado. `#os` é o único condicional, e é
por **igualdade de string de OS**, não por expressão. `#arena_size(N)` e `#test` são os outros atributos
(`parse_decl.tks:1201-1215`), nenhum condicional.

### 1.3 Macro — não existe; a única "macro" é a `extern macro fn` do FFI

| fato | cite |
|---|---|
| `macro` não aparece no lexer, parser ou AST | `c-types-and-marshalling-0.3.1.md:94,106` (zero hits) |
| Metaprogramação (comptime geral / macros de usuário / manipulação de AST) = **pós-1.0, fora da LTS** | `DECISION_LOG.md:320`; `docs/memory/teko-laws-digest.md:85` |
| Lei do preprocessor: `#define` só para guard, **nunca** metaprogramação ("Teko has no macros") | `TEKO_LEGISLATION.md:606-608` |
| Sem extensão sintática/semântica de usuário via macro | `REBOOT_PLAN.md:880-881,1092-1094` |
| A ÚNICA "macro" desenhada: `extern macro fn N(p): R = "M" from header "h"` — RESOLVEDOR de macros C do FFI (Tiers 0–3), NÃO metaprogramação | `star-ref-and-ffi-0.3.1.md:147-194` |

**Portanto "macro" em §12 é ambíguo e a primeira coisa a desambiguar com o dono (Q1).** Há duas leituras
completamente diferentes:
- **(a) `extern macro fn`** — bindar uma macro do preprocessor C (`O_RDONLY`, `htonl`) como função
  tipada, resolvida pelo **resolvedor teko-native** sem `cc` (`star-ref-and-ffi-0.3.1.md:147-194`,
  Tiers 0–3). Isto é **FFI**, coeso com libc-direct, e já tem desenho.
- **(b) macro de usuário / metaprogramação** — expressamente **fora da LTS** por lei
  (`DECISION_LOG.md:320`). Se §12 quisesse isto, colidiria com uma decisão selada.

Minha leitura: §12 quase certamente significa **(a)** — o cluster inteiro (libc-direct + condicional +
macro) é a **superfície FFI/portabilidade**. Confirmar em Q1.

---

## 2. AS FORKS — o que o dono precisa decidir (parte-a-parte)

### FORK A — o que "libc-direct" SIGNIFICA (o escopo da costura)

O termo comporta três âmbitos crescentes:

- **A1 — só religar (mover a fronteira para baixo).** Manter `extern fn`→`teko_rt`, mas religar a
  Camada 3 (lógica pura) aos gêmeos Teko que já existem, deixando só o piso syscall nomeado. É o que
  `nativo-sem-teko-rt.md` já desenha. **Não** adiciona superfície nova de usuário.
- **A2 — `extern fn` direto a símbolo de libc/OS.** O usuário escreve `extern fn c_open(...): i32 =
  "open" from lib "c"` e o **own-linker** resolve contra `libc.so`/`.a` sem `cc`
  (`star-ref-and-ffi:228-243`). É "libc-direct" no sentido forte: código Teko chamando `open`/`read`/
  `socket` diretamente. Depende do **own-linker (.33–.34)** para resolução de símbolo.
- **A3 — híbrido (recomendado).** A1 agora (é fiação, sem superfície nova, e paga o roadmap de retirada
  do `teko_rt`), e A2 como a **superfície ratificada** cujo codegen (refs de símbolo indefinido +
  relocs) é own-native e emite já, mas cuja **resolução** acopla o own-linker. É exatamente o split que
  `star-ref-and-ffi-0.3.1.md:236-243` já propõe.

**Recomendação:** **A3.** Ela respeita a lei "own-backend-first, nada depende de `cc`"
(`star-ref-and-ffi:60-64`) e a lei da semente C (o piso `teko_rt.{c,h}` fica, é o que a exceção Teko-only
permite). **Tensão a decidir na deliberação:** A2 introduz a pergunta "quem escreve os bindings de libc?"
(§2, OPEN QUESTION Q2) — se o usuário, então precisa de `#os`/`#arch` para variar por plataforma, o que
**liga a Fork A à Fork B**.

### FORK B — o escopo da compilação condicional (`#os` hoje só função)

Hoje `#os` guarda **só funções** (`parse_decl.tks:1531`). Para libc-direct real, um `extern fn c_stat`
tem assinatura/símbolo diferente por OS — e um **tipo** `#repr("c")` de layout de sistema (ex. `struct
stat`) também varia por plataforma, o que **hoje é inexpressável** (`c-types-and-marshalling-0.3.1.md:112-115`).

Sub-forks:

- **B1 — alargar o alvo de `#os`.** Opções: (i) manter só-função; (ii) permitir `#os` antes de `type`;
  (iii) permitir `#os` antes de qualquer item top-level (fn/type/const/extern-block). **Recomendação:
  (iii)** — o custo é uniformizar `prune_os` para casar sobre `Item` em vez de só o arm `Function`
  (`project.tks:126` já tem a forma; falta os arms de `Type`/`Const`/extern-block). Sem isso, libc-direct
  com tipos de sistema por-plataforma não fecha.
- **B2 — adicionar `#arch`.** Hoje ausente. Opções: (i) não adicionar (variar por os só); (ii) adicionar
  `#arch("x86_64"|"arm64")` simétrico a `#os`, com um `prune_arch` gêmeo e um `target_arch` derivado do
  alvo (a informação já existe em `target_name`, `project.tks:2988`). **Recomendação: (ii)** — os alvos
  já são os×arch (`Arm64Linux`≠`X8664Linux`); sem `#arch` o usuário não pode escolher entre um `htonl`
  intrínseco por-arch nem um layout ABI por-arch. Baixo custo, simétrico ao `#os` existente.
- **B3 — `#if` geral com predicado.** Em vez de dois atributos de igualdade (`#os`/`#arch`), um
  condicional único `#if(<pred const>)` onde `<pred>` é uma expressão comp-time sobre variáveis de
  build (`os == "linux" && arch == "arm64"`, feature flags, `#if(target_has("sse2"))`). Opções: (i)
  **não** — manter `#os`/`#arch` atômicos, simples, já-quase-prontos; (ii) **sim, e `#os`/`#arch` viram
  açúcar** de `#if(os==…)` / `#if(arch==…)`. **Recomendação: (i) por ora, mas ver Fork C** — `#if` geral
  precisa de um **avaliador de expressão const** no estágio de poda (que hoje não existe ali; a poda é
  igualdade de string em `project.tks:126`), e reabre o eixo comptime que a LTS quer manter fechado.
  `#if` geral é a porta de entrada de metaprogramação-por-condicional; law-first, a versão atômica
  (`#os`/`#arch`) entrega 95% do valor sem essa porta.

**Recomendação da Fork B (composta):** **B1(iii) + B2(ii) + B3(i)** — alargar `#os` a todos os itens,
adicionar `#arch` simétrico, e **adiar** `#if` geral. Isto mantém a poda como igualdade-de-string (barata,
já existente), cobre libc-direct por-plataforma (os×arch em fn E type), e não abre o eixo comptime.

### FORK C — `#os`/`#arch` são casos especiais de um futuro `#if`?

Mesmo que B3 adie `#if` geral, a **forma sintática** de `#os`/`#arch` deve ser escolhida agora para não
pintar o canto:

- **C1 — atributos distintos** (`#os("linux")`, `#arch("arm64")`), combinados por **conjunção implícita
  quando empilhados** (`#os("linux")` + `#arch("arm64")` numa mesma decl = linux-E-arm64). Simples,
  casa com a maquinaria de laço de atributos já existente (`parse_decl.tks:1195-1221` já itera múltiplos
  `#…`). **Sem** disjunção/negação.
- **C2 — `#os` com lista** (`#os("linux","macos")` = linux-OU-macos), para o caso comum "POSIX-like". Um
  passo de expressividade sem virar `#if`.
- **C3 — `#if(os == "linux" || os == "macos")`** desde já, aceitando o custo do avaliador.

**Recomendação:** **C1 agora, com C2 como emenda barata se o dono quiser o "OU" de OS.** C1 é
forward-compatible: se `#if` geral chegar pós-1.0, `#os("x")` reduz a `#if(os=="x")` sem quebrar corpus
(há ZERO usos hoje — reescrita trivial). **OPEN QUESTION Q3:** o dono quer o "OU" (C2) já, ou a conjunção
de atributos empilhados (C1) basta?

### FORK D — `extern macro` (o resolvedor de macros C): ratificar Tiers e o honest-stop

Assumindo Q1 = leitura (a), a decisão é **quais Tiers do resolvedor** entram e onde para o honest-stop
(`star-ref-and-ffi-0.3.1.md:167-194`):

- **Tier 0 (const/flag object-like** — `O_RDONLY`, `SOCK_STREAM`): avaliador de constante C, valor
  inlined. **É o caso mais comum e o mais barato; own-native, zero runtime.**
- **Tier 1 (alias de símbolo** — `#define htonl(x) __bswap_32(x)`): rebind para `extern fn` real —
  acopla own-linker.
- **Tier 2 (expansão de corpo simples** — bit-twiddle aritmético): tradutor C-expr→IR own-backend,
  **hard-bound** a aritmético/bitwise/shift/relacional/ternário/cast-para-int.
- **Tier 3 (macro C arbitrária** — statements, `##`, `#`, efeitos): **HONEST ERROR**, o único stop.

Opções: (D1) só Tier 0 na primeira janela (constantes/flags — cobre a maioria de libc); (D2) Tiers 0–2
own-native + Tier 3 honest-stop; (D3) rejeitar `extern macro` inteiro e exigir que o dev escreva o valor
à mão (`const O_RDONLY = 0` etc.). **Recomendação: D2 como destino, D1 como o primeiro incremento** — é
exatamente o sequenciamento de `star-ref-and-ffi:284-285`. **A tensão real:** o resolvedor é um
**subsistema de verdade** (tokenizer de header C + extrator de `#define` + avaliador de constante + mini
tradutor C-expr→IR). É bounded, mas não trivial (T5 em `star-ref-and-ffi:398-402`). **OPEN QUESTION Q4:**
o dono aceita esse subsistema (D2), ou prefere começar por D1 (só constantes) e reavaliar? **OPEN
QUESTION Q5:** a palavra `macro` na superfície — o dono quer o keyword `extern macro fn`, ou uma grafia
que não use "macro" (dado que a Lei diz "Teko has no macros")? Há tensão de vocabulário aqui (§3, T-Voc).

### FORK E — estágio da poda × interação com deps (.tkb) e seleção de alvo

Hoje `prune_os` roda no front-end **depois** do parse e **antes** do check (`project.tks:465`), e o
`os_guard` é **serializado no .tkb** (`tkb_write.tks:537`) — logo uma dep entrega itens já-guardados que
o consumidor poda com o **seu** target OS. Decisões:

- **E1 — a poda continua no front-end (source-level, pré-check)?** Isso significa que um item guardado
  para OUTRO os **nunca é checado** (não precisa nem compilar no host atual). Alternativa: checar tudo e
  podar no lowering. **Recomendação: manter pré-check** (é o que existe, e é o que permite um `extern fn`
  linux-only referir um símbolo que não existe no host de build). `#arch` deve entrar no **mesmo** ponto.
- **E2 — a chave de poda para deps.** Uma dep compilada para linux e consumida em macos: o `.tkb` carrega
  guards de AMBOS? Hoje o `.tkb` guarda o `os_guard` string (`tkb_frame.tks:519`), então sim — a poda é
  do consumidor. **Confirmar** que `#arch` entra na mesma serialização (um campo `arch_guard` gêmeo). Se
  não, uma dep cross-arch quebra. **OPEN QUESTION Q6:** deps são distribuídas em source (.tkb com todos
  os guards, poda no consumidor) ou pré-podadas por-alvo? O modelo atual é o primeiro; ratificar.

### FORK F — os ZERO usos de `#os` (a maquinaria inexercitada)

`#os` está completo mas **nunca exercitado por produção** (§1.2). Isso é uma decisão latente:

- **F1 — a primeira aplicação real de `#os` é a própria libc-direct.** Quando `extern fn` por-plataforma
  entrar, `#os`/`#arch` ganham o primeiro uso de verdade. Recomendo **casar a fixture de `#os`/`#arch`
  com a primeira libc-direct** (ex. `extern fn` de `write` linux vs `WriteFile` windows) — assim a
  maquinaria condicional sai do "falso-verde" (compila mas nada usa) no mesmo movimento.
- **F2 — auditar a poda contra o serializer AGORA.** Como `#os` nunca rodou com um item realmente
  removido num build multi-plataforma, há risco de bug latente na interação poda×.tkb×check (um item
  podado que uma dep ainda referencia). É uma verificação de leitura, não código. **Recomendação:** parte
  do plano de execução, não da deliberação.

---

## 3. TENSÕES DE LEI / CONSTITUIÇÃO (resolvidas law-first onde possível)

- **T-Voc [MÉDIA] — "macro" na superfície vs "Teko has no macros" (`TEKO_LEGISLATION.md:607`).** A Lei
  nega **macros de usuário/metaprogramação**; a `extern macro fn` é bindar uma macro **estrangeira** (C),
  não criar uma. São coisas diferentes, mas o **vocabulário colide**. Resolução law-first proposta: ou
  (i) manter `extern macro` (é honesto: descreve o que a coisa estrangeira É), documentando que "macro"
  aqui é sempre `extern`; ou (ii) escolher outra grafia (`extern const from header`, `extern cmacro`).
  **Não resolvo sozinho — é escolha de vocabulário do dono (Q5).**
- **T-LTS [ALTA] — `#if` geral reabre o eixo comptime que a LTS fechou (`DECISION_LOG.md:320`).** Um
  `#if(<expr const>)` precisa avaliar expressões em comp-time no estágio de poda; isso é o degrau de
  comptime que a LTS adia para pós-1.0. Resolução law-first: **adiar `#if` geral** (Fork B3(i)); `#os`/
  `#arch` por igualdade-de-string **não** são comptime (é match de string no build), logo **não** violam
  a Lei. Passa-todas-as-Leis → recomendação firme, não HALT.
- **T-Cc [CRÍTICA, já resolvida no desenho FFI] — nada pode depender de `cc`
  (`star-ref-and-ffi:60-64`).** libc-direct A2 e `extern macro` Tier 1 acoplam o **own-linker
  (.33–.34)**, não `cc`. O split "superfície+refs own-native agora / resolução no own-linker depois"
  (`star-ref-and-ffi:236-243`) já respeita isto. Sem HALT.
- **T-Seed [MÉDIA] — a semente C `teko_rt.{c,h}` é FROZEN, exceto o piso mantido.** libc-direct A1
  (retirar a costura) **não pode** deletar o piso syscall (Camada 1) — a exceção Teko-only mantém
  `teko_rt.{c,h}` como semente mínima (`teko-laws-digest.md:11-13`). A meta é "piso mínimo nomeado", não
  "zero `tk_*`" (`nativo-sem-teko-rt-mapa.md:48`). Sem tensão se A1 respeitar isso.
- **T-Native [ALTA, dependência] — a retirada do `teko_rt` FFI está TRAVADA até o gate nativo estar de
  pé.** `nativo-sem-teko-rt-mapa.md:51-57`: nada se religa até `teko` ser 100% nativo e o gate nativo/tdb
  verde; o **#112** (fixpoint rep, o "bug nativo #1") está aberto. Logo **libc-direct A1 é DESIGN-AHEAD
  hoje, bloqueado por #112 + gate nativo.** libc-direct A2 (superfície `extern fn`) NÃO está bloqueado —
  a superfície e os refs de símbolo são own-native e podem ser desenhados/definidos já; só a *resolução*
  espera o own-linker. **Isto separa o que se pode deliberar-e-fechar agora (superfície: Forks B, C, D,
  E) do que é design-ahead-bloqueado (A1 religação; A2 resolução).**

**Nenhuma tensão exige HALT.** Todas ou têm resolução law-first (adiar `#if`; own-linker não `cc`;
piso mantido) ou são escolha de dono explicitada como OPEN QUESTION (vocabulário macro; escopo Tiers;
"OU" de OS). Este é um doc de prep — a deliberação é do dono.

---

## 4. DEPENDÊNCIAS E INTERAÇÕES (a foto de acoplamento)

```
#os (existe, 0 usos) ──┐
#arch (não existe)  ───┼──► compilação condicional POR-PLATAFORMA
                       │        │
                       │        └── é o que faz libc-direct (A2) variar extern fn/type por os×arch
                       │
extern fn / extern from lib (superfície) ──► own-native AGORA (refs+relocs)
                                             resolução de símbolo ──► OWN-LINKER (.33–.34)   [não cc]
                       │
extern macro fn (Tiers 0–3) ──► Tier 0 own-native agora · Tier 1 own-linker · Tier 3 honest-stop
                       │
teko_rt FFI (costura tk_*) ──► RETIRADA (A1) BLOQUEADA por: gate nativo verde + #112 (bug nativo #1)
                                piso syscall (Camada 1) FICA — semente Teko-only mantida
```

**Regra de sequência-de-semente:** o corpus não pode USAR uma feature ainda ausente na semente. `#os`
já está na semente (parseia); `#arch`/`extern macro`/`extern fn`-direto são superfície NOVA — entram na
janela **aditiva** (aceitas ao lado, corpus na grafia velha) antes de qualquer sweep, como toda a onda
0.3.1 (`mudancas-superficie-0.3.1.md:18-21`).

---

## 5. OPEN QUESTIONS PARA O DONO (a agenda da deliberação)

1. **Q1 — desambiguar "macro" em §12:** é (a) `extern macro fn` (resolvedor de macro C do FFI), ou (b)
   macro de usuário/metaprogramação? A Lei sela (b) como pós-1.0 (`DECISION_LOG.md:320`). Minha leitura:
   (a). Confirmar.
2. **Q2 — escopo de libc-direct (Fork A):** A1 (só religar, baixar a fronteira), A2 (`extern fn` direto a
   símbolo de libc via own-linker), ou A3 (híbrido, recomendado)? E: quem escreve os bindings de libc — o
   usuário (então precisa Fork B) ou uma stdlib teko::sys curada?
3. **Q3 — expressividade condicional (Forks B/C):** alargar `#os` a todos os itens (B1)? adicionar
   `#arch` (B2)? adiar `#if` geral (B3, recomendado)? e a forma: atributos-conjunção (C1), `#os` com lista
   "OU" (C2), ou `#if` já (C3)?
4. **Q4 — Tiers do `extern macro` (Fork D):** só Tier 0 (constantes/flags) primeiro (D1), ou o subsistema
   Tiers 0–2 own-native + Tier 3 honest-stop (D2)? Aceita o custo do tokenizer/avaliador de header C?
5. **Q5 — vocabulário (T-Voc):** manter a grafia `extern macro fn`, ou trocar para não usar "macro" (a
   Lei diz "Teko has no macros")?
6. **Q6 — distribuição de deps (Fork E):** deps em source com todos os guards no .tkb + poda no consumidor
   (modelo atual), ou pré-podadas por-alvo? E: `#arch` ganha um `arch_guard` gêmeo na serialização .tkb?
7. **Q7 — confirmar "bug nativo #1":** é o **#112** (rep-flip do fixpoint nativo) que tagueia a retirada
   do `teko_rt`, ou outra entrada? Não localizei um artefato literalmente numerado "bug #1".

---

## 6. O QUE JÁ SE PODE FECHAR HOJE vs O QUE É DESIGN-AHEAD-BLOQUEADO

**Deliberável e fechável agora (superfície, own-native, não depende de `cc` nem do own-linker):**
- Forks B (escopo condicional: `#os` a todos os itens, `#arch`, adiar `#if`), C (forma), D-Tier0
  (constantes), E (estágio de poda + .tkb), F1 (casar fixture de `#os`/`#arch` com a primeira
  libc-direct). Estes são decisões de superfície que o parser/checker/poda expressam sem tocar backend.

**Design-ahead, bloqueado (não fechar sem a dependência):**
- **libc-direct A1 (religar a Camada 3 / retirar a costura):** BLOQUEADO por gate nativo verde + #112
  (`nativo-sem-teko-rt-mapa.md:51-57`). Desenho pronto em `nativo-sem-teko-rt.md`.
- **libc-direct A2 resolução de símbolo + `extern macro` Tier 1:** acopla o **own-linker (.33–.34)**. A
  *superfície* (grafia `extern fn`/`extern from lib`, refs de símbolo indefinido) é own-native e
  deliberável já; só a *resolução* espera.
- **`extern macro` Tiers 1–2 codegen:** acopla a maturação da expansão de IR own-backend
  (`star-ref-and-ffi:337-338`).

---

*Design-ahead, doc-only. Nenhum código de produto tocado. `teko test` NÃO foi executado (leak de
monomorph que derruba o container). O dono delibera parte-a-parte; depois disto viro num plano-secao12
executável (crumbs/tipos/fixtures/gates).*
