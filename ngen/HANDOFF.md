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
- **NADA DE `Variant` nesta versão teko-mc (dono 2026-09-04, D217).** Não se ensina
  união dinâmica/valor etiquetado em runtime; tipos são estáticos e conhecidos pelo
  oráculo. Se um construto "pedir" Variant, é fork — parar e perguntar.
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
  - **método NÃO declara receptor** (D219, entrega 5 crumb 0): `i64 area() { return side; }`
    — o compilador injeta `this`; nome não-qualificado que não é local nem parâmetro é
    membro de `this`; `this.campo` explícito; `base.m()` chama a implementação da base
    DIRETO (sem vtable). `self` é recusado (`teko: methods take no explicit receiver;
    use this`).
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

## 3.1 CI do repositório — só o `ngen` conta (dono 2026-09-04)

O CI do compilador antigo (`pr.yml` fixpoint/self-host, nightly, seeds, `theory/*`,
release do bootstrap, tag-on-version-bump) está **desativado** no GitHub — 17
workflows em `disabled_manually`; os arquivos seguem no repo, `src/` está congelado.
Ativos: **`ngen (mc) CI`**, CodeQL, Branch policy, Mirror PR. O ruleset `main` passou
a exigir **só** o check `mc build ngen && run` (antes: "CI gate" e "Test suite gate" do
`pr.yml`, que nunca mais fechariam). `fix/retirement` não tem proteção; o ruleset
`others` só barra force-push fora de `main`/`fix/**`/`theory/**`/`f1/**`/`cargo/**`.

### As CINCO pernas nativas (dono 2026-09-04)

`ngen.yml` é uma **matriz de 5 pernas**, cada uma no runner do próprio SO **e**
arquitetura — nada é cross-compilado e deixado sem rodar. Cada perna baixa a
release do `mc` daquele par, confere o `.sha256`, **assere `mc --host`** contra o
par da perna, constrói o compilador ensinado e **executa** `hello` + as 17
fixtures do glob (`// expect-exit: N` como oráculo) — 18 programas no total.

| runner | `[target]` os/arch | asset da release | `[linker]` |
|---|---|---|---|
| `ubuntu-latest` | `linux`/`x86_64` | `linux-x86_64` | **nenhum** — ELF dinâmico do M42; `[target]` glibc (`interp`/`libc`) |
| `ubuntu-24.04-arm` | `linux`/`aarch64` | `linux-arm64` | **nenhum** — ELF dinâmico do M42; `[target]` glibc (`interp`/`libc`) |
| `macos-latest` | `macos`/`aarch64` | `macos-arm64` | **nenhum** (backend `macho-exe`) |
| `windows-latest` | `windows`/`x86_64` | `windows-x86_64` | `lld-link` |
| `windows-11-arm` | `windows`/`aarch64` | `windows-arm64` | `lld-link` |

O nome do asset usa `arm64`, o `[target]` usa `aarch64` — os dois nomes convivem
na matriz. O check dos legs é `ngen (<os>/<arch>)`; o **agregador** mantém o nome
exato que o ruleset exige, `mc build ngen && run`, e falha se qualquer perna falhar
(nada a trocar no ruleset).

O config de cada perna é **derivado do `ngen/mc.toml`** (nunca editado): remove-se o
`[linker]`, troca-se `[target] os`/`arch` e o `out` ganha `.exe` no Windows; o resto
(`[project]`/`[compiler]`/`[include]`/`[limits]`) não pode divergir entre pernas.

**Windows não tem C runtime nem backend de executável direto**, então a perna monta
antes o sysroot que o link precisa, tudo com o próprio `mc` + LLVM da imagem:
`winstart.obj` (`#include <sys_windows_start>`) e `mcrt.obj`
(`#include <sys_windows_host>`) compilados com `--backend=coff-obj-{arm64,x86_64}`, e
`kernel32.lib` gerado por `llvm-dlltool` a partir da lista de 15 exports (a mesma de
`schivei/mc` `scripts/sysroot-windows.sh`). A linha de link é a do próprio `mc`
(`src/mc.windows-*.toml`): `-entry:mc_start -nodefaultlib -stack:8388608`.

## 3.2 O mc que o CI usa hoje: 0.12.1 (2026-09-05, 05:26)

**0.12.1 (PR #21, patch de cooperação):** `[target].libc = "gnu"|"musl"` (FAMÍLIA; a grafia
soname é recusada), `[target].link = "dynamic"|"static"` (static = asserção; com importação
recusa nomeando `[linker]`), flags `--libc/--link/--interp` só com `--exe`. O CI do ngen escolhe
a grafia pela versão (`sort -V` vs 0.12.1) — nada a fazer. Baseline local no 0.12.1: 23/23.
Resposta do mc ao D227: `on_jump` = `blk_depth` (blocos), a pilha de laços só existe no
walker (`gen_walk.mc`), o parser não sabe o que é laço (`while`/`for` são `#rule`) —
`p_loopdepth()` enfileirado como lacuna a preçar, não entra no M45. M45 em correção.

(Registro anterior, 0.12.0:)

**0.10.3 = M41.5** ("the follow-ups the ngen consumer exposed"): **`syntax_param(&f)`** +
`p_decl_name()` — o hook de declaração de função que faltava (**C6 desbloqueado**); e
**`syntax_infix` sobre operador do core funciona** (`ops_init` lazy). **0.11.0 = M40** (AVR).
**0.12.0 = M42**: `--exe` em Linux sem `[linker]`/sysroot. Baseline 18/18 no 0.12.0. Plano §22.
Os avisos de release da sessão do mc **não chegam** por mensagem — o dono repassa; conferir
`gh api repos/schivei/mc/releases/latest` ao começar o dia.

(Registro anterior, 0.10.2:)

A sessão do mc coopera por **patch release** (`x.x.N`), sem mensagem: o CI resolve
`latest` sozinho. **0.10.1** = M41 (core composto em cinco partes, `type_disable`/
`intrinsic_disable`, largura declarada de `uptr`). **0.10.2** = os defeitos que o
`ngen` achou: `--exe` resolve o host (antes: Mach-O sempre), `.note.GNU-stack` em todo
ELF, `examples/lang` avaliava o receptor duas vezes na chamada virtual (o achado do
crumb 2), README do `lx` com `MAXPARAMS` 12. **Segue aberto no mc:** hook de declaração
de função (C6 — `on_param` ou geral) e `syntax_infix` sobre operador do core morrendo em
silêncio no `ops_init()` (rota do C5 é `pass()` de qualquer forma). Ao sair release nova:
baixar, trocar o symlink, **reconferir o baseline** (§4).

## 4. Loop local — o `mc` vem da RELEASE, não de submodule

A sandbox remota **não consegue rodar o `mc`** (rede para o GitHub bloqueada, 403);
lá só se valida estaticamente e o CI é o gate. **Localmente roda-se o `mc` de
verdade**, e é onde esta sessão rende mais — o ciclo fecha em ~1 s.

**Instalação (uma vez, e a cada release nova).** Baixa-se o EXECUTÁVEL das releases
de `schivei/mc` — **nada de submodule**, e **não se usa binário de dentro do clone do
mc** (pode estar à frente do que o CI usa). Troque `macos-arm64` pelo seu alvo:

```sh
tag=$(gh api repos/schivei/mc/releases/latest --jq .tag_name); ver=${tag#v}
gh release download "$tag" --repo schivei/mc --pattern "mc-$ver-macos-arm64.tar.gz*"
shasum -a 256 -c "mc-$ver-macos-arm64.tar.gz.sha256"
mkdir -p ~/.local/mc && tar xzf "mc-$ver-macos-arm64.tar.gz" -C ~/.local/mc
ln -sf ~/.local/mc/mc-$ver-macos-arm64/mc ~/.local/bin/mc
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

**Config sempre RELATIVO, cwd no repo** (`ngen/mc.macos.toml`, não `/abs/...`): com caminho
absoluto o módulo trata todo arquivo como "fora do projeto" e a checagem de `internal` fica
cega — o CI pegou um defeito que a validação absoluta não via (D224).

Hoje isso dá **18/18 em exit 42**. `ngen/mc.macos.toml`, os `ngen/mc.*.toml` transientes
e `ngen/build/` **nunca se commitam**, e `ngen/mc.toml` fica **intacto** — alterá-lo
quebra o CI.

**O `mc` NÃO emite C.** Ele emite objeto nativo e linka; não existe passo de `gcc`
sobre saída do compilador ensinado. Compile sempre por `mc build DIR --config FILE`
(ver armadilha 1 do §5.1).


## 5. Entrega 4 em curso — estado e fila

Plano executável em `docs/design/plano-ngen-entrega4.md` (leia §1 descobertas medidas,
§6 correção de rota do escopo, §7-§8 C7b/C8/C7c e a fila revista).

**Landados em `fix/retirement`** (18 fixtures verdes, cada crumb com verificação
independente e revalidação pós-cherry-pick):
- **C0** glob do CI aceita `surface_*.tk`; erratas do handoff.
- **C1** default de parâmetro em MÉTODO (`i64 scale(i64 k = 2)`), inclusive em
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

- **Escopo** (plano §6): tabela de locais com escopo por bloco via `syntax_stmt("{")`
  + `p_blockdepth()` (sem `on_jump` — a pilha é de parse e o parser sempre chega ao
  `}`); busca de membro restrita ao tipo do receptor (`unknown member of A: extra`);
  `-2` desambiguado (`wrong number of arguments for X` ≠ tipo desconhecido); o oráculo
  também com escopo por bloco. Fecha os dois defeitos silenciosos da entrega 3.
- **C4** sobrecarga de FUNÇÃO DE TOPO por `pass()` (`teko_over.mc`, registrado depois
  de `params` e do oráculo): **toda** sobrecarga é renomeada (`pick__i64`, `pick__Vec`),
  nenhuma fica plana — sítio não reescrito vira erro de link, não fallback silencioso.
  Guards: `&f`, colisão ABI com `params`, `extern`/`main`, ambiguidade.

- **C8** genéricos com CONSTANTES por record/replay (`teko_generic.mc`):
  `class`/`struct Name<T, const N: i64>` gravado por `p_skip_balanced` e re-parseado
  por instância (`p_subst_name`/`p_subst_int` + `p_push_source`), memoizado por
  (nome, argumentos), mangling `Box__Circle__4`; a instância entra na tabela de tipos
  pelo mesmo `type_new` de uma classe qualquer. Nasce no primeiro uso: posição de
  declaração (o nome do genérico vira `syntax_stmt`), tipo de campo/parâmetro/retorno,
  e `new`. `>>` desmontado por `p_resplit_punct(1)`. Campo array inline `T items[N]`
  faz o layout crescer com a constante, e `items[k]` com `k` literal fora de `[0, N)`
  é **erro de compilação** na instância (índice não-literal recebe guard de runtime
  `tk_ix`, emitido uma vez e só no programa que indexa).

- **C3b** oráculo tipa `N_BINARY`/`N_UNARY` espelhando `res_binary` do core (usa o
  próprio `cmp_cond`) e membro escalar (`xt_ty`); o `.` deferido vira chamada a
  `tk_unresolved_member` — sem o pass, o `res_call` do core recusa `call to unknown
  function` com `arquivo:linha` (antes: `INT 0` e binário errado em silêncio).

- **C3c** `.` sobre receptor ESCALAR (`b.side.x` com `side: i64`) era SIGSEGV em
  runtime; agora `teko: i64 has no members: x` em compile-time. `tk_ty_struct_of`
  saiu do oráculo — "sem tipo" (−1, deferível) ≠ "tem tipo e não tem membros".
- **C7c** `params` instanciado por `N` constante (cópia de AST, `total__k` por sítio;
  o record TEXTUAL de função é inalcançável — medido, `cannot redefine core keyword:
  i64`): `xs[lit]` fora de `[0, N)` é erro de compilação; não-literal mantém guard.
  Guard de forma `(uptr, i64)` do C4 removido (obsoleto).
- **CI com 5 pernas nativas** (linux x86_64/arm64, macos arm64, windows x86_64/arm64),
  agregador `mc build ngen && run`; sem filtro de `paths:`.

- **C5** sobrecarga de OPERADORES por `pass()` sobre `N_BINARY` (`teko_ops.mc`).
  **SUPERSEDIDO pelo C5b da entrega 5** (D218: a forma com receptor implícito não é mais
  aceita). O que sobreviveu inteiro: a **regra de endereço** — `N_BINARY` construído pelo
  próprio ngen (`ld64(p+OFF)`, `items[i]`) é reconhecido e nunca tratado como operador — e
  o **core+core não se toca**.

**Entrega 4 FECHADA** (menos C6, bloqueado no hook do mc).

**Entrega 5 — crumb 0 LANDADO: `this` implícito e `base`** (D219, plano §16), o SWEEP que
vem antes do reclaim e do C5b porque os dois escreveriam código na forma velha:
- `ngen/teko_this.mc` (novo) — o receptor que o método não declara. `tk_params`
  (`teko_class.mc`) prepende `this` e RECUSA um parâmetro `self`; `this` é palavra
  (`syntax_expr`) válida só dentro de corpo de tipo; `base` é CONTEXTUAL (só dentro de
  corpo de tipo — segue nome comum em `i64 offset_total(i64 base, ...)`) e `base.m()`
  chama o símbolo da base DIRETO, escolhido por assinatura como o C2 faz.
- **Nome não-qualificado** vira membro de `this` **no pass** (`teko_typeof.mc`, mesma
  caminhada do oráculo): o core entrega um `N_IDENT`/`N_ASSIGN`/`N_CALL` cru e não há
  hook na posição de identificador; o pass é também onde parâmetros e locais são
  legíveis, que é o que a regra do C# exige — **local/parâmetro sombreia o campo**.
  Um campo de tipo classe também vale como RECEPTOR (`inner.v`), e uma chamada
  não-qualificada alcança método declarado ABAIXO (o pass vê o corpo inteiro).
- Limite conhecido: **campo array inline** só pelo receptor escrito (`this.items[i]`) —
  o `[` é rebaixado pelo handler do `.`, que o nome nu não alcança; a recusa diz isso
  (`teko: an array field is reached through \`this.\`: items`).
- As **18 fixtures** foram reescritas na forma nova (`grep -rn self ngen/tests` = 0) e a
  AST final de 16 delas é **byte-idêntica** à da forma velha depois de renomear o
  receptor (`name=self` → `name=this`); as duas que divergem são `types_class` e
  `types_interface`, onde o `override` passou a usar `base.area()` (a diferença é
  exatamente a chamada direta no lugar do corpo antigo).

**Entrega 5 — crumb "membros C#" LANDADO** (D220, plano §17), o modelo de membros que o
reclaim (construtor/destrutor `public`) e o C5b (`public static … operator+`) já escrevem:
- **Modificadores como em C#**, em qualquer ordem e antes do tipo
  (`public static i64 f()`): `public`/`private`/`protected`/`static` em MEMBRO,
  `public`/`internal` em TIPO de topo (`class`/`struct`/`interface`/`trait`).
  **Defaults do C#:** tipo sem modificador é `internal`, membro sem modificador é `private`.
- **Palavras:** só `public` e `internal` são reservadas (elas ABREM uma declaração de topo);
  `private`/`protected`/`static` seguem **contextuais**, como `virtual`/`override` — valem
  dentro de corpo de tipo, que é onde este módulo é o parser, e continuam nomes comuns fora.
- **`internal` = código do PROJETO — a regra exata implementada.** O mc não tem unidade de
  compilação (`core-language.md:422`), então a unidade sai da **origem da declaração**:
  *uma declaração é do projeto quando o arquivo de que foi lida é um caminho **dentro do
  diretório do projeto** — o diretório do `mc.toml` que a build usou (`cfg_file`), e, sem
  config (CLI de arquivo único), o diretório do arquivo de entrada.* São EXTERNOS: caminho
  absoluto, caminho que sobe (`../fora/x.tk`) e `#include <bundle>`, que é nome e não caminho.
  Todo nome de arquivo que o lexer produz é normalizado contra o mesmo lugar do config
  (`path_join`/`path_norm`), então um prefixo puro responde, sem syscall e sem heurística.
  Distinguem-se **duas** origens (o projeto e todo o resto), logo `internal` lê-se
  "declaração e sítio de uso têm a mesma origem" — dois pacotes externos distintos não se
  distinguem entre si (o mc não tem identidade de pacote). Local e CI batem: o config é
  `ngen/mc.*.toml`, o diretório do projeto é `ngen`, e `<float>`/`<mc/core>` ficam de fora.
  Declaração que **não vem de arquivo** não pergunta: instância de genérico
  (`p_push_source`, cujo "arquivo" é o nome do frame) recebe a origem do **template**, e
  membro copiado de trait vira membro da **classe**, com a origem dela.
- **Checagem em todo sítio:** `.` no parse e no pass deferido, chamada de método, `new`,
  `base.`, lista `: Base, Iface`, `use` de trait, campo estático `Tipo.campo` e nome
  não-qualificado dentro do tipo. `protected` = o próprio tipo e as derivadas; `private` =
  só o próprio tipo. Mensagens: `arquivo:linha: teko: X.m is private` / `is protected` /
  `X is internal to another project`.
- **`static`:** campo vira **um global manglado `Tipo_campo`** — nenhum byte no objeto, e os
  offsets seguintes não mudam (`POINT_SIZE` segue 24 em `types_struct.tk`); método não recebe
  `this` e é chamado por `Tipo.m()`. O nome do tipo passou a ser palavra em **três** posições:
  tipo, expressão (`Tipo.campo`, que o `parse_primary` do core recusaria) e statement — e o
  statement só desvia quando um `.` segue o nome, senão entra no `parse_var` do próprio core,
  de forma que `Point p = new Point;` é o statement que sempre foi.
- **Recusas com mensagem própria:** `this`/`base` em membro estático; membro de instância
  alcançado de método estático; membro estático alcançado por objeto (`p.made`); método de
  instância alcançado por tipo (`Tipo.m()`); `private`/`protected` em membro de interface;
  método estático em interface (a forma C# 8 com corpo não é ensinada); **tipo dentro de
  tipo** (o par excludente do D220 escolheu `internal`, logo não há aninhado); modificador de
  visibilidade repetido; `public` diante do que não é tipo; método não-público implementando
  interface.
- **Fixtures:** as 18 escrevem `public` onde acessam membro de fora; o que só o próprio tipo
  alcança fica sem modificador e prova o default (`items`/`count` de `Box`, os dois `pick`);
  `Counted.n` é `protected`; `types_struct.tk` ganhou o par estático. A AST final de 17 das 18
  é **byte-idêntica** à da base `c9b8c596` — visibilidade é checagem, não muda a árvore —, e a
  única que diverge é `types_struct`, exatamente pelo `static` que entrou nela.

**Entrega 5 — crumb "propriedades + interface v2" LANDADO** (D223, plano §20), o modelo de
membro completo que o reclaim (construtor/destrutor) e o C5b escrevem contra:
- **`ngen/teko_prop.mc` (novo)** — `T Nome { ... }` em `class`/`struct`/`trait`, nas três
  formas do C#: **auto** (`{ get; set; }`, com campo de apoio `private` gerado, `Nome__backing`),
  **`=> expressão;` / `=> statement;`** (o `set` é um STATEMENT porque `side = value` é um —
  `=` não está na tabela infixa do core) e **bloco** (`get { } set { }`).
- **Cada acessor é um MÉTODO comum** da tabela do `teko_class.mc` (`get_X`/`set_X`, a grafia
  do próprio C#) — daí saem de graça: **slot de vtable POR ACESSOR** (`override` que redeclara
  só o `get` herda o `set`), `static`, símbolos de sobrecarga, e **visibilidade por acessor**
  (`{ get; private set; }`, nunca mais aberta que a da propriedade).
- **`value`** é o parâmetro do `set` e nada mais (nome comum, sombreável por local); **`get`/`set`
  são lidos SÓ dentro das chaves da propriedade** — nenhuma palavra foi confiscada, e
  `public T get()` de `surface_generics.tk` segue método comum.
- `p.X`, `p.X = e`, `X` nu dentro do tipo e `Tipo.X` (estática) resolvem nos **dois caminhos**
  (`tk_member_of` no parse, `tk_pend_*` no pass).
- **Interface v2:** método com **corpo default** (C# 8) compilado como `iface_m(uptr this)` —
  o itab da classe que não redeclara aponta para esse símbolo; `this` no default é o receptor
  IMPLEMENTADOR tipado como a interface, e todo membro alcançado ali despacha **pelo itab**
  (`this.m()` e o nome nu), então a classe que redeclara é quem responde, inclusive para o
  default que chamou. **`static abstract`** (C# 11): o tipo fornece como `static`, `Tipo.m()`
  resolve em compile-time, e a entrada **não ocupa slot** — o slot passa a ser a posição entre
  os membros de instância (`tk_ifslot`/`tk_ifinst`). Interface também declara **propriedade**
  (`i64 X { get; set; }` = assinaturas). Substitui a recusa de `static` em interface do D220.
- **Recusas próprias:** atribuir a `get`-only; `value` fora de `set`; propriedade sem acessor;
  acessor duplicado; acessor mais visível que a propriedade; auto misturado com corpo; chamar a
  propriedade; tipo que não fornece o `static abstract` ou o fornece como instância; `static`
  sem `abstract`; `abstract` com corpo; acessor de interface com corpo; e o acessor faltante
  **nomeado pela propriedade** (``o `set` de uma propriedade de `I` ``, não `set_X`).
- **Fixtures:** `surface_property.tk` e `surface_iface_default.tk` (20/20 em exit 42); a AST
  final das **18 anteriores é byte-idêntica** à de `5579c34b` — nenhuma usa propriedade nem
  default de interface.
- **Limite conhecido (precisado pelo verificador):** num corpo default de interface, só o
  acesso **`this.X()` explícito** é sensível à ordem (resolve no parse: membro declarado ABAIXO
  dá `unknown member` claro). A chamada **nua** (`area()`) resolve no pass e é insensível à
  ordem — funciona em qualquer posição.

**Entrega 5 — D224 LANDADO** (`b60e7dfe`, 22 fixtures): `abstract class`/membro/propriedade
abstratos como C# (slot de vtable sem corpo; derivada concreta sem `override` é erro nomeando
o acessor); **`partial class`** fecha no primeiro uso ou no fim da unidade (parte depois do uso
é erro); tabelas por posse (`fd_cls`/`vs_cls`/`ci_cls`) — tipo declarado entre partes não
corrompe o layout; base só numa parte antes de membros; interfaces em união; método parcial não.

**Entrega 5 — RECLAIM LANDADO** (D218, plano §14/§15; 23 fixtures): a arena fixa de 4 MiB ganhou
**free list por classe de tamanho** e o **refcount por escopo**, e o `ngen` devolve memória — 1M
`new` num laço não esgota mais nada.
- **Layout:** cabeçalho de **16 B** (vtable@+0, contagem@+8 — o `+8` NÃO estava reservado, os
  campos começavam em 8) e vtable com `&Nome_release` na palavra 0 e a itab na palavra 1
  (`TK_VT_FIXED 2`, o layout do `lx`). É o release na palavra 0 que deixa `rc_dec` liberar um
  objeto cuja classe ele não conhece. As quatro fixtures que afirmam offset foram corrigidas
  (`SHAPE_SIDE` 8→16 etc.); `types_struct.tk` não muda (struct não tem cabeçalho).
- **`Nome(params) { }`** é construtor, sobrecarregável por assinatura, com **`: base(args)`** como
  no C# (e a exigência do C#: base que só declara construtor com argumento tem de ser nomeada).
  `new Nome(args)` escolhe pela contagem de argumentos; `new Nome` sem construtor que case segue
  entregando o objeto zerado — é por isso que as 22 fixtures anteriores não mudaram de forma.
  **`~Nome() { }`** é destrutor (sem modificador, sem parâmetro, um por classe), chamado pelo
  release **antes** dos campos, derivada antes da base.
- **`ngen/teko_rc.mc` (novo)** — o passe que injeta o RC. **Vai no PASSE, não no parse** (ver o §5.2
  abaixo: é a decisão que o crumb mandava reportar). Saída de bloco em ordem reversa, `break N`/
  `continue`/`return`, `x = e`, `p.f = e`, `Tipo.f = e`, `x[i] = e` e o `set` de propriedade,
  `rt_drop` para a referência que ninguém pegou, e **temporários** (`rt_park`/`rt_mark`/`rt_sweep`)
  para o valor possuído que cai em posição sem dono — argumento, receptor de `.`, operando.
- **Sem RC nesta fatia (dívida declarada em `lib/rt.mc`):** `struct` (não tem vtable, logo não tem
  release nem contagem) e o pacote de `params` (nasce e morre dentro de uma expressão, não há nome
  para segurá-lo). `rt_live()` conta os dois, então um programa que os mistura com classes vê um
  piso acima de zero em vez de uma resposta errada. Campo `static` de tipo classe guarda a
  referência corretamente, mas nunca é liberado (vive o programa inteiro).

**Entrega 5 — C5b LANDADO** (D218, plano §15/§27; 23 fixtures): operadores refeitos **como C#** —
a forma velha do C5 (receptor implícito) deixa de ser aceita, com mensagem própria.
- **Declaração** `public static T operator<op>(A a[, B b])` em `class` e em `struct`. O operador é
  um membro **estático**: sem `this`, sem slot de vtable, e é a assinatura que diz tudo. Binários
  `+ - * / % == != < <= > >= & | ^ << >>`, unários `- ! ~` (e `+`, aceito na declaração — o core
  não tem prefixo `+`, então ainda não há sítio que o alcance; `mc/src/parse.mc` `ops_init`).
- **Resolução pelos DOIS operandos** (`teko_ops.mc`, tabela `op_*`): candidatos = operadores
  declarados pelo tipo de QUALQUER operando **e pelas bases dele**; pelo menos um parâmetro tem
  de ser do tipo declarante. Três rodadas, nessa ordem — **exata**, **literal** (a do C4: um
  `N_INT` cai em `i64` na 1ª e em qualquer inteiro do core na 2ª) e **base** (operando de tipo
  DERIVADO num parâmetro da base — zero bits de conversão, o objeto derivado já é um da base).
  Duas declarações na mesma rodada = ambiguidade recusada, **exceto na rodada base**: entre
  `GrandBase`/`MidA`/`Kid` sem redeclarar, `Kid + 2` escolhe o operador de `MidA` (o ancestral
  MAIS PRÓXIMO), pelo "better function member" do C# (§12.6.4) — `tk_op_pick_best` mede a
  distância na cadeia de `base` de cada candidato e só desempata quando um domina o outro nos
  dois operandos; ambiguidade real entre bases não-relacionadas segue recusada. `2 + v`
  (reversed) e `-a` (unário) resolvem por essa mesma máquina.
- **Pares obrigatórios** (`==`/`!=`, `<`/`>`, `<=`/`>=`) checados quando a unidade fecha (no
  `pass`), então `partial class` pode escrever as duas metades em partes diferentes.
- **Visibilidade checada NO SÍTIO** (`tk_check_member`, o achado 3 do crumb de membros que o C5
  não fazia): `Vec.operator+ is private` de fora, aceito dentro do próprio tipo.
- **Rota = `pass()` sobre `N_BINARY`/`N_UNARY`, não `syntax_infix`.** Desde o 0.10.3 o
  `syntax_infix` sobre operador do core FUNCIONA (M41.5), mas continua sendo a rota errada aqui:
  no parse o tipo de um operando que é parâmetro/`.` deferido não existe, e o handler não veria a
  **regra de endereço** do §12 (o `ld64(p+OFF)` que o próprio ngen constrói). O cabeçalho do
  `teko_ops.mc` registra os dois motivos.
- **Posse:** o resultado de um operador que devolve classe é **possuído** — o `tk_xt_put` do pass
  é o que diz isso ao `teko_rc.mc`. Medido com `rt_live()`: `(a+b)==c` não muda a contagem (o
  temporário é parked/sweeped com a statement), `-a` e `2+a` sobem 1 cada (o local segura), e a
  saída do bloco volta a **0**.
- **Recusas próprias:** forma velha (`an operator is static and names both operands`); `==` sem
  `!=`; ambiguidade; `private` de fora; operando sem tipo; `this` num operador; token unário com
  dois parâmetros e vice-versa; 0 ou 3+ parâmetros; nenhum parâmetro do tipo declarante; default
  em parâmetro; retorno `void`; `virtual`/`override`/`abstract`; token não sobrecarregável; e
  `operator` em `interface` (a mensagem confusa que o plano §12 apontou como adjacente).
- **Fixture** `surface_operator.tk` reescrita na forma nova; a AST final das **22 outras é
  byte-idêntica** à de `05dc7181`.

**Fila:** **C6** default em função de topo (`syntax_param`) → `while`/`for` →
`namespace`/`import`/`using` → `const` → `switch` (D222) → closures/`ref`/`out` (D221,
architect-first) → compilador teko de `<mc/core_min>` (plano §26). **Fora:** `var`, `type`,
`match`, Variant, método parcial, nested.

**Dívida do C8:** `p.items[i]` sobre um receptor que o parser NÃO tipa (um parâmetro,
que só o oráculo do `pass()` resolve) não chega ao `[` de array — cai no `[` do `params`
e é recusado com `teko: \`[\` indexes a \`params\` list only`. Recusa clara, nunca
miscompilação; fechar isso é trabalho no `teko_typeof.mc` (C3b, em voo em paralelo).

**Dívidas conhecidas:** default em função de topo bloqueado (C6); **o core não tem prefixo
`+`** (`ops_init` registra só `- ~ ! &`), então `operator+` unário é declarável e não tem
sítio — não existe hook `syntax_prefix`, é pedido ao mc; `struct`, pacote de `params` e
campo `static` de classe sem reclaim (acima). Fechadas: a arena bump sem reclaim (D218) e o
`syntax_infix` sobre operador do core (mc 0.10.3/M41.5 — mas a rota do C5b segue sendo o
`pass()`, pelos dois motivos do cabeçalho de `teko_ops.mc`).

### Por que o RC ficou no PASSE e não no parse (achado do crumb do reclaim)

O crumb mandava injetar o RC no parse, como o `lx` (`lang_stmt.mc` `lg_decs_from`), e
**parar e reportar** se o `on_jump` não desse a contabilidade ou se parse e pass
divergissem. As duas coisas apareceram, e são de fundo:

1. **`on_jump` dá profundidade de BLOCO, não de laço.** O `lx` conta laços porque é dono
   de `while`/`for` e empilha uma marca em cada um (`lg_lp`). Aqui `loop` é palavra do
   CORE e `word_add` recusa sequestrar keyword do core (`mc/src/hooks.mc:237-241`), então
   não existe `syntax_stmt("loop")` para empilhar marca — `break N` não teria como saber
   quantos escopos atravessa. Na árvore o nó `N_LOOP` está lá e a conta sai de graça.
2. **A POSSE não é decidível no parse.** Se `e` já carrega uma referência própria é uma
   pergunta sobre o TIPO ESTÁTICO de `e`, e no `ngen` o `.` sobre receptor que o parser
   não tipa é **deferido por desenho** (`teko_expr.mc` `tk_defer_member`): no parse o nó é
   um placeholder sem tipo nenhum. Chutar ali é vazamento silencioso (um incremento a
   mais) ou use-after-free silencioso (um a menos) — exatamente o que o crumb proíbe. O
   `lx` não tem esse buraco porque tipa todo receptor no parse (o `self` dele é parâmetro
   explícito).

Logo **escopo e posse têm UM dono só, o passe** — que é o que a própria sessão do mc
sugeriu no alerta do §23 do plano ("candidato a unificar, o pass como fonte única, quando
o reclaim/RC entrar"). A pilha de locais do parse fica intacta e segue fazendo o que
sempre fez, resolver `.`. **Nada ficou híbrido.**

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
8. **Sem construtor com argumentos** (`new Nome` apenas) (`base.m()` existe desde a entrega 5, crumb 0 — D219) —
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

12. **`extern` de libc POSIX não linka no Windows** (achado ao abrir as 5 pernas).
    O Windows não tem C runtime nenhum: o link é `-nodefaultlib` + kernel32, e o que
    resolve os nomes POSIX é a camada de sistema do `mc` (`mcrt.obj`, quinze nomes:
    `open`/`read`/`write`/`close`/`creat`/`exit`/`_exit`/`mmap`/`chmod`/`mkdir`/
    `unlink`/`posix_spawnp`/`waitpid`/…). `surface_overload_free.tk` declarava
    `extern i64 getpid()` e dava `undefined symbol: getpid` nas duas pernas Windows —
    trocado por `chmod` (existe nas cinco), perguntado sobre um path inexistente e
    comparado contra resultado POSITIVO, porque POSIX responde -1 e o shim do Windows
    responde 0. **Fixture nova só declara `extern` que esteja nessa lista.** Um
    `extern` declarado e NÃO chamado não custa nada: o `mc` só emite símbolo
    indefinido para o que é referenciado (é por isso que os `<sys>` de `lib/rt.mc`
    — `munmap`, `_NSGetEnviron`, `posix_spawnp` — não quebram o link).

13. **`p_start()` NÃO aponta para a fonte quando o token foi substituído.** A
    substituição higiênica do `p_subst_name` (instância de genérico) troca
    `tok_start`/`tok_len` pelo LEXEMA DE SUBSTITUIÇÃO, que mora na arena
    (`mc/src/lex.mc` `subst_apply`) — então `p_start() + tamanho-do-nome` cai em
    lugar nenhum e varrer a partir dali é lixo. Quem precisa **espiar o que vem
    depois do token atual** (o `Tipo.campo` do D220 tem que distinguir
    `Shape.made = 1;` de `Shape s = new Shape;`, e o parser guarda UM token de
    lookahead) usa **`cp`**, o cursor do lexer — é de onde o próximo token vai ser
    lido, no mesmo buffer que `p_src_end()` limita, e é o que o próprio
    `stmt_syntax` do core compara no seu guard. Um comentário entre o nome e o `.`
    não é lido pela varredura (só espaço em branco), e o caso cai na recusa clara
    do `parse_var`, nunca em silêncio.

14. **mc ≥ 0.12.1 (patch pós-M42): `[target].libc` vira FAMÍLIA (`"gnu"|"musl"`) e a grafia
    soname (`"libc.so.6"`) é RECUSADA.** O workflow escolhe a grafia pela versão resolvida
    (`sort -V` contra 0.12.1) — as pernas Linux carregam `libc_family: gnu` na matriz.
    Também novo: `[target].link = "dynamic"|"static"`, flags `--libc=`/`--link=`/`--interp=`.
15. **mc ≥ 0.12.0 (M42): o `mc build` Linux escreve ELF dinâmico SEM `[linker]`, com loader
    e soname **musl por default**.** Num runner glibc (ubuntu) o compilador ensinado sai com
    `interp` de musl e o `mc build` falha em `mc: cannot run: ngen/build/mc-teko`. As pernas
    Linux do CI nomeiam o par glibc no `[target]` (`interp = "/lib64/ld-linux-x86-64.so.2"` ou
    `"/lib/ld-linux-aarch64.so.1"`, `libc = "libc.so.6"` — mc `docs/build.md` §`[target]`) e
    não têm mais `[linker]`. `ngen/mc.toml` versionado (linux/x86_64 + `[linker] cc`) segue
    intacto; o CI deriva o config por perna.

## 5.2 Canal com a sessão do mc

`send_message` só funciona teko→mc. A sessão do mc escreve para nós em
**`/Users/schivei/projects/mini_compiler/build/NOTICES-teko.md`** (gitignored) — **ler ao
começar cada lote**; respostas dela e releases estão lá. Plano §23 tem o resumo do que já
respondeu. Regra do dono: **sem 1.0.0 do mc sem coordenação com o ngen**; e o M44 prevê o
ngen como pacote (`teko_init()` exportado, nunca `user_init`).

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
