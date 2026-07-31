# O actuador de região — as duas taxas, a inversão, e o que ficou por fazer

Encargo do dono, 2026-07-31: *"Vais construir o actuador — a peça sem a qual toda a disciplina de
arenas desta lane não liberta um único byte."* Duas metades independentes: a variante com região da
concatenação de `str`, e o selector de roteamento a passar de dois níveis para N.

Ambas entregues. **A taxa de recuperação do build do compilador NÃO subiu**, e a razão está medida
abaixo — não é do selector.

---

## As duas taxas de recuperação

Corridas iguais, `TEKO_BACKEND=c ./teko . -o <dir> --no-verify --release` com `TEKO_ARENA_OBS`, na
mesma caixa, com a mesma árvore.

| | antes (`ca19dedf`) | depois (este ramo) |
|---|---|---|
| `reclaim ratio` | **0,0 %** | **0,0 %** |
| raiz (tempo de vida do processo) | 1946,3 MB | 1940,0 MB |
| âmbito (libertado ao largar a região) | 0,0 MB | 0,0 MB |
| regiões largadas | 11 de 5007 | 11 de 5013 |
| pico auto-reportado | 1594,9 MB | 1595,3 MB |

```
antes:   reclaim ratio: 0.0%  (reclaimed / allocated)
depois:  reclaim ratio: 0.0%  (reclaimed / allocated)
```

**O actuador não actuou no compilador.** Actuou, e está provado que actua, no regressor — a diferença
entre as duas coisas é o assunto de todo este documento.

---

## Que o actuador ACTUA — a prova, no C emitido

O mesmo projecto (`examples/regressions/region_actuator`) compilado pelo compilador do pai e pelo
compilador deste ramo, na rota C. As duas saídas correm e dão **42**.

| | pai | este ramo |
|---|---|---|
| `tk_slice_push(` (raiz) | 3 | **1** |
| `tk_slice_push_r(` (região) | 0 | **2** |

E a linha que interessa, no C do ramo:

```c
uint64_t *_sp1293 = (uint64_t *)tk_slice_push_r(_sb1291.ptr, _sb1291.len, &_si1292,
                                                sizeof(uint64_t), &_sl1294, _tkbr841);
```

O `push` está escrito **dentro de um ciclo aninhado**, e a região que ele recebe (`_tkbr841`) é a do
bloco **de fora**, o que declara o acumulador. Isto é exactamente o que os dois níveis não sabiam
fazer: o roteamento já era por destino, e agora vai buscar a região do bloco DONO do destino em vez
da moldura da função.

O único `tk_slice_push(` que sobra é o do braço que **escapa** — e essa é a inversão.

---

## A inversão, que era a condição de entrega

O alarme do dono: *"Hoje o escape é seguro PORQUE nada é libertado. No dia em que o roteamento
funcionar, esse mesmo caso passa a ser libertação de storage vivo."*

O regressor tem os dois braços lado a lado, na mesma forma, com uma só diferença:

* `block_local_rounds` — o acumulador nasce no topo do bloco, cresce por um `push` num ciclo
  aninhado, e só é lido dentro do bloco. **Roteado** (`tk_slice_push_r`, `_tkbr`);
* `escaped_tail` — o MESMO acumulador, mas com um `kept = acc` que o entrega a uma ligação de fora.
  A análise de escape marca-o e o selector **recusa a região do bloco**. **Preservado**
  (`tk_slice_push`, raiz).

O valor escapado é lido de volta **depois** de 2048 rondas posteriores abrirem e largarem regiões por
cima do mesmo armazenamento, e volta com o comprimento certo e os bytes certos. O mesmo para uma
`str` construída dentro do bloco e entregue para fora.

| configuração | código de saída |
|---|---|
| rota nativa | **42** |
| rota C | **42** |
| rota nativa + `TEKO_MEM_PARANOID=1` | **42** |
| rota C + `TEKO_MEM_PARANOID=1` | **42** |

Os quatro cenários estão no `.tkr` e o projecto está registado no `teko.tkp`. Um valor que escapa o
bloco é **preservado**; nunca libertado em silêncio. Os códigos de falha são distintos por braço
(11 = o acumulador do bloco corrompeu-se; 12/13 = o `[]u64` escapado foi libertado; 14 = a `str`
escapada foi libertada), logo uma regressão diz qual das metades cedeu.

---

## Porque a taxa não subiu — e o número é brutal

O censo do C emitido pelo próprio compilador, antes e depois, é o mesmo:

| | quantidade |
|---|---|
| nascimentos de acumulador (`= teko::list::empty()`) em `src/` | **936** |
| chamadas a `teko::list::push(` em `src/` | **1587** |
| regiões de MOLDURA distintas no `teko.c` gerado (`_tkfr`) | **1** |
| regiões de BLOCO distintas no `teko.c` gerado (`_tkbr`) | **1** |
| `tk_slice_push_r(` no `teko.c` gerado | **1** |

**O selector tinha, no compilador inteiro, duas regiões para onde rotear.** Agora sabe caminhar N
níveis — e continua a ter duas, porque o que falta não é o caminho: é o destino qualificado.

A porta que fecha não é o selector nem o predicado `cg_same_named_struct` (esse foi alargado neste
ramo: um acumulador de slice provado local ao bloco passa a ABRIR região, `binding_is_block_local_slice`).
A porta que fecha é o **conjunto de escape**, e fecha por três regras que estão no
`src/checker/escape.tks` por escrito:

1. **`TCall` — todo o argumento de chamada escapa** (`mark_expr`, arm `TCall`: *"every argument may
   be RETAINED by the callee → escaping (one-depth, conservative)"*). Um acumulador que seja passado
   a uma função — que é como se consome um acumulador — está marcado.
2. **Toda a inicialização de ligação escapa** (`mark_stmt`, arm `TBinding`: `mark_expr(b.value, true, acc)`,
   com o comentário *"we do not run a second fixpoint pass"*). Um `let n = xs.len` marca `xs`.
3. **A cauda da função escapa.** Um acumulador devolvido está marcado, e correctamente.

Das 936 ligações, **uma** sobrevive às três.

---

## O alvo que o oráculo apontou, e porque este actuador não lhe chega

O coordenador apontou `src/checker/resolve.tks:1752` — o `out` de `variant_siblings`: 930,7 MB na
tabela RA1 do `TEKO_ARENA_OBS`, 1 233 373 *copy-grows*, **48 % de toda a memória do build**, uma ordem
de grandeza à frente do segundo colocado.

Fui lá. A função é seis linhas:

```teko
fn variant_siblings(xs: []Type, skip: i64) -> []Type {
    mut out: []Type = teko::list::empty()
    …
    out
}
```

**O `out` é DEVOLVIDO.** Não é uma imprecisão da análise: o valor sai mesmo da função. Nenhuma região
desta função — nem de bloco, nem de moldura — o pode possuir, e é a regra 3 acima, a que está
correcta, que o marca.

E o sítio de chamada mostra onde ele podia morrer (`resolve.tks:1838`):

```teko
match variant_member_admissible(members[j], variant_siblings(members, j), table) { … }
```

O temporário nasce, é consumido e morre **dentro de uma iteração do ciclo do CHAMADOR**. A região
certa para o possuir é a do corpo desse ciclo. Mas o armazenamento é alocado **dentro do chamado**, e
o chamado não tem como saber da região do chamador: isso é uma **convenção de chamada com passagem
de arena** (um parâmetro de região no chamado), ou a espinha transitiva. Não é o selector de
roteamento, e o selector não lá chega por mais níveis que saiba caminhar.

**Registo, porque é a tentação óbvia e é a que o alarme do dono proíbe:** dar ao `tk_alloc` uma
"região corrente" global que o bloco do chamador empurra e desempilha resolveria o `variant_siblings`
num dia — e libertaria o valor que o chamado DEVOLVE. É exactamente a troca de um vazamento por uma
corrupção. Não foi feito, e não deve ser feito sem a espinha.

---

## O que foi entregue

**METADE 1 — `tk_str_concat_r`** (`src/runtime/teko_rt.{c,h}`). A variante que aceita região, na
relação exacta que o `tk_slice_push_r` tem com o `tk_slice_push`: o `_r` recebe a região do destino, e
o `tk_str_concat` passa a ser o seu invólucro de raiz. O caso raiz mantém o `malloc` histórico byte a
byte — mesma posse, mesma contabilidade de obs, mesmo pânico de OOM — e o RA do chamador é
estacionado (`tk_g_concat_ra`) tal como o `tk_g_push_ra` faz, para a atribuição do obs continuar a
apontar à função gerada. A superfície que faltava ao runtime existe.

**METADE 2 — o selector de N níveis** (`src/codegen/codegen.tks`, `src/checker/escape.tks`):

* `RegionFrame` passa a carregar o CORPO do bloco que serve (`body`/`is_value`);
* `cg_owning_block_region` caminha a pilha de dentro para fora e devolve a região do primeiro bloco
  que **declara** o destino e o prova local ao bloco;
* `cg_push_route` é o selector inteiro numa função: bloco dono → `@fo` → moldura → raiz;
* `cg_block_has_block_local` abre região também para um acumulador de slice local ao bloco — a porta
  sem a qual o selector não teria destino;
* `binding_is_block_local_slice` / `assign_routes_to_block` / `count_block_local_reads_slice` são os
  predicados novos, conservadores por construção.

**O regressor da inversão** — `examples/regressions/region_actuator`, quatro cenários, registado no
`teko.tkp`.

---

## O que ficou por fazer

1. **O consumidor do `tk_str_concat_r`.** A superfície existe e nada a chama. Rotear uma `str` para
   uma região exige provar que ela não sai do bloco, e a `str` do compilador é quase sempre um
   argumento de chamada ou uma inicialização de ligação — as duas regras que marcam tudo. O braço
   `escaped_label` do regressor já fixa a inversão para o dia em que for ligada.
2. **A espinha transitiva** (`typer.tks:5404`) e/ou uma **convenção de chamada com passagem de
   arena**. É o que separa os 930,7 MB do `variant_siblings` de qualquer região. Sem ela o actuador
   está do lado certo da fronteira e não chega ao volume.
3. **A precisão do conjunto de escape.** A regra 2 acima (*toda a inicialização de ligação escapa*)
   admite um ponto fixo monótono e sóbrio — o próprio comentário do código diz que não corre a
   segunda passagem. Não o fiz aqui: alargar o conjunto de escape torna MAIS coisas roteáveis, e um
   engano nessa direcção é um use-after-free, não um vazamento. É trabalho com o alarme do dono em
   cima e merece a sua própria medição.
4. **A rota nativa não emite roteamento por região.** O `tk_slice_push_r` é emitido só pela rota C
   (era já assim antes deste ramo). As duas rotas dão a mesma resposta — o regressor prova-o nos
   quatro cenários — mas a recuperação de arena, quando houver, será só da rota C até a rota nativa
   ganhar a mesma emissão.

---

## Ritual

* build da rota C limpo, sem avisos;
* **FIXPOINT fechado**: gen2 e gen3 construídos no MESMO caminho de saída, `teko.c` e binário
  **byte-idênticos** (`sha256 d15201b5ebad62c8605553ee4cb49cdb6a829517ab07b833891f893b5310f1b2`);
* picos auto-reportados: gen1 1704,3 MB, gen2 1595,3 MB, gen3 1583,8 MB.
