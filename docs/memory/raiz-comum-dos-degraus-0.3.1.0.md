# A raiz comum dos degraus — 0.3.1.0

**Encomenda:** testar a hipótese do dono (2026-07-29, literal) *"Ouso dizer que é provável que muito
dos nossos travamentos se dão pelo runtime precisar de correções"* contra o registo real, e devolver
um veredicto com números. Confirmar OU refutar.

**Resposta curta, para quem só lê uma linha:** a hipótese **REFUTA-SE na letra e confirma-se no
espírito**. O runtime foi tocado em **2 dos 10 degraus**; `src/lir/lower.tks` foi tocado em **10 dos
10**; `src/backend/**` foi tocado em **0 dos 10**. Mas nos DOIS toques do runtime a razão escrita no
próprio cabeçalho `teko_rt.h` é a mesma: o backend nativo não consegue capturar um resultado de dois
words. O runtime não precisa de correções — **o backend precisa de deixar de pedir ao runtime aquilo
que ele próprio não sabe fazer**.

---

## 0. Método, e o compilador com que isto foi medido

| etapa | comando | resultado |
|---|---|---|
| gen1 (trampolim, `bootstrap/teko.c` commitado) | `sh scripts/build_gen1_from_c.sh bootstrap/teko.c src .gen1` | ✓, `.gen1/teko` 9 408 936 bytes |
| gen1c (SOURCE actual desta lane, rota C) | `TEKO_BACKEND=c ./.gen1/teko . -o .gen1c --no-verify --release` | ✓ |
| auto-construção nativa | `TEKO_BACKEND=native ./.gen1c/teko . -o .gen2-native --no-verify --release` | pára no degrau 10 (secção 2) |

Todas as sondas desta análise foram compiladas com `.gen1c` — o compilador do tip actual
(`d4ad700`), com o degrau 9 dentro. A rota C é o ORÁCULO, pela regra já legislada neste ficheiro
irmão (`0.3.1.0-linux-native-first-stop.md`, "THE ORACLE RULE").

Nada abaixo é afirmado sem a linha de código ou a saída do programa colada junto.

---

## 1. A tabela dos degraus fechados — um por linha

A coluna "ficheiro CORRIGIDO" foi lida do `git show --stat` de cada commit, não do texto do registo.

| # | mensagem literal de paragem | função onde rebentou | commit | ficheiro(s) de facto CORRIGIDO(s) | classe da causa |
|---|---|---|---|---|---|
| 1 | `native backend N1: a push whose element is an AGGREGATE (…) needs slice elements held BY VALUE` | `teko::backend::emit_u32_le` | `c436cdd` | `src/lir/lower.tks` | LOWERING partilhada (classe de máquina em falta: `byte`) |
| 2 | `native backend N1: unknown variant case `u32` (internal)` | `teko::backend::encode_alu_imm` | `ac1b6c5` | `src/checker/resolve.tks` **+** `src/lir/lower.tks` | LOWERING partilhada + **CHECKER** |
| 3 | `native backend N1: integer operator not yet lowered (N2)` | `teko::backend::needs_lr_save` | `e5357d2` | `src/lir/lower.tks` | LOWERING partilhada (forma de controlo em falta) |
| 4 | `native backend N1: a push whose element is an AGGREGATE (…)` | `teko::backend::wrap_plain_words` | `c46808b` | `src/lir/lower.tks` **+** `src/runtime/teko_rt.{c,h}` | LOWERING partilhada **+ RUNTIME** (`tk_slice_elem_box`) |
| 5 | `native backend N1: fat-pointer receiver `match-expression` not yet lowered (N2)` | `teko::backend::encode_one` | `ab00643` | `src/lir/lower.tks` | LOWERING partilhada (**dois words**) |
| 6 | `native backend N1: slice match pattern not yet lowered (N2)` | `teko::backend::encode_one` | `52da0b4` | `src/lir/lower.tks` **+** `src/lir/lower_const.tks` | LOWERING partilhada (**dois words**) |
| 7 | ``native backend N1: `str` has no single PrimKind, asked by the comparison chain (N2)`` | `teko::backend::name_in_symbols` | `b8cfb32` | `src/lir/lower.tks` | LOWERING partilhada (**dois words**) |
| 8 | ``native backend N1: a push whose element is the nominal type `teko::backend::MInst`, which has no registered layout`` | `teko::backend::push_minst_block` | `78a7254` + `2e8e73b` | `src/lir/lower.tks` | LOWERING partilhada (agregado POR ENDEREÇO) |
| 9 | ``native backend N1: fat-pointer receiver `string interpolation` not yet lowered (N2)`` | `teko::backend::ar_header` | `24ee25c` | `src/lir/lower.tks` **+** `src/runtime/teko_rt.{c,h}` | LOWERING partilhada (**dois words**) **+ RUNTIME** |
| 10 | ``native backend N1: a push onto a slice of FAT elements (`[]str`/`[][]T`) needs the two-word element stride the array-literal store and the index load do not carry yet (N2)`` | `teko::backend::global_symbol_names` | ABERTO | `src/lir/lower.tks` (três sítios, não dois — ver §5) | LOWERING partilhada (**dois words**) |

### Correcções ao que me foi dado como controlo

O pedido dizia "os degraus 1, 2, 3, 5, 6, 7 e 8 foram, tanto quanto sei, `src/lir/lower.tks`".
Verificado um a um, com duas correcções:

1. **O degrau 2 não foi só `lower.tks`.** `ac1b6c5` toca também `src/checker/resolve.tks` — a
   correcção foi criar `checker::builtin_case_name`, o inverso da tabela de `scope.builtin_type`.
   É o ÚNICO degrau desta lane com uma metade no CHECKER.
2. **O degrau 6 não foi só `lower.tks`.** `52da0b4` toca também `src/lir/lower_const.tks`
   (`const_variant_tag_bytes`: a imagem em rodata de uma variante de caso vazio passou a ser os 24
   bytes inteiros).

Os degraus 4 e 9 estão exactamente como descritos: 4 = boxing de arena (`tk_slice_elem_box`), 9 = um
braço `TInterp` em falta no `lower_fat_expr` MAIS três funções novas no runtime C
(`tk_str_concat_len`, `tk_i64_to_str_len`, `tk_u64_to_str_len`, `src/runtime/teko_rt.h:223-225`).

**Uma metade do degrau 9 não estava no enunciado e é importante:** o mesmo commit fechou também o
builtin `teko::str::concat`, que `call_symbol` não conhecia e que por isso **manglava em silêncio
para um símbolo indefinido** em vez de parar honestamente. Isso não é um detalhe — é o mecanismo da
previsão P2 (§7).

---

## 2. O veredicto sobre a hipótese do dono, com a contagem

### Contagem por FICHEIRO tocado (10 degraus, os commits acima)

| ficheiro | degraus em que foi corrigido | fracção |
|---|---|---|
| `src/lir/lower.tks` | 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 | **10/10 (100%)** |
| `src/lir/lower_const.tks` | 6 | 1/10 |
| `src/runtime/teko_rt.{c,h}` | 4, 9 | **2/10 (20%)** |
| `src/checker/resolve.tks` | 2 | 1/10 |
| `src/backend/**` (isel, encode, objfile, regalloc) | *nenhum* | **0/10 (0%)** |

### Contagem por CLASSE DE CAUSA

| classe | degraus | contagem |
|---|---|---|
| LOWERING partilhada | 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 | **10** |
| RUNTIME (como metade de um degrau de lowering) | 4, 9 | **2** |
| CHECKER (como metade de um degrau de lowering) | 2 | **1** |
| EMISSÃO por plataforma (`src/backend/**`) | *nenhum* | **0** |
| outra | *nenhuma* | **0** |

**Nenhum degrau foi RUNTIME de facto.** Não existe um único degrau desta lane cuja correcção tenha
sido no runtime E SÓ no runtime. Os dois toques no runtime (4 e 9) são metades secundárias de
degraus cuja metade principal está em `src/lir/lower.tks`, e em ambos a razão está escrita no
próprio código:

- degrau 4, `tk_slice_elem_box` — *"the LIR has no memcpy op and no allocation op, and inventing an
  unrolled load/store copy in the lowering would have put an ALLOCATION POLICY … into codegen, where
  the arena is invisible"* (registo do degrau 4);
- degrau 9, `tk_str_concat_len` e twins — `src/runtime/teko_rt.h:214-222`, literal:

  > *"A genuine C caller never needs these (a `tk_str` return already carries both halves); they
  > exist for the native backend's own `LCall`, whose result capture is ONE register
  > (`select_call_result_x86`/`select_call_result_arm64`) — the same width a plain scalar/pointer
  > call answers with, one short of the two-eightbyte SysV/AAPCS64 register pair a `tk_str`-by-value
  > return actually occupies."*

### O veredicto, dito claramente

**A hipótese está REFUTADA na letra: 2 em 10, e nenhum deles um degrau de runtime "de facto".** O
número que domina é outro e é esmagador: `src/lir/lower.tks`, 10 em 10.

**E está CONFIRMADA no espírito, com uma inversão da direcção da seta.** Nos dois casos, não foi o
runtime que estava errado — foi o backend que não sabia falar com ele. O runtime estava certo antes
e depois; o que se acrescentou foi uma PRÓTESE para uma limitação do backend, e o próprio ficheiro
de cabeçalho diz qual (uma só register de resultado). Reformulada de modo a passar no teste:

> **Muitos dos nossos travamentos dão-se porque o backend nativo não sabe pedir ao runtime aquilo
> que precisa — e o sintoma aparece no runtime porque as funções do runtime devolvem `tk_str`, que é
> gordo.**

Que é, palavra por palavra, a suspeita que me foi pedida para testar. Ver §3-§5.

---

## 3. A raiz comum — a suspeita, testada

A suspeita entregue: *"os valores gordos são cidadãos de segunda no backend nativo"*.

**CONFIRMA-SE**, e a prova mais forte não é minha: está escrita no runtime pela mão de quem fechou o
degrau 9, citada acima. Mas confirma-se com **uma correcção importante que muda o plano**: não é
UMA raiz, são DUAS, e elas alternam. Ver §4.

### O facto de arquitectura, numa linha

`src/lir/lir.tks:20` — a LIR não tem classe de máquina gorda nenhuma:

```
pub type LType = enum {
    I8; I16; I32; I64
    F32; F64
    Ptr
    Void
}
```

`src/lir/lir.tks:66,70,134` — e a instrução tem exactamente UM resultado e UM valor de retorno:

```
pub type LCall = struct { symbol: str; args: []u32; variadic: bool }
pub type LRet = struct { has_value: bool; value: u32 }
pub type LInst = struct { result: u32; has_result: bool; op: LOp; line: u32; col: u32 }
```

`src/lir/lower.tks:270,276` — e o mapa do tipo semântico para a máquina colapsa `str`/`[]T` em `Ptr`:

```
pub fn ltype_of(t: checker::Type, enums: []LEnumInfo) -> LType {
    match t {
        checker::Prim as p => ltype_of_prim(p.kind)
        checker::Byte => LType::I8
        checker::Void => LType::Void
        checker::Named as n => ltype_of_named(n, enums)
        _ => LType::Ptr          // <-- Str e Slice caem AQUI. 8 bytes onde a verdade é 16.
    }
}
```

**A expressão `ltype_size(ltype_of(t, enums))` é a raiz textual.** Onde quer que ela apareça sobre um
tipo gordo, o backend escreve 8 onde a verdade é 16. O próprio doc-comment de `ltype_of` já avisa
para a doença gémea (`byte` dentro de um contentor) — *"CAUTION: only apply this mapping to an
operand's OWN type, never to a container's element type"* — sem reparar que `Str`/`Slice` sofrem do
mesmo mal na posição de OPERANDO, não só na de elemento.

### As NOVE representações de "dois words" que este backend tem hoje

Nenhuma delas é partilhada com as outras. Cada uma foi inventada no sítio onde fez falta. É esta
dispersão, e não a LIR de um word, que faz os degraus parecerem separados.

| # | representação | onde vive | quem a escreve / quem a lê |
|---|---|---|---|
| 1 | par de VRegs `LoweredFat{ptr,len}` | `src/lir/lower.tks:5600` | `lower_fat_expr` (5622) |
| 2 | dois registos de entrada consecutivos | `bind_fat_param`, `lower.tks:7541`; `param_ltypes`, `lower.tks:7734` | convenção "tk_str-twin", ruling do dono |
| 3 | dois argumentos achatados na chamada | `lower_arg_vregs`, `lower.tks:1631` | ptr primeiro, len segundo |
| 4 | dois block-args no merge | `merge_fat_value_args`, `lower.tks:6151` | `lower_if_fat` (6269), `lower_match_fat` (6307) |
| 5 | slot de frame de 16 bytes `{ptr@0,len@8}` | `alloc_fat_slot`/`store_fat_slot`/`load_fat_slot`, `lower.tks:6468/6485/6501` | só para um `mut` gordo local |
| 6 | slot escondido de saída para o COMPRIMENTO | `bind_ret_len_slot` (7562) + `lower_return_fat` (2939) + `lower_call_fat` (5667) | a prótese do retorno |
| 7 | gémeos `_len` do runtime com parâmetro de saída | `teko_rt.h:223-225`; `lower_len_out_call`, `lower.tks:6681` | a prótese da chamada ao runtime |
| 8 | `{tag@0, ptr@8, len@16}` no invólucro de 24 bytes | `variant_payload_offset` (4476) / `variant_payload_len_offset` (4486) | degrau 6 |
| 9 | `{ptr@off, len@off+8}` num campo de struct | `field_layout_size`, `lower.tks:8176`; leitor `lower_fat_field`, `lower.tks:6409` | **escritor EM FALTA — §5** |
| — | **elemento gordo de slice** | **não existe** | **degrau 10, aberto** |

Há ainda duas coisas de dois words que este backend trata BEM, e servem de contraste útil: o
ponteiro gordo de INTERFACE (`IfaceFatPtr{data,vtable}`, `lower.tks:1985`) e o de CLOSURE
(`ClosureFatPtr{target,env}`, `lower.tks:2381`). Ambos são um `alloca` de 16 bytes manipulado
SEMPRE POR ENDEREÇO — um só word viaja, e o par nunca precisa de atravessar uma fronteira de ABI.
É por isso que nunca deram degrau nenhum.

### Onde um valor de dois words é tratado como UM SÓ — inventário com ficheiro e linha

| operação | sítio | o que faz hoje | estado |
|---|---|---|---|
| **captura do resultado de chamada** | `select_call_result_x86`, `src/backend/isel_x86_64.tks:1022` | `mov dst, RAX` — UM registo | **impossível hoje**; contornado pela prótese 6/7 |
| **captura do resultado de chamada** | `select_call_result`, `src/backend/isel_arm64.tks:1577` | `mov dst, x0` — UM registo | idem |
| **retorno** | `select_ret_x86`, `src/backend/isel_x86_64.tks:510` | `mov RAX, v` — UM registo; sem RAX:RDX | idem |
| **retorno** | `select_ret`, `src/backend/isel_arm64.tks:971` | `mov x0, v` — UM registo; sem x0:x1 | idem |
| **passagem de argumentos** | `pin_args_x86` (875) / `pin_args` (`isel_arm64.tks:1537`) | um registo por VReg | **CORRECTO** — a lowering já achatou em dois VRegs (repr. 3) |
| **passagem por referência (Win64)** | `str_pair_by_ref_x86`, `isel_x86_64.tks:942` | rematerializa o par num slot de 16 bytes | **CORRECTO mas de LISTA FECHADA** — só 7 símbolos (`is_str_arg_builtin`, `lower.tks:1523`) |
| **store de campo de struct (construção)** | `store_struct_fields`, `src/lir/lower.tks:6931` | `store_inst(addr, vo.vreg, ltype_of(field.type))` — UM word | **DEFEITO SILENCIOSO — medido, §5** |
| **store de campo de struct (atribuição)** | `lower_assign_field` (7276) → `store_assign_value` (7300), largura `ltype_of(a.bound)` | UM word | **DEFEITO SILENCIOSO — medido, §5** |
| **store de elemento de array literal** | `store_array_elements`, `lower.tks:7060` | `stride` = `ltype_size(ltype_of(elem))` = 8 | **DEFEITO SILENCIOSO — medido, §5** |
| **load por índice** | `lower_index_slice`, `lower.tks:7034` | `elem_ty = ltype_of(e.type)`; um `load` | paragem honesta a jusante (degrau 10) |
| **push** | `lower_list_push` → `fat_element_push_stop`, `lower.tks:5869` | recusa | **paragem honesta** (degrau 10) |
| **merge / block-args** | `promote_env`, `lower.tks:3043` | *"A FAT (str/slice) binding is left UNPROMOTED"* | contornado pela repr. 5 (slot) |
| **payload de variante** | `store_fat_variant_payload` (4618) / `bind_fat_case_payload` (4344) | ptr@8 + len@16, à mão | **CORRECTO** (degrau 6) |
| **leitura de campo gordo** | `lower_fat_field`, `lower.tks:6409` | ptr@off + len@off+8, à mão | **CORRECTO** (#594 T-B6) |

---

## 4. Quais degraus caem nessa lista e quais NÃO caem

Testei a suspeita contra cada degrau, e não a confirmei por cortesia. **Metade não cai.**

| # | é "dois words"? | porquê |
|---|---|---|
| 1 | **NÃO** | `byte` sem classe de máquina. É o oposto: um valor de UM octeto a receber a classe de OITO. Família "largura", não família "dois words". |
| 2 | **NÃO** | caso de variante nomeado por palavra-chave; resolução de nome. Metade no CHECKER. |
| 3 | **NÃO** | `&&`/`||` precisam de curto-circuito. Forma de CONTROLO em falta. |
| 4 | **NÃO** | `alloca` é um slot por INSTRUÇÃO, não por EXECUÇÃO. É a família `NATIVE-AGG-SLICE-BY-ADDRESS` — agregados por endereço. |
| 5 | **SIM** | um `match`-VALOR gordo precisa de DOIS block-args no merge (`merge_fat_value_args`, repr. 4). |
| 6 | **SIM** | *"For a `str`/`[]T` member `ltype_of` answers `Ptr`, so **the LENGTH is dropped at construction**"* — o registo do degrau 6, literal. O invólucro de 24 bytes existe SÓ para caber o par. |
| 7 | **SIM** | *"A `str` is two words and has none"* — o registo do degrau 7, literal, sobre `prim_kind_of` pedir UMA classe de máquina. |
| 8 | **NÃO** | `MInst` é uma variante DECLARADA que chega como `checker::Named`. É a família do degrau 4 (por endereço) + a família da miscompilação do braço. |
| 9 | **SIM** | o cabeçalho `teko_rt.h:214-222` nomeia `select_call_result_x86`/`select_call_result_arm64` explicitamente. |
| 10 | **SIM** | elemento gordo: `fat_element_push_stop` diz-o pelo nome. |

**Cinco em dez.** E a distribuição no TEMPO é o resultado que importa: dos degraus 1 a 4, **zero**
são de dois words; dos degraus 5 a 10, **cinco em seis** são (todos menos o 8).

### O contra-exemplo, que é o que impede este documento de ser uma arrumação

**O degrau 8 é o contra-exemplo e ele é sério.** Está no meio de 6-7-9-10 e NÃO é de dois words: a
sua correcção (`expand_named_variant` em `aggregate_box_bytes`, e o box do payload) não tocou em
nenhuma das nove representações de §3. Ele pertence à OUTRA raiz, e essa outra raiz também está
aberta e também é grave:

> **RAIZ B — o valor de um agregado É o endereço de um slot de frame** (`NATIVE-AGG-SLICE-BY-ADDRESS`,
> §5.1 da lowering). Dela vêm os degraus 4 e 8, o invólucro devolvido que pendura (já reclassificado
> como BUG pelo dono a 2026-07-28) e o SIGSEGV que a retracção do degrau 8 documenta.

Portanto a formulação certa da resposta à pergunta que me foi feita é:

> **Não há UMA raiz comum. Há DUAS, elas alternam, e cada uma explica metade dos degraus.**
> RAIZ A (dois words) explica 5, 6, 7, 9, 10. RAIZ B (agregado por endereço) explica 4 e 8.
> Os degraus 1, 2 e 3 não pertencem a nenhuma das duas — são gaps genuinamente isolados, e já
> fecharam.

Isto é melhor notícia do que uma raiz só, não pior: significa que as duas raízes podem ser fechadas
por vagões independentes, em paralelo, e que a RAIZ A — a que este documento desenha — é a que está
no caminho crítico hoje.

---

## 5. As TRÊS miscompilações silenciosas que o modelo previu, e que foram MEDIDAS aqui

Se a RAIZ A é real, ela tem de ter deixado buracos onde ninguém foi olhar. Fui olhar ao sítio que o
inventário de §3 aponta como o único com LEITOR e sem ESCRITOR: o campo gordo de struct
(representação 9).

**Todas as sondas: projecto standalone, `main.tks` = `exit(probe())`, compilador `.gen1c`.**

### 5.1 Construir um struct com campo `str` e ler `.len` — nativo dá a resposta errada, calado

```teko
pub type Holder = struct { name: str; n: i32 }

pub fn probe() -> i32 {
    let h = Holder { name = "abcde"; n = 7 }
    if h.n != 7 { return 1 }
    if h.name.len != 5 { return 2 }
    0
}
```

```
$ TEKO_BACKEND=c      .gen1c/teko … && ./out_c/fatfield ; echo $?     ->  0
$ TEKO_BACKEND=native .gen1c/teko … && ./out_n/fatfield ; echo $?     ->  2
```

**Isolado até ao osso, três sondas mais:**

| sonda | o que mede | C | nativo |
|---|---|---|---|
| `fatobs` — devolve `h.name.len to i32` como exit code | QUAL é o comprimento observado | **5** | **0** |
| `fatptrhalf` — lê `h.name[0]` e `h.name[4]` por índice | a metade PONTEIRO está certa? | 0 | **0** |
| `fatconst` — o mesmo `Holder` como `const` em rodata | o caminho de const está certo? | 0 | **0** |

Ler as três juntas dá a causa exacta e fecha-a: **a metade PONTEIRO é escrita correctamente; a
metade COMPRIMENTO nunca é escrita (lê-se 0); e o caminho de CONST está certo.**

O sítio, com o código colado — `store_struct_fields`, `src/lir/lower.tks:6931`:

```
cur = ctx_append(with_addr, store_inst(addr.vreg, vo.vreg, ltype_of(field_vals[i].type, cur.enums), line, col))
```

`ltype_of(Str)` é `Ptr`. Um store de 8 bytes em `off`. O `off+8` nunca é escrito. E o LEITOR,
`lower_fat_field`, `src/lir/lower.tks:6409`, lê-o:

```
let with_la = ctx_append(with_pr, field_addr_inst(la.vreg, ro.vreg, off + ltype_size(LType::Ptr), …))
… load_inst(lr.vreg, la.vreg, LType::I64, …)
```

**Por que ninguém deu por isto:** `field_layout_size` (`lower.tks:8176`) e `lower_fat_field` nasceram
no crumb #594 T-B6, cujo alvo eram os agregados CONST em rodata — e o escritor de const
(`src/lir/lower_const.tks:420`, `const_fat_field`) ESCREVE os 16 bytes correctamente. A sonda
`fatconst` prova-o: o caminho const passa nas duas rotas. Foi construído um leitor de 16 bytes para
um escritor que só existia para consts, e o escritor de RUNTIME nunca foi alargado.

### 5.2 Atribuir a um campo gordo — o mesmo defeito, o outro escritor

```teko
pub type Holder = struct { name: str; n: i32 }

pub fn probe() -> i32 {
    mut h = Holder { name = "abcde"; n = 7 }
    h.name = "xy"
    if h.name.len != 2 { return 3 }
    0
}
```

C: **0**. Nativo: **3**. Sítio: `lower_assign_field` (`lower.tks:7276`) passa
`ltype_of(a.bound, …)` como largura a `store_assign_value` (`lower.tks:7300`) — 8 bytes.

### 5.3 Passar um campo gordo como argumento — a terceira boca do mesmo buraco

```teko
pub type Holder = struct { n: i32; name: str }
fn take(s: str) -> i32 { s.len to i32 }

pub fn probe() -> i32 {
    let h = Holder { n = 7; name = "abcde" }
    if take(h.name) != 5 { return 1 }
    0
}
```

C: **0**. Nativo: **1**. (Com o campo gordo em offset 8 desta vez — o defeito não depende do offset.)

### 5.4 O mesmo com um campo `[]T`, não só `str`

```teko
pub type Holder = struct { xs: []i64; n: i32 }
… if h.xs.len != 3 { return 1 }
```

C: **0**. Nativo: **1**. `is_fat_type` cobre `Str` E `Slice`; o defeito também.

### 5.5 E o literal `[]str` constrói-se em SILÊNCIO com o stride errado

```teko
pub fn probe() -> i32 {
    let xs: []str = ["aa", "bbb", "cccc"]
    if xs.len != 3 { return 1 }
    0
}
```

Nativo: **compila e devolve 0**. O buffer JÁ está corrompido — `store_array_elements`
(`lower.tks:7060`) escreveu três ponteiros com stride 8 e nenhum comprimento. Só o LEITOR pára
(`fat-pointer receiver \`index\` not yet lowered`). É por isso que o degrau 10 tem **TRÊS** sítios,
não dois: o store do literal, o load por índice e o `esz` do push — exactamente os três que
`fat_element_push_stop` já nomeia no seu próprio doc-comment (`lower.tks:5869`).

### Por que estes três defeitos silenciosos são o achado mais grave deste documento

**383 das 717 declarações de `struct` em `src/**` (53%) têm pelo menos um campo `str` ou `[]T`.**
(Contagem por varrimento das declarações, comentários removidos.)

E uma delas é o coração da própria lowering — `src/lir/lir.tks:527`:

```
pub type LStructLayout = struct { name: str; size: u32; align: u32; field_names: []str; field_offsets: []u32; field_types: []LType }
```

Quatro campos gordos. É construída em `layout_of_fields` (`lower.tks:8014`) e lida em
`field_offset_of` (6885) e `field_type_of` (6907), ambos por `layout.field_names.len`. Medido em
§5.1, esse `.len` lê **0** no nativo — logo o laço sai imediatamente e toda a procura de campo
devolveria "unknown field". **Um compilador construído por esta rota não conseguiria compilar um
único `struct`.**

Isto NÃO é um degrau. Um degrau pára e diz onde. Isto passa por todos os portões:
`teko test .` corre pela rota C (medido de forma independente em `mapa-native-6-pernas-0.3.1.0.md`,
Probe D); o `fixpoint` compara gen2 com gen3, dois binários do MESMO compilador errado.

---

## 6. O desenho da correcção de raiz — RAIZ A

O objectivo, dito de forma falsificável: **um valor de dois words tem UMA representação em memória
(`{ptr@0, len@8}`), UMA largura (16), UM escritor e UM leitor — e nenhum sítio do ficheiro volta a
calcular largura de valor gordo com `ltype_size(ltype_of(t))`.**

**O que NÃO se faz, e porquê.** Não se acrescenta uma classe gorda à `LType` nem um segundo VReg de
resultado à `LInst`. Isso obrigaria a mexer nos dois isels, no regalloc, no stackify, no printer e
no interpretador de LIR (`src/lir/lir_interp.tks`) — e não resolveria nada que a convenção de slot
escondido (repr. 6) não resolva já, provada a funcionar (sonda `fatret`: nativo e C ambos 0). A
doença é ARITMÉTICA DISPERSA, não a LIR de um word. Isto alinha com o ruling do dono no degrau 6
(*"uniformity is the opposite of what has bitten this lane"*).

### A sequência de crumbs — a ordem é a lição do degrau 3 e do degrau 6

Cada crumb é fechável e portável sozinho, e cada um tem o seu portão.

---

**R1 — UMA autoridade de largura. Nenhum comportamento muda ainda.**

Introduzir a pergunta única, e converter os sítios que já respondem 16 (`field_layout_size`) a
chamá-la, sem tocar em nenhum que responda 8. Portão: `teko test .` e o corpus `own_native`
byte-idênticos ao de hoje.

```teko
/**
 * fat_value_bytes — a largura em bytes da imagem `{ptr@0, len@8}` de um valor gordo.
 *
 * A ÚNICA resposta do backend à pergunta "quantos bytes ocupa um `str`/`[]T` em memória".
 * Existe para que o campo de struct, o elemento de slice, o slot de frame e o payload de
 * invólucro de variante não possam voltar a discordar: uma largura calculada em dois sítios é
 * uma corrupção silenciosa à espera, não uma paragem.
 *
 * @return u32  a largura, sempre `2 * ltype_size(LType::Ptr)`
 * @see fat_len_offset
 * @since 0.3.1.0 raiz A
 */
fn fat_value_bytes() -> u32 {
    ltype_size(LType::Ptr) * (2 to u32)
}

/**
 * fat_len_offset — o deslocamento, dentro da imagem de um valor gordo, da metade COMPRIMENTO.
 *
 * A metade PONTEIRO vive sempre no offset 0 da imagem; esta função nomeia a outra. A ORDEM
 * (ptr primeiro, len depois) é a mesma que `bind_param` liga nos registos de entrada e a mesma
 * dos campos de `tk_str` em `src/runtime/teko_rt.h` — um sítio que a invertesse leria um
 * ponteiro como comprimento, em silêncio.
 *
 * @return u32  o deslocamento da metade comprimento
 * @see fat_value_bytes
 * @since 0.3.1.0 raiz A
 */
fn fat_len_offset() -> u32 {
    ltype_size(LType::Ptr)
}

/**
 * value_image_bytes — quantos bytes de memória a imagem de um valor de tipo `t` ocupa quando
 * um contentor o guarda POR VALOR: `fat_value_bytes()` para `str`/`[]T`, a largura escalar da
 * sua classe de máquina para tudo o resto.
 *
 * Esta é a função que substitui `ltype_size(ltype_of(t, enums))` em TODO o sítio que fale de
 * armazenamento — o stride de um literal de array, o passo de um índice, o `esz` de um push, a
 * largura de um store de campo. `ltype_of` responde `Ptr` a um tipo gordo, e `ltype_size(Ptr)`
 * é 8: metade da verdade, exactamente a metade que se perde em silêncio.
 *
 * @param checker::Type t  o tipo cujo valor vai ser armazenado
 * @param []LEnumInfo enums  a tabela de enums declarados (um enum leva a largura do seu carrier)
 * @return u32  a largura da imagem em memória
 * @see fat_value_bytes
 * @since 0.3.1.0 raiz A
 */
fn value_image_bytes(t: checker::Type, enums: []LEnumInfo) -> u32 {
    if is_fat_type(t) { return fat_value_bytes() }
    ltype_size(ltype_of(t, enums))
}
```

Reescrever, com o corpo inalterado no resultado: `fat_slot_bytes` (`lower.tks:6456`) →
`fat_value_bytes()`; `field_layout_size` (`lower.tks:8176`) → `fat_value_bytes()`;
`variant_payload_len_offset` (`lower.tks:4486`) → `variant_payload_offset() + fat_len_offset()`.

---

**R2 — UM escritor e UM leitor de imagem gorda.** Generalizar `store_fat_slot`/`load_fat_slot`
(`lower.tks:6485`/`6501`), que hoje só servem o slot de `mut` local, para qualquer endereço, e
converter os DOIS sítios que hoje fazem o mesmo à mão (`store_fat_variant_payload` 4618,
`bind_fat_case_payload` 4344, `lower_fat_field` 6409). Ainda sem mudar comportamento — os offsets
são os mesmos. Portão: corpus `own_native` byte-idêntico.

```teko
/**
 * store_fat_image — escrever a imagem `{ptr@0, len@8}` de um valor gordo no endereço `addr`.
 *
 * O ÚNICO escritor de valor gordo em memória do backend. `addr` pode ser o slot de frame de um
 * `mut` local, o offset de um campo de struct, o offset de um payload de invólucro de variante,
 * ou o endereço de um elemento de slice — todos são a MESMA imagem, e é por isso que só há uma
 * função. Escrever só a metade ponteiro é o defeito que a sonda `fatfield` mede (registo de
 * 2026-07-29, §5.1).
 *
 * @param LowerCtx ctx  o contexto de lowering a estender
 * @param u32 addr  o VReg do endereço-base da imagem
 * @param LoweredFat fo  o par (ptr, len) a escrever
 * @param u32 line  a linha de origem a carimbar nas instruções emitidas
 * @param u32 col  a coluna de origem a carimbar nas instruções emitidas
 * @return LowerCtx  o contexto com os DOIS stores acrescentados
 * @see load_fat_image
 * @since 0.3.1.0 raiz A
 */
fn store_fat_image(ctx: LowerCtx, addr: u32, fo: LoweredFat, line: u32, col: u32) -> LowerCtx {
    let with_ptr = ctx_append(ctx, store_inst(addr, fo.ptr, LType::Ptr, line, col))
    let la = ctx_alloc(with_ptr)
    let with_la = ctx_append(la.ctx, field_addr_inst(la.vreg, addr, fat_len_offset(), line, col))
    ctx_append(with_la, store_inst(la.vreg, fo.len, LType::I64, line, col))
}

/**
 * load_fat_image — ler de volta a imagem `{ptr@0, len@8}` escrita por `store_fat_image`.
 *
 * O ÚNICO leitor. Emparelhado com o escritor pela mesma dupla `fat_len_offset`, para que a
 * pergunta "onde vive o comprimento" tenha uma resposta e não duas.
 *
 * @param LowerCtx ctx  o contexto de lowering a estender
 * @param u32 addr  o VReg do endereço-base da imagem
 * @param u32 line  a linha de origem a carimbar nas instruções emitidas
 * @param u32 col  a coluna de origem a carimbar nas instruções emitidas
 * @return LoweredFat  o par (ptr, len) lido
 * @see store_fat_image
 * @since 0.3.1.0 raiz A
 */
fn load_fat_image(ctx: LowerCtx, addr: u32, line: u32, col: u32) -> LoweredFat {
    let pr = ctx_alloc(ctx)
    let with_pr = ctx_append(pr.ctx, load_inst(pr.vreg, addr, LType::Ptr, line, col))
    let la = ctx_alloc(with_pr)
    let with_la = ctx_append(la.ctx, field_addr_inst(la.vreg, addr, fat_len_offset(), line, col))
    let lr = ctx_alloc(with_la)
    LoweredFat { ctx = ctx_append(lr.ctx, load_inst(lr.vreg, la.vreg, LType::I64, line, col)); ptr = pr.vreg; len = lr.vreg }
}
```

---

**R3 — O campo gordo de struct passa a ser ESCRITO. Fecha as miscompilações silenciosas de §5.1-5.4.**

`store_struct_fields` (`lower.tks:6931`) e `store_assign_value` (`lower.tks:7300`) ganham a bifurcação
que `store_variant_payload` (`lower.tks:4536`) já tem, e que é o modelo a copiar:

```teko
/**
 * store_field_value — escrever UM valor de campo no endereço já calculado `addr`.
 *
 * Bifurca em `is_fat_type` exactamente como `store_variant_payload` (degrau 6) faz para o
 * payload de um invólucro: um campo gordo escreve a sua imagem de 16 bytes por
 * `store_fat_image`, um campo escalar escreve o seu único word. O layout já reserva 16 bytes
 * para o campo gordo (`field_layout_size`) e `lower_fat_field` já lê 16 — antes desta função
 * escrevia-se 8, e o comprimento lido de volta era o que estivesse no slot (medido: 0).
 *
 * @param LowerCtx ctx  o contexto de lowering a estender
 * @param u32 addr  o VReg do endereço do campo
 * @param checker::Type declared  o tipo DECLARADO do campo (não o do valor — uma colocação
 *                                num slot de união constrói o invólucro)
 * @param checker::TExpr value  a expressão do valor a colocar
 * @param u32 line  a linha de origem
 * @param u32 col  a coluna de origem
 * @return LowerCtx | error  o contexto com o(s) store(s), ou propagado do lowering do valor
 * @throws propagado de `lower_fat_expr`/`lower_value_into_type`
 * @since 0.3.1.0 raiz A
 */
fn store_field_value(ctx: LowerCtx, addr: u32, declared: checker::Type, value: checker::TExpr, line: u32, col: u32) -> LowerCtx | error {
    if is_fat_type(declared) {
        let fo = match lower_fat_expr(ctx, value) { LoweredFat as x => x; error as err => return err }
        return store_fat_image(fo.ctx, addr, fo, line, col)
    }
    let vo = match lower_value_into_type(ctx, declared, value) { Lowered as x => x; error as err => return err }
    ctx_append(vo.ctx, store_inst(addr, vo.vreg, ltype_of(declared, ctx.enums), line, col))
}
```

**Nota que vale o crumb inteiro:** hoje `store_struct_fields` usa `ltype_of(field_vals[i].type)` — o
tipo do VALOR — e não o do CAMPO. Ao passar ao tipo DECLARADO fecha-se, de graça, a mesma classe de
divergência que o degrau 8 fechou no push (*"a placement into a union slot must build the uniform
tag+payload wrapper"*).

---

**R4 — O elemento gordo de slice. É o degrau 10, e sai inteiro dos crumbs anteriores.**

Três sítios, os que `fat_element_push_stop` já nomeia:

1. `lower_array_lit` (`lower.tks:7048`) e `store_array_elements` (7060): `stride` passa a
   `value_image_bytes(elem_t, ctx.enums)`, e o store de cada elemento a `store_field_value`.
2. `lower_index_slice` (`lower.tks:7034`): quando o elemento é gordo o load é `load_fat_image` — e
   por isso `lower_fat_expr` (`lower.tks:5622`) ganha o braço `checker::TIndex => lower_index_fat`,
   o que fecha a paragem `fat-pointer receiver \`index\`` medida em §5.5.
3. `lower_list_push` (`lower.tks:5839`): `fat_element_push_stop` desaparece; `elem_lt`/`esz` passam
   por `value_image_bytes` (16), e o item vai para um slot de item de 16 bytes por `store_fat_image`.
   **Não há boxing:** um valor gordo JÁ é os seus dois words; o ponteiro que carrega já aponta para
   armazenamento de outrem. Isto é o que o distingue da RAIZ B e é a razão pela qual não abre
   nenhuma questão de posse.

---

**R5 — A prótese do resultado do runtime deixa de ser artesanal.**

`call_symbol` (`lower.tks:1911`) ganha a família `teko::str::*`/`teko::string::*` que hoje lhe falta,
e cada entrada gorda resolve para o seu gémeo `_len`, chamado por `lower_len_out_call`
(`lower.tks:6681`) — a função que o degrau 9 já escreveu e que hoje tem um só cliente. Os gémeos que
o fonte do compilador precisa hoje, em `src/runtime/teko_rt.{c,h}`, ao abrigo do ruling do dono de
2026-07-29 (`d4ad700`) e com a mesma razão escrita no sítio: `tk_str_slice_len`, `tk_str_slice_to_len`,
`tk_str_slice_from_len`.

---

**R6 — O backstop honesto, que é o que impede o próximo silêncio.**

Hoje um builtin de `str` sem entrada em `call_symbol` cai em `mangle_fn_symbol` e produz um símbolo
indefinido — falha de LINKER, não paragem nomeada. **Medido, §7 P2.** `call_symbol` passa a parar
por nome para qualquer callee de `call_ns` vazio com prefixo `teko::str`/`teko::string` sem entrada.
É a aplicação directa da lei de `d4ad700`: *"Silêncio é a palavra que manda. Uma paragem ruidosa e
endereçada é um degrau para fechar, não motivo para ir buscar C."*

---

### As fixtures de regressão — desenhadas para FALHAR sem a correcção

Membros novos do canal `own_native` (`examples/regressions/own_native/`), corpus a sair 42 nas duas
rotas. Cada um foi já MEDIDO a dar o número errado com o compilador de hoje (§5), o que é a prova
que o registo desta lane exige e que uma asserção sozinha não dá.

| membro | o que afirma | nativo HOJE | nativo com R1-R4 | C |
|---|---|---|---|---|
| `fat_field_roundtrip` | `Holder{name:str; n:i32}` construído, `.n`, `.len` e o conteúdo por índice | **exit 2** | 42 | 42 |
| `fat_field_offset_shift` | o mesmo com o campo gordo em segundo lugar (offset 8) | **exit 1** | 42 | 42 |
| `fat_field_reassign` | `h.name = "xy"` e reler `.len` | **exit 3** | 42 | 42 |
| `fat_field_as_argument` | `take(h.name)` a ler `.len` do lado de lá | **exit 1** | 42 | 42 |
| `fat_slice_field` | campo `[]i64`: `.len` e `[2]` | **exit 1** | 42 | 42 |
| `fat_slice_literal_index` | `[]str` literal, `.len` e `xs[i].len` de cada elemento | **PARA** (`fat-pointer receiver \`index\``) | 42 | 42 |
| `fat_slice_push_in_loop` | push de `str` num laço, cada elemento relido pelo seu próprio comprimento | **PARA** (`push onto a slice of FAT elements`) | 42 | 42 |
| `fat_str_slice_builtin` | `teko::str::slice/slice_to/slice_from` e o comprimento de cada resultado | **FALHA A LINKAR** (`undefined reference to teko_slice`) | 42 | 42 |

Duas asserções unitárias em `src/lir/lower_test.tkt`, no espírito de
`lwt_agg_empty_variant_const_interns_its_tag_image` (que foi o teste que apanhou o consumidor
esquecido no degrau 6 — uma asserção de LARGURA é o que apanha um consumidor esquecido):

- `lwt_fat_struct_field_stores_both_halves` — o texto do LIR de um `Holder{name = "x"}` tem DOIS
  stores, `Ptr` no offset do campo e `I64` no offset+8;
- `lwt_fat_slice_element_stride_is_sixteen` — o `alloca` de um `[]str` de 3 elementos é 48, não 24.

### Os pontos de ritual — onde o portão completo tem de passar

| depois de | portão |
|---|---|
| R1 | `teko test .` verde **e** corpus `own_native` byte-idêntico ao de hoje nas duas rotas (R1 não muda comportamento; se mudar, R1 está errado) |
| R2 | idem |
| R3 | rota completa de três estágios + as cinco fixtures de campo gordo a 42 nas duas rotas |
| R4 | rota completa + `fat_slice_*` a 42 + auto-construção nativa a passar `global_symbol_names` |
| R5 | rota completa + `fat_str_slice_builtin` a 42 |
| R6 | `teko test .` + uma sonda que chame um builtin `teko::str::*` não implementado e receba PARAGEM NOMEADA, não erro de linker |

### O tamanho, comparado honestamente com continuar degrau a degrau

**Continuar degrau a degrau.** Medido: 10 degraus fecharam em cerca de dois dias, cada um 1-2
commits. O que resta da RAIZ A, contado do inventário de §3 e das medições de §5 e §7:
elemento gordo (3 sítios), escritor de campo gordo (2 sítios), `str::slice`/`slice_to`/`slice_from`
(80 sítios de chamada no fonte), resultado gordo de closure (`lower.tks:2439`), resultado gordo de
despacho de interface (`lower.tks:2105`), retorno gordo de lambda (`lower.tks:2940`), comparação
ORDENADA de `str`, `[][]T`, e os 29 dos 32 pontos de entrada do runtime que devolvem `tk_str` e ainda
não têm gémeo. **Chamemos-lhe 10 degraus.** E três deles — os de §5 — **NÃO SE ANUNCIAM**: não há
paragem a apontar-lhes o sítio, e o `fixpoint` é cego a eles por construção.

**Correcção de raiz.** Seis crumbs, **um ficheiro de produto** (`src/lir/lower.tks`) mais três
entradas em `src/runtime/teko_rt.{c,h}`. Zero mudanças em `src/lir/lir.tks`, zero em
`src/backend/**`, zero em regalloc/stackify/printer/`lir_interp.tks`. Nenhuma funcionalidade nova de
linguagem, portanto nenhuma restrição de seed de bootstrap.

**A comparação:** a correcção de raiz custa aproximadamente o mesmo que **3** degraus e fecha **10**,
incluindo os três que não se anunciam. E o argumento decisivo não é o custo, é este: os três
silenciosos de §5 **não são fecháveis degrau a degrau**, porque um degrau é uma paragem e eles não
param. Fechá-los exige exactamente a pergunta que este desenho faz — "quem escreve a imagem de um
valor gordo?" — e essa pergunta só tem uma resposta boa se for feita uma vez.

### Tensões de lei, e a resolução

1. **Alargar o stride de elemento parece a "by-value-element lane" que o dono adiou no degrau 4.**
   Não é, e o teste que separa as duas é o que o próprio ruling do degrau 4 usa: *precisa de uma
   decisão de POSSE?* Um agregado por valor precisa (que região? quem liberta?) — por isso foi
   adiado. Um valor gordo não precisa de nenhuma: os seus dois words SÃO o valor, o ponteiro que
   carrega já aponta para armazenamento que já tem dono, e não se copia nada. R1-R4 não acrescentam
   uma única política de alocação. **Resolvido: R1-R4 cabem dentro do ruling do degrau 4, não o
   reabrem.**
2. **"Não devem existir novas emissões em C".** R5 acrescenta a `teko_rt.{c,h}`, que é o RUNTIME
   contra o qual o nativo LINKA, não emissão do compilador — a distinção que `d4ad700` legisla, e
   com o mesmo padrão de razão escrita no sítio que o degrau 9 deixou como exemplo trabalhado.
   **Resolvido, dentro da permissão datada.**
3. **A regra do oráculo.** Todos os defeitos de §5 têm a rota C a acertar e o nativo a errar. Pela
   regra, são bugs do nativo até prova em contrário, e não questões de arquitectura. **Não há
   ruling a pedir. Não há HALT.**

---

## 7. A previsão — que é o que torna isto útil e não apenas arrumado

Ordenada, falsificável, e com o mecanismo nomeado. Onde pude medir em avanço, medi.

### P1 — O degrau 10 é a paragem corrente. **JÁ VERIFICADA nesta sessão.**

```
teko: .: native backend N1: a push onto a slice of FAT elements (`[]str`/`[][]T`) needs the
two-word element stride the array-literal store and the index load do not carry yet (N2)
[in `teko::backend::global_symbol_names`]
```

Auto-construção nativa com `.gen1c` sobre o tip `d4ad700`. Confirma o enunciado ("degrau 10, aberto,
o passo de elementos gordos") e corrige-o num ponto: são **três** sítios, não dois — o store do
literal também (§5.5).

### P2 — Logo atrás do degrau 10 vem `teko::str::slice`/`slice_to`/`slice_from`, e **NÃO como paragem: como FALHA DE LINKER.** **JÁ VERIFICADA em sonda isolada.**

```
$ TEKO_BACKEND=native .gen1c/teko <sonda com teko::str::slice(s, 1, 3)> …
(.text+0x3a): undefined reference to `teko_slice'
collect2: error: ld returned 1 exit status
```

E a gémea escalar, pelo mesmo mecanismo:

```
(.text+0x29): undefined reference to `teko_contains'
```

**Mecanismo, com o código colado.** `call_symbol` (`lower.tks:1911-1928`): com `call_ns` vazio,
`native_builtin_symbol` (io/cov/arena) não conhece `slice`; `is_list_builtin_call` não casa; o ramo
de paragem honesta só dispara para `segs.len == 1` e `teko::str::slice` tem três segmentos — logo cai
em `mangle_fn_symbol("", "slice", false)` = `teko_slice`, que nunca é definido. **É exactamente o
mesmo mecanismo que o degrau 9 mediu para `concat`** (*"failed the link with 'undefined reference to
teko_concat'"*, mensagem do commit `24ee25c`), e que ficou fechado para `concat` e para mais nenhum.

**Volume:** `teko::str::slice` 34 sítios de chamada, `slice_to` 24, `slice_from` 22 — **80** no fonte
do compilador. `ends_with` 37 e `contains` 20 pelo mesmo mecanismo (escalares, mesma falha de link).

### P3 — Fechados o degrau 10 e o P2, a auto-construção produz um gen2 que **constrói e depois mente**. É a previsão mais importante e é a que vale a pena registar antes de acontecer.

Os três defeitos de §5 não param. **383 das 717 declarações de `struct` do compilador (53%) têm
campo gordo.** A mais central é `LStructLayout` (`src/lir/lir.tks:527`), com quatro; é lida por
`layout.field_names.len` em `field_offset_of` (`lower.tks:6885`) e `field_type_of` (6907), e esse
`.len` mede **0** hoje.

**Previsão específica e falsificável:** assim que o gen2 nativo existir, ele falhará a compilar
qualquer programa com `struct`, com um erro da família `unknown field … (internal)` ou com SIGSEGV,
**antes** de o `fixpoint` chegar a comparar gen2 com gen3. Se em vez disso o gen2 compilar
normalmente, o meu modelo está errado e quero saber.

### P4 — Resultado gordo de CLOSURE e de despacho de INTERFACE, ambos já com paragem nomeada à espera

`lower_call_fat`, `src/lir/lower.tks:5668-5669`:

```
if cl.is_closure_call { return error { message = "native backend N1: fat-pointer closure-call result not yet lowered (N2)" } }
if cl.is_iface_dispatch { return error { message = "native backend N1: fat-pointer interface-dispatch result not yet lowered (N2)" } }
```

A segunda já foi vista em corpus antes (`native_iface_fat_known_stop`, e o mapa das 6 pernas conta-a
como uma das 26). Ambas são a mesma prótese (repr. 6) por estender ao lado do chamado — uma vtable
ou um thunk não reservam hoje o slot escondido. **Previsão: aparecem depois de P2, e a correcção é
uma só, não duas.**

### P5 — Retorno gordo de LAMBDA

`lower_return_fat`, `lower.tks:2940`: `"native backend N1: a fat-typed lambda return is not yet
lowered (N2)"`. Mesma prótese, mesma família de P4.

### P6 — Comparação ORDENADA de `str` (`<`, `>=`)

Paragem nomeada já no ficheiro (degrau 7 deixou-a de propósito). Precisa de `tk_str_cmp` e de um
contrato lexicográfico. **Previsão: só aparece se o fonte do compilador ordenar strings** — se
nenhum `<` sobre `str` existir em `src/**`, não aparece nesta lane.

### P7 — Windows, e só Windows: a lista fechada de `is_str_arg_builtin`

`str_pair_by_ref_x86` (`isel_x86_64.tks:942`) rematerializa o par por referência **só** para os sete
símbolos de `is_str_arg_builtin` (`lower.tks:1523`: `tk_print`, `tk_println`, `tk_eprint`,
`tk_eprintln`, `tk_write`, `tk_ewrite`, `tk_panic_str`). O runtime declara **44** assinaturas que
recebem `tk_str` por valor. Qualquer uma das outras 37, alcançada no Win64, passa dois registos onde
o Win64 quer um endereço — **exactamente o SIGSEGV do `own_print_exit` que o doc-comment desse sítio
descreve**, outra vez. Fora do caminho crítico do Linux; morde na perna Windows em 0.3.1.2/0.3.1.3.

### P8 — O que eu prevejo que NÃO vai acontecer, porque uma previsão sem lado negativo não se pode falsificar

**Não vai aparecer nenhum degrau desta lane cuja correcção seja em `src/backend/**`** (isel, encode,
objfile, regalloc, stackify), enquanto a perna for Linux e o alvo o host. Base: 0 em 10 até aqui, e
as 26 mensagens de LOWERING do mapa das 6 pernas byte-idênticas nas seis plataformas contra 1 de
EMISSÃO (`isel x86-64: B1-args`) que só aparece no Windows. Se aparecer um degrau de `src/backend/**`
na perna Linux, o modelo "a lowering é a estrangulação" está errado e isso é notícia.

---

## 8. Resumo — confirma ou refuta, e o que recomendo

**A hipótese do dono REFUTA-SE na letra.** O runtime foi tocado em **2 de 10** degraus e nunca
sozinho; `src/lir/lower.tks` foi tocado em **10 de 10**; `src/backend/**` em **0 de 10**. Nenhum
degrau desta lane foi um degrau de runtime.

**E confirma-se no espírito, com a seta invertida.** Nos dois toques, o runtime estava certo e o
backend é que não sabia falar com ele — e a razão está escrita no próprio `teko_rt.h:214-222`: a
captura de resultado do backend é **um** registo, e um `tk_str` devolvido por valor ocupa **dois**.

**A suspeita sobre os valores gordos CONFIRMA-SE, com uma correcção que muda o plano.** Não há uma
raiz comum, há **duas**, e elas alternam:

- **RAIZ A — os valores gordos são cidadãos de segunda.** Degraus 5, 6, 7, 9, 10. Nove
  representações de "dois words" espalhadas pelo ficheiro, nenhuma partilhada, e uma décima que não
  existe (o elemento de slice).
- **RAIZ B — o valor de um agregado é o endereço de um slot de frame.** Degraus 4 e 8, mais o
  invólucro devolvido que pendura. Já reclassificada como BUG pelo dono.
- Os degraus 1, 2 e 3 não são de nenhuma das duas, e já fecharam.

**O achado mais grave não é nenhum degrau: são três respostas erradas e caladas**, previstas pelo
modelo e MEDIDAS nesta sessão — construir um struct com campo `str`/`[]T` e ler `.len` dá **0**;
atribuir a esse campo não muda o comprimento; passá-lo como argumento leva o comprimento errado. A
metade ponteiro está certa, a metade comprimento nunca é escrita, e o caminho de `const` está certo
(que é a razão pela qual isto sobreviveu). **53% dos structs do compilador têm um campo gordo, e um
deles é `LStructLayout`.**

**O que recomendo, por ordem:**

1. **Não fechar o degrau 10 sozinho.** Ele é o crumb R4 de um desenho de seis, e fechá-lo sozinho
   entrega um compilador que compila e mente (P3).
2. **Abrir o vagão da RAIZ A com os seis crumbs R1-R6 de §6.** Um ficheiro de produto, três entradas
   de runtime, sem tocar na LIR nem em `src/backend/**`. Custa cerca de três degraus e fecha dez —
   incluindo os três que nunca se anunciariam.
3. **Pôr as oito fixtures de §6 no corpus `own_native` ANTES das correcções**, medidas a falhar. Seis
   delas já têm o número errado registado aqui.
4. **R6 primeiro se houver pressa** — é o crumb mais barato e converte a próxima falha de linker
   (P2, já medida) numa paragem nomeada. Uma paragem endereçada é um degrau; um `undefined
   reference` é arqueologia.
5. **Reportar, sem transformar em vagão:** a RAIZ B continua aberta e é independente; a lista fechada
   de `is_str_arg_builtin` (P7) morde no Windows mais tarde.

**A previsão pela qual quero ser julgado:** P2 (`undefined reference to teko_slice`, já medida) é o
que aparece a seguir ao degrau 10, e P3 (o gen2 que constrói e depois falha a compilar qualquer
`struct`) é o que aparece a seguir a P2. Se P3 não se verificar — se o gen2 nativo compilar um
`struct` normalmente — o modelo deste documento está errado, e isso é a coisa mais útil que ele pode
produzir.
