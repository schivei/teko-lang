# Poda / jardinagem da AST — projeção por fronteira de fase (0.3.1)

**Base de leitura:** `fix/retirement` @ `8a5985cb`, cruzada com `perf/expurgo-total-mecanico`
(tip ~`271dda2d`). **Escopo:** LER + escrever design-doc. Nenhum código de produto. Este
documento transforma a pergunta do dono numa arquitetura executável (crumb sequence,
contratos de tipo, fixtures, prova de segurança) e termina com uma RECOMENDAÇÃO clara para
o dono decidir.

> **A pergunta do dono (preservada):** *"Uma vez que um trecho da AST é analisado, não dá
> pra 'comprimir'? Ou seja, dropar o que não é usado na próxima etapa, pra reduzir o quanto
> há na árvore para a próxima etapa?"*
>
> **Nomenclatura do dono para o mecanismo: poda / jardinagem (pruning / gardening).** Depois
> que uma fase analisa um trecho, você PODA os ramos/campos que não alimentam a próxima
> etapa — dirigindo a memória só para o que segue vivo. "Jardinagem" é o processo contínuo
> de tender a árvore fase a fase. O documento usa esse nome ao longo do texto.

**Meta do dono:** < 1 GB de RSS no build seco. **Muro atual** (`TEKO_PHASE_RSS`): after
checker 3140 · monomorph 3407 · consteval 3689 · codegen OOM (>4 GiB). O pico é RETENÇÃO
ENTRE FASES.

---

## 1. O que o código revela — por que "comprimir" hoje é impossível sem quebrar aliases

A intuição do dono está certa, mas o obstáculo é mais fundo do que "dropar campos". O
rastreio do código expõe TRÊS fatos que governam todo o desenho:

### 1.1 O typed-AST NÃO é uma cópia do cru — ele É o cru, por referência

`checker::TProgram.items` tem tipo `@TItem()`, cuja lowering é
(`src/checker/tast.tks:97`):

```
TItem() = checker::TFunction | parser::TypeDecl | parser::UseDecl | @TStatement() | checker::TConstDecl
```

Ou seja, **os nós CRUS do parser `parser::TypeDecl` e `parser::UseDecl` são embutidos
DIRETAMENTE no programa tipado** — não há versão tipada deles. E dentro do que É tipado, o
`checker::TFunction` (`tast.tks:71`) carrega campos crus por referência:

- `params: []parser::Param` — o array cru de parâmetros do parser (com `default_expr: Expr`
  cru dentro);
- `doc: parser::DocSpan`, `has_doc: bool` — trivia de doc-comment;
- `guard: parser::Pred` — a árvore de predicado de plataforma crua;
- `type_constraints: []@ConstraintExpr()` — as restrições crus.

`checker::TConstDecl` idem (`doc: parser::DocSpan`, `guard: parser::Pred`). Cada
`checker::TExpr` (`tast.tks:5`) carrega `line: u32; col: u32` — trivia de fonte.

**Consequência:** o "trecho analisado" que o dono quer comprimir está fisicamente entrelaçado
com o cru. Não existe uma "AST crua" separada, à parte, que se possa soltar — o typed
aponta para dentro dela.

### 1.2 Cada fase RECONSTRÓI a árvore mas RE-ALIASA o cru — prendendo tudo vivo

O driver (`src/build/project.tks:204-256`, `checked_program_of`) roda:

```
checker  (type_program_with_deps_pre_mono) → PreMono { prog: TProgram; table: TypeTable }
mono     (monomorphize(prog, table))       → TProgram novo
comptime (expand_comptime(prog, table))    → TProgram novo
consteval(inline_consts(prog))             → TProgram novo
→ codegen (backend(prog))
```

Cada fase constrói uma árvore NOVA (mono reescreve corpos em `mono_walk`; consteval em
`inline_rw_expr`), mas **carrega os campos crus por referência**. Provas no código:

- `monomorph.tks:1035` — o instanciado sai com `doc = gf.doc`, `guard = gf.guard`
  (aliasa a `DocSpan`/`Pred` do genérico); os corpos passam por
  `default_expr = tmpl.params[pi].default_expr` (`:849`, `:1027`) — aliasa o `Expr` cru.
- `consteval.tks:432,748` — `TConstDecl` reconstruído com `doc = cd.doc; guard = cd.guard`
  (passthrough por referência).
- Toda reconstrução de `TExpr` em mono e consteval faz `line = e.line; col = e.col`
  (dezenas de sítios, `monomorph.tks:469-674`, `consteval.tks:301-654`).

Como o alocador é ARENA em `root` e **`root` nunca é liberado até a saída do processo**
(`ast-computed-arena-assessment-0.3.1.md` §1.2: reclaim 0,0 %), as árvores de TODAS as
fases coexistem: `pre.prog` + `pre.table` + `checked` + `expanded` + `inlined`, cada uma
aliando a anterior. O pico em codegen é essa SOMA. É o "segurar tudo" que
`reducao-memoria-arrays-0.3.1.md` §6bis nomeia como o residual não-push.

### 1.3 O sharing por referência é o cadeado (risco R2 / UAF)

O diagnóstico do agente anterior está correto e agora tem endereço: `params = f.params`,
`doc = gf.doc`, `TProgram.items` embutindo `parser::TypeDecl`. **Dropar a região de uma fase
anterior com o alias intacto = use-after-free**, exatamente o modo de falha que derrubou a
passagem `arena-por-escopo` (clone em massa + drop = UAF). A poda ingênua ("solta o cru")
corrompe o vizinho. A poda SEGURA precisa primeiro QUEBRAR o alias.

**É esse o insight central deste documento: "comprimir a AST" não é remover campos — é
QUEBRAR O ALIAS com uma cópia-projeção total, para então poder derrubar a região da fase
anterior INTEIRA (não só a árvore: também o scaffolding do checker — `Env`, escopos, tabelas
de resolução — que domina o baseline de 3140 MB).** A poda de campos (§3) é um refinamento
secundário sobre a cópia; o golpe grande é o DROP DE REGIÃO habilitado pela cópia.

---

## 2. O mapa por fronteira — o que cada fase LÊ / o que é DROPÁVEL

Rastreado no código. "Dropável" = nenhuma fase a jusante lê o campo (só é copiado adiante
como passageiro morto, ou nem isso).

### Fronteira parse → check

O checker lê TUDO do cru (nomes, tipos, corpos, `line/col` para diagnóstico, `doc` para o
`.tkh`/LSP, `guard` para filtro de plataforma). **Nada dropável aqui** — é a primeira
consumidora. O que muda de valor é que, DEPOIS do checker, o scaffolding do checker (`Env`,
escopos, caches de resolução) e as árvores de parse originais tornam-se lixo — reclamáveis
por drop de região (§4), não por poda de campo.

### Fronteira check → mono

Mono lê: `prog` (corpos tipados, para reescrever), `table: TypeTable`
(`monomorph.tks:946`) para resolver genéricos (`type_param_table` usa `type_constraints`,
`resolve_type` usa `table.regs`). Mono NÃO lê: `doc`, `has_doc` (só copia — `:1035`).
**Dropável ao entrar em mono:** `doc`/`has_doc`/`DocSpan` de todo `TFunction`/`TConstDecl`/
`TypeDecl`/`Field`. `line/col` são RE-EMITIDOS por mono (passageiros) — mas veja a fronteira
final: são load-bearing lá, então não podem ser podados aqui.

### Fronteira mono → comptime → consteval

`expand_comptime` e `inline_consts` leem corpos + `table` (comptime). Após consteval,
`table: TypeTable` está MORTA — ela NÃO é passada ao codegen (o `backend`/`codegen_and_report`
recebe só `prog: checker::TProgram`, `project.tks:2388`). **Dropável após consteval:** a
`TypeTable` inteira (`regs`, `by_last: NameIndex`, `comptimes`) — potencialmente grande
(índice de todos os tipos + registro por-último-segmento). `type_constraints` também morre
após mono (genéricos já resolvidos), MAS confirme que codegen não as toca antes de podar
(risco baixo, mas verificar — ver R-B).

### Fronteira consteval → codegen (a fronteira que o brief supôs ser o grande alvo)

**Correção honesta e load-bearing.** O brief sugeriu que a trivia de fonte (`line`/`col`,
`DocSpan`, forma sintática) seria o grande dropável frontend→codegen. O código diz o
contrário para `line`/`col`:

- **`line`/`col` NÃO são dropáveis.** Codegen os EMITE dentro do `teko.c`:
  `_tk_cast_loc_line = <e.line>; _tk_cast_loc_col = <e.col>` para diagnóstico de overflow de
  cast em runtime (`codegen.tks:2411-2440`, `:3465`, `:3574-3607`), e os usa na
  instrumentação de cobertura (`emit_cov_branch`, `:4264`, `:5723`). Podá-los muda o `teko.c`
  emitido byte-a-byte → **quebra o fixpoint `gen2==gen3`**. São leaves que vivem até o codegen.
- **`DocSpan`/`doc`/`has_doc` SÃO dropáveis** — nenhum sítio de codegen/mono/consteval/backend/
  lir lê `.doc`/`.byte_offset`/`DocSpan` (grep vazio fora de checker/collect/tkh). Só o
  emissor de `.tkh` (`emit/tkh.tks`) e o LSP os leem, e nenhum está no caminho do build seco
  do `teko.c`. **Este é o único campo de trivia limpo para poda.**
- **Codegen LÊ o cru `parser::TypeDecl` intensivamente** — `cg_find_decl`, `cg_find_variant_decl`,
  corpos de struct/class/interface/trait/enum, `f.params` (self-param, `emit_type`)
  (`codegen.tks:554-856`, `:806`, `:1204-1212`). Portanto `parser::TypeDecl`, `parser::UseDecl`
  e `parser::Param` **sobrevivem até o codegen** — não são dropáveis.

**Resumo do mapa (o que a poda de CAMPO pode remover, com honestidade sobre o tamanho):**

| Campo | Lido por | Dropável em | Tamanho do ganho |
|---|---|---|---|
| `doc: DocSpan` + `has_doc` (fn/const/type/field) | checker diag, `.tkh`, LSP | **entrada de mono** | pequeno (≈24 B + backing de str por decl; milhares de decls) |
| `TypeTable` (table) | mono, comptime | **após consteval** (não vai a codegen) | médio (índice de tipos + `by_last`) |
| `type_constraints` | mono | após mono (verificar codegen) | pequeno |
| `line`/`col` | **codegen (emitido no `.c`) + cobertura** | **NUNCA antes do codegen** | — (não podável) |
| `parser::TypeDecl`/`UseDecl`/`Param` | codegen | **NUNCA** | — (não podável) |

**Conclusão do mapa:** a poda de CAMPO rende pouco (basicamente `DocSpan` + a `TypeTable`
morta pós-consteval). **O ganho grande NÃO está em podar campos — está em derrubar a REGIÃO
inteira da fase anterior** (scaffolding do checker/parser, tabelas mortas), o que só é
possível depois de QUEBRAR O ALIAS via cópia-projeção. É isso que a §4 desenha.

---

## 3. O que "poda / jardinagem" realmente é, mecanicamente

Duas operações compostas, aplicadas em cada fronteira de fase:

1. **Projeção (quebra de alias):** deep-copy TOTAL da saída da fase N para uma região que
   SOBREVIVE, incluindo copiar/internar as strings (ver R2, §5) — de modo que a cópia não
   aponte para NADA da região da fase N.
2. **Poda (compressão):** durante a cópia, OMITIR os campos que a fase N+1 não lê (`DocSpan`,
   `doc`, `has_doc`; e, pós-consteval, não copiar a `TypeTable`).
3. **Despejo (drop):** derrubar a região INTEIRA da fase N (`region_drop_subtree`) — que
   agora não tem mais nenhum ponteiro vivo apontando para dentro dela.

O pico deixa de ser a SOMA das fases e passa a ser o MÁXIMO de (uma fase viva + a árvore
slim projetada). É o §6bis "despejo entre estágios" de `reducao-memoria-arrays-0.3.1.md`,
expresso como projeção de campos + drop de região, na variante WHOLE-PROGRAM (não por
unidade) e EM MEMÓRIA (sem disco).

---

## 4. Mecanismo — comparação dos dois candidatos

### (a) Projeção deep-copy EM MEMÓRIA (recomendado para as fronteiras internas)

Um walk estrutural que reconstrói a árvore da fase N numa região de sobrevivência,
internando strings e omitindo campos podados. O bracket de jardinagem por fronteira:

```
R_phase   = region_new(surviving_parent)     // região própria da fase N
region_enter(R_phase)
  out_raw = <roda a fase N; TODA alocação cai em R_phase>
region_leave()
out_slim  = project_*(surviving_parent, out_raw)   // deep-copy total p/ surviving_parent, poda doc
region_drop_subtree(R_phase)                        // reclama TODO o working set da fase N
// out_slim vive em surviving_parent, livre de alias
```

`project_*` aloca no `surviving_parent` (via `region_alloc` corrente), interna cada string
numa tabela local à cópia, e reconstrói cada nó. Depois do `region_drop_subtree`, o
scaffolding da fase (Env, escopos, tabelas, árvores intermediárias) some — e `out_slim`,
sendo alias-free, permanece válido.

**Custo:** pico TRANSITÓRIO de 2× a ÁRVORE durante a cópia (old em R_phase + slim em parent),
pago para reclamar tudo o que R_phase tinha ALÉM da árvore (o scaffolding, que é a maior
parte de 3140 MB). Sem IO, sem serializer, sem O(n²).

**Determinismo (R6):** trivial — walk em ordem fixa de campo, sem iteração de `map`/`hashset`,
sem ponteiro, sem timestamp. A tabela de intern é indexada por conteúdo de string em ordem de
visita determinística.

### (b) Round-trip `.tkb` (serialize → drop → deserialize)

`serialize_program(prog): []byte` (`emit/tkb_frame.tks:370`) →
`deserialize_program(data): TProgram` (`emit/tkb_read.tks`). Já em produção no path de pacote.

**Vantagem estrutural:** é total-copy-BY-CONSTRUCTION e drop-safe por construção — a
serialização achata TUDO (inclusive strings, via `StrTable` em `collect_program`) num buffer
`[]byte` auto-contido; a deserialização reconstrói tudo fresco na região nova, com ZERO alias
para a região velha. Determinismo já exercitado no path de pacote.

**Desvantagens decisivas para uso INTER-FASE em memória:**

1. **É O(n²) hoje.** O writer usa copy-grow por byte: `write_u8(buf, x) = [..buf, x]`
   (`emit/tkb_buf.tks:4`), e `write_u32`/`write_u64`/`write_str` idem. Serializar um
   `TProgram` grande é exatamente a patologia copy-grow de 4980 MB (o alvo do Eixo A) —
   **o serializer SOBE o pico, o oposto do que a jardinagem quer.** Precisa ser reescrito
   para O(n) (duas-passadas: conta total → `[total]byte = []` → escreve por índice) ANTES de
   servir a qualquer coisa. Isso é trabalho do Eixo A (matar push no `emit/`), não da
   jardinagem.
2. **Três árvores coexistem no pico:** árvore-fonte + buffer `[]byte` + árvore-resultado
   (durante deserialize). Em memória, isso é PIOR que o 2× da projeção direta.
3. **Só ganha no trough se o buffer for a DISCO.** A única vantagem real do `.tkb` é despejar
   o buffer em disco: no vale, só existem os bytes no disco (fonte já dropada, resultado ainda
   não construído). Esse é o despejo-em-DISCO — legítimo onde a barreira global impede fusão
   (o LINK do Eixo C, crumb RM-C13), não para as fronteiras internas fundíveis.

### Recomendação de mecanismo

**Para as fronteiras internas (check→mono→comptime→consteval→codegen): projeção
deep-copy EM MEMÓRIA (a).** É mais leve (sem O(n²), sem IO, 2× < 3×), determinística por
construção, e o drop de região a torna drop-safe.

**Reservar o `.tkb` round-trip (b) para o despejo em DISCO do Eixo C (RM-C13)** — a barreira
do LINK, onde o estágio não funde e o trough em disco vence o 2× de memória — E somente
DEPOIS de o serializer ser reescrito para O(n) (Eixo A no `emit/`). O `.tkb` continua o
formato certo do artefato de disco/incremental; é o mecanismo errado para comprimir entre
fases residentes.

**Ponto de honestidade sobre segurança:** o `.tkb` é drop-safe DE GRAÇA (auto-contido); a
projeção em memória exige que o deep-copy seja TOTAL, inclusive internar strings (§5). É o
único ponto onde (a) é mais arriscado que (b). Mitiga-se com a fixture de identidade (§7) e a
disciplina de intern obrigatória (§5).

---

## 5. Drop-safety (R2) — prova de que nada aliasa a memória dropada

**Obrigação de prova:** após `project_*(parent, out_raw)`, NENHUM ponteiro em `out_slim`
endereça `R_phase`. Só então `region_drop_subtree(R_phase)` é seguro.

**Onde o sharing existe hoje (os aliases a quebrar):**

| Alias | Sítio | Como a projeção quebra |
|---|---|---|
| `TProgram.items` embute `parser::TypeDecl`/`UseDecl` | `tast.tks:97` | deep-copy do `TypeDecl`/`UseDecl` inteiro (corpo, campos, métodos) para `parent` |
| `TFunction.params = f.params` (Param cru) | `monomorph.tks`, passthrough | copiar o array `Param`, incl. `default_expr: Expr` cru recursivamente |
| `doc = gf.doc` (`DocSpan`) | `monomorph.tks:1035`, `consteval.tks:432` | PODAR (não copiar) — `DocSpan` vira `docspan_none()` |
| `guard = gf.guard` (`Pred`) | idem | deep-copy da árvore `Pred` |
| strings (`str` = `{ptr,len}` para backing) | onipresente | **internar** cada string numa tabela em `parent` (copiar o backing) |

**O ponto sutil e LOAD-BEARING (a armadilha do R2):** em Teko, `str` é um cabeçalho
`{ptr,len}` que aponta para um backing. Copiar um `str` copia o CABEÇALHO mas ALIASA o
backing. Se os backings das strings (identificadores, literais) vivem em `R_phase`, o
`out_slim` "copiado" ainda aponta para `R_phase` pelos backings → o drop libera o backing sob
cabeçalhos vivos = UAF. **Portanto o deep-copy TEM de internar/copiar os backings de string
para `parent`.** Um deep-copy que esquece isso passa em teste superficial e corrompe em
produção — é a exata classe de bug do R2.

Isto reforça a §4: o `.tkb` faz esse intern DE GRAÇA (a `StrTable` de `collect_program`); a
projeção em memória precisa replicar a disciplina de intern à mão. O contrato do
`project_*` deve declarar, em Javadoc, a invariante "toda string internada em `parent`;
nenhum backing remanescente em `R_phase`".

**Postura conservadora:** a projeção é TOTAL por padrão (copia tudo o que é lido a jusante).
A poda só remove o que provadamente NENHUMA fase a jusante lê (`doc`/`has_doc`, `TypeTable`
pós-consteval). Na dúvida sobre um campo, COPIA (leak-safe: fica no `parent`, custa memória,
nunca UAF) — jamais poda especulativa. É a mesma postura de `escape.tks` ("dúvida → escapa").

---

## 6. Preservação do fixpoint (R6) — o `teko.c` byte-idêntico

A jardinagem NÃO pode mudar o `teko.c` emitido: `gen2==gen3` byte-idêntico é a prova.

- **`line`/`col` preservados até o codegen** (§2): a projeção COPIA `line/col` fielmente em
  todas as fronteiras — nunca poda (são emitidos no `.c`). Fixture: `gardening_line_col_emitted`.
- **A poda de `doc` é invisível ao codegen:** codegen nunca lê `DocSpan` → remover `doc` não
  muda um byte do `.c`. Prova: fixture de identidade `gardening_teko_c_identical` — build com
  jardinagem == build sem, byte-a-byte.
- **Determinismo do walk:** ordem fixa de campo; sem `map`/`hashset` no caminho da cópia; sem
  ponteiro/endereço; sem timestamp. Mesma entrada → mesma `out_slim` → mesmo `teko.c`.
- **Independência do gensym:** a jardinagem é WHOLE-PROGRAM — a emissão continua num único
  buffer global, então os nomes temporários derivados de `buf.len` NÃO mudam (a jardinagem
  não faz streaming por unidade). Logo a jardinagem **não depende de RM-C10** (determinizar
  gensym) — ao contrário do Eixo C. Ver §7.

**Guarda por crumb:** cada crumb que insere um drop valida `gen2==gen3` byte-idêntico E a
identidade contra o build pré-jardinagem. Na menor divergência: PARAR e achar o alias/campo
que a projeção copiou/podou errado — nunca maquiar.

---

## 7. Relação com o desenho existente (Eixo C, RM-C10)

### É o Eixo C (§6bis "despejo entre estágios") expresso como projeção de campos?

**Sim — é a variante WHOLE-PROGRAM, EM MEMÓRIA, do mesmo princípio unificador.** O Eixo C
(`reducao-memoria-arrays-0.3.1.md` §6bis) diz: "a cada ETAPA, gerar um artefato para a
próxima e despejar a memória; o pico vira o MÁXIMO de um estágio, não a soma". A jardinagem
É esse despejo, com duas diferenças de granularidade:

| | Jardinagem (este doc) | Eixo C (RM-C11/12/13) |
|---|---|---|
| Unidade de despejo | fase inteira (whole-program) | namespace (por unidade) |
| Mecanismo | projeção deep-copy em memória + drop de região | AST-incompleta + LINK/FFI-interna + fusão por unidade (+ `.tkb` na barreira) |
| Reduz o pico a | máx(uma FASE viva + slim) | máx(uma UNIDADE viva) |
| Custo de construção | BAIXO (um walk de cópia + brackets de região) | ALTO (reestruturar frontend, LINK, gensym streaming) |
| Depende de RM-C10 (gensym) | **NÃO** (emissão segue global) | **SIM** (pré-condição) |
| Depende do serializer O(n) | não (usa memória) | sim, na barreira (RM-C13) |

**Complementa ou supera?** COMPLEMENTA e ANTECIPA. A jardinagem entrega o teto "pico = máx de
uma fase" SEM a reestruturação do Eixo C — é o ganho que "dá para adiantar" já. O Eixo C vai
mais fundo ("pico = máx de uma UNIDADE"), mas é grande e arquitetural. Recomenda-se: **fazer
a jardinagem primeiro** (barato, whole-program); se o residual pós-jardinagem ainda exceder
a folga, o Eixo C aperta de fase→unidade. As duas dividem a mesma peça-fundação C6
(arena-por-escopo / `region_drop_subtree`): a jardinagem é C6 aplicado na FRONTEIRA DE FASE.

**Onde encaixa o gensym determinístico (RM-C10)?** A jardinagem NÃO precisa dele (emissão
whole-program preserva `buf.len`). RM-C10 continua sendo pré-condição EXCLUSIVA do Eixo C
(streaming por unidade). Isto é uma boa notícia de sequenciamento: a jardinagem pode entrar
ANTES e INDEPENDENTE de RM-C10/C11/C12.

**Relação com o outro doc (`ast-computed-arena-assessment`):** a jardinagem é ortogonal às
três ideias daquele doc (floor de arena, elisão de região, DPS de retorno). DPS ataca a
retenção no eixo de RETORNO (valor nasce na arena do caller); a jardinagem ataca a retenção
no eixo de FASE (árvore da fase anterior é derrubada). Somam. Nenhuma conflita.

---

## 8. Contratos de tipo / assinaturas (Teko, full-Javadoc — copiar verbatim)

As funções que o implementer adiciona. Todas em `.tks` (Teko-only). Nomes de módulo
sugeridos: um novo `src/checker/gardening.tks` (o walk de projeção) + brackets no driver
`src/build/project.tks`.

```teko
/**
 * project_program — a PODA da jardinagem: deep-copy TOTAL de um TProgram para a região
 * corrente (de sobrevivência), quebrando todo alias com a região da fase que o produziu e
 * OMITINDO os campos que nenhuma fase a jusante lê (doc/has_doc/DocSpan).
 *
 * Invariante de drop-safety (R2), load-bearing: ao retornar, NENHUM ponteiro na árvore
 * resultante endereça a região de origem — inclusive os BACKINGS de string, que são
 * internados na região corrente (um str copiado sem copiar o backing ainda aliasa a origem
 * e causaria UAF no drop). Ver docs/design/projecao-ast-entre-fases-0.3.1.md §5.
 *
 * Determinismo (R6): o walk visita campos em ordem fixa, sem iterar map/hashset, sem ler
 * endereço/timestamp — mesma entrada produz a MESMA árvore e o mesmo teko.c.
 *
 * @param prog  o programa tipado a projetar (vive na região da fase de origem)
 * @return      um TProgram equivalente, alias-free na região corrente, com doc podado
 * @since 0.3.1
 */
fn project_program(prog: checker::TProgram): checker::TProgram

/**
 * intern_str_into — interna uma string na região corrente, copiando o backing, de modo que
 * o resultado não aliase a região de origem. O primitivo de segurança de string do
 * project_* (§5): sem ele o deep-copy é incompleto e o drop vira UAF.
 *
 * @param tab  a tabela de intern acumulada nesta projeção (dedup por conteúdo)
 * @param s    a string a internar (seu backing pode viver na região de origem)
 * @return     a tabela atualizada e a string cujo backing vive na região corrente
 * @since 0.3.1
 */
fn intern_str_into(tab: InternTable, s: str): InternResult

/**
 * garden_phase — o BRACKET de jardinagem: roda a fase `run` numa região-filha própria,
 * projeta sua saída para o pai (de sobrevivência) e derruba a região-filha inteira,
 * reclamando todo o scaffolding da fase (Env, escopos, tabelas) que a projeção não copiou.
 *
 * É o C6 (arena-por-escopo) aplicado na FRONTEIRA DE FASE. O drop só é seguro porque
 * project_program quebrou todo alias (§5).
 *
 * @param parent  a região de sobrevivência onde a saída projetada passa a viver
 * @param run     a fase a executar (produz um TProgram na região-filha)
 * @return        a saída da fase, projetada e alias-free em `parent`
 * @since 0.3.1
 */
fn garden_phase(parent: ptr, run: fn(): checker::TProgram | error): checker::TProgram | error
```

**Funções existentes tocadas (sem reescrever a lógica delas — só envelopar/podar):**

- `src/build/project.tks:204` `checked_program_of` — inserir os brackets `garden_phase` nas
  fronteiras mono / comptime / consteval; após consteval, a `TypeTable` (`pre.table`) sai de
  escopo com a região do checker.
- `src/checker/monomorph.tks:1035` e `consteval.tks:432,748` — trocar `doc = gf.doc` por
  `doc = parser::docspan_none()` quando a projeção pós-mono for a fonte de verdade do doc
  (ou deixar a poda inteiramente para `project_program`, mais limpo — decisão do implementer).
- Reuso: `src/runtime/arena.tks` `region_new`/`region_enter`/`region_leave`/
  `region_drop_subtree` (já prontos, `reducao-memoria-arrays-0.3.1.md` §2).

---

## 9. Fixtures de regressão (inputs → códigos de saída nativos)

| fixture | asserta | exit |
|---|---|---|
| `gardening_teko_c_identical` | build COM jardinagem emite `teko.c` byte-idêntico ao build SEM (a poda de `doc` é invisível ao codegen) | 0 |
| `gardening_line_col_emitted` | um cast que estreita inteiro emite `_tk_cast_loc_line`/`col` com os números de fonte corretos após a projeção (line/col preservados) | 0 |
| `gardening_docspan_pruned` | após a projeção, o `.doc` de toda decl é `docspan_none()` (inversão: se algo a jusante lê doc, DEVE falhar) | 0 / (inversão falha) |
| `gardening_string_backing_copied` | inversão de drop-safety: uma projeção que copia o header de str mas NÃO o backing, seguida de `region_drop_subtree`, DEVE dar SIGSEGV/UAF — prova que o intern é obrigatório | (inversão falha) |
| `gardening_typetable_dropped` | a `TypeTable` é liberada após consteval (não sobrevive ao codegen); build permanece verde | 0 |
| `gardening_generic_instance_survives` | uma fn genérica instanciada em mono, cujo genérico vivia na região dropada, ainda emite corpo correto (deep-copy total do instanciado) | 0 |
| `gardening_fixpoint` | `gen2==gen3` byte-idêntico com a jardinagem ligada | 0 |
| `gardening_pred_guard_survives` | uma decl sob `#os(...)` (guard `Pred`) projetada da região dropada ainda filtra por plataforma corretamente | 0 |

Fixtures de disco/`.tkb` NÃO entram aqui — pertencem ao Eixo C (RM-C13). A jardinagem é
memória-pura.

---

## 10. Sequência de crumbs (do maior/mais-seguro ganho primeiro)

Ordem: construir a FUNDAÇÃO de projeção provada-idêntica ANTES de qualquer drop; depois
introduzir os drops do maior working-set-reclamável ao menor. Fixpoint `gen2==gen3` a cada
harvest; guard 6,5 GiB inviolável.

### PJ-1 — Instrumentar o split retenção-por-fase (baseline, nenhum drop)
Estender `report_phase_rss` para reportar, por fase, quanto do RSS é ÁRVORE viva vs
scaffolding reclamável (aproximar via marca de região antes/depois de cada fase). Baseline
registrada: quanto cada `region_drop_subtree` de fase RECLAMARIA. Nenhuma mudança de
comportamento. Gate: build + `teko test .`. Ritual: NÃO.

### PJ-2 — `project_program` + `intern_str_into` como transformação-IDENTIDADE (aditivo)
Escrever o walk de deep-copy total (com intern de string obrigatório, §5), PODANDO só `doc`/
`has_doc`. Inseri-lo como um passe no-op-ish (projeta, mantém a origem viva, NÃO dropa nada
ainda). Prova de que a cópia é TOTAL e fiel: `gardening_teko_c_identical` +
`gardening_line_col_emitted` + `gardening_fixpoint`. **Este crumb prova a corretude da cópia
ANTES de qualquer drop** — é a rede de segurança do R2. Gate: `[fixpoint]` byte-idêntico +
`gardening_string_backing_copied`. Ritual: SIM.

### PJ-3 — Drop pós-consteval: derrubar a `TypeTable` + a região do checker
O maior working-set morto: após `inline_consts`, `pre.table` (TypeTable) e todo o scaffolding
do checker/parse são lixo (nada disso vai ao codegen). Rodar checker→mono→comptime→consteval
numa região-filha; `garden_phase` projeta o `inlined` para o pai; `region_drop_subtree`
reclama a filha. **Maior queda isolada** (o baseline de 3140 MB é dominado por esse
scaffolding). Gate: `[fixpoint]` + `gardening_typetable_dropped` + RSS pós-drop medido.
Ritual: SIM.

### PJ-4 — Drop na fronteira mono: derrubar a árvore pré-mono + parse
Refinar PJ-3 subdividindo: rodar o checker numa região; projetar `PreMono{prog,table}`;
derrubar a região de parse/checker-scaffolding ANTES de mono. Mono passa a rodar contra a
projeção slim, não contra o cru+scaffolding. Reclama o pico entre checker (3140) e mono
(3407). Gate: `[fixpoint]` + `gardening_generic_instance_survives` +
`gardening_pred_guard_survives`. Ritual: SIM.

### PJ-5 — Poda de campo confirmada (DocSpan já; avaliar `type_constraints`)
Com os drops verdes, formalizar a poda: `DocSpan` já sai em PJ-2; medir e, se codegen
comprovadamente não ler `type_constraints`, podá-las pós-mono também. Só campos com poda
provada NENHUM-leitor-a-jusante. Gate: `[fixpoint]` + `gardening_docspan_pruned`. Ritual: SIM.

### PJ-6 — (opcional, medir antes) reciclagem intra-codegen do buffer de emissão
Se o pico remanescente for o buffer do codegen (Eixo A / C3, não retenção), NÃO é jardinagem
— reportar para o Eixo A. A jardinagem termina onde a retenção-entre-fases termina. Registrar
o hand-off, não construir aqui.

**Pontos de ritual (gate completo obrigatório):** PJ-2 (a cópia entra no caminho de emissão),
PJ-3, PJ-4, PJ-5 — cada um muda a topologia de memória e deve passar o gate nativo completo +
`gen2==gen3` byte-idêntico.

---

## 11. Riscos e tensões de lei

- **R2 (drop-safety / UAF) — o risco dominante.** Um deep-copy incompleto (esp. esquecer o
  backing de string, §5) passa superficial e corrompe. RESOLUÇÃO: projeção TOTAL por padrão,
  intern de string obrigatório declarado no contrato, PJ-2 prova a cópia como identidade
  ANTES de qualquer drop, e a fixture-inversão `gardening_string_backing_copied` guarda o
  regresso. Postura conservadora: dúvida → copia (leak-safe), nunca poda especulativa.
- **R6 (fixpoint) — RESOLVIDO por construção.** `line`/`col` são preservados (emitidos no
  `.c`); `doc` podado é invisível ao codegen; o walk é determinístico. A jardinagem é
  whole-program, logo NÃO depende de RM-C10 e não mexe no gensym. `gen2==gen3` byte-idêntico
  é a guarda a cada crumb.
- **R-A (custo transitório 2×).** A projeção paga 2× a ÁRVORE durante a cópia. RESOLUÇÃO:
  o ganho (reclamar o scaffolding, muito maior que a árvore) domina; PJ-1 mede o split para
  confirmar antes de PJ-3. Se, num alvo, a árvore sozinha for grande demais para o 2×
  transitório caber sob o guard, a fronteira daquele estágio migra para o `.tkb`-em-DISCO
  (mecanismo b / Eixo C RM-C13) — a jardinagem e o Eixo C compartilham o mesmo princípio,
  então o fallback é contínuo, não uma reescrita.
- **R-B (poda de `type_constraints`).** Precisa confirmar que codegen não as lê antes de
  podar pós-mono. RESOLUÇÃO: PJ-5 mede; na dúvida, NÃO poda (fica no slim, leak-safe).
- **Teko-only:** tudo em `.tks` (`gardening.tks` novo + brackets no driver); reusa
  `arena.tks` (pronto). Nenhum `teko_rt.c` novo. Sem tensão.
- **`.tkb` O(n²):** NÃO é dependência da jardinagem (mecanismo a é memória). É dependência do
  Eixo C (b/RM-C13) e deve ser consertado pelo Eixo A no `emit/` de todo modo. Registrado,
  não bloqueia.

**Nenhuma tensão de Lei genuína — nada HALTa.** A única decisão do dono é de PRIORIDADE
(jardinagem antes/independente do Eixo C), registrada na §12.

---

## 12. RECOMENDAÇÃO ao dono

**Sim — dá para "comprimir" a AST entre fases, e a forma certa em Teko é a JARDINAGEM:
quebrar o alias com uma cópia-projeção total e então DERRUBAR a região inteira da fase
anterior.** Mas a intuição precisa de duas correções que o código impõe:

1. **O ganho grande NÃO é podar campos — é derrubar a REGIÃO da fase (o scaffolding do
   checker/parse, a `TypeTable` morta), que domina os 3140 MB.** A poda de campo (só `DocSpan`
   é limpo) é um refinamento pequeno por cima. A trivia de fonte `line`/`col` que o brief
   supôs dropável NÃO é dropável — ela é EMITIDA no `teko.c` (diagnóstico de cast + cobertura);
   podá-la quebra o fixpoint.

2. **O que torna o drop possível — e perigoso — é o alias por referência** (o typed-AST É o
   cru; strings compartilham backing). A cópia-projeção existe para QUEBRAR esse alias antes
   do drop. O risco é R2 (UAF por cópia incompleta de backing de string); mitigado por
   projeção total + PJ-2 provando identidade antes de qualquer drop.

**Mecanismo recomendado:** projeção deep-copy EM MEMÓRIA para as fronteiras internas (mais
leve, determinística, sem o O(n²) do `.tkb`); reservar o `.tkb` serialize→drop→deserialize
para o despejo em DISCO do Eixo C (RM-C13), onde a barreira do LINK não funde — e só depois
de o serializer virar O(n).

**Trade-offs para a decisão:**

| Caminho | Ganho | Custo/risco | Quando |
|---|---|---|---|
| **Jardinagem (este doc), em memória** | pico = máx(uma FASE + slim); reclama scaffolding; barato de construir; independe de RM-C10 | 2× transitório da árvore; R2 (intern de string obrigatório) | **AGORA — adiantar; o maior ganho por menor esforço** |
| **Eixo C por unidade (`.tkb` disco)** | pico = máx(uma UNIDADE); teto mais apertado | grande reestruturação; exige RM-C10 + serializer O(n) | depois, SE o residual pós-jardinagem ainda exceder a folga |
| **DPS de retorno (`ast-computed-arena-assessment`)** | reclama o eixo de RETORNO + correção nativa | mudança de ABI nativa | ortogonal; soma; sua própria trilha |

**Recomendação de prioridade (decisão do dono, sem tensão de Lei):** construir a jardinagem
AGORA (PJ-1..PJ-5), independente e antes do Eixo C, porque (a) entrega o teto "pico = máx de
uma fase" com um único walk de cópia + brackets de região que já existem em `arena.tks`, (b)
não depende de RM-C10 nem do serializer O(n), e (c) provavelmente já leva o build seco para
perto de < 1 GB ao reclamar o scaffolding do checker. Se, medido pós-PJ-4, o residual ainda
exceder a meta, o Eixo C aperta de fase→unidade usando a MESMA fundação C6. A jardinagem é o
"adiantar o que der" do despejo entre estágios.
