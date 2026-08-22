# Rewrite mecânico do writer de saída do codegen (`src/codegen/codegen.tks`)

Desenho 100% validado — staging reference para a conversão em curso. NÃO re-derive,
NÃO reprojete; aplique os templates abaixo às 243 funções que hoje giram em torno de `Cb`.

## Alvo

Só `src/codegen/codegen.tks`. Elimina a struct `Cb` inteira (buffer/chunk machinery,
`cb_new`, `cb_new_stream`, `cb_stream_flush`, `cb_stream_add`, `cb_prev_room`,
`cb_seal_grow`, `cb_add`, `cb_flat`, `type Chunk`). Duas famílias substituem o parâmetro
`buf: Cb` de cada função:

- **fd-bucket (WRITER)** — escreve direto no descritor de saída via
  `teko::io::stream_write`, sem buffer intermediário.
- **str-bucket** — monta e retorna uma `str` pura (via `teko::str::concat`), sem `Cb`,
  sem `fd`.

## Classificação (228 com `Cb` na assinatura + 15 internas)

Uma função é **fd-bucket** SE:
- a assinatura já tem `escaping:`/`regions:`/`fn_body:`/`dctx:`, OU
- o corpo anda `prog.items`, OU
- ela chama transitivamente outra fd-bucket.

Caso contrário é **str-bucket** (inclui as 15 internas — funções sem `Cb` na própria
assinatura mas que hoje usam `Cb` internamente, ex.: `fresh_named`, `tk_emit_meta`).
Esperado: ~115 fd-bucket, ~113 str-bucket.

## Template fd-bucket

- Troca o param `Cb` por `fd: i64, ref id: u64` (mais `ref prog: <tipo do programa>`
  só nas que andam `prog.items`).
- Retorno: `Cb` → `void`; `Cb | error` → `error | null`; `Cb | null | error` → `bool | error`.
- Corpo: `X = cb(X, "…")` → `cb(fd, "…")` (sem reatribuição — `cb` escreve, não retorna
  buffer). `X = match F(X, …) { Cb as o => o; error as e => return e }` →
  `match F(fd, id, …) { error as e => return e; null => {} }`.

## Template str-bucket

- Dropa o param `Cb`; retorna `str` / `str | error` / `str | null | error`.
- Corpo de cadeias `cb(x, "…")` → `teko::str::concat(…)`.

## Primitivo de write

`cb(fd, s)` = `teko::io::stream_write` ao fd — sem buffer, sem struct, sem contador (o
codegen não rastreia bytes emitidos). `cb_byte`, `cb_i64`/`cb_u64_digits` viram wrappers
finos sobre `teko::runtime::one_byte`/`i64_to_str`/`u64_to_str` + `cb`.

## Gensym (correção do dono — contador POR-FUNÇÃO, não global)

A emissão é estruturada, append-only, sem goto → a unicidade dos nomes temporários vem
do ESCOPO C, não de um número global. Cada função C emitida declara seu próprio
contador `var id: u64 = 0` no ponto de entrada da sua própria emissão (`emit_function`,
`emit_function_cov`, `cg_emit_vtthunk_def`, `emit_spawn_thunk_one`,
`emit_program_main_body`, `emit_test_main*`, …) e threada esse `id` (por `ref`) só
dentro da emissão daquela função — funções C distintas podem reusar os mesmos nomes,
já que têm escopos C distintos.

`fresh_named`/`fresh_tmp_name` passam a tomar `ref id: u64` no lugar do antigo
parâmetro numérico (que hoje deriva de `buf.len`, o acoplamento frágil banido); cada
chamada consome uma unidade do contador e o incrementa (`id = id + 1`). Os ~40 sites de
`$"_teq{buf.len}"`/`$"_ws{buf.len}"`/… migram para `$"_teq{id}"` com o mesmo incremento.
Threading de `ref id` é só de assinatura — call sites passam a variável nua (o
compilador auto-refs; NÃO usa a keyword `ref` no argumento).

## Gêmeos str já existentes (zero máquina nova)

`cg_opt_mangle` / `cg_variant_typename` / `cg_variant_typename_texpr` / `cg_member_key`
já têm gêmeos byte-equivalentes `cg_opt_mangle_str` / `cg_union_mangle_str` /
`cg_union_texpr_mangle_str` / `cg_member_key_str`. Deleta os `Cb`-originais, redireciona
chamadores para os `_str`.

## Outros ajustes mecânicos

- `TThunkEmit { buf: Cb; emitted: []str }` → `{ emitted: []str }`;
  `cg_emit_vtable_thunks` vira fd-bucket retornando `[]str | error`.
- Os 4 passes `Cb | null | error` (`emit_variant_wrap_{exact,widen,transitive}_pass` →
  `bool | error`; `cg_wrap_elem_str_contract` → `str | null | error`, já str-bucket).
- Sem formatação além de `\n`. Elimina array-builds intermediários — sub-peça direto
  no fd.
- AST/programa de entrada por `ref` (não por valor) na entrada e nas funções que
  andam `prog.items`.
- Zero tipo novo. Sem `teko_rt`/`from` novo.

## Guard (fim de ciclo, não no meio)

Os nomes gensym MUDAM (contador por-função, +1 determinístico) — não é byte-idêntico
ao `teko.c` antigo; isso é esperado. Só quando o arquivo inteiro compila: `CC=clang
scripts/build_gen1_from_c.sh` a partir do `bootstrap/teko.c` commitado (nunca
`fetch_teko.sh`), `TEKO_CC=clang`, `ulimit -v 6815744`. Verificar: fixpoint
`gen2.c == gen3.c` byte-idêntico (auto-consistência — os nomes gensym são
determinísticos), o C compila no clang, `TEKO_MEM_PARANOID=1` sai 0, e reseed
(`gen2.c` capturado → `bootstrap/teko.c`, `gen0`-novo → `gen1`-novo reproduz).
