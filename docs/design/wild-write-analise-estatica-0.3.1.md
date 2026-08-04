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
