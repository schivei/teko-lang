# Handoff — sessão local do `ngen/` (port teko → mc)

Documento de entrada para uma **sessão local** assumir o trabalho do `ngen/`.
Escrito pela sessão remota coordenadora; leia inteiro antes do primeiro commit.

## 1. O que é o `ngen/`

O **port do teko para o `mc`** (minicompiler.dev, `schivei/mc`), morando dentro
deste repositório. O teko passa a ser uma **linguagem ensinada ao `mc`** por
módulos `.mc` (hooks), em vez de um compilador próprio.

**`src/` está CONGELADO e NÃO se toca.** Todo trabalho novo vive em `ngen/`.

Contexto completo: `docs/design/port-teko-mc.md` e as entradas **D211, D212,
D213, D214** do `DECISION_LOG.md`. Leia-as — são leis, não sugestões.

## 2. Leis que valem aqui (resumo do que mais pega)

- **Comunicação com o dono é sempre em PT-BR.** Nunca use menu de opções/quiz;
  pergunta é em prosa curta.
- **D213 — reuse a base do mc, ensine só o DELTA.** O core do mc já dá a
  gramática Pratt, `fn`, `if`, `return`, expressões e os tipos nativos
  (`u8/u16/u32/u64/i64/uptr/void`). **Não reimplemente nada disso.** Onde a
  forma do mc já resolve, **adote a do mc**: fidelidade sintática ao
  teko-clássico **não é requisito**, funcionalidade é.
- **D214 — ordem das entregas:** (1) primitivas → (2) tipos (`class`,
  `struct`, `interface`, `trait`) → (3) crescer superfície e comportamento base.
- **D197 — não regrida memória ao surfacear:** o que era view/reinterpret
  (zero-cópia) continua view. Surfacear um bypass-de-memória como fn-que-copia
  é regressão.
- **Forward-only, sem PR.** Dreno para `fix/retirement` por cherry-pick.
- **Base-lock antes de trabalhar:** parta de `origin/fix/retirement`, confirme
  que o HEAD é de 2026-09+ e que `src/parser/ast.tks:92` diz
  `BindKind = enum { Var; Const }` (sem `Let`/`Mut`). Base velha já causou
  retrabalho caro.
- **Nada de tocar** `src/`, `bootstrap/`, `cases/`, `examples/`, `tklib/`,
  `tooling/`, `main.tks` da raiz.

## 3. Estado atual

- **Branches:** `fix/retirement` (base canônica) e
  `claude/conversation-recovery-memory-cu1x6d` — mantidas **tree-idênticas**.
  Trabalhe a partir de **`fix/retirement`**.
- **Entrega 1 (fatia vertical) — LANDADA.** `ngen/` com `mc.toml`, `teko.mc`
  (registro `user_init`), `teko_{type,class,stmt,expr}.mc`, `lib/rt.mc`,
  `tests/hello.tk`, e o CI `.github/workflows/ngen.yml`.
  Ensinado: **`bool`** (`type_alias` → `TY_U8`) + honest-stops para
  `class`/`type`/`interface`/`namespace`/`import`/`using`, `var`/`const`/
  `match`/`when`, `new`.
- **Entrega 2 (primitivas) — LANDADA.** Ensinado por `type_alias` (identidade
  pura): **`char`** (`u32`), **`byte`** (`u8`), **`isize`/`usize`**,
  **`ptr`** (colapsa em `uptr`, o único ponteiro opaco do core), **`str`**
  (**NUL-terminated `uptr`** — D215 registra a forma). **`f32`/`f64`**: a lib
  `<float>` do mc (M24) **já registra essas mesmas palavras** — foi só
  **conectar** (`ngen/teko_float.mc`), não reimplementar. `lib/rt.mc` ganhou
  as fns ordinárias (não-hook): `tk_str_len`/`tk_str_slice` (**view por
  ponteiro, zero-cópia — D197**) e `tk_f64_bits`/`tk_f64_from_bits` (o
  reinterpret concreto `f64↔u64`; a forma genérica `<T>` do `wrap`/`unwrap`
  precisa de generics record/replay e fica para a entrega de tipos).
- **Entrega 3 (tipos) — COMPLETA: `struct`, `class`, `interface` e `trait`
  LANDADOS** (SHAs `984f268e`, `06db615d`, `a8757cef`, `2821c261`). Todos
  passam pela mesma tabela de tipos (`ngen/teko_struct.mc`), e `struct`/`class`/
  `interface` são registrados com `type_new(nome, 8, 8, TK_INT)` — identidade
  estática preservada para o `.` resolver membro.
  - `class`: palavra 0 da vtable é a itab, slots virtuais depois
    (`TK_VT_FIXED 1`); campos base-first; `virtual`/`override` contextuais.
  - `interface`: assinaturas + conformidade checada dentro do `Classe_vt_init`;
    despacho por `tk_itab` (`ngen/lib/rt.mc:44`), dinâmico.
  - `trait` (**D216 — modelo do PHP**): corpo gravado por `p_skip_balanced` e
    re-parseado por classe via `p_push_source`; flattening pela MESMA máquina de
    membros; precedência classe > trait > base; **não é tipo** (sem `type_new`);
    `use` lida como identificador contextual, nunca registrada.
  - **Polimorfismo FUNCIONA**, por interface E por classe base, inclusive sobre
    parâmetro (verificado: `area_of(Shape s)` com a derivada devolve o
    `override`; três níveis `A→B→C` devolvem 1/2/3). O despacho não depende do
    tipo estático porque a vtable da derivada é extensão-PREFIXO da base — o
    slot é o mesmo em toda a cadeia. Ver a dívida do §5 para o limite real.
- **CI VERDE, com execução real** (Linux x86_64, ~15 s ponta-a-ponta): baixa
  `mc-0.10.0-linux-x86_64`, confere checksum, `mc build ngen` constrói o
  compilador ensinado `build/mc-teko`, e **9 fixtures compilam, linkam e
  RODAM**: `hello.tk` + `primitives_{float,ptr,scalar,str}.tk` +
  `types_{struct,class,interface,trait}.tk`, todas com exit 42.

## 4. Loop local — o `mc` vem da RELEASE, não de submodule

A sandbox remota **não consegue rodar o `mc`** (rede para o GitHub bloqueada, 403);
lá só se valida estaticamente e o CI é o gate. **Localmente roda-se o `mc` de
verdade**, e é onde esta sessão rende mais — o ciclo fecha em ~1 s.

**Instalação (uma vez, e a cada release nova).** Baixa-se o EXECUTÁVEL das releases
de `schivei/mc` — **nada de submodule**, e **não se usa binário de dentro do clone do
mc** (pode estar à frente do que o CI usa). Troque `macos-arm64` pelo seu alvo:

```sh
gh release download v0.10.0 --repo schivei/mc \
  --pattern 'mc-0.10.0-macos-arm64.tar.gz*'
shasum -a 256 mc-0.10.0-macos-arm64.tar.gz   # conferir contra o .sha256 publicado
mkdir -p ~/.local/mc && tar xzf mc-0.10.0-macos-arm64.tar.gz -C ~/.local/mc
ln -sf ~/.local/mc/mc-0.10.0-macos-arm64/mc ~/.local/bin/mc
```

O CI resolve a release **`latest`** dinamicamente (`.github/workflows/ngen.yml:40`),
não uma versão fixa — então release nova entra no gate sozinha, e o local precisa
acompanhar (§6).

**Config de host.** O `ngen/mc.toml` versionado mira `linux/x86_64`, o alvo do CI, e
não linka neste host. Deriva-se um config em scratch — `os = "macos"`,
`arch = "aarch64"` (`arm64` **não** é aceito; `mc --host` diz o par certo) — sem o
bloco `[linker]`, para o alvo sair pelo backend `macho-exe` embutido:

```sh
sed -e 's#^os   = .*#os   = "macos"#' -e 's#^arch = .*#arch = "aarch64"#' ngen/mc.toml \
  | grep -v '^\[linker\]' | grep -v '^cmd  = ' | grep -v '^args = ' > ngen/mc.macos.toml
mc build ngen --config ngen/mc.macos.toml
./ngen/build/teko-hello; echo $?          # 42
```

**As fixtures**, no mesmo laço que o CI usa — `--entry-only` reaproveita o compilador
ensinado em vez de reconstruí-lo por fixture:

```sh
for src in ngen/tests/*.tk; do
  n=$(basename "$src" .tk); w=$(grep -m1 '// expect-exit:' "$src" | sed 's/.*expect-exit: *//')
  sed -e "s#^entry = .*#entry = \"tests/$n.tk\"#" -e "s#^out   = .*#out   = \"build/$n\"#" \
      ngen/mc.macos.toml > "ngen/mc.$n.toml"
  ngen/build/mc-teko build ngen --config "ngen/mc.$n.toml" --entry-only && "ngen/build/$n"
  echo "$n exit=$?  want=$w"; rm -f "ngen/mc.$n.toml"
done
```

Hoje isso dá **11/11 em exit 42**. `ngen/mc.macos.toml`, os `ngen/mc.*.toml` transientes
e `ngen/build/` **nunca se commitam**, e `ngen/mc.toml` fica **intacto** — alterá-lo
quebra o CI.

**O `mc` NÃO emite C.** Ele emite objeto nativo e linka; não existe passo de `gcc`
sobre saída do compilador ensinado. Compile sempre por `mc build DIR --config FILE`
(ver armadilha 1 do §5.1).


## 5. Entrega 4 em curso — estado e fila

Plano executável em `docs/design/plano-ngen-entrega4.md` (leia §1 descobertas medidas,
§6 correção de rota do escopo, §7-§8 C7b/C8/C7c e a fila revista).

**Landados em `fix/retirement`** (13 fixtures verdes, cada crumb com verificação
independente e revalidação pós-cherry-pick):
- **C0** glob do CI aceita `surface_*.tk`; erratas do handoff.
- **C1** default de parâmetro em MÉTODO (`i64 scale(self, i64 k = 2)`), inclusive em
  assinatura de `interface`; só constante `fold()`-ável; nó clonado por sítio.
- **C2** sobrecarga de MÉTODO por assinatura; slots virtuais chaveados por (nome,
  assinatura); símbolo do 1º método preservado, sobrecargas com sufixo
  (`shape_area__i64`). Resolução por **aridade nível a nível** na cadeia (não é o
  hiding por nome do C#). Mesma aridade com tipos diferentes → erro até o oráculo
  entrar nesse ponto (`tk_method_pick` devolve `-3`).
- **C3** oráculo de tipo estático em `pass()` (`teko_typeof.mc`), consumido quando o
  nome não decide o tipo; costurado com C1/C2; prova de no-op nas 11 anteriores.
- **C7 + C7b** `params` (`i64 total(params xs)`): pacote alocado por sítio na arena,
  `xs[i]` com guard `rt_panic` nos dois lados, reentrante; teto 10 fixos / 12 por sítio.

**Em voo:** crumb de **escopo** (plano §6) — tabela de locais com escopo por bloco via
`syntax_stmt("{")`/`p_blockdepth()`, busca de membro restrita ao tipo do receptor,
`-2` desambiguado; fixture `surface_scope.tk`.

**Fila (plano §8):** escopo → **C8** genéricos com constantes (record/replay) ∥ **C4**
sobrecarga de função de topo → **C7c** `params` genérico em `N` (índice literal
bloqueado em compile-time) e **C5** operadores por `pass()` → **C6** quando o mc der o
hook de declaração de função (pedido feito à sessão do mc, sai como `0.10.N`).

**Dívidas conhecidas:** arena bump sem reclaim (`new` e `params` em loop quente
esgotam 4 MiB ruidosamente — entrega de comportamento base); default em função de topo
bloqueado (C6); `syntax_infix` sobre operador do core morre em silêncio (bug reportado
ao mc; rota é `pass()`).

## 5.1 Armadilhas já pagas (não repita)

1. **`mc --exe` emite Mach-O SEMPRE.** `schivei/mc` `src/main.mc:227` faz
   `--exe → bname = "macho-exe"`, ignorando host e `[target]`. Num runner/host
   Linux isso gera `Exec format error` (ENOEXEC, exit 126). **Compile sempre
   pelo caminho `mc build DIR --config FILE`**, que honra `[target] os/arch` e
   `[linker]` do toml. O CI já faz assim (gera um toml por fixture a partir do
   `ngen/mc.toml`, trocando só `[project].entry`/`out`).
2. **O runner injeta `bash -e`.** Um passo de CI que pretende acumular falhas
   (`status=1; continue`) precisa de **`set +e`** no topo, senão aborta na
   primeira e esconde o estado das demais.
3. **Confira o CI da branch de feature ANTES de drenar.** Drenar primeiro e
   olhar depois já deixou as duas branches canônicas vermelhas uma vez.
4. **`ld` avisa `missing .note.GNU-stack`** em todo `.o` emitido pelo mc → o
   binário sai com stack executável. É item do lado do mc, não do `ngen/`.
5. **Registrar tipo SEM `type_new` = identidade colapsa.** Usar `type_alias`
   num struct/class (ex.: `type_alias("Point", TY_UPTR)`) faz o id colar ao
   `TY_UPTR` → `.` resolve membro por NOME cru, sem distinguir tipos
   não-relacionados que declarem o mesmo campo. **Usar `type_new(name, 8, 8,
   TK_INT)`** — preserva identidade estática; se dois tipos de-fato-não-ligados
   declaram `x`, é error claro (`"type of the left side of '.' is not known"`).
   Auditado contra `mc docs/reference/hooks.md:350`; SEM regressão de ABI
   (8/8 = pointer).
6. **O core reporta o tipo de PARÂMETRO — mas só depois de a declaração
   fechar.** As cinco `decl_*` do M31 respondem a assinatura já parseada, e
   `decl_param_type` devolve o id de `type_new` sem colapsar em `TY_*` (medido).
   Dentro do corpo que está sendo parseado não há resposta — daí o `.` sobre
   receptor de tipo desconhecido ser **DEFERIDO** ao `pass()` (`teko_typeof.mc`),
   onde a unidade inteira existe e o tipo declarado do parâmetro se lê. Resolver
   pelo NOME do membro **não** é aceitável nesse caso: o nome que só OUTRO tipo
   declara não é membro deste receptor (foi o defeito 2 da entrega 3, corrigido
   na entrega 4). O por-nome só sobra como último recurso DENTRO do pass, depois
   de o oráculo dizer que não sabe o tipo — um global, ou expressão que ninguém
   tipa.
7. **Ordem de declaração:** método só chama métodos ACIMA dele (mesma limitação
   do `examples/lang`); consertar exige record/replay. Planejado pra release
   seguinte do mc.
8. **Sem construtor com argumentos** (`new Nome` apenas) e sem `base.m()` —
   ainda não ensinados. Fila de D215.
9. **Achado no repo do mc (não confirmado):** `examples/lang/lang_expr.mc:42-44`
   reutiliza nó do receptor em `ld64` vtable e na lista de args; método virtual
   de aridade ≥1 quebraria. No `ngen/` está contornado por clonagem com guarda.

10. **Trait como tipo de declaração dá mensagem GENÉRICA.** `new Trait` e
    `class C : Trait` acusam com mensagem dedicada e clara, mas `A a;` (trait
    como tipo de variável) falha antes, no parser do core, com
    `expected ; after expression` — sem dizer que a causa é "trait não é tipo".
    Rejeita corretamente, mas o diagnóstico é pobre; é consequência de o trait
    não ter `type_new` (por desenho, D216). Dívida cosmética conhecida.
11. **Campos vindos de trait entram DEPOIS dos campos próprios da classe**,
    independentemente de onde o `use` aparece no corpo (`ngen/teko_class.mc:439`).
    Duas classes que usam o mesmo trait têm offsets independentes e corretos.

## 6. Comunicação — coordenador remoto + sessão local

**Sessão remota coordenadora**: guarda o histórico completo da virada (por que o
port existe, o que aposenta, o que já foi decidido) e drena para as duas branches
canônicas — <https://claude.ai/code/session_01VX6NuV7RoBLyW6tBCrwEde>

**Sessão local** (mini_compiler, `/Users/schivei/projects/mini_compiler`):
desenvolve o `mc` paralelo; repo **somente leitura** para o `ngen`. Regras:

1. A sessão local **avisa quando sai release nova** do mc → o `ngen` baixa a
   nova release, troca o symlink, reconfere o baseline (CI usa `latest`).
2. Quando bater **tensão que o ferramental do mc não resolva** (ex.: sintaxe
   de construto novo, capacidade de hook), a sessão local do `ngen` **pergunta
   a ela** por onde se resolve ou se precisa de suporte novo.
3. **Nenhuma edição direta do repo do mc por parte do `ngen`.** Tudo é hook ou
   solicitação de feature ao dono via sessão local.

Consulte o coordenador remoto quando: (a) aparecer um **fork de design** que o
`DECISION_LOG` e `port-teko-mc.md` não resolvam; (b) houver dúvida sobre se
algo **aposenta** ou se porta; (c) for preciso drenar/alinhar as branches.

## 7. Gate do fecho

O port **começou** (o gatilho M24/floats do mc disparou), mas **só fecha quando
o mc chegar a 1.0.0** — o cálculo automático de arena (M13) e o restante da fila
do mc vêm antes. Até lá: crescer o ensino da superfície, sempre por baixo,
sempre com o CI verde.
