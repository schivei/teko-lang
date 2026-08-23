# Terminal native — `.tkb`/`.tkh` por-namespace + linker interno (0.3.1)

Base de leitura: `docs/design/reducao-memoria-arrays-0.3.1.md` (§6bis Eixo C e os parágrafos
do "terminal native") + crumbs `RM-C10`..`RM-C17`. Autor: arquiteto (SÓ design; não toca
código de produto). Este documento **dá forma** ao terminal native que o dono descreveu,
**amarrado ao Eixo C já ratificado** — não é um fork nem uma re-deliberação: é o
preenchimento do terminal de um pipeline que já está desenhado. Onde o Eixo C já decidiu,
aqui só se **cita e estende**.

> **DESIGN-AHEAD (a regra "adiantar o que der").** A rota native é a **última perna** do
> programa 0.3.1 — está **gated no marco de memória** (Eixos A/B entregam a meta ≤1,5 GB nas
> DUAS rotas; a rota native é o *endgame* arquitetural, não o que fecha a meta). Os crumbs
> `RM-C15`/`C16`/`C17` são **M4** e dependem das pernas NAT (encoders ELF/Mach-O/COFF,
> aarch64, resolução de target) já enfileiradas. Portanto este doc é **desenho adiantado**:
> fixa contratos, sabores de artefato, mapeamentos e pontos de decisão HOJE, para o
> implementer retomar em minutos quando as pernas NAT e o Eixo C (C10–C13) fecharem verde. O
> que permanece BLOQUEADO está marcado em §11.

---

## 1. Visão — o arbusto vira per-namespace `.tkb`+`.tkh` + linker interno

Hoje a build é whole-program e termina em UM `teko.c` que o `cc` compila. O dono corrige o
endgame: **`teko.c` é muleta**; a linguagem tem que emitir seu **próprio objeto linkável**.
A rota native é a **única a se beneficiar de `.tkb`** — porque native é compilação separada
clássica, e `.tkb`/`.tkh` são exatamente o par "objeto intermediário + header de símbolos"
que a compilação separada precisa.

O "arbusto" (o programa inteiro como uma árvore densa de namespaces com recursão mútua) é
**podado em galhos independentes**: ao ir para native, o compilador emite, **POR NAMESPACE**,
um `.tkb` intermediário (o typed/lowered daquela unidade) + o seu `.tkh` de link (a interface
de símbolos daquela unidade). Um **linker próprio** — que conhece SÓ os NOSSOS objetos, como
um IR — associa os símbolos entre unidades (a FFI interna de `RM-C11`) e roda os passos de
pipeline que faltam **na ponta** (monomorfização + isel/regalloc/encode do alvo). A saída
final depende do destino (package/lib/binary — §5).

Este arranjo dá **o mesmo ganho que C já tem** (gera `.o`, depois linka) — só que **o linker
é nosso**, opera sobre o nosso IR/`.tkb`, e conhece a semântica Teko (visibilidade,
monomorfização pendente, ABI). É esse conhecimento que habilita os dois grandes ganhos
seguintes: **paralelização do build** (§6) e **build incremental** (§7).

O terminal native NÃO é um novo pipeline: é a **realização native da saída-de-unidade
abstrata de `RM-C12`** (`UnitOutput`). Na rota C, a saída-de-unidade é um pedaço de texto
concatenado num `teko.c`; na rota native, é um `.o` no disco. Mesmo laço de streaming, dois
back-ends de emissão (`reducao…` 311-320, 546-559).

---

## 2. O pipeline — parse-per-unit → LINK/FFI-interna → check/lower/emit por namespace → linker interno → saída por destino

O pipeline do Eixo C já está desenhado (`reducao…` §6bis, crumbs C10–C13). O terminal native
**pluga na saída-de-unidade** sem re-estruturar o laço. Sequência:

```
1. parse-per-unit  (RM-C11)  → AST INCOMPLETA por namespace
                               (decls exp+pub: tipos/assinaturas/const + refs pendentes;
                                CORPOS não residentes)
2. LINK (barreira global, RM-C11) → FFI INTERNA (exp+pub: tipo Teko + símbolo + ABI)
                               resolve as refs pendentes; alimenta o checker de cada unidade;
                               TRANSITÓRIA (some após o streaming)
3. check+lower+emit POR NAMESPACE (RM-C12, fundidos; despejo = arena-drop / disco RM-C13)
     por unidade, em ordem determinística:
       (re)carrega corpos → checa contra a FFI interna → lower → EMITE a saída-de-unidade
       → derruba a região da unidade (o despejo)
     saída-de-unidade abstrata:
       • rota C      → pedaço de texto (concatena no teko.c)          [muleta]
       • rota native → .tkb intermediário PRÉ-mono + .tkh de link      [endgame, RM-C15]
                       POR NAMESPACE
4. LINKER INTERNO (o "linker próprio")
       • associa símbolos entre os .tkb/.tkh de link (a FFI interna no plano de linkagem;
         tabela de símbolos do .o = exp+pub global, privado = local)
       • roda os passos restantes NA PONTA conforme o destino:
           - MONOMORFIZAÇÃO (para lib/binary) — os .tkb são pré-mono
           - isel → regalloc → encode → emit_elf/macho/coff por unidade
       • junta via ld do SO (ou objfile_ar) no artefato final
5. SAÍDA POR DESTINO (§5): package | lib (shared/static) | binary/tool
```

**Ponto-chave do desenho:** os estágios 3 e 4 são **separados de propósito**. O estágio 3
emite artefatos **PRÉ-monomorfização, por namespace** (genéricos crus + pedidos de
monomorfização que cruzam unidades). O estágio 4 (o linker interno) é onde a monomorfização
**mora** — porque só na ponta se conhece o conjunto FECHADO de instanciações que cruzam todas
as unidades (§4). Isto casa com o modelo de pico do Eixo C: cada unidade emitida é despejada
(região derrubada), e o linker interno segura só a FFI interna (compacta) + a unidade
corrente.

---

## 3. Os dois `.tkh` — header de LINK (por-namespace) × header PUBLICADO (final)

O `.tkh` de hoje é **um só**: a superfície `exp` agregada (`src/emit/tkh.tks:217`
`emit_tkh`), interface do usuário. O redesign native precisa **distinguir dois sabores**,
porque o `.tkh` passa a servir a dois tempos diferentes (link interno × FFI externa).
Nomeados e distintos:

### 3.1 `.tkhl` — o header de LINK, por-namespace (tempo de LINK interno)

- **O que é:** a interface de **símbolos** de UMA unidade no tempo de link — a materialização
  em disco (ou em memória) da **FFI interna** de `RM-C11` restrita àquele namespace:
  `exp`+`pub`, cada entrada com **tipo Teko + símbolo de linkagem + ABI**.
- **Sabor:** **PRÉ-monomorfização** (assinaturas genéricas cruas). Companheiro do `.tkb`
  intermediário por-namespace.
- **Tempo de vida:** **TRANSITÓRIO** — vive o link, some após o streaming; **NUNCA** é
  embarcado. É a "FFI interna no plano de linkagem" (`reducao…` 421-426, R8).
- **Nome sugerido:** `.tkhl` (`tkh`-link) para não colidir com o `.tkh` publicado. Alternativa
  law-clean: manter em memória (nunca tocar disco) quando o estágio 3→4 funde; só materializar
  em `.tkhl` na fronteira que não funde (o LINK global, análogo a `RM-C13`).

### 3.2 `.tkh` — o header PUBLICADO, final (FFI externa / IDE)

- **O que é:** a superfície `exp` **agregada** do artefato inteiro — a interface do USUÁRIO
  (`src/emit/tkh.tks`, `RM-C17`). SÓ `exp`; `pub` jamais aparece (lei de visibilidade
  `tast.tks` M.4 + R8).
- **Sabor depende do destino (§5):**
  - **package** → **PRÉ-monomorph, portátil** (poli; genéricos preservados para o consumidor
    instanciar).
  - **lib (shared/static)** e **binary/tool** → **MONOMÓRFICO** (para FFI com outras
    linguagens — C/Rust/… precisam de assinaturas concretas, sem genéricos).
- **Tempo de vida:** **EMBARCADO** — entregue junto do artefato (`RM-C17`).

**A tabela de distinção (registrar):**

| Header | Tempo | Escopo | Visibilidade | Mono? | Embarcado? | Papel |
|---|---|---|---|---|---|---|
| **`.tkhl`** (link) | LINK interno | 1 namespace | `exp`+`pub` | PRÉ-mono | **não** (transitório) | associar símbolos entre unidades |
| **`.tkh`** (publicado) | pós-build | agregado | só `exp` | package: PRÉ · lib/bin: MONO | **sim** | FFI externa + IDE/intellisense |

Os dois são **ortogonais** (R8): `.tkhl` carrega `pub` para o link; `.tkh` **nunca** vê `pub`.
Reusar a projeção do `.tkb` estendida com `pub` (`RM-C11`) para montar o `.tkhl` é correto —
mas o emissor de `.tkh` (`emit_tkh`) tem que continuar **filtrando só `exp`**; embarcar o
`.tkhl` no `.tkh` por descuido é o erro que R8 nomeia.

---

## 4. Onde a monomorfização mora — na PONTA (target), nunca no `.tkb` intermediário

**Estado de hoje (verificado):** `src/build/project.tks:204-256` (`checked_program_of`) roda
`checker::monomorphize` (`src/checker/monomorph.tks:946`) **antes** de qualquer emissão, e o
`.tkb` de package (`:1145` `emit::serialize_program(prog)`) serializa o programa **POST-mono**.
Ou seja: hoje o `.tkb` carrega o monomorfizado. **O redesign inverte isto.**

**Regra do redesign (`reducao…` + descrição do dono):** o `.tkb` intermediário por-namespace
é **PRÉ-monomorph** — genéricos crus + **pedidos de monomorfização** que cruzam unidades
(`reducao…` 441: "Pedidos de monomorfização que cruzam unidades" é o que a FFI interna
retém). A monomorfização roda **na ponta (o linker interno, estágio 4)**, porque:

1. **Fecho global.** A monomorfização precisa do conjunto FECHADO de instanciações alcançáveis
   a partir do `main`/exports. Sob emissão por-namespace, esse fecho só é conhecido depois que
   TODAS as unidades foram vistas — logo, na ponta.
2. **Portabilidade do package.** Um package pré-mono é **reinstanciável** pelo consumidor
   (que combina os genéricos do package com os seus próprios tipos). Monomorfizar no package
   **congelaria** as instanciações e quebraria a composição.
3. **Bounded memory.** O `.tkb` pré-mono é MENOR (uma cópia do genérico, não N cópias
   monomorfizadas), o que reforça o teto de pico do Eixo C.

**Consequência de sequenciamento:** o passo `checker::monomorphize` sai do meio do frontend
(`checked_program_of`) e migra para o **linker interno** (estágio 4), rodando SÓ para os
destinos lib/binary. Para package, **nunca** roda — o `.tkb` final agrega todos os namespaces
ainda poli (§5.1). Ver **[DECISÃO DO DONO] 2**.

---

## 5. Saída por destino

O `Artifact` já existe (`src/build/init.tks`: `binary`/`static`/`shared`/`package`; enum
`Artifact::{Binary,Tool,Static,Shared,Package}` em `project.tks`). O terminal native
especializa a saída:

### 5.1 `package` → um `.tkb` final (TODOS os namespaces) + um único `.tkh`, PRÉ-mono, portátil

- **`.tkb` final:** agrega os `.tkb` intermediários de TODOS os namespaces num único artefato,
  mantido **PRÉ-monomorfização** (poli). É o `.tkb` reinstanciável que o consumidor combina.
- **`.tkh` final:** um único header, **só `exp`**, **PRÉ-mono/portátil** (§3.2).
- **Sem monomorfização, sem isel/encode:** package NÃO produz binário — entrega o par
  `.tkb`+`.tkh` (dentro do `.tkl`, como hoje `src/build/project.tks:1137-1168`), agora
  pré-mono em vez de post-mono. Este é o único ponto onde o `.tkb` de HOJE muda de sabor.

### 5.2 `lib` (shared/static) → binário MONOMORFIZADO + `.tkh` MONOMÓRFICO

- **Binário:** o linker interno **monomorfiza na ponta** (fecho de instanciações da lib),
  isel/regalloc/encode por unidade, e junta num `.a` (static, `objfile_ar`) ou `.so`/`.dylib`/
  `.dll` (shared — hoje `project.tks:1134-1136` ainda é honest-stop "not yet implemented";
  ver §11).
- **`.tkh` monomórfico:** a interface `exp` já monomorfizada — **para FFI com outras
  linguagens** (C/Rust chamam símbolos concretos, sem genéricos).

### 5.3 `binary`/`tool` → executável + `.tkh` monomórfico

- **Executável:** monomorfização na ponta (fecho a partir do `main`) → isel/encode por
  unidade → `ld` do SO junta os `.o` + os externals do runtime (`reducao…` 320) → executável.
- **`.tkh` monomórfico:** emitido junto (`RM-C17`), para linkar/estender o compilador e
  intellisense na IDE.

**Nota de ABI:** para `abi = "c"`, o checker de export FFI (`codegen::check_ffi_export`,
`project.tks:1175`) e o header C (`ffi_write_c_header`) continuam válidos — o `.tkh`
monomórfico é o análogo Teko-nativo do header C.

---

## 6. Paralelização do build

O linker interno + a emissão por-namespace habilitam **paralelizar o build por unidade** — o
próximo grande ganho de tempo (depois de memória). O mecanismo é **exatamente o de C**: o
compilador "miscompila" cada namespace **confiando no `.tkhl`/FFI-interna** (como o `cc`
compila cada `.c` confiando nos headers), sem precisar dos corpos das outras unidades.

**Pré-condição arquitetural (já garantida pelo Eixo C):**

1. A **barreira de LINK** (`RM-C11`) resolve TODAS as pendências cross-unit ANTES do estágio
   3. Depois dela, cada unidade tem tudo o que precisa (a FFI interna) para ser checada/emitida
   **em isolamento** → o estágio 3 é **embaraçosamente paralelo** por namespace.
2. O **determinismo** (`RM-C10` gensym + R6 frame) garante que a ordem de execução NÃO afeta o
   byte de saída — condição necessária para paralelizar sem quebrar o fixpoint.
3. O **despejo por unidade** (região de arena por namespace, `RM-C6`/`C12`) dá **isolamento de
   memória** entre workers: cada unidade numa região própria, derrubada ao fim.

**Sequenciamento:** paralelização é **posterior** a `RM-C12`/`C15` (a emissão por-unidade
serial primeiro, verde e byte-idêntica). Só então introduzir workers. NÃO é um crumb desta
onda — é o ganho que o desenho HABILITA (registrar como linha de roadmap, não como gate de
0.3.1). O fixpoint continua serial-determinístico; a paralela tem que reproduzir o mesmo byte.

---

## 7. Build incremental (RM-C14)

Já desenhado em `RM-C14` (`.crumbs/0069`): cache do `.tkb` typed por-unidade em disco,
chaveado por `hash(unidade) + hash(fatia da FFI interna de que ela depende)`; recompila só o
que mudou (fonte ou assinatura `pub` de dependência). É o **caso persistente** do
despejo-em-disco de `RM-C13` — o mesmo `.tkb` que bounded o pico serve de cache entre builds.

**Amarração ao terminal native:** o `.tkb` intermediário por-namespace do terminal native é
**exatamente a unidade de cache** do incremental. Um namespace cuja fonte e cujas dependências
(via `.tkhl`) não mudaram **reusa o `.tkb`/`.o` cacheado** — não re-checa, não re-emite, não
re-encoda. Como o linker interno conhece o grafo de dependência (a FFI interna), a
invalidação é precisa: mudar uma assinatura `pub` invalida os dependentes.

**Lei (RM-C14):** o incremental é **DESLIGADO no self-build/fixpoint** — um build limpo tem
que produzir o artefato byte-idêntico. É otimização de **tempo de dev**, não reduz pico, e
**nunca** influencia o byte do fixpoint. É `[dry]`, reseed-class `none`.

---

## 8. Migração do fixpoint (RM-C16) — de `teko.c` idêntico para objeto native reproduzível

Hoje o fixpoint é `gen2.c == gen3.c` (o `teko.c` emitido byte-idêntico). No endgame o critério
migra para **objeto native reproduzível** (`RM-C16`, `.crumbs/0106`):

- **Determinismo do `.o` (pré-condição):** sem timestamp, ordem estável de símbolos e seções,
  sem path absoluto embutido, relocations em ordem canônica, padding zerado. Auditar TODO
  `src/backend/objfile_*` + `objfile_ar` (o `objfile_ar` já ordena símbolos —
  `coff_lib_sorted_symbols`/`bsd_sorted_symbols`; o `objfile_elf` já separa
  `STB_LOCAL`/`STB_GLOBAL` deterministicamente, `objfile_elf.tks:36-127`).
- **Critério novo:** o harness/CI compara `gen2.o == gen3.o` (por unidade + binário juntado)
  em vez dos dois `teko.c`. Uma divergência é **causa-raiz no writer do objfile**, NUNCA um
  critério relaxado.
- **Remoção da muleta C (gated):** SÓ quando as 4 pernas native do CI (x86_64-linux,
  arm64-linux, arm64-macos, x86_64-windows) fecham verde E o objeto reproduz — aí `teko.c` +
  `cc` são REMOVIDOS, as 2 pernas C do CI viram native, e o **reseed do bootstrap passa a ser
  o objeto/binário native**. Requer também `S16-SWEEP` (a morte da dependência de runtime C):
  §16 tira o runtime C, C16 tira o compile C — ambos fecham para "no C" ser verdade.

O **mapeamento visibilidade → tabela de símbolos do `.o`** é a peça load-bearing que o
terminal native introduz e que o fixpoint native exercita: `exp`+`pub` = símbolo GLOBAL
(`!local`, visível ao `ld`/link cross-unit); privado = `static`/local (nem entra na tabela). A
tabela de símbolos do `.o` **É** a FFI interna no plano de linkagem — `pub` alcança o `ld` sem
vazar para o `.tkh` (R8). O `Symbol.local` do backend (`objfile_elf.tks`) é o slot exato desse
mapeamento.

---

## 9. Relação explícita com cada crumb RM-C10..C17

O terminal native NÃO cria crumbs novos — ele **é** a segunda metade da cadeia RM já
enfileirada. Amarração um-a-um:

| Crumb | Papel no terminal native | Sítio |
|---|---|---|
| **RM-C10** (`0065`) determinizar gensym | **PRÉ-REQUISITO** do per-unit native: gensym derivado de `buf.len` global quebra a reprodutibilidade sob emissão por-namespace (o buffer passa a ser por-unidade). Bloqueia C11. Ver **[DECISÃO DO DONO] 4**. | `codegen.tks:3840,3932` + `next_temp()` |
| **RM-C11** (`0066`) parse-per-unit → LINK / FFI interna | A **FFI interna** (`exp`+`pub`: tipo Teko + símbolo + ABI) — a base do `.tkhl` (§3.1) e a tabela que o **linker interno** usa para associar símbolos. É o "linker próprio" no plano lógico. | `project.tks:352,272` + novo `src/build/link.tks` |
| **RM-C12** (`0067`) check+lower+emit fundidos por unidade | O laço de streaming cuja **saída-de-unidade abstrata** (`UnitOutput`) o terminal native realiza como `.tkb`+`.tkhl`+`.o`. Despejo = região derrubada por namespace. | `project.tks:1165,2426` + `UnitOutput` |
| **RM-C13** (`0068`) dump typed `.tkb` por unidade | O `.tkb` intermediário **por-namespace** (agora PRÉ-mono, §4) — despejo em disco na fronteira do LINK que não funde. `serialize_unit`/`deserialize_unit`. | `tkb_frame.tks:369`, `tkb_read.tks:930` |
| **RM-C14** (`0069`) build incremental | Cache do `.tkb`/`.o` por-unidade (§7). DESLIGADO no fixpoint. | novo `src/build/incremental.tks` |
| **RM-C15** (`0105`) terminal native `.o` por unidade | **O coração deste doc:** cada namespace → `lower→isel→regalloc→encode→emit_elf/macho/coff` → `.o` no disco; região derrubada (o `.o` É o despejo). Visibilidade → tabela de símbolos. | `project.tks:1598,1615` + `objfile_*` |
| **RM-C16** (`0106`) fixpoint de objeto native + retirar muleta C | Migração do fixpoint (§8); remoção de `teko.c`+`cc` gated nas 4 pernas verdes + `S16-SWEEP`. | `objfile_*`, harness/CI, `codegen.tks` |
| **RM-C17** (`0107`) emitir + empacotar `.tkh` | O `.tkh` **publicado** (§3.2), monomórfico (lib/binary) ou pré-mono (package). Só `exp`; FFI interna nunca embarca. | `emit/tkh.tks:217`, `header.tks:144` |

**A monomorfização-na-ponta (§4) e os dois `.tkh` (§3) são a extensão que este doc acrescenta
ao desenho de C15/C17** — não os re-delibera; concretiza *onde* a mono roda (linker interno,
estágio 4) e *qual* header é qual (link `.tkhl` × publicado `.tkh`).

---

## 10. Restrição SQLite / no-C (LEI do doc — registrar)

**Restrição firme do dono, com força de lei neste projeto:** qualquer integração SQLite (por
exemplo, se o `.tsym`/debug-info virar consultável) tem que ser **construção NATIVA em Teko**
— **ABI/syscall direto OU driver próprio na stdlib**. **NUNCA** linkar `libsqlite3` C.

- Isto é coerente com o `TEKO_ROADMAP_DB.md` (Princípio 0: "Native over FFI"; SQLite é a
  exceção FFI que só entra sob a keystone de link sob-demanda `DB-KEYSTONE`/`C7.20`). Mas para
  **o compilador/toolchain** a regra é MAIS forte: **zero `libsqlite3`** — não é "linkar sob
  demanda", é "não linkar C, ponto". Um SQLite consumível pelo toolchain seria
  **SQLite-em-Teko** (ABI/syscall ou driver puro-Teko), linha de **roadmap DB separada**
  (`teko::db::*`), **NÃO um gate de 0.3.1**.
- **O `.tsym` fica como blob binário Teko puro por ora.** O `.tsym` (sidecar de posições,
  projeto "poda/jardinagem") permanece um **blob binário Teko puro** — hoje é até um stub
  (`codegen.tks:9685` `tk_emit_tsym` emite um comentário "disabled (debug-only, native
  backend)"). Torná-lo consultável via SQLite-em-Teko é **linha de roadmap DB separada, não
  gate** do terminal native. O terminal native NÃO depende de, nem introduz, qualquer
  dependência de DB.
- **Coerência com C16 "no C":** todo o endgame é *remover* dependência de C (compile: `teko.c`+
  `cc` saem em C16; runtime: `S16-SWEEP`). Introduzir `libsqlite3` C seria andar para trás.
  A restrição SQLite/no-C é a mesma lei "no-C" aplicada ao futuro do `.tsym`.

---

## 11. O que permanece BLOQUEADO (DESIGN-AHEAD — §11)

Este doc é desenho adiantado. O que já é **executável hoje** vs. o que **espera dependência**:

**Executável agora (não bloqueado):**
- Todo o design deste doc (contratos, sabores de artefato, mapeamentos, os 4 pontos).
- `RM-C10` (determinizar gensym) — dep `RM-C5` já pousou; é rename mecânico byte-preservante.
- `RM-C11`/`C12`/`C13` — deps na própria onda RM; a FFI interna, o laço por-unidade e o dump
  `.tkb` por-namespace são construíveis contra a forma DECLARADA do `.tkb` de hoje.
- Scaffolding que compila hoje: skeletons de `src/build/link.tks` (a barreira LINK +
  `InternalFfi`/`FfiEntry`/`link_units`), `src/build/incremental.tks` (`incremental_enabled`/
  `cache_key`), com doc-comments W15 e honest-stops onde a lógica ainda depende de C15.

**Bloqueado (aguarda dependência):**
- **`RM-C15`/`C16`/`C17` (M4):** dependem das pernas **NAT** (encoders `NAT-A4` Mach-O, `NAT-B1`
  ELF, `NAT-B3` COFF, `NAT-AARCH` aarch64-ELF, `NAT-XL` resolução de target) E de `RM-C12`
  verde na rota C. O `.o` por-unidade, o fixpoint de objeto e o `.tkh` publicado só fecham
  quando essas pernas fecham.
- **Monomorfização-na-ponta (§4):** a MOVIMENTAÇÃO física do `checker::monomorphize`
  (`monomorph.tks:946`) do frontend para o linker interno depende do estágio 4 (linker
  interno) existir — ou seja, gated em `RM-C15`. Até lá, o desenho fica registrado; a mono
  continua no frontend (whole-program) e o `.tkb` de package continua post-mono. **O flip
  para pré-mono é uma mudança de sabor de artefato → exige reseed** (superfície do `.tkb`).
- **`shared` lib (`.so`/`.dylib`/`.dll`):** `project.tks:1134-1136` é honest-stop hoje ("not
  yet implemented"). O `.tkh` monomórfico de lib (§5.2) só é entregável quando o shared-link
  fechar. **Permanece honest-stop até então.**
- **Paralelização (§6) e incremental-ON (§7):** posteriores a C12/C15 verdes e seriais; não
  são gate de 0.3.1.

---

## 12. Riscos e tensões de lei

Os riscos R4/R6/R7/R8 do Eixo C (`reducao…` §8) valem integralmente aqui. Específicos do
terminal native:

- **T-mono (§4) — flip pré-mono é mudança de superfície.** Mover a mono para a ponta muda o
  sabor do `.tkb` de package (post-mono → pré-mono). Isso quebra a **compatibilidade do `.tkl`
  publicado** (um consumidor 0.3.0 leria um `.tkb` post-mono; um 0.3.1 lê pré-mono). Mitigação:
  o flip é gated em `RM-C15` e **exige reseed** (mudança de superfície/ABI de artefato); marcar
  o `.tkb` com um **byte de versão de sabor** (poli vs mono) para o deserializador rejeitar o
  sabor errado com erro claro. **Não é tensão de lei** — é sequenciamento + versionamento.
- **T-tkhl (§3.1) — vazamento de `pub` para o `.tkh` (R8).** Reusar a projeção `.tkb`+`pub`
  para o `.tkhl` e, por descuido, alimentá-la em `emit_tkh`. Guarda: `emit_tkh` filtra só
  `exp`; o `.tkhl` vive só na região do link e é derrubado antes do emit final. Já coberto por
  R8 — reforçado aqui pela existência de DOIS headers.
- **T-det (§8) — não-determinismo do encoder.** O fixpoint native (`gen2.o==gen3.o`) exige
  determinismo TOTAL do `.o`. Já há ordenação de símbolos, mas o audit de `RM-C16` tem que
  cobrir toda fonte de não-determinismo (timestamps, paths, ordem de relocations). Causa-raiz
  no writer, nunca critério relaxado.
- **Sem tensão de lei não resolvida — nada HALTa.** Todos os pontos abaixo têm recomendação
  law-first; são **confirmações do dono**, não impasses.

---

## 13. Pontos de decisão do dono

### [DECISÃO DO DONO] 1 — Duas tabelas ortogonais (R8 já ratificado)

A compilação separada cross-namespace DENTRO do programa confia na **FFI interna =
`exp`+`pub`** (transitória, do link — materializada como `.tkhl`, §3.1), **não** no `.tkh` (só
`exp`, interface do usuário). São tabelas ortogonais.

**Recomendação do coordenador (CONFIRMAR):** **manter separadas.** Cross-namespace *interno*
→ FFI interna (`.tkhl`, `exp`+`pub`, transitória). Dependência *externa* (outro package) →
`.tkh` (só `exp`, embarcado). É a ratificação de R8 já vigente; o terminal native só a
concretiza em DOIS artefatos de header distintos (§3). Sem tensão de lei — `.tkh`-só-`exp`
preserva a lei de visibilidade `tast.tks` M.4; a FFI interna resolve o furo do
link-só-por-interface sem inchar o header.

### [DECISÃO DO DONO] 2 — Monomorfização mora na PONTA (target)

O `.tkb` intermediário é **PRÉ-monomorph** (genéricos crus + pedidos de monomorfização);
**package fica poli**, **lib/binary monomorfizam no target**.

**Recomendação do coordenador (CONFIRMAR):** o **`.tkb` NUNCA carrega o monomorfizado**. A
mono roda no **linker interno (estágio 4)**, só para lib/binary; package agrega pré-mono e
entrega poli/portátil (§4, §5.1). Isto **inverte o comportamento de hoje** (`project.tks:1145`
serializa post-mono) — é uma mudança de sabor de artefato, gated em `RM-C15`, **com reseed** e
byte de versão de sabor (T-mono, §12). Recomendado por três razões law-first: fecho global
(mono precisa do conjunto fechado, só conhecido na ponta), portabilidade do package
(pré-mono é reinstanciável), e bounded memory (pré-mono é menor). Sem tensão de lei.

### [DECISÃO DO DONO] 3 — Dois sabores de `.tkh`

O `.tkh` por-namespace de **LINK** (interface de símbolos, tempo de link) vs. o `.tkh` final
**MONOMÓRFICO** (FFI externa).

**Recomendação do coordenador (nomear e distinguir):** adotar **`.tkhl`** (`tkh`-link,
por-namespace, `exp`+`pub`, PRÉ-mono, transitório) para o tempo de link (§3.1), e **`.tkh`**
(publicado, agregado, só `exp`, monomórfico para lib/binary e pré-mono para package,
embarcado) para a FFI externa/IDE (§3.2). São ortogonais (R8): `.tkhl` carrega `pub`; `.tkh`
nunca. Alternativa aceitável: NÃO materializar o `.tkhl` em disco quando o estágio 3→4 funde
em memória — só serializá-lo na fronteira do LINK que não funde (análogo a `RM-C13`). CONFIRMAR
o nome `.tkhl` (ou o preferido do dono) e a política disco-vs-memória.

### [DECISÃO DO DONO] 4 — RM-C10 (gensym determinístico) é PRÉ-REQUISITO de C11

Gensym derivado de `buf.len` global quebra a reprodutibilidade sob emissão por-namespace (o
buffer passa a ser por-unidade → o mesmo corpo gera nome diferente → fixpoint quebra; R4).

**Recomendação do coordenador (CONFIRMAR):** **marcar `RM-C10` como bloqueador do per-unit
native.** Já está assim nos crumbs (`RM-C10` "BLOCKS C11"; `RM-C11 deps: [RM-C10]`), e o
terminal native (`RM-C15`) é transitivamente gated em C10 via C11/C12. Sem C10, tanto a rota C
por-unidade quanto o `.o` native por-unidade divergem. É a pré-condição mecânica de todo o
Eixo C. Sem tensão de lei — é sequenciamento factual.

---

## 14. Fixtures de regressão (o que o implementer adiciona)

O Eixo C é **exercitado pelo self-build/fixpoint** (o compilador É o monólito multi-namespace)
— por isso C10–C13/C15–C17 têm `Fixtures: none` e o fixpoint É a regressão. As fixtures
isoladas ficam onde o self-build NÃO exercita o caminho:

| Fixture | Entrada → asserção | Exit esperado |
|---|---|---|
| `tkb_flavor_premono_roundtrip` | serializar um namespace genérico pré-mono e deserializar → o `.tkb` recupera os genéricos crus + pedidos de mono, sem instanciar (sabor poli preservado) | `0` |
| `tkb_flavor_rejects_wrong` | deserializar um `.tkb` de sabor mono onde se espera poli (byte de versão de sabor errado) → erro claro, não crash | `0` |
| `tkhl_excludes_from_tkh` | montar `.tkhl` (`exp`+`pub`) e emitir `.tkh` do mesmo namespace → `pub` NÃO aparece no `.tkh`; só `exp` | `0` |
| `visibility_symbol_binding` | um namespace com `exp`/`pub`/privado → no `.o`, `exp`+`pub` são GLOBAL (`!local`), privado é LOCAL/`static` (nem entra na tabela) | `0` |
| `package_tkh_is_poly` | package com genérico exportado → o `.tkh` publicado preserva o genérico (pré-mono/portátil) | `0` |
| `lib_tkh_is_mono` | lib com `exp` genérico instanciado → o `.tkh` publicado é monomórfico (assinaturas concretas, para FFI) | `0` |
| `incremental_off_self_build` (já em RM-C14) | `self_build=true` → cache bypassed, saída byte-idêntica a um build limpo C12/C13 | `0` |

As fixtures de `RM-C14` (`incremental_hit_reuses`, `incremental_dep_invalidates`,
`incremental_off_self_build`) já estão no crumb `0069` e permanecem. As de sabor de `.tkb` e
de header são **novas deste desenho** (acompanham o flip pré-mono do §4 e os dois headers do
§3), e só ganham gate quando `RM-C15`/`C17` saírem do bloqueio (§11).

---

## 15. Pontos de ritual (onde o gate cheio tem que passar)

- **RM-C10..C14:** cada um é `[fixpoint]` (exceto C14 `[dry]`) — `gen2==gen3` byte-idêntico +
  `ulimit -v 6815744`. São `fixpoint-rebuild` (o core consome; sem teaching seed).
- **RM-C15:** `[fixpoint]` — ainda na rota C (`gen2.c==gen3.c`), MAIS as 4 pernas native
  emitindo `.o` por-unidade que o `ld`/`objfile_ar` juntam num binário executável nas 4
  pernas. **RITUAL:** a muleta C fica; C15 só prova que o `.o` roda.
- **RM-C16:** **`[RITUAL]`** — a escada native completa + reseed genuíno (migração do seed
  native). É AQUI que o gate cheio migra: `gen2.o==gen3.o` byte-idêntico nas 4 pernas, `teko.c`+
  `cc` REMOVIDOS, as 2 pernas C viram native, seed do bootstrap = objeto native. Requer
  `S16-SWEEP` fechado. **Este é o ritual maior do terminal native.**
- **RM-C17:** `[fixpoint]` — `gen2==gen3` do objeto native + o `.tkh` publicado reproduzido
  byte-a-byte.

---

## 16. Resumo do amarre

Este doc **não re-delibera** o Eixo C — ele preenche o terminal native com o que faltava
concretizar: (a) o **arbusto → `.tkb`+`.tkhl` por-namespace + linker interno** (§1–2); (b) os
**dois sabores de `.tkh`** (`.tkhl` de link × `.tkh` publicado, §3); (c) **onde a
monomorfização mora** (na ponta / linker interno, `.tkb` sempre pré-mono, §4); (d) a **saída
por destino** (package poli · lib/binary mono, §5); (e) **paralelização** (§6) e
**incremental** (§7) como os ganhos que o desenho habilita; (f) a **migração do fixpoint**
para objeto native (§8); (g) a **restrição SQLite/no-C** como lei (§10). Tudo gated no marco de
memória e nas pernas NAT (§11) — é **design-ahead**. Os 4 pontos do dono (§13) têm recomendação
law-first e **nenhum HALT**: são confirmações, não impasses.
