# Teste de mesa — o overrun do self-build nativo é a divergência `typeexpr_is_fat` × `is_fat_type`

Arquiteto: SÓ leu o código-fonte e escreveu ESTE doc. Nenhuma linha de produto tocada, nenhum
build executado. Teste de mesa (dry-run) puro sobre a baixada NATIVA (`src/lir/lower.tks`,
`src/lir/lower_const.tks`) comparada com a rota C oráculo (`src/codegen/codegen.tks`).

## Veredito (uma linha)

O `store` mal-dimensionado que overruna o bloco bump é um **campo de agregado (`str`/`[]T`) cuja
ANOTAÇÃO o `teko::backend::typeexpr_is_fat` classifica THIN (8 bytes) enquanto o VALOR gravado o
`is_fat_type` classifica FAT (16 bytes)** — o layout reserva 8, o `store_fat_slot` escreve 16, e os
8 bytes de sobra (a metade `len` do par `{ptr,len}`) caem no campo/vizinho seguinte. É a MESMA classe
que o diagnóstico só-de-const `TEKO_NATIVE_CONST_SIZE_CHECK` já nomeia — só que no caminho
NÃO-const (`store_struct_field`), que não tem guarda equivalente.

## Método (o oráculo + os 2 invariantes)

- **Oráculo:** a rota C está VERDE em toda plataforma → é correta. Para cada construto comparo como o
  NATIVO dimensiona/aloca/grava vs. como o C faz. A divergência é o bug.
- **Invariante #1 (TAMANHO):** `alocado(dest) == escrito(valor)`? Um `store` maior que o bloco é o
  overrun.
- **Invariante #2 (VIDA/ALIAS):** todo alias que escapa de uma região é conduzido antes de a região
  cair?

O achado desta rodada é **puramente invariante #1** (tamanho), num único par de predicados.

## O mecanismo, confirmado pelo próprio código

O backend nativo tem DUAS perguntas "isto é fat?", lidas de fontes diferentes:

| Predicado | file:line | Lê | Serve a | Segue alias? |
|---|---|---|---|---|
| `typeexpr_is_fat(te, table)` | `src/lir/lower.tks:15032` (+ `_walk` 14988, `_named` 14944, `_decl_body` 14965) | a ANOTAÇÃO sintática (`parser::TypeExpr`) | LAYOUT / largura de campo / bind de param | SIM (via `type_table_find_path`) |
| `is_fat_type(t)` | `src/lir/lower.tks:10192` | o `checker::Type` RESOLVIDO do VALOR | STORE / gravação do `{ptr,len}` | NÃO (casa só `Str`/`Slice` literais) |

O `src/lir/lower_const.tks:909-957` (`check_struct_fat_span_at`) já DOCUMENTA e detecta a colisão no
caminho de const:

> "the layout's per-field width came from `typeexpr_is_fat` over the field ANNOTATION, while the
> serializer writes a 16-byte `{ptr, len}` slot whenever the field VALUE `is_fat_type` — the two
> predicates the raiz-A alias bug already proved can disagree. When they do (annotation classified
> NON-fat → 8 bytes; value fat → 16-byte slot), the slot overflows its field, every following field
> shifts, a later pointer lands misaligned (macOS `ld`) and the const's bytes read as garbage
> (Linux/x86 gen1 crash)."

A mensagem literal do stop (`lower_const.tks:957`): *"was allotted N bytes but a fat slot needs 16 —
its layout under-sized it (a typeexpr_is_fat / is_fat_type mismatch)"*. O crash do self-build é ESSA
frase, num struct/slice comum em vez de num const.

## QUAL entrada faz as duas divergirem HOJE

Percorri as duas funções lado a lado. Os casos em que os predicados PODERIAM divergir e o veredito de
cada um:

1. **`str` literal / `[]T` literal** — `typeexpr_is_fat`: `SliceType`→true, `"str"`→true
   (`lower.tks:14990,14946`). `is_fat_type`: `Slice`/`Str`→true. **Concordam (16/16).** Sem bug.

2. **Alias BARE de tipo fat (`type S = str`)** — `typeexpr_is_fat_named` acha `S` na tabela, é
   `AliasBody`, recorre no RHS (`lower.tks:14948-14949,14967`)→true. `is_fat_type` vê o valor já
   resolvido `Str`→true. **Concordam (16/16).** Foi o bug da raiz-A, hoje FECHADO.

3. **Alias QUALIFICADO de tipo fat (`campo: checker::TypeTable` = `[]TypeReg`)** — `path_qualifier_of`
   + `type_table_find_path` resolvem (`lower.tks:14933,14948`)→true. Concordam. Foi o degrau 31, hoje
   FECHADO.

4. **Parâmetro de tipo genérico `T` ainda-ABSTRATO, instanciado fat, deixado sem re-stamp** — ESTE
   é o que ainda diverge:
   - `typeexpr_is_fat_named` (`lower.tks:14945-14951`): `path_last_segment_name` = `"T"`; não é
     `"str"`; `type_table_find_path(table, "", "T")` NÃO acha `T` (um type-param nunca é registrado
     como tipo) → cai no braço **`error => false`** (`lower.tks:14950`) → **THIN**. O próprio
     doc-comment confirma: *"A name the table does not know at all — a generic type-PARAMETER (`T`),
     never registered as a type — answers `false`"* (`lower.tks:14916-14917`).
   - `is_fat_type` (`lower.tks:10192`): o VALOR que entra no campo tem `checker::Type` já resolvido
     para `Str`/`Slice` (o typer monomorfizou o argumento) → **FAT**.
   - **DIVERGEM: THIN(8) × FAT(16).**

O ponto onde as rotas se separam entre TypeExpr e Type: a instância genérica só fica coerente se o
monomorfizador SUBSTITUIR a anotação do campo `T`→`str` (`subst_texpr_names`, feito em
`src/checker/resolve.tks:2377` para `subst_body_names` e `:3006` para `normalize_inst`). Quando a
substituição acontece, o campo vira `value: str` e `typeexpr_is_fat("str")`=true → coerente, SEM bug.
A divergência é EXATAMENTE a lacuna nomeada em `src/mem/unsafe/rawbuf.tks:71-74`:

> "a free `fn make<T>(...) -> Owned<T>` hits the known generic-stack gap where a function's OWN
> still-abstract type param isn't re-stamped inside its body … the static-factory method shape
> sidesteps it because the type argument is concrete at every call site."

Quando essa lacuna deixa a anotação do campo como `T` abstrato (instância `__g__` não concretizada,
ou o layout caindo no template genérico), o layout é medido em cima de `T` (8) e o `store` em cima do
valor `str` (16). `Owned<T> { value: T }` (`rawbuf.tks:76-78`) é o construto-molde: um campo BARE de
type-param, cujo `T` fat só é seguro se re-stampado.

Nota: um campo `[]T` NUNCA dispara — `SliceType` é fat por FORMA em `typeexpr_is_fat_walk`
(`lower.tks:14990`) e em `is_fat_type` (`Slice`), independente de `T`. Só o **BARE `T`** (não `[]T`,
não `str`) é o vetor.

## A baixada nativa, passo a passo (onde alocado ≠ escrito)

Construto: `Owned { value = s }` com `s: str`, tipo do literal uma instância cujo campo `value` ficou
anotado com o `T` abstrato (lacuna generic-stack).

1. `lower_struct_init` (`lower.tks:12530`) → `find_struct_layout(name)` → layout de `layout_of_fields`
   (`lower.tks:14732`).
2. Para o campo `value`: `ft = ltype_of_typeexpr(T)` = `Ptr` (`ltype_of_named_path`→`ltype_of_prim_name("T")` cai no `_ => Ptr`, `lower.tks:402-403`).
3. `field_layout_size(te=T, ft=Ptr, nested=null, table)` (`lower.tks:15049`): `typeexpr_is_fat(T)` =
   **false** → `field_size_of(Ptr, null)` = `ltype_size(Ptr)` = **8**. `align` = 8.
   → **o campo recebe 8 bytes**; `layout.size` é medido com esse 8.
4. `store_struct_field` (`lower.tks:12677`): NÃO é union; `is_fat_type(value.type=Str)` = **true**
   (`lower.tks:12688`) → `store_fat_slot(addr, fo)` (`lower.tks:12691`).
5. `store_fat_slot` (`lower.tks:11567`): `store Ptr @ off` (8 bytes) **+** `field_addr(off+8)` **+**
   `store I64 @ off+8` (8 bytes) → **grava 16 bytes**.
6. **Overrun:** o campo tinha 8 bytes reservados; a metade `len` é escrita em `off+8`, que é o slot do
   PRÓXIMO campo (ou, se `value` é o último campo, `off+8 = layout.size` → 8 bytes PASSANDO o fim do
   objeto).

Onde a corrupção CAI (por que o crash é layout-sensível e cumulativo):

- **Instância no frame (`alloca layout.size`, `lower.tks:12534`):** o overrun de 8 bytes pisa o slot
  vizinho na PILHA. Layout-sensível — qual vizinho depende do frame/plataforma (casa com os sítios
  itinerantes 5083/4850/5821). Se o vizinho for uma `tk_region *` residente na pilha (ou um ponteiro
  spillado), o próximo `tk_region_alloc` lê `r->head` podre e SIGSEGVa — exatamente o cenário que o
  `tk_canary_check_region` prevê (`teko_rt.c:1766-1771`: a `tk_region` é malloc'd, não é chunk; um
  wild-write nela dá "no chunk-table trip, yet `tk_region_alloc` still dies dereferencing `r->head`").
- **Const (`lower_const.tks`):** a mesma metade `len` cai no blob de rodata (um `[]byte` de região) —
  overrun DIRETO num bloco bump → cabeçalho de chunk vizinho. É a variante que o
  `TEKO_NATIVE_CONST_SIZE_CHECK` já pega; a variante struct-literal comum NÃO tem guarda.

## Comparação lado a lado com a rota C (por que o C não tem a dualidade)

A rota C emite UM struct C monomorfizado por instanciação e mede CADA campo por `sizeof(<tipo C>)` —
UMA fonte de verdade, sem predicado-de-anotação separado do predicado-de-valor:

- `emit_list_push`/`emit_array_lit` (`codegen.tks:3731,4887`): `sizeof(<elem C>)` — o `cc` calcula a
  largura real; um `str` é `tk_str` = 16 sempre, um campo é o campo do struct C emitido.
- Um campo `value: T` num `Owned<str>` vira, no struct C, `tk_str value;` (o tipo resolvido) — e
  `sizeof(tk_str)` = 16 tanto na definição do struct quanto em qualquer cópia. Não existe um segundo
  predicado sintático que possa discordar do valor: ou o `cc` resolve o tipo, ou nem compila (nunca
  "compila e mente"). A dualidade `typeexpr_is_fat` × `is_fat_type` é uma invenção da baixada nativa
  (o modelo por-endereço), ausente no C.

## Programa surreal / fixture que dispara (para o gate, não implementar agora)

```teko
type Own<T> = struct {
    value: T
    tail: i32          // vizinho: recebe a metade `len` que vaza (lê de volta podre)
    fn make(v: T) -> Own<T> { Own { value = v; tail = 7 } }
}

fn main() -> i32 {
    let b = Own::make("hello")   // T = str: campo fat, mas anotação `T` no molde
    // se `tail` ler != 7, a metade `len` de "hello" (=5) vazou por cima dele
    if b.tail == 7 { 42 } else { 25 }
}
```

Fixtures de regressão a ADICIONAR (entrada → exit nativo esperado; oráculo = mesma exit na rota C):
- `own_fat_field_str` (campo `T`=`str`, vizinho escalar) → exit `42` (hoje: exit divergente/lixo).
- `own_fat_field_slice` (campo `T`=`[]i32`) → exit `42`.
- `own_fat_field_last` (o campo fat é o ÚLTIMO → overrun PASSA o fim do objeto) → exit `42`.
- variante const: `const C: Own<str> = Own { value = "x"; tail = 7 }` sob
  `TEKO_NATIVE_CONST_SIZE_CHECK=1` deve PARAR com a mensagem de `lower_const.tks:957` (prova de que
  o non-const e o const são a mesma classe).

## Suspeito secundário (invariante #2, honesto)

Ao varrer a matriz achei UMA divergência real adicional, do outro invariante, que reporto sem inflar:
**`store_array_elem` (`lower.tks:13054-13065`) NÃO conduz (não faz `own_embedded_value`/box) um
elemento AGREGADO não-fat de literal de array** — grava o endereço cru do `alloca` per-instrução.
Todo outro container conduz (push `lower.tks:10564`, payload de variante `9171`, campo de struct via
`own_embedded_value` `12695`, retorno `own_returned_value` `10824`). O doc de
`embedded_aggregate_box_bytes` (`lower.tks:10974`) AFIRMA "the array literal already obey[s]" a regra —
mas o boxing que existe é do BUFFER inteiro (`emit_elem_box(filled, total)`, `lower.tks:13030`), não
por-elemento; os pointees dos elementos agregados continuam apontando para o frame. É invisível ao
`frame_escape_guard` (`frame_escape.tks`), que só rastreia endereço-de-frame que chega ao `ret`, não
o que é GRAVADO num container e escapa por ele. Alvo do dano: a PILHA (frame reclamado), não
diretamente o chunk — por isso o ranqueio abaixo o coloca em 2º. Reachability incerta (o corpus
constrói poucos `[]struct` por literal; a maioria é via `push`, que conduz).

## Suspeitos ranqueados para o watchpoint

1. **[PRINCIPAL] Campo `str`/`[]T` de agregado com anotação `T` abstrato não-re-stampado** —
   `typeexpr_is_fat`=THIN(8) × `is_fat_type`=FAT(16). Divergência em `lower.tks:14950` (`error =>
   false`) vs `lower.tks:10192`; consumida em `field_layout_size` `lower.tks:15050` vs
   `store_struct_field`/`store_fat_slot` `lower.tks:12691,11567`. Gatilho: lacuna generic-stack
   (`rawbuf.tks:71-74`) sobre um type-param fat. **Watchpoint:** generalizar
   `check_struct_fat_span_at` (`lower_const.tks:950`) para o caminho NÃO-const — asserção em
   `store_struct_field`/`layout_of_fields`: `typeexpr_is_fat(anotação) == is_fat_type(valor)` para
   TODO campo, ou `field_allotted_span >= 16` sempre que `is_fat_type(valor)`. É a MESMA guarda que
   já cravou a classe no const.

2. **[SECUNDÁRIO, invariante #2] `store_array_elem` não conduz elemento agregado** —
   `lower.tks:13061-13064`. Alvo: pilha. Watchpoint: `TEKO_NATIVE_PUSH_REDZONE` num fixture
   `return [P{...}, Q{...}]` de agregado + leitura pós-retorno.

3. **[FRÁGIL, mesmo par] Alias fat que `type_table_find_path` ainda erra** — se algum shape de
   nome/namespace escapar de `typeexpr_is_fat_named` (`lower.tks:14948`), reabre a raiz-A. Hoje
   fechado para bare/qualificado; o watchpoint do item 1 cobre isto de graça (assere sobre o RESULTADO
   dos dois predicados, não sobre a resolução).

## O que está CORRETO (honestidade)

Auditei a cadeia de largura de slice/agregado e ela é internamente coerente FORA da dualidade acima:
`elem_byte_stride` unifica push/array-lit/index-read/index-write/const (`lower.tks:12928`); o push fat
(esz=16) e o escalar (esz=`ltype_size`) batem com o buffer que alocam; `variant_wrapper_bytes` (24) e
os offsets de payload são uniformes; `region_alloc_elem_size`/`store_init` casam tamanho e cópia;
`field_layout_size` JÁ alarga campos fat a 16 para `str`/`[]T`/alias-bare/alias-qualificado; a isel
honra a largura do `LType` no `store` (`mem_size_of_x86`, `isel_x86_64.tks:1108,1202`) — nada de
"store sempre 64-bit". A ÚNICA fenda de tamanho é o BARE type-param abstrato fat. Não inventei um
segundo ponto: os demais construtos da matriz (let/mut/ref, params por valor/ref, retorno por
valor/ref, métodos de instância/estáticos, optional/closure — que HONEST-STOPam, `lower.tks:10692-10693`)
passaram no teste de mesa de tamanho.
