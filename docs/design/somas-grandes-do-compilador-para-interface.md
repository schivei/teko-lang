# Somas grandes do compilador → interface + dispatch (convenção de código, NÃO mudança de linguagem)

> DESIGN-AHEAD, arquiteto. Documento de projeto (não implementação). Nenhum `.tks`
> de produto foi tocado por este doc. Branch `design/compiler-sum-to-iface` de
> `fix/retirement` (topo `5605a8d6`). Todas as citações `arquivo:linha` são reais
> na árvore em `5605a8d6`. Disciplina de reseed: `TEKO_CC=clang`, `--no-verify`,
> nunca `teko test .`, `ulimit -v 4718592`.
>
> **Relação com `plano-match-universal-e-migracao-variant.md`: ZERO fork.** Aquele
> plano migra `variant` NOMEADO → hierarquia sealed e amplia `match`. ESTE plano
> NÃO mexe em nenhuma regra de linguagem: o maquinário de soma anônima `A | B`
> permanece intacto, `| null`/`| error` permanecem uniões, e a ruling de match
> sealed continua respeitada. A única mudança é de **convenção do código do
> compilador**: o compilador para de usar somas GRANDES inline na sua própria IR.

---

## 0. A decisão do dono (verbatim, com grau de certeza)

> **Não é mudança de linguagem — é convenção de código do compilador.** O
> maquinário de somas de tipo (`A | B`) FICA na linguagem (o dev usa à vontade; a
> ruling `plano-match-universal-e-migracao-variant.md` continua respeitada — ZERO
> fork). O compilador é que se auto-impõe o estilo determinístico.

As cinco afirmações da decisão, cada uma verificada contra a árvore e anotada com
grau de certeza (ALTO = confirmado por citação; MÉDIO = confirmado mas com nuance;
requer-medição = depende do censo, §9):

1. **[ALTO] Sem somas GRANDES no código do compilador.** As uniões-macro grandes —
   `@Type()` (14 membros, `src/checker/type.tks:59`), `@TExprKind()` (26 membros,
   `src/checker/tast.tks:52`) e as demais 24 macros-união `macro X() { lowering {
   … } }` do compilador — deixam de ser tipo-soma inline e passam a **interface +
   conformância de classe + dispatch/match sobre o conjunto selado de impls**.
   Confirmado: são exatamente macros de baixamento para união anônima (§1).

2. **[ALTO] Motivo-raiz de memória: a soma inline é embutida em cada nó.** Uma
   união grande baixa para um **struct C de tag-inline dimensionado ao maior
   membro** (`cg_union_tag_ctype`, `src/codegen/codegen.tks:1508`; o ramo
   não-niche/não-box). `TExpr` (`tast.tks:5`) embute DOIS desses inline: `kind:
   @TExprKind()` e `type: @Type()`. Trocar por **valor de interface = fat-pointer
   de 16 B** (`{ void *data; fnptr *vtable; }`, typedef de interface documentado em
   `interface-value-type.md` §3.1) + objeto concreto **right-sized** alocado em
   arena → a memória cai. A magnitude exata (−0,8 a −1,5 GB estimado) **depende do
   censo** (§9): `N_TExpr`, `N_Type_distintos`, e o grau de compartilhamento.

3. **[ALTO] Exaustividade preservada pela selagem-por-padrão — MAS falta UMA peça
   de maquinário.** No Teko um `class` sem `abstract`/`virtual` é **selado**
   (`lexer.tks:263` os modificadores; `parse_decl.tks:281` o parse deles;
   `parse_decl.tks:721` "a service is always sealed"). Logo os implementadores de
   uma interface do compilador formam um **conjunto FECHADO e whole-program**. O
   checker JÁ enumera o conjunto fechado de subclasses para exaustividade
   (`subclasses_of`, `match.tks:457`; `class_covered`, `match.tks:478`), e JÁ casa
   `match` sobre impls concretas de interface via `type_conforms_to`
   (`match.tks:62`) com discriminação por ponteiro de vtable no baixamento
   (`codegen.tks:5340`, `).vtable == tk_vt_…`). **A peça que FALTA:**
   `exhaustive_type_subject` (`match.tks:511`) hoje devolve `false` para um subject
   de interface a não ser que um arm ligue a interface inteira (`match.tks:518-520`)
   — i.e. HOJE um `match` exaustivo sobre interface EXIGE catch-all. Para preservar
   a exaustividade "de graça" é preciso **enumerar os implementadores** (o gêmeo
   `implementors_of` de `subclasses_of`) e cobrir o conjunto fechado. É a única
   adição real de lógica de checagem desta reforma (§4, CS2I-EXH).

4. **[ALTO] `| null` e `| error` FICAM uniões.** São binárias, baratas e
   determinísticas. `| null` é niche zero-overhead (`cg_union_niche_member`,
   `codegen.tks:1454`) — e um valor de interface é niche-ável pelo mesmo caminho
   (fat-pointer com `.data == NULL` == `null`; `interface-value-type.md` §2.3).
   `| error` é união de 2 membros (`cg_union_box_member`, `codegen.tks:1567`). A
   verificação de nulo NÃO muda. Convertê-los custaria milhares de reescritas por
   ZERO memória — proibido.

5. **[ALTO] `error` continua `T | error`** — não vira múltiplo retorno; múltiplo
   retorno é ferramenta ortogonal (já existe, desugar-para-struct). O único item de
   maquinário de baixa urgência é que um método `abstract`/de-interface ainda não
   aceita retorno `(A, B)` (o "yet" em `parse_decl.tks:358`). Ensina agora, usa
   depois (§8, CS2I-ABI-MRET).

---

## 1. Inventário das macros-união do compilador

Comando: `grep -rnE 'macro [A-Za-z0-9_]+\(\)\s*\{\s*lowering' src`.

Uma macro-união é literalmente `macro Name() { lowering { A | B | … } }`: invocá-la
como `@Name()` numa anotação expande para a união anônima `A | B | …`. É açúcar
sobre a soma anônima da linguagem — o maquinário que a decisão do dono mantém.

### 1.1 Núcleo do compilador (ALVO da reforma)

| macro | arquivo:linha | #membros | onde embute | prioridade (memória) |
|---|---|---|---|---|
| `Type` | `src/checker/type.tks:59` | 14 | `TExpr.type`, e ~862 sítios de anotação `@Type()` em 27 arquivos | **P0 (marquee)** |
| `TExprKind` | `src/checker/tast.tks:52` | 26 | `TExpr.kind` (o nó tipado de TODA expressão) | **P0 (marquee)** |
| `ExprKind` | `src/parser/ast.tks:89` | 27 | `Expr.kind` (nó de expressão do parser) | P1 |
| `TStatement` | `src/checker/tast.tks:69` | 11 | corpos tipados (`[]@TStatement()`) | P1 |
| `Statement` | `src/parser/ast.tks:154` | 13 | corpos do parser | P1 |
| `TItem` | `src/checker/tast.tks:97` | 5 (inclui `@TStatement()` aninhado + compartilhados) | `TProgram.items` | P2 (compartilhado) |
| `ItemKind` | `src/parser/ast.tks:258` | 7 (inclui `@Statement()` aninhado + compartilhados) | itens do parser | P2 (compartilhado) |
| `Decl` | `src/parser/ast.tks:254` | 5 (membros compartilhados) | declarações | P2 (compartilhado) |
| `TypeBody` | `src/parser/ast.tks:228` | 11 | `TypeDecl.body` (serializado, §7) | P2 |
| `TypeExpr` | `src/parser/type.tks:12` | 6 | anotações de tipo do parser | P2 |
| `Pattern` | `src/parser/pattern.tks:21` | 9 | arms de match | P2 |
| `ConstraintExpr` | `src/parser/ast.tks:163` | 7 | constraints genéricos | P3 |
| `PredKind` | `src/parser/ast.tks:263` | 6 | guardas `#os`/pred | P3 |
| `LOp` | `src/lir/lir.tks:62` | 16 | operações LIR | P2 (backend) |
| `MInst` | `src/backend/minst.tks:228` | 32 | instrução ARM64 | P2 (backend) |
| `MInstX86` | `src/backend/minst_x86.tks:174` | 27 | instrução x86-64 | P2 (backend) |
| `RegAssignment` | `src/backend/regalloc.tks:523` | 2 | atribuição de reg | P3 (→ avaliar `enum`) |
| `TFSpecKind` | `src/checker/tast.tks:36` | 3 | `TFSpec.kind` | **PILOTO** |
| `ResidenceTier` | `src/checker/residence.tks:18` | 5 | tier de residência | P3 (enum-like) |
| `PointsTo` | `src/checker/spine.tks:18` | 5 | análise de spine | P3 (enum-like) |
| `BorrowedFrom` | `src/checker/spine.tks:28` | 4 | análise de spine | P3 (enum-like) |
| `Unique` | `src/checker/spine.tks:33` | 3 | análise de spine | P3 (enum-like) |
| `FSpecKind` | `src/parser/ast.tks` (fmt-spec) | 3 | fmt-spec | P3 (enum-like) |
| `ConstValueKind` | `src/checker/comptime_fold.tks` | 5 | comptime | P3 |

### 1.2 Fora de escopo (stdlib — REPORTADO, não acionado)

`JsonValue` (`src/encoding/json/json.tks:9`), `JoseKey` (`src/crypto/jose/jwk.tks:72`),
`RegexNode` (`src/regex/regex.tks:15`). São **superfície de stdlib**, não nós de IR
quentes do compilador; converter rende memória desprezível e muda tipo consumido por
usuários da lib. A decisão do dono é sobre "o código do compilador". **Deixá-los como
estão.** (Adjacência reportada, não vira issue nova.)

---

## 2. O mecanismo, exato — o que muda de FORMA

### 2.1 A forma nova, por união

Para cada macro-união-alvo `@U()` com membros `M1 | … | Mk`:

1. **Declarar a interface.** `pub type U = interface { }` (inicialmente SEM métodos
   — ver §5). O nome `U` substitui `@U()` em toda anotação.
2. **Cada membro conforma como classe SELADA.** `type Mi = class & U { <campos> }`
   (era `pub type Mi = struct { <campos> }`). `class` sem `abstract`/`virtual` =
   selado por construção → conjunto de impls FECHADO. Uma classe pode conformar a
   **VÁRIAS** interfaces ao mesmo tempo — isto resolve os membros compartilhados
   (§6) sem a dor da herança-única do plano-variant.
3. **Trocar os campos.** Todo `campo: @U()` → `campo: U`. Todo `[]@U()` → `[]U`. Todo
   `@U() | null` → `U | null` (niche). Todo `@U() | error` → `U | error` (união de 2).
4. **`match` continua igual.** `match x { Mi as m => … }` sobre `x: U` casa a impl
   concreta por ponteiro de vtable (`codegen.tks:5340`) — o corpo do arm NÃO muda.
5. **As free-fns de dispatch duplo NÃO mudam de corpo** (§3).

O valor de `U` passa a ser um **fat-pointer de 16 B** cujo `.data` aponta o objeto
concreto arena-alocado (right-sized) e cujo `.vtable` identifica o membro. O campo
inline de 136 B / 290 B some.

### 2.2 A consequência valor→referência (o RISCO de 1ª ordem — §10 R1)

Hoje os membros são `struct` = **tipos de VALOR** (a união inline é copiada por
valor). Como `class` são **tipos de REFERÊNCIA** (ponteiro, arena-alocado). É a
MESMA virada valor→referência que o plano-variant cataloga (`plano-match-
universal…` §4.2). Por que aqui é **sã e NÃO precisa da spine**:

- O caminho **classe→interface JÁ é o meio que funciona hoje**, C-backend, ponta a
  ponta: resolve como valor-tipo, faz upcast/box no fat-pointer, dispatcha por
  vtable, slices heterogêneas e upcast covariante provados por `class_slices`
  (`interface-value-type.md` §0/§4.1). O que a spine bloqueia é **struct**→
  interface (struct não tem endereço estável) — que esta reforma **NÃO usa**: os
  membros viram classes, não structs boxados.
- A soundness repousa num **INVARIANTE que o audit (CS2I-0) precisa provar:** os
  ADTs de IR do compilador (`Type`/`TExpr`/`TExprKind`/…) são **imutáveis após
  construção** — o compilador CONSTRÓI nós frescos e nunca muta um campo in-place.
  Sob esse invariante, referência-compartilhada é observacionalmente idêntica a
  cópia-por-valor (não dá para distinguir dado imutável compartilhado de dado
  copiado). O objeto vive na arena da fase (checagem/baixamento) → tempo de vida ≥
  todas as referências → sem UAF (o argumento §4.1 de `interface-value-type.md`).
- **Se o audit achar mutação in-place**, o sítio se converte para **construir
  fresco** (o mesmo idioma NO-PUSHES / purge-na-reatribuição que a CLAUDE.md já
  manda). Não é HALT: a reforma foi DECIDIDA; o audit é mecânico.

### 2.3 Interning (otimização de memória de 2ª ordem — informada pelo censo)

Membros SEM payload (`Void`, `Byte`, `Char`, `Str`, `Error`, `Null`, `Uptr`) podem
ser **singletons construídos-uma-vez** (um objeto imortal por membro, `.data`
compartilhado). Para `Type`, se o censo mostrar alto compartilhamento
(`N_Type_distintos << N_TExpr`), interning dos payload-free e talvez dos `Prim`
comuns multiplica o ganho. É follow-on (CS2I-INTERN, §5), gated no censo — não
bloqueia a virada de forma.

---

## 3. O resíduo de DISPATCH DUPLO (o que o dono pediu para desenhar)

Classifiquei todo `match` sobre `@Type()`/`@TExprKind()`/… em **single-dispatch**
(discrimina UM valor) vs **double-dispatch** (compara DOIS valores do mesmo ADT).

### 3.1 Single-dispatch → método limpo (opcional, o "dispatch de método")

Ex.: `type_is_void(t)` (`type.tks:61`), `type_contains_ref(t)` (`type.tks:110`),
`type_render`, e o eixo de UM tipo de `type_mangle`. Estes discriminam um único valor
→ viram método de interface: `pub abstract fn is_void(self): bool` na interface `Type`,
cada impl com o corpo do seu arm (`Void { … } fn is_void(self): bool { true }`, os
demais `false`). O `match` free-fn é **substituído por `t.is_void()`**. (`prim_is_*`/
`prim_width` são sobre `PrimKind`, que é `enum`, não união — N/A.)

> **Isto é STYLE, não é o que rende a memória.** O ganho de memória vem inteiro da
> troca de FORMA do campo (§2). A migração match→método é a convenção "dispatch de
> método" que o dono quer, e é **sequenciada DEPOIS** da virada de forma, por
> operação, cada passo verde (§5, CS2I-METHODS). Pode ser encenada/deferida mas
> fica **no mesmo plano**.

### 3.2 Double-dispatch → **free-fn com match residual sobre o conjunto selado** (recomendado)

Os comparadores/transformadores de DOIS operandos:

- `type_eq(a, b)` (`type.tks:72`) — casa `a`, e ANINHADO casa `b`.
- `types_eq(xs, ys)` (`type.tks:99`) — via `type_eq`.
- `ptr_inner_eq` (`type.tks:65`), `union_member_is_first` (`resolve.tks:1169`).
- `unify(pattern, arg, s, table)` (`resolve.tks:1124`) — casa `pattern`, aninhado
  casa `arg`.
- `subst_type(t, s)` (`resolve.tks:1087`), `collect_sig_type_params`
  (`resolve.tks:1146`), `type_mangle` (`resolve.tks:1381`) — transformadores que
  RECONSTROEM (produzem `Type` novos), inerentemente multi-arg/construtivos.

**A descoberta central: o corpo destes NÃO muda.** O `match a { Prim as pa => match
b { Prim as pb => … } }` de hoje é um dispatch duplo por **match aninhado**. Depois
da virada, `a` e `b` são valores de interface, e o MESMO match aninhado casa sobre o
conjunto **selado** de impls (por vtable). Exaustividade preservada pela selagem
(§4). Portanto:

> **RECOMENDAÇÃO (law-first, custo mínimo): manter os dispatch-duplo como free-fns
> com match residual aninhado sobre o conjunto selado de impls.** NÃO virar visitor
> duplo (N² métodos, sem ganho) nem forçar método single-receiver num operador
> simétrico. O único requisito é que a exaustividade do match residual funcione
> sobre interface selada — que é a peça CS2I-EXH (§4). É exatamente o "type-switch
> residual" que a decisão do dono (afirmação 3) autoriza.

Opção intermediária, se o dono quiser `type_eq` como método por consistência de
estilo: `pub abstract fn eq(self, other: Type): bool`, cada impl fazendo `match other
{ MesmoTipo as o => <campos iguais>; _ => false }` (single-dispatch no 1º operando +
match residual no 2º). É viável e barato, mas NÃO recomendo como obrigatório —
`type_eq` free-fn já é ótimo. Fica como escolha do dono no CS2I-METHODS.

---

## 4. A ÚNICA peça de maquinário nova: exaustividade sobre interface selada

Hoje `exhaustive_type_subject` (`match.tks:511`) devolve `false` para subject de
interface salvo bind-all da interface inteira (`match.tks:518-520`) — o conjunto de
impls é tratado como **ABERTO** → um `match` que lista todos os membros sem `_` é
marcado NÃO-exaustivo. Depois da virada, `type_eq` (que lista os 14 casos de `Type`
sem `_`) quebraria. Fechar isto é obrigatório e **precede toda conversão**.

Gêmeo exato de `subclasses_of`/`class_covered` (que já existem para base
polimórfica), no eixo de interface:

```teko
/**
 * implementors_of — o conjunto FECHADO (whole-program) de classes concretas que
 * conformam à interface `iface`. Gêmeo de `subclasses_of` no eixo de interface:
 * varre a type-table por toda classe não-abstrata cuja conformância alcança
 * `iface` (via `type_conforms_to`). É o que fecha a exaustividade de `match` sobre
 * um valor de interface do compilador (impls seladas por construção).
 *
 * @param iface  o nome canônico da interface
 * @param table  a type-table do programa (mundo fechado da compilação)
 * @return       os nomes das classes conformantes concretas, em ordem de declaração
 * @see          teko::checker::subclasses_of — o gêmeo sub->base
 * @since 0.3.1
 */
fn implementors_of(iface: str, table: TypeTable): []str

/**
 * iface_covered — todo implementador do conjunto FECHADO de `iface` é nomeado por
 * algum arm SEM guarda? Gêmeo de `class_covered` para o eixo de interface; um arm
 * com `when` NÃO cobre seu caso (a regra já vigente `!arms[i].has_when`).
 *
 * @param arms    os arms do match
 * @param impls   os implementadores concretos (de `implementors_of`)
 * @param table   a type-table
 * @param ref_ns  o namespace do sítio (resolução dos nomes de arm)
 * @return        true sse todo `impls[i]` é coberto por um arm sem guarda
 * @since 0.3.1
 */
fn iface_covered(arms: []parser::Arm, impls: []str, table: TypeTable, ref_ns: str): bool
```

`exhaustive_type_subject` ganha, no ramo `is_interface_name` (`match.tks:518`), a
cobertura por enumeração (espelhando as linhas 514-516 do ramo polimórfico):

```
if is_interface_name(n.name, table) {
    if some_arm_names(arms, name_last_segment(n.name), table, ref_ns) { return true }
    return iface_covered(arms, implementors_of(n.name, table), table, ref_ns)
}
```

**Fecho do conjunto (a regra):** o conjunto de implementadores = as classes
conformantes VISÍVEIS no programa compilado (mundo fechado). Para o compilador
compilando a si mesmo isto é bem-definido e são. **Nota de design (§10 R4):** como o
maquinário fica na linguagem, o dev também passa a ter exaustividade-sobre-interface-
selada. É a extensão natural do `subclasses_of` já existente e casa com a selagem-
por-padrão da afirmação 3 — recomendo adotá-la. NÃO é fork (deliberado pela ruling de
selagem); registro como nota, não como HALT.

**Aditivo/inerte:** CS2I-EXH só ADICIONA um caminho de ACEITAÇÃO (um match antes
rejeitado por falta de `_` passa a compilar quando cobre os impls). Não muda o C
emitido de nenhum programa hoje válido → byte-preservante → gate `[fixpoint]`.

---

## 5. Sequência de crumbs

Ordem: (1) medir + auditar; (2) fechar a exaustividade; (3) piloto minúsculo prova o
padrão; (4) marquee `Type`, depois `TExprKind`, depois os demais por impacto; (5)
método-dispatch (estilo) e o item de maquinário `(A,B)`, deferíveis mas in-plan.

Cada conversão de forma MUDA a representação → **NÃO é byte-preserving** → o C
emitido muda (fat-pointer em vez de union inline) → reseed-class `fixpoint-rebuild`
(o core consome o swap e se auto-reproduz; `gen2==gen3` byte-idêntico é a guarda).
Encena-se **uma união por crumb** — verde a cada passo.

Fixtures: o **fixpoint self-build é a prova** de toda conversão (o compilador é o
maior consumidor de `Type`/`TExpr`/… — se a forma nova miscompila, o self-build
quebra). `Fixtures: none` salvo os poucos paths de REJEIÇÃO que o self-build nunca
dirige (exaustividade quebrada, `(A,B)` em abstract). Isto obedece a lei "não se
escreve teste para o que o compilador exercita ao se compilar".

Crumb files: `.crumbs/0200`–`0212` (`CS2I-*`). NÃO indexados em `000-INDEX.md`
(manifest da 0.3.1) — a inserção no manifest/ordem-de-execução é decisão do
coordenador ao adotar a onda.

| seq | crumb-id | alvo | gate | reseed-class |
|---|---|---|---|---|
| 0200 | CS2I-0 | audit imutabilidade + censo | — | none |
| 0201 | CS2I-EXH | exaustividade sobre interface selada | `[fixpoint]` | none `(folds)` |
| 0202 | CS2I-PILOT | `@TFSpecKind()` → interface | `[RITUAL]` | fixpoint-rebuild |
| 0203 | CS2I-TYPE | `@Type()` → `interface Type` (marquee) | `[RITUAL]` | fixpoint-rebuild |
| 0204 | CS2I-TEXPRKIND | `@TExprKind()` → interface | `[RITUAL]` | fixpoint-rebuild |
| 0205 | CS2I-EXPRKIND | `@ExprKind()` → interface | `[RITUAL]` | fixpoint-rebuild |
| 0206 | CS2I-STMTS | `@Statement()` + `@TStatement()` | `[RITUAL]` | fixpoint-rebuild |
| 0207 | CS2I-BACKEND-IR | `@LOp()`·`@MInst()`·`@MInstX86()`·`@RegAssignment()` | `[RITUAL]` | fixpoint-rebuild |
| 0208 | CS2I-SHARED | `@TItem()`·`@ItemKind()`·`@Decl()`·`@TypeBody()`·`@TypeExpr()`·`@Pattern()`·`@ConstraintExpr()`·`@PredKind()` | `[RITUAL]` | fixpoint-rebuild |
| 0209 | CS2I-ENUMLIKE | triagem enum-vs-interface | `[fixpoint]`/`[RITUAL]` | fixpoint-rebuild |
| 0210 | CS2I-METHODS | single-dispatch → método (estilo) | `[RITUAL]` | fixpoint-rebuild |
| 0211 | CS2I-ABI-MRET | abstract/interface aceita `(A,B)` | `[fixpoint]` | none/fixpoint-rebuild |
| 0212 | CS2I-INTERN | singletons dos payload-free (censo-gated) | `[RITUAL]` | fixpoint-rebuild |

O detalhe self-contained de cada crumb está no arquivo `.crumbs/` correspondente
(Where/How/Fixtures/Gate/Deps/Done-when). Resumo dos pontos-chave a seguir.

### CS2I-0 · AUDIT + CENSO (dep serial; NÃO-bloqueante para o design)
Varredura read-only. (a) **Audit de imutabilidade:** achar todo `x.<campo> = …` onde
`x` é um `@U()`/impl dos ADTs de IR; listar (espera-se ~0 pelo estilo SSA-like); cada
achado → construir-fresco no crumb da sua união. (b) **Censo:** `N_TExpr`,
`N_Type_distintos`, sizeof atual vs 16 B fat-pointer, pico do build seco baseline.
Quantifica o −ΔGB e o teto de interning. Roda em série; o design não espera.

### CS2I-EXH · exaustividade sobre interface selada (aditivo/inerte)
§4. `implementors_of`/`iface_covered` gêmeos de `subclasses_of:457`/`class_covered:478`
em `src/checker/match.tks`; estende `exhaustive_type_subject:511` no ramo
`is_interface_name:518`. Oráculo de REJEIÇÃO isolado `iface_match_nonexhaustive`
(`EXPECT_COMPILE_FAIL`) — o único fixture (self-build só dirige código válido).
Precede toda conversão.

### CS2I-PILOT · `@TFSpecKind()` → interface (prova do padrão)
`src/checker/tast.tks:33-37`: 3 membros → `class & TFSpecKind`; macro → interface;
`TFSpec.kind` → `TFSpecKind`; matches sobre `.kind`. Menor alvo que exercita a máquina
inteira (campo fat-pointer + match residual + reseed). Fixtures: none. `[RITUAL]`,
medir Δpico (não-crescente, ratchet D68).

### CS2I-TYPE · `@Type()` → `interface Type` (MARQUEE, maior ganho)
`src/checker/type.tks:41-59`: 14 membros → `class & Type`; macro → interface; ~862
sítios `@Type()` → `Type`; `Ptr.inner: @Type() | null` → `Type | null`; o membro
`Variant { members: []@Type() }` (PORTADOR de soma anônima) → `class & Type { members:
[]Type }` **permanece**; dispatch-duplo `type_eq:72`/`unify:1124`/`subst_type:1087`/
`type_mangle:1381` mantêm corpo (§3.2). Atômico por decl, mecânico, bem-definido. SEM
bump de wire (§7). Fixtures: none. `[RITUAL]`, Δpico (o maior corte).

### CS2I-TEXPRKIND · `@TExprKind()` → `interface TExprKind`
`src/checker/tast.tks:12-52`: 26 membros → `class & TExprKind`; `TExpr.kind` →
`TExprKind`. Depende de CS2I-TYPE. `[RITUAL]`, Δpico (o outro grande corte, ~290 B).

### CS2I-EXPRKIND · `@ExprKind()` → `interface ExprKind`
`src/parser/ast.tks:1-89`: 27 membros; `Expr.kind` → `ExprKind`. `[RITUAL]`.

### CS2I-STMTS · `@Statement()` + `@TStatement()` → interfaces
`ast.tks:154`; `tast.tks:69`. Uniões que ANINHAM em `@TItem()`/`@ItemKind()` →
resolver por interface-extends no CS2I-SHARED. `[RITUAL]`.

### CS2I-BACKEND-IR · `@LOp()`·`@MInst()`·`@MInstX86()`·`@RegAssignment()`
`lir.tks:62`; `minst.tks:228`; `minst_x86.tks:174`; `regalloc.tks:523`.
`RegAssignment` (2 membros) → avaliar `enum` puro (CS2I-ENUMLIKE). O backend/`lower.tks`
já COMPILA no self-build (escreve agora, roda depois — CLAUDE.md); a virada compila e
reseeda já. `[RITUAL]` agrupado por arquivo.

### CS2I-SHARED · uniões com membro compartilhado / aninhado
`ast.tks:254/258/228/163/263`; `type.tks:12`; `pattern.tks:21`; `tast.tks:97`. **A
vantagem sobre o plano-variant:** conformância de interface é muitos-para-um →
`TypeDecl` conforma a `Decl` E `ItemKind` E `TItem` (§6); uniões aninhadas → a externa
ESTENDE a interna (`interface ItemKind : Statement`). Sem colisão, sem inline forçado.
`TypeBody` é o subcaso limpo que toca serialização (§7). `[RITUAL]` por grupo.

### CS2I-ENUMLIKE · triagem enum-vs-interface (baixa prioridade)
`residence.tks:18`; `spine.tks:18/28/33`; FSpecKind; `regalloc.tks:523`;
ConstValueKind. Por-tipo: arms sem payload distinto → `enum` puro (mais barato); com
payload → interface. Decisão registrada por-tipo no crumb.

### CS2I-METHODS · single-dispatch → método (a convenção "dispatch de método")
Por operação single-dispatch: `abstract fn` na interface + impl em cada membro (corpo
= o arm de hoje); substitui a free-fn `match` por `x.metodo()`. Double-dispatch
permanece free-fn (§3.2). Uma operação por passo, verde. DEFERÍVEL, in-plan.

### CS2I-ABI-MRET · abstract/interface aceita retorno `(A,B)` (ensina agora, usa depois)
`parse_decl.tks:358` (remover o honest-stop "yet") + o desugar-para-struct de retorno-
múltiplo (`collect.tks`). Aditivo/teaching. Fixture `iface_method_multi_return`
(isolado). `[fixpoint]`.

### CS2I-INTERN · singletons dos payload-free (follow-on, censo-gated)
Membros payload-free de `Type` → objeto imortal por membro; construção devolve `.data`
compartilhado. Só se o censo mostrar volume. `[RITUAL]`, Δpico.

---

## 6. Membros compartilhados e uniões aninhadas — resolvidos por conformância múltipla

Prova da colisão (idêntica ao plano-variant): `TypeDecl` é membro de `Decl`
(`ast.tks:254`), `ItemKind` (`ast.tks:258`) E `TItem` (`tast.tks:97`);
`@TStatement()` está aninhado em `@TItem()`, `@Statement()` em `@ItemKind()`.

No plano-variant (herança-única de classe) isto forçava estratégia INLINE. **Nesta
reforma a colisão SOME:** conformância de interface é **muitos-para-um**. Portanto:

- **Compartilhado** → conforma a todas: `type TypeDecl = class & Decl & ItemKind &
  TItem { … }`.
- **Aninhado** → interface-estende-interface: `pub type ItemKind = interface :
  Statement { }` (o `extends` de InterfaceBody já existe, `parse_decl.tks:824`), de
  modo que todo `Statement`-impl é também `ItemKind`-impl. `match item { <stmt> as s
  => …; Function as f => … }` casa ambos os eixos.

Verificar (o implementador confere no crumb): (a) uma classe conformando a ≥2
interfaces gera ≥2 vtables `tk_vt_<Class>_<IfaceI>` (o box escolhe a certa por
tipo-esperado — `emit_as` já resolve); (b) match sobre uma interface que estende outra
enumera a união dos implementadores (o `implementors_of` transitivo pelo
`type_conforms_to`, que já é transitivo em `extends`).

---

## 7. Serialização (`.tkb`/`.tkh`) — SEM bump de wire

`@Type()` e `@TypeBody()` são serializados (`write_typebody`,
`src/emit/tkb_write.tks:446`; leitor gêmeo `tkb_read.tks`). **Crucial:** o serializer
escreve uma **forma de wire estável** (byte-de-tag + campos) por `match` sobre os
membros — **independente da representação em memória**. Trocar união-inline por
fat-pointer NÃO muda os bytes que `write_typebody` emite (continua casando os mesmos
membros e escrevendo os mesmos tags). Portanto, **diferente do plano-variant** (que
MUDA o CONJUNTO de membros de `Type` e por isso bumpa wire), esta reforma **preserva o
wire** e NÃO bumpa `TKB_EXPR/PROGRAM_VERSION` nem `TKH_VERSION`. O implementador
confirma no CS2I-TYPE/SHARED que os matches do serializer cobrem os impls (a
exaustividade CS2I-EXH garante).

---

## 8. Pontos de ritual (gate cheio + reseed)

- **R-EXH** — após CS2I-EXH: `[fixpoint]`, oráculo `iface_match_nonexhaustive`.
- **R-PILOT** — após CS2I-PILOT: reseed, `gen2==gen3`, 1ª medição de Δpico.
- **R-TYPE** / **R-TEXPRKIND** — após cada marquee: reseed + `gen2==gen3` + Δpico
  (as duas maiores quedas; ratchet D68 = queda ESTRITA obrigatória).
- **R-* por conversão** — CADA união é ritual próprio (reseed + fixpoint + Δpico
  não-crescente).
- **R-METHODS** — por grupo de método migrado.
- **R-ABI-MRET** — `[fixpoint]` + `iface_method_multi_return`.

---

## 9. Dependência do censo (quantifica a memória — roda em série)

CS2I-0 mede, no build seco instrumentado:
- `N_TExpr` — quantos nós tipados o self-build constrói.
- `N_Type_distintos` — tipos distintos (grau de compartilhamento → teto de interning).
- `sizeof(TExpr)` hoje (≈160 B com `kind`+`type` inline) vs pós-reforma (≈40 B:
  16+16+line+col) — o Δ por-nó.
- pico `teko: memory: peak <N> MB` do build seco ANTES da reforma (baseline maçã).

O doc REFERENCIA esses números; a reforma NÃO bloqueia neles (a virada de forma é
correta independentemente da magnitude). O censo confirma o −0,8…−1,5 GB estimado e
prioriza interning. **Roda em série, depois; este design não espera.**

---

## 10. Riscos + tensões de lei (com resolução recomendada)

1. **[MAIOR] valor→referência dos ADTs de IR (§2.2).** Membros viram classes
   (referência arena). *Resolução:* usa o caminho classe→interface **que já
   funciona** (sem spine, diferente do struct→interface); soundness sob o invariante
   "ADTs imutáveis pós-construção", **provado pelo audit CS2I-0**; mutação in-place
   achada → converte para construir-fresco (idioma já mandado). NÃO é HALT — a
   reforma foi decidida; o audit é mecânico. É o mesmo risco que o plano-variant §4.2,
   mas AQUI resolvido porque não depende da spine.

2. **[MÉDIA] custo de dispatch/alocação.** Cada `Prim{}` vira alocação de arena +
   dispatch indireto no match (vtable cmp). *Resolução:* arena é bump-pointer (overhead
   ~0); o cmp de vtable substitui o cmp de tag (mesma ordem). O ratchet (D68) é a
   guarda dura: se ALGUMA conversão CRESCER o pico, reverte-se antes de landar. O
   ganho de campo (136→16 B) domina; o censo confirma.

3. **[MÉDIA] amplitude mecânica (~862 sítios `@Type()`).** *Resolução:* atômico por
   decl, mecânico; o self-build É a prova (miscompile quebra o fixpoint). Sem fixtures
   afirmativos (lei).

4. **[NOTA, não-HALT] exaustividade-sobre-interface vira visível ao dev (§4).** O
   maquinário fica na linguagem → o dev ganha a exaustividade enumerada. *Resolução:*
   é a extensão natural de `subclasses_of`, endossada pela selagem-por-padrão
   (afirmação 3). Deliberado pela ruling de selagem — registro como nota, recomendo
   adotar; não é fork.

5. **[BAIXA] `Variant` (portador de soma anônima) dentro de `Type` (§5 CS2I-TYPE).**
   Migrar `Type`→interface sem perder o portador interno de `A | B`. *Resolução:*
   `Variant` vira só mais um `class & Type { members: []Type }` — o maquinário de
   soma anônima fica intacto (a decisão o exige). Zero tensão.

6. **[BAIXA] interfaces com ZERO métodos.** A forma mínima (§5) declara a interface
   sem métodos (discriminação pura por vtable). *Resolução:* funciona — o PONTEIRO de
   vtable distingue os concretos mesmo com vtable vazia (`tk_vt_Prim_Type !=
   tk_vt_Byte_Type`); os métodos entram depois em CS2I-METHODS. Se o checker exigir
   ≥1 método na decl de interface, o crumb usa um marcador trivial ou o caminho de
   base-polimórfica; o implementador confirma no CS2I-EXH/PILOT.

**Sem tensão residual que HALTe.** A reforma é convenção (maquinário fica), o caminho
técnico usa maquinário existente + UMA adição pequena (CS2I-EXH), e o único risco de
1ª ordem (valor→referência) resolve law-first pelo audit — não pela spine. **Único
gatilho de HALT possível, condicional:** se o audit CS2I-0 revelar mutação in-place
PERVASIVA e estrutural dos ADTs (improvável dado o estilo SSA-like do checker), ISSO
seria a tensão real a subir ao dono. Registrado; não presumido.

---

## 11. O que permanece BLOQUEADO / deferido

- **CS2I-0 censo** (a magnitude do ganho) — roda em série; o design não espera.
- **Backend native RODANDO** — a virada de forma COMPILA e reseeda pela rota C já; o
  `lower.tks` reescreve junto mas só EXECUTA no marco native (CLAUDE.md). Não bloqueia.
- **CS2I-METHODS / CS2I-INTERN** — deferíveis (estilo/2ª-ordem), in-plan.

Nada mais bloqueia: a exaustividade (a única lógica nova) é desenhada e pequena (§4);
o caminho classe→interface é o que já funciona; a serialização não bumpa (§7).

---

*Grounding: citações contra `fix/retirement` @ `5605a8d6`. Designs companheiros:
`interface-value-type.md` (fat-pointer/vtable/box classe→interface, já shipado),
`plano-match-universal-e-migracao-variant.md` (match sealed + a virada valor→ref, ZERO
fork com este), `null-as-union-type.md` (niche `| null`).*
