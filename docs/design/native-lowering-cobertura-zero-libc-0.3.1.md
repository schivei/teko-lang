# Cobertura de lowering NATIVE do expurgo zero-libc — mapa definitivo (0.3.1)

Status: DESIGN (arquiteto). Read-and-design ONLY — nenhum código de produto escrito aqui.
Base medida: `origin/fix/retirement` HEAD `4f488589`; DECISION_LOG @ D110. Idioma: PT-BR.

> **O buraco que este doc fecha.** O plano do expurgo zero-libc
> (`expurgo-s16-refresh-orquestracao-0.3.1.md`) desenhou a rota **C** (F1–F9) e o orçamento de reseeds,
> mas **não cobriu o lowering NATIVE**. A lei do dono: **todo C emitido tem que ser expressável em
> native** — cada emissão C de `src/codegen/codegen.tks` precisa do espelho ESCRITO em
> `src/lir/lower.tks` + `src/backend/**`. Native é **WRITE-ONLY**: escreve-se agora, NÃO se roda
> `TEKO_BACKEND=native` antes do pós-F9. Este doc enumera a cobertura, corrige a MÉTRICA de aderência,
> decide o escopo, ordena pela fronteira empírica do CI, e amarra tudo aos reseeds do tail-§16 dentro do
> teto ≤5 + 1 sweep, em 3 passadas de ensino.

---

## §0 — A MÉTRICA (corrigida): "gen1 emite gen2 COMPLETO na perna native", por-arch, write-only

O plano velho media aderência por "contar honest-stops". **Errado e stale.** A prova de aderência é
empírica e já existe no CI: a perna native roda `gen1` sobre o **próprio source do compilador** e conta
`native-lowering item N/TOTAL`. Se o lowering fosse aderente, `gen2.c`/objeto seria produzido. Não é:
morre no meio. **A métrica única é: `gen1` percorre os ~9378 itens e EMITE gen2 até o fim, em cada
perna (arch×SO), sem parar e sem corromper.** (Write-only: emitir gen2, NÃO executar o binário; o
fixpoint da rota **C** segue sendo o gate de reseed.)

Os crumbs NAT existentes (0113/0114) declaram gate `[fixpoint] gen2==gen3` — **inalcançável no native
hoje**: gen2 native nem existe. Correção: o gate de aderência native é **"gen1 emite gen2 native
completo por-arch"** (write-only), medido pelo `item N/TOTAL` chegar ao fim; o `gen2==gen3` native só
vale pós-F9, quando a perna roda.

### Aderência REAL medida (CI Pull Request CI #1135, run 32913378908, commit `4f488589`)

| perna | fronteira (onde `gen1` para) | item / ~9378 | classe da falha |
|---|---|---|---|
| **linux-x86_64-glibc** | `teko::backend::emit_elf_shdrs` (`objfile_elf.tks:425`) — hang/OOM (>3,2 s no item, 8–30× os vizinhos, SIGTERM 143 sozinha, sem fail-fast) | **934** (~10%) | **(b) bug de lowering** (retorno-fat `[]byte` em cadeia) |
| **linux-arm64-musl** | `teko::backend::debug_info_locals` (`dwarf.tks:508`) — `*** stack smashing detected ***` | **90** (~1%) | **(b) bug de lowering** (retorno-agregado sret AAPCS64) |
| **windows-x86_64** | `teko::build::asm_module_items` — VERDICT FAILED (dura) | **1145** (~12%) | (a) honest-stop OU (b) — a auditar no sítio |
| **macos-arm64** | `bootstrap/teko.c:191627` **nem COMPILA** — `__builtin_longjmp is not supported for the current target` (capture_panic C-leg) | 0 | **portabilidade C-leg** (não é native; bloqueia mac inteiro) |

**Leitura de uma linha:** aderência native **<12%** no melhor caso, **CRASH a ~1%** no arm64, e o mac
**nem compila o teko.c**. Confirma o veredito do dono: **native <50%** — na verdade **<12%**. E o
**padrão-chave: quem mata cedo são BUGS DE LOWERING (classe b: hang/OOM/stack-smash), não honest-stops
"not yet lowered".** São eles que IMPEDEM gen2 de existir. Prioridade absoluta = classe (b).

---

## §1 — As DUAS classes de falha (catalogar AMBAS)

Um scout que só grepa `"not yet lowered"` vê **uma** classe e perde a que mais dói.

### Classe (a) — honest-stops explícitos: `error { "native backend N1: … not yet lowered (N2)" }`
Contagem medida em `src/lir/lower.tks` @ `4f488589`: **38** ocorrências de `not yet lowered`. Destas,
por leitura do código, **~7 são invariante-interno** (braço `_ =>` de um `match` exaustivo sobre um
conjunto que o checker/mono já restringe — o self-build NUNCA os atinge se o checker estiver correto):

| linha | honest-stop | por que é invariante |
|---|---|---|
| 239 | float operator `_ =>` | aritmética float só é `+ - * /`; `%`/comparação não chegam aqui |
| 255 | integer operator `_ =>` | todos os binops inteiros cobertos; comparações vão a outro path |
| 290 | unary operator `_ =>` | só `-`/`~` são unários válidos de operando numérico |
| 917 | named-type não-enum como operando aritmético `null =>` | struct/class/flags não tipam como operando aritmético |
| 934 / 946 | comparison operator `_ =>` | todos os operadores de comparação reais cobertos |
| 5722 | compound-assign operator `_ =>` | default sobre o conjunto que o parser produz |
| 6070 | função genérica (`type_params.len != 0`) | **mono roda ANTES do emit** (`project.tks:227`) → pós-mono nenhuma fn tem `type_params` |

Os **~31 restantes são construtos REAIS** sem lowering (match-patterns range/alt/float/slice/field/
grouped-bind; fat-pointer iface-dispatch/closure-call/cast/receiver/lambda-return; vtable de classe
polimórfica; interpolação bool/typed; `in`-expr; `str::concat` sobre `[]str`; if-sem-else como valor;
destructuring; `float::parse` result-class). **Estes já estão desenhados nos crumbs NAT 0113/0114** (ver §4).

### Classe (b) — bugs de lowering INTERNOS: erro `(internal)`, hang, OOM, stack-smash
**NÃO grepáveis por `not yet lowered`.** São invariantes que o checker "deveria" garantir mas que o
lowering VIOLA — ou pior, lowering que nem para: apenas emite errado, trava ou corrompe a pilha.
Contagem de error-paths `(internal)` em `lower.tks`: **77**. Dois já CONFIRMADOS atingidos pelo CI/dono:

- **`lower.tks:3602` e `:3819`** — `"value's type is not a member of its declared variant (internal)"`,
  em `variant_member_index_of`. Atingido lowerando `teko::checker::collect_const_sig` (log do dono, item
  ~2163). Causa-raiz: `variant_member_type_matches` (`lower.tks:3605`) só casa `Named`/`Error`/`Prim`/
  `Str`/`Byte`/`Slice`; um membro de variante que seja `Null` (null-como-união), `Func` (closure),
  `Void` ou agregado cai no `_ => false` → índice não encontrado → falso-erro-interno. **É invariante
  ERRADO, não honest-stop.**
- **`objfile_elf.tks:425` `emit_elf_shdrs`** — hang/OOM (x86_64@934). A função encadeia ~9
  `b = emit_elf_shdr(b, …)`, cada um retornando um novo `[]byte` (retorno-fat). O lowering do
  threading fat-`[]byte`-por-cadeia (DPS/dest-passing) está defeituoso → realoca/copia ou entra em laço
  → cresce sem limite. **É a MESMA família dos honest-stops "fat-typed lambda return" (2824/2859/6020),
  mas manifesta como HANG, não como stop.**
- **`dwarf.tks:508` `debug_info_locals`** — stack-smash (arm64@90). Retorna `DwLocalsInfo` (agregado).
  Retorno-agregado por `sret` na ABI **AAPCS64** (arm64) escrevendo além do slot, OU overrun de buffer
  `[]byte`/`[]u32` no encoder arm64. **Bug de backend arch-específico (isel/regalloc/encode arm64).**

**Implicação de design:** os 77 `(internal)` são a lista latente de classe-(b). O CI é o oráculo que
diz QUAIS são atingidos e em que ordem (fronteira). O trabalho de classe (b) é **frontier-driven e
iterativo**: corrige o primeiro-ofensor por-arch → re-emite gen2 native → o próximo ofensor aparece
mais fundo → repete até o item chegar a `TOTAL`. Não é um checklist fechável por leitura.

---

## §2 — DOIS EIXOS e a DECISÃO de escopo (decidida, não devolvida como fork)

### Eixo (A) — emissões PRÓPRIAS da campanha zero-libc
O C que a campanha ELA MESMA passou a emitir (F7/F8/entry-native) e cujo espelho native precisa existir:

| emissão C (campanha) | sítio C | espelho native | status native | crumb dono |
|---|---|---|---|---|
| `capture_panic`/exit/cancel — **REDESENHADO graceful-stop** (dono 2026-08-26; SUPERSEDE o setjmp/longjmp de D45/0064) | `codegen.tks:3215` `emit_capture_panic` + `:3249` `emit_capture_longjmp`, dispatch `:3513-3514` — a REESCREVER | **controle de fluxo puro** (check-flag + defers/drops + return); MESMA lógica das duas rotas — some a primitiva de unwind/PC-SP-restore | **a REDESENHAR** (mecanismo antigo estava errado; agente do longjmp foi PARADO) | **0126 (redesenhado)** |
| `thread_clone` helper x86_64 (raw clone) | `codegen.tks:9606` `cg_emit_thread_clone_helper_text_x86_64` | idem em asm-inline **aarch64** | **AUSENTE aarch64** (`#error` `:9451`/`:9554`) — sendo escrito agora | 0064/F7a |
| entry `_start` + `stack_ptr` intrínseco per-OS | `codegen.tks:9762-9859`; `lower.tks:6684-6730` `native_entry_stub` | `_start` LFunc; `stack_ptr`→`mov reg,%rsp`/`mov reg,sp` (isel) | **desenhado, a ESCREVER** | 0125 |
| spawn mmap/VirtualAlloc glue (thread stack) | `thread.tks` (SYS_MMAP) / kernel32 | chamada extern/syscall — já lowerável | rides F7a | 0064/F7a |
| símbolos Teko do harness + cov env/str | `codegen.tks:19-33` `emit_cov_*`; builtins `cov_*` `:3556-3560` | `builtin_cov_symbol` (`lower.tks:1970-1984`) já mapeia todos os `cov_*` a `tk_cov_*` | **FEITO** (extern-call) | 0064/RT-L6 |
| `load_u8`/`store_u8`, syscallN, atomics, `f64_bits` | `codegen.tks:3192`+ | `lower_load_u8_call`/`lower_store_u8_call` (`:1771/:1786`) + intrínsecos | **FEITO** | — |

**Verificação do briefing:** a afirmação do scout Haiku de um honest-stop `syscall6@ar_mmap` em
`lower.tks` é **FALSA** — não existe (grep vazio). Os `cov_*` e `load_u8/store_u8` **já** têm espelho
native. O eixo-A que FALTA é: (1) o C-leg portável de setjmp/longjmp (0126, mac-blocker), (2) clone
aarch64, (3) entry `_start`/`stack_ptr` (0125). Capture native propriamente dito está EM VOO — não redesenhar.

### Eixo (B) — backlog native geral pré-existente
Operadores/comparações/match-patterns/fat-pointers/union-nullable/result-class/vtable — os ~31
honest-stops reais da classe (a) MAIS os bugs de classe (b). **JÁ DESENHADO** em `0097` (NAT-A1),
`0113` (NAT-N1 union/nullable) e `0114` (NAT-N2 fat/dispatch/match/operator/KNOWN-WRONG) +
`recon-native-n1n2-gaps-strategy.md` (taxonomia: 84 KNOWN-STOP · 21 KNOWN-WRONG · 20 BLOCKED). **O que
NÃO está coberto por 0113/0114:** a classe (b) hang/OOM/stack-smash (elas tratam honest-stops e valores
errados, não travas/crashes), a ordem empírica por-arch, e a correção do invariante-errado 3602/3819.

### DECISÃO (law-first, sem fork): **A e B ambos ENTRAM nesta campanha (write-native-now).**
Razões, todas de lei já ratificada:
1. **"Todo C expressável em native"** (dono, âncora do briefing) é absoluta; **D106/D99 "antecipar,
   nunca deferir"** proíbe estacionar (B) num track M4/pós-RM-C15 diferido. O grosso da <12% de
   aderência é (B) — deferir (B) = deferir a métrica = o "trabalho meio-feito" que o dono condena.
2. **Native é WRITE-ONLY** → escrever (B) **não adiciona reseed de validação** (native não roda agora);
   rides os 5 reseeds do tail-§16. O único custo é tamanho/offset do `teko.c`, absorvido nos reseeds já orçados.
3. **(B) já está desenhado** (0097/0113/0114 + recon). Puxá-lo para frente custa design-zero — só
   RE-SEQUENCIAR e CORRIGIR o gate/métrica + adicionar a cobertura de classe (b) que faltou.

**O que legitimamente FICA pós-F9** (NÃO é write-native-now, é outro eixo): a **execução/validação
runtime** do native e o **endgame .tkb/.tkh do LINKER** (RM-C15/`0105`, RM-C17/`0107`). Esses são
native-RUN + distribuição, corretamente pós-F9. O track **RM-C (0097-0107)** é o LINKER/artefato, NÃO
o lowering write-only — o eixo (B) NÃO pertence a RM-C; dobra-se nos crumbs de fase do tail-§16.

---

## §3 — Ordenação pela FRONTIER empírica (piores/mais-cedo primeiro) + 3 passadas de ensino

Classe (b) mata mais cedo → vem primeiro. Dentro dela, o primeiro-ofensor POR-ARCH dita a ordem (o
frontier avança sozinho a cada correção). As passadas são camadas de ENSINO (dependência lógica), NÃO
gates temporais — **passadas são livres, RESEEDS são o teto (≤5 + 1 sweep)**.

### Passada 1 — DESTRAVAR A FRONTEIRA (classe b cedo + completude de variante/match)
O que impede gen2 de EXISTIR. Alvos concretos, na ordem do CI:
- **P1.1 · retorno-fat `[]byte` em cadeia (x86_64@934 `emit_elf_shdrs`, hang/OOM).** Ensinar o
  threading fat-`[]byte`/DPS por-cadeia sem realloc/cópia por chamada (dest-passing na arena do caller).
  Cobre também os honest-stops "fat-typed lambda return" (`lower.tks:2824/2859/6020`).
- **P1.2 · retorno-agregado sret AAPCS64 (arm64@90 `debug_info_locals`, stack-smash).** Corrigir a
  classificação sret/register-pair no `abi_aapcs64.tks` + encoder/regalloc arm64; guard de overrun
  `[]byte`/`[]u32`.
- **P1.3 · invariante-errado de membro-de-variante (`lower.tks:3602/3819`).** Estender
  `variant_member_type_matches` para `Null` (null-como-união), `Func`, `Void` e agregado. Desbloqueia
  `collect_const_sig` (item ~2163) e a família union/nullable inteira.
- **P1.4 · match-patterns + operator residual + union/nullable** (= **NAT-N1 0113 + parte match/operator
  de NAT-N2 0114**, PUXADOS PARA FRENTE): range/alt/float/slice/field/grouped-bind; `lbinop_of`/
  `lunop_of` residual.

**Por que P1 primeiro:** as travas de classe (b) e os stops de match/variante estão nos itens ~90–1145,
ANTES de qualquer construto de eixo-A (threads/capture/entry estão bem mais fundo no corpus). Sem P1, o
stream de emissão native não passa dos ~12% — nenhum item de A ou B posterior é sequer alcançado.

### Passada 2 — FAT/ABI/RESULT-CLASS + vtable (o meio do corpus)
- **P2.1 · result-register-class** (o KNOWN-WRONG keystone): `LResultClass = variant { IntReg;
  FloatReg; FatPair; Void }` (0114) — corrige `float::parse` (XMM0/D0 vs int-reg, `lower.tks:2112`) e
  todo resultado float/fat lido do banco errado.
- **P2.2 · fat-pointer através de indireção**: iface-dispatch result, closure-call result, fat cast, fat
  receiver, mutable-fat capture (`lower.tks:2187/2316/4347/4353/4914/2376`).
- **P2.3 · vtable de classe polimórfica** (`lower.tks:2200`): slot-vtable no offset 0 + `LCallIndirect`.
- **P2.4 · resíduo de expr/stmt**: interpolação bool/typed, `in`-expr, field/index não-struct,
  destructuring, if-sem-else-como-valor, `str::concat` sobre `[]str` (o restante da classe-(a) real).

### Passada 3 — PRIMITIVAS DE RUNTIME DA CAMPANHA (eixo A próprio)
- **P3.1 · capture/exit/cancel = GRACEFUL-STOP** (0126 redesenhado; dono 2026-08-26): controle de fluxo
  PURO — flag de panic-pendente (na área de captura por-task, já em `arena.tks`) + após cada call que
  pode entrar em panic, emitir `if (panicking()) { <defers/drops do frame>; return <sentinela>; }`; o
  frame de captura testa/consome. **Elimina `__builtin_setjmp`/`__builtin_longjmp`** (causa-raiz do erro
  macOS) → portável Linux/Windows/Mac de graça, zero-libc. **Não há primitiva de unwind/PC-SP-restore.**
- **P3.2 · capture native = a MESMA lógica** (as duas direções idênticas): `lower.tks` emite o mesmo
  check-flag+cleanup+return que o codegen C — controle de fluxo normal, reusa o caminho de defer/drop
  do return. **Some a "unwind primitive" do mapa de ensino** — o native SIMPLIFICA, não precisa de
  restore de PC/SP.
- **P3.3 · `stack_ptr` intrínseco + naked `_start` per-OS** (0125): isel `mov reg,%rsp`/`mov reg,sp`.
- **P3.4 · clone aarch64** (asm-inline, `#error` hoje `codegen.tks:9451`): espelho do helper x86_64.

**Interdependência e nº de passadas:** o dono confirmou "1 etapa não basta, aceita 2-3". **São 3** — a
razão real: P1 (fronteira) PRECEDE tudo (senão o stream para a <12%); P2 (fat/ABI/result-class) e P3
(runtime próprio) são independentes ENTRE SI mas ambas exigem o stream de P1 fluindo além dos primeiros
~1145 itens. NÃO cabe em 1 passada (P1 é pré-requisito de medir P2/P3); NÃO precisa de 4 (P2 e P3 não
dependem uma da outra). **Simplificação (dono 2026-08-26):** o graceful-stop ELIMINA a primitiva de
unwind/PC-SP-restore que o mapa de ensino original listava — P3 encolhe no lado runtime (capture vira
controle de fluxo, idêntico nas duas rotas). **Nota de intercalação:** o crash arm64@90 (P1.2) é MUITO
cedo/urgente → intercala com P1 por-arch, guiado pelo frontier, não pelo rótulo de passada. O
mac-blocker do longjmp DESAPARECE (o graceful-stop não emite builtin nenhum).

---

## §4 — AMARRAÇÃO aos reseeds do tail-§16 (dentro do teto ≤5 + 1)

O tail-§16 (D105/D106/D109) já tem sua sequência de reseeds SÉRIE:
**F7a threads → F8/RT-L6 (0064) → F7b canais → entry-native (0125) → F9 SWEEP.**
São **4 reseeds de fase + 1 sweep (F9) = ≤5 + 1**. O trabalho de lowering native **RIDE esses reseeds**
(write-only não adiciona reseed próprio). Cada dispatch R2→F9 carrega seu escopo native:

| reseed (fase tail) | escopo NATIVE que carrega (write-only) | passada |
|---|---|---|
| **R#1 · F7a threads** | clone-aarch64 (P3.4) · spawn-mmap native (A) · **P1.1 fat-`[]byte`-return + P1.2 sret-arm64 + P1.3 variante-membro + P1.4 match/operator/union** (destrava a fronteira antes de tudo) | **1** (+3.4) |
| **R#2 · F8/RT-L6 (0064)** | **0126 capture/exit/cancel REDESENHADO graceful-stop (P3.1+P3.2, C+native idênticos, elimina longjmp, mac-unblock)** · harness/cov native (feito) · **P2.1 result-class + P2.2 fat-pointer + P2.3 vtable** | **2, 3** |
| **R#3 · F7b canais** | canais AF_UNIX/named-pipe native · **classe-(b) sweep frontier-driven** (próximos ofensores `(internal)` que surgirem após R#1/R#2, incl. windows@1145 `asm_module_items`) | **1(b)/2** |
| **R#4 · entry-native (0125)** | `_start`/`stack_ptr` per-OS (P3.3) · **P2.4 resíduo expr/stmt** (interpolação/in-expr/field-index/destructuring) | **2, 3** |
| **R#5 = SWEEP · F9** | **GATE de aderência native (write-only): `gen1` emite gen2 native COMPLETO nas 4 pernas** (item→TOTAL, sem parar/corromper) ANTES do sweep C. +1 = o próprio SWEEP. | — |

**Regra de ouro do teto:** nenhum lowering native abre reseed NOVO. Se o frontier-driven (R#3) revelar
mais ofensores que um reseed comporta, eles se acumulam no MESMO reseed de fase (o native não roda →
não há pressa de validar cada um isoladamente). O ratchet de memória (D100: adds proporcionais C→Teko
até o F9 reclamar) governa os reseeds que REALMENTE rodam (rota C).

**Re-sequenciamento dos crumbs existentes:** `0113`/`0114` hoje são milestone **M4** com dep "logical
predecessor of RM-C15/0105" (pós-marco de memória). **Puxar para frente** para rodar em R#1/R#2 (não
esperam M4 nem RM-C15) — a lei "write-native-now / antecipar nunca deferir" (D106) o exige. Ajuste de
gate: trocar o gate deles de `[fixpoint] gen2==gen3-native` (inalcançável) para **"gen1 emite gen2
native completo (write-only) + fixpoint C gen2==gen3 do reseed de fase"**.

---

## §5 — Crumbs NOVOS (write-native-now) — 0126, 0127, 0128

Criados em `.crumbs/` em ordem após `0125`:
- **`0126-NAT-B0-graceful-stop-capture.md`** — capture/exit/cancel REDESENHADO como graceful-stop
  (unwind cooperativo pela via de return, defers/drops por frame; ELIMINA setjmp/longjmp). Supersede o
  C-leg longjmp de 0064/D45. C e native emitem a MESMA lógica de controle de fluxo. Rides R#2/F8-0064.
  **Desbloqueia mac (o erro `__builtin_longjmp` era sintoma do mecanismo errado).**
- **`0127-NAT-B3-lowering-crash-invariant-sweep.md`** — classe (b): fat-`[]byte`-return (x86@934),
  sret-agregado arm64 (arm64@90), invariante-errado membro-de-variante (3602/3819), + sweep
  frontier-driven dos `(internal)`. Rides R#1 (destrava) e R#3 (resíduo).
- **`0128-NAT-B4-campaign-emission-native-mirror.md`** — espelho native das emissões próprias da
  campanha (clone-aarch64, `stack_ptr`/`_start`, spawn-mmap) amarrado a F7a/entry-native/0125; capture
  native construído EM CIMA do D45. Rides R#1/R#4.

E o **re-sequenciamento** de `0113`/`0114` (pull-forward + correção de gate) é registrado no
`EXECUTION-ORDER.md` como edição dirigida (não reescreve os crumbs; muda ordem e gate).

---

## §6 — Índice
- §0 A métrica corrigida (gen1-emite-gen2-native por-arch, write-only) + aderência REAL do CI #1135
- §1 As duas classes de falha (a honest-stops / b bugs internos hang/OOM/crash) — contadas no código
- §2 Dois eixos (A campanha / B backlog) + a DECISÃO de escopo (ambos entram, law-first)
- §3 Ordenação pela fronteira empírica + 3 passadas de ensino
- §4 Amarração aos 5 reseeds do tail-§16 (≤5 + 1 sweep)
- §5 Crumbs novos 0126/0127/0128 + re-sequenciamento de 0113/0114
