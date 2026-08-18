# Mapa de remoção de testes — 0.3.1 (frontend D)

Blueprint read-only. Diz quais testes CAEM e quais FICAM sob a LEI do dono
(ratificada 2026-08-18): o fixpoint (self-build) É a prova do que o compilador
exercita; a stdlib É o compilador (monólito). Este doc NÃO remove nada — só
classifica. HEAD base: `64c07d77` (`fix/retirement`).

## 0. A LEI aplicada (critério)

REMOVER:
- (a) testes que validam a COMPILAÇÃO (strings/AST sintéticas p/ parser/codegen/ast) — tautológicos.
- (b) comportamento de qualquer função que `src/` chama (gen1 a executa ao compilar).
- (c) genéricos/monomorph — o compilador USA genéricos e compila o próprio código que os instancia.
- (d) backend native — o CI (6 pernas, 4 native) o exercita.

MANTER SÓ:
- erro/diagnóstico — o self-build só compila código VÁLIDO, nunca dispara caminhos de rejeição.
- comportamento de função que o compilador NUNCA chama ao rodar (subcomando fora do caminho de compilação; concorrência/FFI que o self-host não dispara).

Regra de desempate (LEI do dono): "na dúvida, LISTAR para revisão — não remover".
Por isso este mapa tem uma faixa AMBÍGUO explícita e conservadora.

## 1. Inventário do corpus

| Corpus | Local | Contagem |
|---|---|---|
| Suites unitárias `.tkt` (`#test`) | `src/**` | 127 |
| Fixtures de regressão `.tkr` | `examples/regressions/*` + `regressor.tkr` (raiz) | 86 (85 dirs + 1 raiz) |
| Fontes de cenário `.tks` | `cases/` (raiz, citados por `regressor.tkr`) | 15 |
| Manifestos de fixture `.tkp` | 1 por dir de regressão | 86 |

Como cada corpus é executado:
- `.tkt`: descobertos no source tree e montados no programa mesclado (`filter_tkt`/
  `include_tests`); seus `#test` rodam no lane `teko test .`.
- `.tkr`: SEM discovery/glob. Só rodam os caminhos EXPLÍCITOS e ORDENADOS de
  `teko.tkp [tests] regression` (owner ruling 2026-07-24). Um path listado que não
  resolve é ERRO DE MANIFESTO (M.3), nunca skip silencioso.

## 2. A wiring do gate (fonte da verdade do "protegido")

`teko.tkp [tests] regression` lista 31 paths. 29 resolvem a dirs existentes + `regressor.tkr` (raiz):

`regressor.tkr`, `own_native`, `mem`(*), `const`, `syntax`, `crossmodule`,
`diagnostics`, `builtins`, `manifest`, `iso`, `multi_assign`,
`multi_assign_reject`, `capability_iface`, `operator_overload_compose`,
`macro_expand`, `macro_type_splice`, `comptime_expand`, `comptime_reflect`,
`comptime_fields`, `grouped_union`, `group_bind`, `group_bind_reject`,
`recursive_union`, `generic_union_arg`, `global_access`, `global_reject`,
`s17_if_region`, `s17_arch`, `s17_composite`, `s16_cond_const`, `s16_os_cross`.

(*) ACHADO ADJACENTE (reportar, não corrigir aqui): `examples/regressions/mem/mem.tkr`
está na lista mas o dir NÃO EXISTE. Pelo próprio manifesto isso é M.3 (manifest error).
Ou o lane fail-closed está quebrado, ou a lista não é lida como o header afirma. REPORTADO.

Os outros 56 dirs de `examples/regressions/*` NÃO estão na lista — não são executados
pelo gate principal. Isso decide a classificação: fixture no gate = PROTEGIDA; fora do
gate = classificável só por conteúdo.

## 3. Classificação — `.tkt` (src/**, 127)

Sinal usado: idiomas de asserção. Rejeição/diagnóstico = `expect_error`,
`expect_error_message`, `expect_message`, `assert_stop`, `diagnostic`. Comportamento
feliz = `expect_value`, `assert_eq_str`, `assert_word`, `assert_vector`,
`expect_present`, `expect_ok`, `expect_node`, `assert_fixpoint`, `assert_float`.

### 3.1 MANTER — asseveram diagnóstico de ESTÁGIO DO COMPILADOR (keep forte)

O checker/parser/harness rejeitando entrada inválida É o caminho de rejeição que o
self-build nunca dispara.

- `src/checker/checker_test.tkt` (28 barreiras — typer recusa AST mal-tipada)
- `src/parser/parser_test.tkt`
- `src/build/regression_test.tkt` (17 — diagnósticos do harness de regressão)
- `src/build/regr_group_test.tkt`
- `src/build/tkr_test.tkt`
- `src/build/assemble_test.tkt`
- `src/build/project_test.tkt`
- `src/build/linker_test.tkt`
- `src/lir/lower_test.tkt`

### 3.2 REMOVER — comportamento de função que src/ chama / compilação tautológica / native / genéricos (104)

Todos os `.tkt` sem asserção de erro. Agrupados por motivo:

- (d) BACKEND NATIVE — exercitado pelas 4 pernas native do CI:
  `src/backend/abi_sysv64_test.tkt`, `abi_win64_test.tkt`, `dwarf_test.tkt`,
  `isel_arm64_test.tkt`, `isel_x86_64_test.tkt`, `minst_test.tkt`, `minst_x86_test.tkt`,
  `objfile_ar_coff_test.tkt`, `objfile_ar_macho_test.tkt`, `objfile_ar_test.tkt`,
  `objfile_coff_test.tkt`, `objfile_elf_test.tkt`, `objfile_macho_test.tkt`,
  `regalloc_match_test.tkt`, `regalloc_test.tkt`, `regalloc_x86_test.tkt`.
- (a)/(b) CAMINHO DE COMPILAÇÃO — o self-build monta e roda estas funções ao se compilar:
  `src/codegen/codegen_test.tkt`, `codegen/ffi_export_test.tkt`, `emit/tkb_test.tkt`,
  `lexer/lexer_test.tkt`, `lir/lir_test.tkt`, `lir/lower_variant_test.tkt`,
  `checker/*` (todos exceto `checker_test`): `assert_builtin_test`, `borrow_test`,
  `closures_test`, `comptime_fold_test`, `consteval_test`, `instantiate_order_test`,
  `metrics_test`, `nidx_test`, `pt_census_test`, `residence_test`, `spine_test`,
  `test_assert_test`, `names/names_test`, `casting/casting_test`, `cmp/keys_test`,
  `build/manifest_test`, `build/tkp_rule_test`, `build/reachability_test`,
  `build/prune_test`, `build/project_binary_path_test`, `build/own_native_pairing_test`,
  `build/fixture_guard_test`, `build/ledger_test`.
- (c) GENÉRICOS/MONOMORPH — o compilador instancia genéricos ao se compilar:
  `src/checker/generics_test.tkt`, `sort/sort_generic_test.tkt`.
- (b) BIBLIOTECA (happy-path) que o `src/` chama ao compilar — list/map/str/fmt/io/
  collections/sort/iter/numeric/text/time/math etc. exercitados pelo próprio compilador:
  `collections/combinators_test`, `collections/dictionary_test`, `collections/hashset_test`,
  `collections/list_test`, `collections/map_test`, `collections/map_to_dictionary_test`,
  `collections/priority_queue_test`, `collections/sorted_dictionary_test`,
  `collections/sorted_set_test`, `list/list_test`, `sort/cmp_test`, `sort/search_test`,
  `sort/select_test`, `sort/sort_test`, `iter/iter_test`, `fmt/fmt_test`,
  `io/stream_test`, `math/checked_test`, `math/math_test`, `numeric/bigint/bigint_test`,
  `numeric/bigint/bigint_ops_test`, `numeric/dec/dec_test`, `numeric/surface_test`,
  `text/text_test`, `time/time_test`, `runtime/teko_rt_test`.
- (b) HARNESS/ASSERT — plumbing exercitado por rodar `teko test .`:
  `assert/assert_test.tkt`, `test/capture_test.tkt`, `test/journal_guard_test.tkt`,
  `test/scratch_collision_test.tkt`, `test/scratch_guard_test.tkt`,
  `journal/journal_test.tkt`, `build/progress_test.tkt`, `build/help_test.tkt`,
  `build/init_test.tkt`.

(Ver §5 AMBÍGUO para os que não vão confiantes p/ REMOVER.)

### 3.3 AMBÍGUO — `.tkt` (marcar, não decidir)

- STDLIB, caminho de ERRO (o self-build alimenta só entrada válida → o caminho de erro
  destes parsers NUNCA dispara no self-build; keep-set (a) por analogia, mas é
  comportamento de biblioteca, não diagnóstico de compilador):
  `encoding/base64/base64_test`, `encoding/cbor/cbor_test`, `encoding/ini/ini_test`,
  `encoding/mime/mime_test`, `encoding/msgpack/msgpack_test`,
  `encoding/protobuf/protobuf_test` (28 casos de decode malformado),
  `encoding/url/url_test`, `encoding/xml/xml_test`, `encoding/yaml/yaml_test`,
  `crypto/xmldsig/xmldsig_test`, `process/process_test`.
  Sub-caso: os `#test` HAPPY dentro destes arquivos são REMOVER; os de erro, MANTER.
  Recomendação: MANTER o arquivo, PODAR os `#test` happy (passe de precisão por-função).
- ENCODE native com diagnóstico "unsupported instruction/form":
  `backend/encode_arm64_test` (16), `backend/encode_x86_64_test` (2). Native (d) diz
  REMOVER; mas asseveram o caminho de recusa do emissor. Marcar.
- SUBCOMANDO FORA DO CAMINHO DE COMPILAÇÃO (self-build nunca roda o LSP):
  `lsp/jsonrpc_test`, `lsp/server_test` (24 asserções de mensagem/erro), `lsp/symbols_test`.
  keep-set (b'): "função que o compilador NUNCA chama ao rodar" — o self-host compila,
  não serve LSP. Lean MANTER. Marcar.
- CRYPTO (correção, não erro): `crypto/*` (aead, cipher, cose, crypto, des, ec_p256,
  ed25519, hash, jose/jws, jose/jwt, kdf, mac, password, rsa), `compress/inflate_test`,
  `regex/regex_test`, `encoding/asn1`, `encoding/bson`, `encoding/csv`, `encoding/fixed`,
  `encoding/protobuf` (happy), `encoding/xml/yaml` (happy). O compilador NÃO chama crypto
  ao compilar (não há assinatura no caminho de build) → keep-set (b'). PORÉM são
  comportamento de biblioteca pura de valor, alto custo de manutenção. Marcar: o dono
  decide se cripto entra no keep-set "nunca-chamado" ou cai por ser fora do escopo do
  monólito-compilador. (Recomendação arquiteto: KATV — a prova de cripto não está no
  self-build; se cair, cai por decisão de produto, não pela LEI do fixpoint.)

## 4. Classificação — `.tkr` (regressão, 86)

### 4.1 PROTEGIDO — o build/gate DEPENDE (NÃO PODE CAIR)

Referenciadas por `teko.tkp [tests] regression` ou `regressor.tkr`. Marcadas PROTEGIDO
(item 4 do escopo). Remover qualquer uma exige cirurgia CASADA em `teko.tkp` +
re-medir o invariante "10 builds por host" do lane fail-closed — FORA de um passe de
remoção seguro.

- `regressor.tkr` (raiz) + os 15 `cases/*.tks` que ela cita — PROTEGIDO absoluto (R0,
  native-args, builtin-qualifier, defer-nas-saídas, alias-fat-field, null-union,
  byte-view, bare-block). Vários já são cenários de rejeição (`When compilation fails`).
- `examples/regressions/s16_cond_const/` — PROTEGIDO (§16 const condicional; nomeado no escopo).
- `examples/regressions/s16_os_cross/` — PROTEGIDO (§16 emissão OS-cross; nomeado no escopo).
- Demais no `[tests] regression`: `own_native`, `const`, `syntax`, `crossmodule`,
  `diagnostics`, `builtins`, `manifest`, `iso`, `multi_assign`, `multi_assign_reject`,
  `capability_iface`, `operator_overload_compose`, `macro_expand`, `macro_type_splice`,
  `comptime_expand`, `comptime_reflect`, `comptime_fields`, `grouped_union`, `group_bind`,
  `group_bind_reject`, `recursive_union`, `generic_union_arg`, `global_access`,
  `global_reject`, `s17_if_region`, `s17_arch`, `s17_composite`.

TENSÃO DE LEI registrada: `generic_union_arg` (genérico, LEI (c)), `comptime_*`/`macro_*`/
`recursive_union`/`grouped_union`/`operator_overload_compose`/`iso`/`builtins`/`const`/
`syntax`/`global_access`/`capability_iface`/`multi_assign` (comportamento, LEI (b)) e os
`s17_*` são, POR CONTEÚDO, removíveis. Estão PROTEGIDOS pela wiring do gate. Resolução
recomendada: NÃO tocar neste passe; se o dono quiser purgá-los, é um segundo passe
"enxugar `[tests] regression`" com re-medição do lane. HALT p/ dono só se ele exigir
purga agora (tensão genuína entre LEI-de-testes e invariante-de-build).

### 4.2 MANTER — rejeição/diagnóstico, FORA do gate (keep-set erro/diagnóstico) (17)

`When compilation fails` / `Then diagnostic`. O self-build nunca dispara estes.
Não estão no `[tests] regression` — ver ACHADO em §6 (keep-worthy mas órfãos do gate).

`block_expr_reject`, `class_key_no_ihash`, `defaults_named_reject`, `dict_key_no_ihash`,
`dot_construction_reject`, `marshall_reject`, `match_universal_reject`,
`properties_reject`, `s17_if_reject`, `self_ctor_removed_err`, `shape_constraint_reject`,
`svc_reject`, `trait_fold_reject`, `trait_mixin_reject`, `trait_type_reject`,
`type_overload_reject`, `value_struct_mut_reject`.

### 4.3 REMOVER — comportamento/genérico exercitado pelo self-build, FORA do gate (20)

- (c) GENÉRICOS: `generic_factory_owner_param`, `generic_sibling_method`, `generic_sort`,
  `type_param_union_return`, `shape_constraint`.
- (b) COMPORTAMENTO que o compilador roda ao compilar (match/overload/props/dot/union/
  value/collections/marshall/defaults/block): `block_expr` (já coberto por
  `cases/bare_block_scope.tks` em `regressor.tkr`), `collections_fase1b`,
  `defaults_named`, `dot_construction`, `map_to_dictionary`, `marshall`,
  `match_universal`, `overload_resolve`, `properties`, `service_svc`, `trait_mixin`,
  `type_overload`, `union_tag_cross_ns`, `value_struct_mut`, `value_type_operators`.

Cada REMOVER acima tem um irmão `_reject` na §4.2 (o valor de diagnóstico fica; o de
comportamento cai).

### 4.4 AMBÍGUO — `.tkr` (marcar, não decidir) (19)

- CONCORRÊNCIA — o compilador compila single-thread; o self-host NUNCA faz spawn/chan/
  futex ao rodar → keep-set (b') "nunca-chamado". Lean MANTER:
  `chan_memchan_spawn`, `chan_oschan_spawn`, `futex_condvar_barrier`,
  `futex_mutex_counter`, `spawn_basic`, `thread_stack_new`, `rx_pop_closed`.
- SYSCALL/§16 raw — o compilador USA write/mmap/clock/getrandom/exit (self-build
  exercita, LEI (b) → REMOVER), MAS a correção do raw-syscall por (arch,os) é a prova do
  §16 que o C-route não cobre e que o self-build só prova no host. Marcar:
  `sys_write`, `sys_mmap`, `sys_clock`, `sys_clock_syscall`, `sys_getrandom`, `sys_exit_group`.
- ARENA raw load/store/mmap/wordptr/region — arena é usada pesado pelo compilador (LEI (b)
  → REMOVER), mas os primitivos raw load/store podem testar cantos de lowering native:
  `arena_loadstore`, `arena_mmap`, `arena_wordptr`, `current_region_stack`,
  `user_prog_arena_alloc`.
- FFI struct-by-value: `extern_type_struct` — o compilador linka só libc; struct-by-value
  FFI pode não ser exercitado. Marcar.

## 5. Ambíguos consolidados (fronteira erro/diagnóstico × comportamento exercitado)

Para revisão do dono, sem decisão do arquiteto:
1. `.tkt` stdlib com caminho de erro (base64/cbor/ini/mime/msgpack/protobuf/url/xml/yaml/
   xmldsig/process) — erro-de-biblioteca conta como "rejeição que o self-build não
   dispara"? (Recomendo: MANTER só os `#test` de erro, podar os happy.)
2. `.tkt` crypto/regex/compress/asn1/bson/csv/fixed — comportamento puro que o compilador
   NÃO chama ao compilar. keep-set (b') diz MANTER; custo de manutenção diz REMOVER.
3. `.tkt` LSP (jsonrpc/server/symbols) — subcomando fora do caminho de compilação.
4. `.tkt` encode native (`encode_arm64`/`encode_x86_64`) — native (d) vs diagnóstico de emissor.
5. `.tkr` concorrência (7), syscall/§16 (6), arena raw (5), FFI (1) — §4.4.

## 6. Remoção casada + achados adjacentes (mapa de referências)

- NENHUM dir que eu marco REMOVER (§4.3) está no `teko.tkp [tests] regression` → a remoção
  desses dirs NÃO exige editar `teko.tkp`. Só apaga o dir inteiro (`.tkr`+`.tkp`+`.tks`+`cases`).
- Os `.tkp` de dirs cross-referenciam OUTROS dirs em COMENTÁRIOS de header (ex.:
  `arena_wordptr.tkp`/`sys_getrandom.tkp`/`sys_mmap.tkp` citam `sys_write`;
  `current_region_stack.tkp` cita `arena_mmap`; `trait_type_reject.tkr` cita `trait_mixin`;
  `sys_clock.tkp` cita `generic_sort`). Remover um dir deixa prosa órfã no header de um
  irmão — limpeza COSMÉTICA, casada, não quebra build. Mapear ao remover.
- ACHADO 1 (REPORTAR): `examples/regressions/mem/mem.tkr` listado em `teko.tkp` mas
  inexistente. M.3 por definição. Não corrigir aqui.
- ACHADO 2 (REPORTAR): os 17 fixtures de rejeição da §4.2 têm VALOR de LEI (diagnóstico
  que o self-build não dispara) mas NÃO estão no `[tests] regression` — são keep-worthy
  porém ÓRFÃOS do gate: hoje não rodam. Decisão do dono: wire-in (adicionar ao
  `[tests] regression`, casado) ou aceitar como corpus dormente. Fora do escopo deste passe.

## 7. Resumo quantitativo

`.tkt` (127):
- MANTER (diagnóstico de compilador): 9.
- REMOVER (native/compilação/genérico/lib-happy/harness): 82.
- AMBÍGUO (stdlib-erro 11, crypto/lib-valor ~20, LSP 3, encode-native 2): ~36.

`.tkr` (86):
- PROTEGIDO (gate depende — inclui `regressor.tkr`+`cases/` e os `s16_*`): 29 dirs + `regressor.tkr` + 15 `cases/`. (29+17+20+19 = 85 dirs + `regressor.tkr` = 86 `.tkr`.)
- MANTER (rejeição/diagnóstico fora do gate): 17.
- REMOVER (comportamento/genérico fora do gate): 20.
- AMBÍGUO (concorrência 7, syscall/§16 6, arena raw 5, FFI 1): 19.

Total de queda CONFIANTE (sem tocar ambíguo nem protegido): 82 `.tkt` + 20 dirs `.tkr` = 102 itens.
Faixa AMBÍGUA (revisão do dono): ~36 `.tkt` + 19 dirs `.tkr`.
