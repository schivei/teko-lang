# Escrita selvagem nativa — análise ESTÁTICA (0.3.1)

Reverse-engineering estático a partir dos logs já capturados (`gen2_gdb.log`,
`gen2.nm`, `gen2b_build.log`, `fix_g3.txt`) + a fonte em `work-native`.
NENHUM build/repro foi feito. Objetivo: cravar o SÍTIO DA ESCRITA e o SPEC do fix.

## 0. Veredito em uma linha

A escrita selvagem é o **store da metade LEN de um par fat `{ptr,len}`** (o segundo
`store_inst` de `store_fat_slot`, `src/lir/lower.tks:11567`, que grava `len` em
`slot+8`) num destino que o **layout reservou apenas 8 bytes**. O overrun de 8
bytes cai no primeiro word do vizinho — um `tk_freenode` de 16 B estacionado no
`bin[0]` — e sobrescreve o campo `next` dele com o valor da LEN. A alocação
seguinte de 16 B faz o pop desse bin e desreferencia o `next` corrompido.

A `raiz A` (commit `90b926e7`, tip de work-native) fechou a divergência
layout↔store para o caminho de **struct-literal** e para `str`/`[]T`/aliases, mas
**não fechou** porque (a) o caminho de **atribuição de campo** (`obj.f = fatval`)
não tem o guard `check_store_fat_span`, e (b) o predicado de layout
`field_is_fat_resolved` ainda responde THIN para um subconjunto de campos fat
monomorfizados/aliased. O guard existente nunca dispara porque ele só está armado
no caminho errado (struct-literal), não no caminho que realmente escreve.

## 1. Cadeia de LEITURA reconstruída (o crash / vítima)

Do `gen2_gdb.log` (registradores + backtrace) e `gen2.nm`:

```
SIGSEGV em tk_region_alloc+650  (0x5555558be4aa)
  rax = 0x7972 (= 31090)   <- o ponteiro corrompido sendo desreferenciado
  rbx = rcx = r12 = 0x10   <- an = 16 : a alocação corrente pede 16 bytes
#1 frame_pass_args   (src/lir/frame_escape.tks)   <- VÍTIMA (só aloca 16 B)
#2 frame_sweep_inst
#3 frame_sweep_block
#4 frame_sweep
#5 frame_marks_of
#6 func_returns_frame_address
#7 frame_escaping_funcs
#8 frame_escape_guard
#9 build.fuse_lower_item_x86
#10 build.emit_native_x86     <- gen2 baixando gen3 p/ nativo
#11 build.codegen_and_report
#12 build.build_ungated
#13 main
```

`tk_region_alloc` (`src/runtime/teko_rt.c:1880`) consulta a free-list ANTES de
bumpar (`tk_free_take`, inline, `teko_rt.c:1475`). O pop de um bin exato é:

```c
tk_freenode *n = *bin;   *bin = n->next;   // teko_rt.c:1485
```

`an=16` ⇒ `qa=16` ⇒ `bin = &tk_free_bins[0]` (a classe de blocos de 16 B, i.e.
exatamente pares fat / structs de 2 campos). `rax = n = 0x7972`: um valor pequeno
(31090) com os 6 bytes altos ZERO. Isso **não é um ponteiro** (um ponteiro de heap
seria `0x5555xxxxxxxx`, 6 bytes) — é um **inteiro de 8 bytes cujo conteúdo é
pequeno**, a assinatura exata de uma metade LEN de um par fat. Num pop anterior,
`*bin` recebeu `n->next` de um freenode cujo `next` fora sobrescrito por essa LEN;
o pop atual carrega `n=0x7972` e desreferencia `n->next` em `0x7972` → SIGSEGV.

Todos os "sítios de leitura" observados (frame_escape/sweep aqui; `collect_stmt_insts`
em `fix_g3.txt`; `tk_registry_free`/`tk_rt_getenv` em capturas anteriores) são
VÍTIMAS intercambiáveis: qualquer código que peça 16 B a seguir. O heisenbug (o
sítio de leitura muda com o layout) é a assinatura clássica de um **overrun no
VIZINHO físico**, não de um UAF em endereço fixo.

Determinismo: `gen2c_run{1,2,3}.log` reproduzem no mesmo ponto (linha 13);
`gen2b_build.log` fixa o crash logo após `native-lowering item 372/6644`
(`teko::backend::honest_globals_x86`).

## 2. SÍTIO DA ESCRITA (o corruptor)

### Função emissora
`store_fat_slot` — `src/lir/lower.tks:11567`:

```
store_inst(slot,      fo.ptr, LType::Ptr, …)                 // ptr @ slot+0
field_addr_inst(la, slot, ltype_size(LType::Ptr), …)        // la = slot+8
store_inst(la,        fo.len, LType::I64, …)                 // len @ slot+8  <-- OVERRUN
```

Sempre grava 16 bytes. Se o destino `slot` tiver reserva de 8 B, o segundo store
(`len @ slot+8`) escreve fora dos limites.

### Construto Teko que dispara
Um **campo `str`/`[]T` de um struct/class cujo layout registrado dimensionou o
campo (ou o tamanho total do struct) em 8 B** — uma divergência
`typeexpr_is_fat`/`is_fat_type` que sobrevive à `raiz A` — populado por um
**store fat de 16 B num caminho SEM o guard**. Os destinos fat computados a partir
de um OFFSET de layout que estão **sem guard** são:

- `lower_assign_field` → `store_fat_through_addr` — **`src/lir/lower.tks:13547`**
  (`obj.f = fatval`). PRIMÁRIO. Sem `check_store_fat_span`.
- `store_array_elem` (arm fat) — `src/lir/lower.tks:13058-13061` (offset = `i*stride`).
- `store_fat_variant_payload_pair` — `src/lir/lower.tks:9218`.

O único caminho guardado é o de struct-literal (`store_struct_fields` →
`check_store_fat_span`, `src/lir/lower.tks:12565`). Por isso **nenhum diagnóstico
disparou**: o guard existe, mas está armado no caminho que a `raiz A` já
consertou, não no caminho de atribuição/elemento que ainda escreve selvagem.

### Por que o layout ainda sub-dimensiona (a raiz que a raiz A não fechou)
`field_is_fat_resolved` (`lower.tks:15090`) = `typeexpr_is_fat(te) OR is_fat_type(rft)`.
Para um campo genérico `node: T` monomorfizado:
- `type_to_texpr` (`src/checker/monomorph.tks:213`) estampa o tipo concreto como um
  **`NamedType` de nome nu** (`mono_named_texpr`), sem namespace.
- `resolved_field_type` (`lower_const.tks:1372`) chama `checker::resolve_type` com
  o namespace VAZIO da instância; a resolução de um nome nu falha e cai em
  `unresolved_named_fallback` (`lower_const.tks:1401`) → `checker::Named` (não-fat)
  ou `Void` (não-fat) ⇒ `is_fat_type(rft)` = **false**.
- Resta `typeexpr_is_fat(te)`: cobre `"str"` (l.14961), `[]T` (SliceType) e aliases
  resolvíveis por `type_table_find`. **NÃO cobre**: (i) um alias-para-fat cujo nome
  nu não bate na tabela canônica-keyed (o mesmo modo de falha
  bare-vs-canonical que o próprio doc de `typeexpr_is_fat` descreve, l.15029-15037),
  e (ii) qualquer tipo concreto fat cuja re-síntese por `type_to_texpr` produza um
  nome que nenhum dos dois predicados reconheça.

Nesses casos o layout reserva 8 B para um campo que o checker sabe ser fat, o
store escreve 16, e — se o campo é o ÚLTIMO do struct (ou o sub-dimensionamento
encolhe o tamanho total) — a metade LEN transborda para além do objeto, no
`tk_freenode` vizinho. Confirma bit-a-bit o `rax=0x7972` (LEN pequena, não ponteiro).

## 3. SPEC DO FIX (para um coder aplicar; NÃO implementado aqui)

Dois níveis. O nível 1 (guard) transforma o crash silencioso em honest-stop
nomeado E captura o struct+campo exatos numa única corrida (ver §4). O nível 2
(raiz) fecha a divergência de dimensionamento.

### Nível 1 — armar o guard em TODOS os stores fat com offset de layout
Onde: `src/lir/lower.tks`.
- Em `lower_assign_field` (13539), ANTES de `store_fat_through_addr` (13547),
  chamar `check_store_fat_span(ro.ctx, layout, fa.field, decl_do_campo, a.value)`
  onde `decl_do_campo = find_field_decl_type(ctx.field_decls, name, fa.field)`.
  Retornar o `error` se disparar.
- Idem em `store_array_elem` (arm fat, 13058): antes do `store_fat_slot`, assertar
  que `stride >= fat_slot_bytes()` para elemento fat (guard de stride, não de span).
- Idem em `store_fat_variant_payload_pair` (9218): assertar que o wrapper reserva
  `>= fat_slot_bytes()` para a carga fat.

Assinatura reutilizada (já existe, `lower_const.tks:1019`):
```
fn check_store_fat_span(ctx: LowerCtx, layout: LStructLayout, field_name: str,
                        decl: checker::Type | null, value: checker::TExpr) -> null | error
```
Por que fecha o crash: torna IMPOSSÍVEL um store fat de 16 B num campo de span < 16
sem um stop nomeado; o overrun deixa de existir silenciosamente.

### Nível 2 — parar de sub-dimensionar o campo fat (a raiz)
`field_is_fat_resolved` deve responder `true` quando o campo é fat de fato. O sinal
CONFIÁVEL não é o `TypeExpr` estampado (nome nu que não resolve) nem `resolve_type`
(namespace vazio) — é o **tipo que o checker JÁ atribuiu ao campo**. Opções, em
ordem de robustez:

1. Preferida: fazer `collect_struct_layouts`/`layout_of_fields` lerem o tipo
   RESOLVIDO do campo direto do checker (o mesmo `checker::Type` que
   `store_struct_field`/`lower_assign_field` consultam via `a.bound`/`value.type`),
   em vez de re-resolver o `TypeExpr` estampado. Threading: já existe `dtable`; o
   que falta é a instância monomorfizada expor o tipo resolvido do campo. Se a
   tabela de campos do checker (`field_decls`) já o tiver, `field_is_fat_resolved`
   deve consultá-la (por nome de struct + nome de campo) antes de cair no
   `TypeExpr`.
2. Alternativa barata: em `resolved_field_type`, quando `resolve_type` falhar num
   nome nu, tentar `type_table_find` (a MESMA busca tolerante que `typeexpr_is_fat`
   usa) em vez de `unresolved_named_fallback` direto — para que um alias-para-fat
   de nome nu resolva a `Str`/`Slice`.

Por que fecha: layout e store passam a ler o MESMO sinal semântico
(`is_fat_type` do tipo que o checker atribuiu), impossibilitando a divergência de
largura na origem — em qualquer caminho (literal, atribuição, elemento, payload).

## 4. Incerteza e o ÚNICO dado empírico a capturar (o coordenador fornece)

Confiança ALTA (assinatura de valor + mecanismo): a escrita é o `len@slot+8` de
`store_fat_slot` num destino reservado com 8 B (§1–§2). Confiança MÉDIA sobre QUAL
struct+campo e QUAL dos 3 caminhos sem-guard (assign / array-elem / variant-payload).

Hipóteses ranqueadas:
- **H1 (primária)** — atribuição de campo fat `obj.f = fatval` (`lower.tks:13547`)
  num struct cujo layout sub-dimensionou o campo. Melhor ajuste: caminho sem guard
  + assinatura LEN + heisenbug de vizinho.
- **H2 (alternativa)** — append in-place em slot de push-cache ESTANTE
  (`tk_slice_push_r`/`tk_push_cache`), grava um elemento fat além do `cap` real de
  um buffer liberado/reusado. O commit `bf3ada36` (`TEKO_NATIVE_PUSH_REDZONE — the
  STALE-SLOT gap applies to the diagnostic too`) mostra suspeita ativa aqui. Também
  produz uma LEN no `next` do freenode, então o valor sozinho não desempata.

DADO A CAPTURAR (uma corrida, discrimina H1×H2 E nomeia o alvo exato):
Aplicar SÓ o Nível-1 (armar `check_store_fat_span` em `lower_assign_field`,
`store_array_elem` fat e `store_fat_variant_payload_pair`) e rebuildar o gen2, sem
o Nível-2.
- Se um **honest-stop** disparar (`native fat-field store guard: struct '<X>' fat
  field '<f>' … allotted 8 … needs 16`) → **H1 CONFIRMADA**, com o struct e o campo
  exatos impressos; o Nível-2 então fecha a raiz.
- Se o gen2 **ainda crashar com M.1** e nenhum guard disparar → **H1 REFUTADA**;
  perseguir H2 com `TEKO_MEM_PARANOID=1` (envenena blocos liberados com 0xDD e não
  os estaciona): se o crash mudar para leitura de `0xDDDD…` num sítio de append,
  H2 confirmada.

Peço ao coordenador: rodar o build do gen2 com o Nível-1 armado e me devolver
(a) qualquer honest-stop `native fat-field store guard: …` e (b) o `item N/6644`
imediatamente anterior a qualquer crash remanescente.

## 5. Ponteiros de arquivo (absolutos)

- Runtime da leitura: `/home/user/teko-lang/src/runtime/teko_rt.c:1475` (`tk_free_take`),
  `:1485` (o pop que crasha), `:1880` (`tk_region_alloc`), `:4483` (`tk_free_block`),
  `:1176` (`struct tk_freenode`), `:1089` (`struct tk_region`).
- Escrita: `/home/user/teko-lang/src/lir/lower.tks:11567` (`store_fat_slot`, o
  `len@+8`), `:13547` (`lower_assign_field`, sem guard — PRIMÁRIO),
  `:13058` (`store_array_elem` fat), `:9218` (`store_fat_variant_payload_pair`),
  `:12565` (o único guard existente, no caminho errado).
- Raiz do dimensionamento: `/home/user/teko-lang/src/lir/lower.tks:15090`
  (`field_is_fat_resolved`), `:15047` (`typeexpr_is_fat`),
  `/home/user/teko-lang/src/lir/lower_const.tks:1372` (`resolved_field_type`),
  `:1401` (`unresolved_named_fallback`),
  `/home/user/teko-lang/src/checker/monomorph.tks:213` (`type_to_texpr`).
- Guard reutilizável: `/home/user/teko-lang/src/lir/lower_const.tks:1019`
  (`check_store_fat_span`), `:973` (`fat_slot_underflows`), `:904`
  (`field_allotted_span`).

---

# §6 H2 — o use-after-free de 560 B / offset 32 (CONFIRMADO pela CI)

## Atualização de status
Dados empíricos da CI (POISON_FREE no leg nativo) REFUTARAM a H1 como causa
RESIDUAL e CONFIRMARAM a H2 (escrita em memória JÁ liberada). O fix H1
(guards Nível-1 + `resolved_field_type`) foi PROGRESSO REAL — o gen2 nativo agora
COMPILA por inteiro (antes morria no lowering item 372) e o leg C mantém o
fixpoint byte-idêntico. O crash residual mudou de fase: agora é na fase do
CHECKER (gen2 construindo gen3 morre em `checker 0/7009`), ANTES de qualquer
lowering — logo, estruturalmente, nenhum dos 3 guards de fat-store poderia
disparar, e a H1 não é o resíduo.

Evidência decisiva:
```
TEKO_NATIVE_POISON_FREE: parked block 0x5555a883a850 (bytes=560) corrupted at
offset 32 (0x20, expected 0xDD), first read at tk_free_block:entry
```

## 6.1 Identidade do bloco de 560 B — é um BUFFER DE SLICE, não um struct

O único caminho que ESTACIONA um bloco na free-list é `tk_free_block`
(`teko_rt.c:4483`), e seus três únicos chamadores passam **buffers de slice /
bytes**, nunca structs:
- `tk_slice_push_fo` (free-old on grow, `teko_rt.c:4442`) — o buffer ANTIGO de um
  slice self-append;
- `tk_append_bytes_fo` (`teko_rt.c:4367`) — o buffer antigo de um builder `[]byte`;
- `teko::mem::free` de um `[]T`.

Um struct NUNCA é estacionado: structs vivem em `alloca` (pilha nativa) ou em
chunk de região (liberados em bloco por `tk_region_drop` via `tk_chunk_free`, que
devolve ao glibc, NÃO à free-list que o poison-scan varre). Portanto o
"struct de 560 B com campo em +32" é a moldura errada: o bloco é um **backing
buffer de slice**, e `560 = len*esz` (o `tk_free_block` recebe `len*esz` de
`tk_slice_push_fo:4412` quando o push-cache não tem o tail vivo — não precisa ser
potência de 2). O `tk_poison_scan_one` varre a partir de `sizeof(tk_freenode)`=16,
então o **offset 32 é payload genuíno** (não o header `next`/`bytes`).

Interpretação de `offset 32` por `esz` (ranqueada):
1. **`[]str` / `[][]T` (esz=16), len=35 ⇒ 560 B; offset 32 = ELEMENTO 2** (a
   metade ptr do 3º elemento). PRIMÁRIA — o compilador é dominado por slices fat
   (listas de nomes/símbolos/tipos), e o gen2 morre na fase do checker, que
   constrói exatamente esses `[]str`/`[]TItem`.
2. `[]T`-por-endereço (esz=8), len=70 ⇒ 560 B; offset 32 = ELEMENTO 4.
3. `[]byte` builder (esz=1), 560 B; offset 32 = byte 32 (via `tk_append_bytes_fo`).

(Se ainda assim houver um struct de exatamente 560 B na fase do checker, ele NÃO
chega à free-list — não é o bloco reportado. Não há candidato de struct a
perseguir.)

## 6.2 Sítio da ESCRITA — stale store nativo no buffer free-old'd (native-only)

O corruptor é uma **escrita através de uma referência retida ao buffer ANTIGO de
um slice self-append liberado por free-old** — exatamente o "aliased write through
some OTHER name/reference to the same buffer" que o próprio comentário do runtime
antecipa (`teko_rt.c:4274-4276`). A escrita in-place do push-cache está
DESCARTADA como corruptor: ela é guardada por `{region, region_gen}`
(`teko_rt.c:4348-4356`) e o `tk_free_block` despeja a entrada do cache no park
(`:4489`); o offset 32 = índice de elemento (não fim de buffer) indica indexação
por ponteiro estante, não overrun de borda.

Sequência:
1. `xs = teko::list::push(xs, item)` com `xs ∈ frees_old_names`
   (`checker::assign_frees_old`, `escape.tks:643`, prova de cadeia linear).
2. O backend NATIVO baixa para `tk_slice_push_fo`
   (`is_frees_old_self_append`/`lower_assign_frees_old`, `lower.tks:13592/13627`).
   No copy-grow, `tk_slice_push_fo` LIBERA o buffer antigo O (560 B) em escopo
   raiz → estacionado + envenenado 0xDD.
3. DEPOIS, uma referência nativa retida a O escreve 8 B em O+32 → corrompe o
   poison.

### Por que só o NATIVO, com o leg C byte-idêntico
A prova `assign_frees_old` é uma propriedade WHOLE-BODY POR NOME (depende só de
`a.name` + `fn_body`, `escape.tks:645-660`), então C (`codegen.tks:9068`, per-assign)
e nativo (`fn_frees_old_vars` → conjunto de nomes) computam o MESMO conjunto
free-old — não há divergência de PROVA. A divergência é de REPRESENTAÇÃO: o leg C
reatribui o lvalue e não retém o endereço antigo; o backend nativo, no seu modelo
agregado/slice-POR-ENDEREÇO (NATIVE-AGG-SLICE-BY-ADDRESS) + a conveyance NP3/NP4,
retém o endereço pré-grow (num spill/segunda ligação/bracket) e armazena através
dele APÓS o free-old. Duas sub-hipóteses (o dado de §6.5 desempata):
- **H2b (LÍDER)** — o backend nativo retém o endereço do buffer antigo (alias que
  a prova não modela para a representação por-endereço) e escreve o elemento
  (offset 32) através dele depois do free-old.
- **H2a (secundária)** — um furo de SOLIDEZ COMPARTILHADO em `assign_frees_old`
  (um alias real que ela perde): o leg C teria o MESMO UAF, apenas latente/silen-
  cioso (park+reuso sem poison), enquanto o leg nativo foi testado com poison. Se
  for isto, rodar poison no leg C também dispara.

Correlação temporal: o free-old auto-upgrade nativo é a fonte de park mais NOVA
(commit `4ccce722`, "free-old parity", 2026-08-03), coincidente com o surgimento
do resíduo — o bisect natural.

## 6.3 Mapeamento do TWIN (regra de acoplamento do dono)

- `src/runtime/teko_rt.c` (C mantido, FROZEN): `tk_slice_push_fo` (:4409),
  `tk_free_block` (:4483), `tk_slice_push_r`/push-cache (:4332), `tk_region_drop`
  (:2025, que NÃO faz `tk_push_cache_purge` — só `tk_arena_pop`:2506 e teardown de
  task:2452 fazem). Linkado por AMBOS os legs; provavelmente CORRETO (o leg C usa
  e passa byte-idêntico).
- Lado NATIVO do twin (onde o bug vive): a DECISÃO de upgrade free-old em Teko —
  `checker::assign_frees_old` (`escape.tks:643`, a prova compartilhada) +
  `is_frees_old_self_append`/`lower_assign_frees_old` (`lower.tks:13608/13627`, a
  aplicação nativa) + a emissão NP4 de region-drop `emit_region_drops`
  (`lower.tks:1459`, native-only). O twin de arena manual é
  `src/mem/unsafe/arena.tks` (documenta explicitamente o "known aliased-UAF gap").
- Veredito: `teko_rt.c` NÃO muda (o leg C depende dele e é o oráculo byte-idênti-
  co). O fix é no lado NATIVO (`lower.tks`, e — se H2a — `escape.tks`).

## 6.4 SPEC do fix (NÃO implementado)

### Nível 1 — desarmar o free-old auto-upgrade NATIVO (mínimo, seguro, confirma)
Em `src/lir/lower.tks`, fazer `is_frees_old_self_append` (`:13608`) retornar
`false` (ou remover o ramo `:13592`), de modo que um self-append nativo caia
sempre no `tk_slice_push` PLANO (sem park). Efeito: o buffer antigo é VAZADO
(nunca liberado) em vez de estacionado — não há memória liberada para a escrita
estante corromper; a janela de UAF fecha no leg nativo.

Por que mantém o leg C byte-idêntico: o free-old é uma otimização SÓ de footprint,
OUTPUT-EQUIVALENTE (mesmo buffer/len retornados). O emissor C (`codegen.tks:9068`)
fica INTOCADO — o leg C segue exatamente igual, o fixpoint byte-idêntico do C
permanece. O programa EMITIDO pelo compilador nativo é semanticamente idêntico
(free-old vs plain não muda a saída), então o self-fixpoint nativo (gen2==gen3)
converge. Custo: reintroduz o vazamento (~984 MB medido em variant_siblings) que o
`4ccce722` fechou — correção > footprint; o footprint volta com o Nível-2.

Isto é TAMBÉM o experimento confirmatório: se o fixpoint nativo convergir com o
free-old nativo desarmado, o free-old É o culpado.

### Nível 2 — fechar o furo de aliasing (raiz)
Com o sítio exato de §6.5 em mãos: ou (H2b) o backend nativo deve garantir que a
ÚNICA referência viva pós-grow é o fat-slot atualizado (não emitir store através
do endereço pré-grow retido), ou (H2a) `assign_frees_old` deve REJEITAR o
self-append cujo alias ela perde (endurecer `chain_stats_block`/`count_reads_block`
para a forma de captura que o backtrace revelar) — corrigindo AMBOS os legs.

## 6.5 O ÚNICO dado a capturar (você roda; um flag, uma corrida)

Preciso do BACKTRACE DO LIBERADOR do bloco de 560 B (o poison reporta o sítio de
DETECÇÃO, que NÃO é o escritor nem o liberador — não persiga esse). O runtime já
tem a alavanca: `TEKO_FO_MAX=N` limita o park aos primeiros N grows (bisect do
park culpado) e `TEKO_FO_TRACE` no limite despeja o backtrace do SÍTIO DE PARK
(`teko_rt.c:4427-4440`).

Procedimento (uma corrida do build nativo culpado, com `POISON_FREE=1`):
1. Bisect `TEKO_FO_MAX` até o menor N em que a corrupção em offset 32 AINDA ocorre
   (N-1 = o park culpado).
2. `TEKO_FO_MAX=N TEKO_FO_TRACE=1` → despeja o backtrace do liberador desse park.

O backtrace do liberador nomeia a FN Teko exata e o acumulador `xs` cujo free-old é
inseguro — pinando o construto-fonte e desempatando H2b×H2a. Em ALTERNATIVA (mais
barato e já é o fix): aplicar o Nível-1 e reportar se o fixpoint nativo converge —
convergência = free-old confirmado; então capturamos o backtrace só para o Nível-2.

Peça única ao coordenador: rode o build nativo culpado com
`TEKO_NATIVE_POISON_FREE=1 TEKO_FO_TRACE=1` (bisectando `TEKO_FO_MAX` se preciso) e
devolva o "== FO park #… ==" backtrace do park cujo bloco é reportado corrompido em
offset 32. OU aplique o Nível-1 e diga se o gen2/gen3 nativo converge.

## 6.6 Ponteiros de arquivo (absolutos) — H2

- Runtime (C, não muda): `/home/user/teko-lang/src/runtime/teko_rt.c:4409`
  (`tk_slice_push_fo`), `:4442`/`:4367` (os `tk_free_block` de free-old),
  `:4483` (`tk_free_block`), `:4332`/`:4348` (push-cache + guarda region_gen),
  `:2025` (`tk_region_drop`, sem purge de cache), `:4427-4440` (TEKO_FO_MAX/TRACE),
  `:1509` (`tk_poison_scan_one`).
- Lado nativo (onde o fix vai): `/home/user/teko-lang/src/lir/lower.tks:13592`
  (o ramo de upgrade — DESARMAR no Nível-1), `:13608` (`is_frees_old_self_append`),
  `:13627` (`lower_assign_frees_old`), `:1459` (`emit_region_drops`, NP4 native).
- Prova compartilhada (Nível-2 se H2a): `/home/user/teko-lang/src/checker/escape.tks:643`
  (`assign_frees_old`), `:365` (`fn_frees_old_vars`), `:389` (`collect_frees_old_stmt`).
- Twin de arena: `/home/user/teko-lang/src/mem/unsafe/arena.tks` (o aliased-UAF gap
  documentado), `/home/user/teko-lang/src/mem/unsafe/rawbuf.tks`.

---

# §7 O DEFEITO DE LOWERING NATIVO (fix que honra o spec)

## Reframe do dono (aceito)
Não é furo de prova (H2a CAI): `assign_frees_old` (`escape.tks:643`) estabelece a
INVARIANTE DE CADEIA-LINEAR — nascido-uma-vez-de-empty, sem renascimento, sem
outras escritas, todo read não-final é base de self-append — logo GARANTE que
NENHUM alias ao buffer antigo sobrevive ao copy-grow; é por isso que estacioná-lo
é são. O modelo semântico de região é COMPARTILHADO e correto; `teko_rt.c` é o
oráculo congelado. Portanto o UAF é um DEFEITO DE IMPLEMENTAÇÃO DO LOWERING
NATIVO que cria um alias que a prova provou não poder existir no nível-fonte.

## 7.1 Por que só o NATIVO estaciona no ROOT (a condição native-only)
`tk_free_block` RETORNA CEDO quando `tk_cur_rsp != 0` (dentro de uma região
scoped, `teko_rt.c:4494`) — um free-old dentro da região de frame `fr` NÃO
estaciona nada (o buffer morre no `tk_region_drop_u` do frame). Então um bloco
ESTACIONADO na free-list (o que o poison-scan varre) só pode nascer em escopo
RAIZ (`cur_rsp == 0`). No backend nativo isso acontece por causa do BRACKET DE
CONVEYANCE NP3b: um acumulador que ESCAPA (é retornado, ∈ `fn_escaping_vars`) tem
seu RHS envolvido por `emit_region_leaves … emit_region_enters`
(`lower_assign_conveyed`, `lower.tks:13224`), que DESVIA a região corrente para o
âncora `rr` = ROOT (`cur_rsp==0`) durante a avaliação do RHS. O free-old do grow
roda então em ROOT → o buffer antigo é ESTACIONADO no root. Um acumulador
não-escapante (frame-local) nunca estaciona. Isto explica:
- por que o bloco corrompido é um buffer estacionado em root e não um chunk de
  região; e
- por que desarmar UM site de free-old só expôs o próximo (§7.2): TODO
  acumulador fat escapante que faz free-old dentro do bracket estaciona em root —
  é um padrão, não um site.

O leg C não tem esse bracket de região nativo (o modelo de lifecycle de região
NÃO é compartilhado — é a metade native-only que o `modelo-de-memoria-por-escopo
-0.3.1.md §3a` marca como propensa a defeito), então o leg C nunca cria a janela
que o representante nativo por-endereço explora → C byte-idêntico.

## 7.2 O SEGUNDO site (confirmado estaticamente)
Com `is_frees_old_self_append` desarmado, o park de 1408 B `[]str` persiste porque
o compilador usa `teko::mem::push_fo` EXPLÍCITO (o free-old direto, que NÃO passa
por `is_frees_old_self_append` — `slice_push_symbol("push_fo")` →
`tk_slice_push_fo`, `lower.tks:10508`), pervasivamente:
- `src/lexer/lexer.tks:762/771/780` (constrói `tokens`, `[]Token`);
- `src/checker/typer.tks:3809/3815/3826` e `src/checker/monomorph.tks:817`
  (constroem listas de statements);
- `src/build/assemble.tks:102` (`teko::mem::free(tokens)`).
Qualquer um cujo acumulador seja `[]str` (ex.: tabelas de nomes/símbolos do
checker — a fase onde o gen2 morre) reproduz o MESMO padrão retained-pre-grow. É
o mesmo defeito manifestando em múltiplos push/free (consistente com o achado
AL3 "distributed copy-grow" ser GLOBAL, `docs/design/al1-proof-report.md:41/63`).

## 7.3 Onde está a escrita estante (ranqueado; a instrução exata precisa do dado de §7.5)
O write é um par fat `{ptr,len}` gravado no índice de elemento `i` (offset `i*16`
= ptr; `i*16+8` = len) através de uma base que aponta para o buffer antigo já
liberado/estacionado — casando com element 2 (offset 32, ptr) e element 6 (offset
104, len). A `store_fat_slot` (`lower.tks:11593`) é o gravador final em TODOS eles
(`ptr@base+0`, `len@base+8`). `lower_fat_element_push` NÃO grava através da base
(passa a base por valor ao runtime e guarda o RESULTADO no slot), então o estante
está num dos caminhos abaixo:

- **(C) LÍDER — liveness de isel/regalloc sobre a chamada de grow.** A base fat
  (`ro.ptr`/`base.ptr`) é o operando de argumento de `tk_slice_push_fo`; a chamada
  REALOCA e LIBERA esse buffer, mas o isel/regalloc nativo pode manter a vreg da
  base VIVA (comum/hoist de um load do slot, ou a base reusada por um store
  subsequente) através da chamada — um alias que a prova de fonte não pode ver
  porque é introduzido pela ALOCAÇÃO DE REGISTRADOR da base. Global (todo site de
  grow), casando o "pattern-level" e o AL3-global. Alvo: `src/backend/isel_x86_64.tks`
  / `src/backend/regalloc*.tks` — a chamada de grow deve CLOBBERAR qualquer vreg de
  base de buffer fat viva.
- **(A) store de elemento por índice através de base pré-grow.**
  `lower_assign_index`/`store_assign_elem` (`lower.tks:13603/13662`) e
  `store_array_elem` (`:13058`) computam `element_addr_at(ro.ptr, i, 16)`
  (`:12813`) e gravam via `store_fat_slot`. Se `ro.ptr` foi materializada antes de
  um grow intercalado (numa sequência de statements sobre o mesmo acumulador),
  grava no buffer estacionado.
- **(B) região do item/conveyance sob o bracket.** `reserve_item_slot`
  (`:11076`) e a materialização do item em `lower_fat_element_push` (`:10588`)
  correm com região corrente = root durante o bracket; um slot/box alocado na
  região errada pode ser lido após o drop. Menos provável (o item é copiado antes
  da chamada), listado para completude.

## 7.4 SPEC do fix (honra o spec; NÃO implementado)
Princípio: o lowering nativo deve SUSTENTAR a invariante de cadeia-linear que a
prova já garante — NENHUM store pode ter como alvo uma base de buffer fat que um
grow substituiu. SEM tocar `assign_frees_old` (prova correta), SEM tocar
`teko_rt.c` (oráculo congelado), SEM desabilitar free-old.

Fix mínimo, por hipótese:
- Se (C): na lowering/ABI da chamada de grow (`tk_slice_push`/`_fo`/
  `tk_append_bytes_fo`), marcar a vreg da base do buffer como MORTA/CLOBBERED na
  saída da chamada, para que o isel/regalloc jamais comum nem hoiste um load da
  base através dela nem reuse a base pré-grow depois. É o fix de PADRÃO (todos os
  sites de grow), o mais provável dado o AL3-global.
- Se (A): todo store de elemento fat deve RE-LER a base do acumulador do seu frame
  slot IMEDIATAMENTE antes de `element_addr_at` (nunca reusar uma base
  materializada antes de um grow intercalado).
Ambos mantêm o leg C byte-idêntico (mudam só a liveness/re-leitura da rota
nativa; o emissor C e o programa emitido não mudam) e o self-fixpoint nativo
converge porque a rota nativa passa a honrar a mesma invariante que o leg C já
honra por construção do seu modelo de lvalue.

Escopo: PADRÃO (não pontual) — toca todos os sites de push/free de elemento fat
e/ou a liveness das chamadas de grow no isel. Consistente com o AL3 "global".

## 7.5 O ÚNICO dado a capturar (você roda; um watchpoint, uma corrida)
Preciso da INSTRUÇÃO GRAVADORA (o poison reporta a DETECÇÃO, não o escritor).
Toda instrução LIR carrega (line,col) da fonte, então o endereço da store
gravadora → a linha/coluna → o statement Teko exato → o caminho de lowering
(A/B/C).

Procedimento (uma corrida do build nativo culpado, sob gdb):
1. Rode com `TEKO_NATIVE_POISON_FREE=1`; no abort, capture o ENDEREÇO A do bloco
   (o poison já imprime `parked block 0x…`).
2. Reexecute; quando A for estacionado (breakpoint em `tk_free_block` com
   `p == A`), coloque um HARDWARE WATCHPOINT em `A+16 .. A+bytes` (o payload).
3. A store que dispara o watchpoint — seu backtrace + a (line,col) da instrução —
   nomeia o statement Teko e desempata (C) isel-liveness × (A) index-store.

Pedido único ao coordenador: rode o build nativo culpado sob gdb com poison,
ponha um watchpoint no payload do bloco de 560 B/1408 B quando estacionado, e
devolva o backtrace + a instrução gravadora (endereço/линha-coluna). Com isso
fecho a instrução exata e finalizo o fix de §7.4 para o coder aplicar.

## 7.6 Ponteiros de arquivo (absolutos) — §7
- Condição native-only: `/home/user/teko-lang/src/lir/lower.tks:13224`
  (`lower_assign_conveyed`, o bracket), `:1416`/`:1447`/`:1480` (leaves/enters/
  drops), `:1507` (`binding_conveys_escape`); runtime early-return
  `/home/user/teko-lang/src/runtime/teko_rt.c:4494`.
- Segundo site (explícito push_fo): `/home/user/teko-lang/src/lexer/lexer.tks:762`,
  `/home/user/teko-lang/src/checker/typer.tks:3809`,
  `/home/user/teko-lang/src/checker/monomorph.tks:817`,
  `/home/user/teko-lang/src/build/assemble.tks:102`;
  mapeamento `/home/user/teko-lang/src/lir/lower.tks:10508` (`slice_push_symbol`).
- Gravadores fat: `/home/user/teko-lang/src/lir/lower.tks:11593` (`store_fat_slot`),
  `:13603`/`:13662` (`lower_assign_index`/`store_assign_elem`), `:13058`
  (`store_array_elem`), `:12813` (`element_addr_at`), `:10588`
  (`lower_fat_element_push`).
- Alvo do fix (C): `/home/user/teko-lang/src/backend/isel_x86_64.tks`,
  `/home/user/teko-lang/src/backend/regalloc.tks`/`regalloc_x86.tks` — liveness da
  base sobre a chamada de grow.

---

# §7.7 ORDEM EMITIDA — resposta ao hint do dono (transfer-após-clear)

Hint do dono: "tentando transferir um ponteiro de região DEPOIS da memória ter
sido limpa" — um bug de SEQUENCIAMENTO: o transfer do ponteiro emitido APÓS o
free/park/region-drop. Âncoras: `port-…-backend-nativo-0.3.1.md §2c` (leave-
antes-de-`own_returned_value`) e `modelo-de-memoria-por-escopo-0.3.1.md §1`
(não-UAF-por-construção). Verifiquei a ORDEM EMITIDA em cada aresta de saída.

## (1) A ordem emitida, aresta por aresta (CLEAR = park/drop; TRANSFER = box/store/ret)

**A. self-append fat `xs = push(xs, item)` escapante (`lower_assign_conveyed:13224`
→ `lower_assign_frees_old:13514`):**
```
tk_region_leave() × N            (emit_region_leaves — current → rr=root, cur_rsp→0)
  load xs.ptr (=O_old), xs.len   (lower_fat_expr base)
  store item → item_slot
  CALL tk_slice_push_fo(O_old,len,item_slot,16,&out)  → O_new; PARK O_old  ← CLEAR
  load out
  store {O_new,out} → xs_slot     (store_fat_slot)                         ← TRANSFER (alvo O_new, VIVO)
tk_region_enter_u(fr) × N        (emit_region_enters — current → fr)
```
O TRANSFER (store_fat_slot) mira O_new (o buffer VIVO), não O_old. A base O_old
(vreg) tem seu último uso NO argumento da CALL — morta depois. Ordem CORRETA.

**B. return de AGREGADO `return s` / auto-return de cauda (`lower_return:6446`,
`lower_fn_body:13825`):**
```
replay_defers
tk_region_leave() × N            (LEAVE — current → root)                 [§2c: ANTES do box]
own_returned_value → tk_slice_elem_box(src, bytes) → dst em root          ← TRANSFER (box copia p/ root)
tk_region_drop_u(fr) × N         (DROP — libera fr)                       ← CLEAR (por ÚLTIMO)
ret dst
```
LEAVE antes do box (§2c HONRADO); DROP estritamente APÓS o box. Ordem CORRETA.

**C. return FAT / auto-return fat `-> []str` (`lower_return_fat:6479`,
`lower_fn_body_fat:13865` + `lower_conveyed_tail_expr_stmt_fat`):**
```
tk_region_leave() × N            (LEAVE — current → root)  [ANTES da construção]
  <constrói o valor fat sob current=root; um push_fo aqui PARKa O_old em root> ← CLEAR
store len → ret_len_slot
tk_region_drop_u(fr) × N         (DROP)                                    ← por ÚLTIMO
ret ptr (=O_new, em root)
```
`lower_conveyed_tail_expr_stmt_fat` faz leave-ANTES/enter-DEPOIS da construção
(confirmado no corpo). LEAVE antes de construir; DROP por último. Ordem CORRETA.

**Conclusão (1):** em TODAS as arestas — `lower_return`, `lower_return_fat`,
`lower_fn_body`, `lower_fn_body_fat`, `lower_conveyed_tail_expr_stmt_fat`,
`close_function`, `close_loop_body` — o CLEAR (park/drop) é emitido ESTRITAMENTE
APÓS o TRANSFER (box/store/ret), e o `tk_region_leave()` precede `own_returned_value`
(§2c honrado). **A hipótese de INVERSÃO DE ORDEM DE EMISSÃO está REFUTADA no
nível de `lower.tks`.** O parking-em-root vem da CORREÇÃO do bracket (current=root
→ `tk_free_block` não retorna cedo), não de um drop fora de ordem.

## (2) Qual "ponteiro de região" e qual regra é violada
NÃO é (a) o endereço do box agregado nem (b) o handle-âncora `rr` — esses estão
ordenados corretamente (box→drop; `rr` nunca é dropado). É **(c) a BASE do buffer
de slice pré-grow** (O_old). A intuição do dono está CORRETA na CLASSE (um
ponteiro para memória limpa é usado), mas o uso-após-clear NÃO está na ORDEM DE
EMISSÃO LIR (que está certa) — está no nível de REGISTRADOR: a vreg da base
sobrevive à CALL de grow que limpou sua memória. A regra violada NÃO é §2c
(honrada) — é a invariante de residência/cadeia-linear do `modelo §1`
(não-UAF-por-construção): a base deve estar MORTA assim que o grow a substitui, e
o isel/regalloc a mantém viva/hoisted.

Nota de reforço: `lower.tks` re-lê `xs` do frame slot a cada statement/iteração
(`lower_var_fat:load_fat_slot`; loops não threadam fat como block-arg — só
escalares, `close_loop_body:7580`), então a base estante NÃO nasce em `lower.tks`.
Nasce na alocação de registrador do backend, que pode comum/hoistar o load da base
ou reusar a vreg da base através da CALL de grow (LICM sobre um loop de
push/index-write) — precisamente "transferir o ponteiro depois da memória limpa",
um passo abaixo da emissão LIR.

## (3) Fix mínimo que honra o spec
Como a ordem de emissão já está certa, o fix NÃO é reordenar `lower.tks` — é
tornar a CALL de grow um CLOBBER da base no modelo de liveness/ABI do backend:
- Em `src/backend/isel_x86_64.tks` (lowering da call) + `src/backend/regalloc.tks`/
  `regalloc_x86.tks`: modelar `tk_slice_push`/`tk_slice_push_fo`/`tk_append_bytes_fo`
  como MATANDO (clobber) o operando-base do buffer fat na saída, de modo que
  nenhum load da base seja hoisted/commoned através dela e nenhuma vreg de base
  pré-grow seja reusada depois. Isso força todo acesso pós-grow a partir do
  RESULTADO (o buffer retornado) — exatamente a invariante linear-chain que a prova
  garante e que o leg C honra por construção do seu modelo de lvalue.
- (Defensivo, `lower.tks`, opcional) re-ler a base do frame slot IMEDIATAMENTE
  antes de cada `element_addr_at`/`store_fat_slot` de elemento, para que mesmo um
  regalloc agressivo não tenha uma base pré-grow para reusar.

Sem tocar `assign_frees_old` (prova sã), sem tocar `teko_rt.c` (oráculo congelado),
sem desabilitar free-old; o emissor C e o programa emitido não mudam → C
byte-idêntico; o self-fixpoint nativo converge.

## (4) Dado único que fecha (emissão-order vs regalloc)
Se o dono suspeita de uma aresta de emissão que EU não inspecionei, o teste é a
INSTRUÇÃO GRAVADORA (§7.5): watchpoint no payload do bloco estacionado; a
(line,col) da store que dispara → se cai num `store_fat_slot`/`element_addr_at`
cuja base foi carregada no MESMO statement pós-leave → é regalloc (clobber fix);
se cai num transfer emitido APÓS um `tk_region_drop_u`/park no MESMO bloco LIR →
é uma inversão de emissão que eu não vi, e reordena-se ali. Uma corrida, um
watchpoint, desempata definitivamente.

## §7.7 ponteiros (absolutos)
- Ordem correta confirmada: `/home/user/teko-lang/src/lir/lower.tks:6446`
  (`lower_return`), `:6479` (`lower_return_fat`), `:13825` (`lower_fn_body`),
  `:13865` (`lower_fn_body_fat`), `:13224` (`lower_assign_conveyed`),
  `lower_conveyed_tail_expr_stmt_fat`, `:13190` (`close_function`), `:7576`
  (`close_loop_body`); box `:10750` (`box_aggregate_value_at`)/`emit_elem_box`.
- Clear-precede-cedo só via bracket: runtime `/home/user/teko-lang/src/runtime/teko_rt.c:4494`.
- Alvo do fix: `/home/user/teko-lang/src/backend/isel_x86_64.tks`,
  `/home/user/teko-lang/src/backend/regalloc.tks`/`regalloc_x86.tks`.
