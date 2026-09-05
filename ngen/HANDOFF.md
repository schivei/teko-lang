# Handoff — sessão local do `ngen/` (port teko → mc)

Documento de entrada para uma **sessão local** assumir o trabalho do `ngen/`.
Escrito pela sessão remota coordenadora; leia inteiro antes do primeiro commit.

## 1. O que é o `ngen/`

O **port do teko para o `mc`** (minicompiler.dev, `minicompiler/mc`), morando dentro
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
`minicompiler/mc` `scripts/sysroot-windows.sh`). A linha de link é a do próprio `mc`
(`src/mc.windows-*.toml`): `-entry:mc_start -nodefaultlib -stack:8388608`.

## 3.1a Repositórios do mc migraram para a organização `minicompiler` (2026-09-05)

`schivei/mc` → **`minicompiler/mc`** (e os privados `mc-registry`/`mc-ops`). O GitHub redireciona os
nomes antigos, mas o CI (`ngen.yml`: API de releases e base de download) e a receita do §4 já apontam
para `minicompiler/mc`. Tags, releases e checksums não mudam. A org é a casa dos pacotes oficiais e a
identidade admin do registro.

## 3.2 O mc que o CI usa hoje: 0.14.1 (2026-09-05)

**0.14.1 (PR #25, patch de cooperação):** `continue N;` no núcleo, espelho de `break N;` — N
níveis de laço contados do mais interno; `continue;` = `continue 1;` (mesmo nó de antes, inerte,
`nd_val` 0 lido como 1); `continue 0;` → `continue expects a positive level`; N além da
profundidade → `continue out of range`; `continue outside loop` inalterado; `on_jump` recebe o nó
ANTES da checagem de nível (o `depth` do gancho continua sendo profundidade de BLOCO, não de laço).
Consumido pela entrega 5 crumb "adoção do 0.14.1": `tk_switch_rewrite_continue_stmt`
(`ngen/teko_switch.mc`, era `tk_switch_no_continue_stmt`) e `tk_loop_rewrite_stmt`
(`ngen/teko_loop.mc`) leem `nd_val` de `N_CONTINUE` a mesma forma que já liam de `N_BREAK`;
`tk_rc_jump` (`ngen/teko_rc.mc`) idem. Baseline local no 0.14.1: 32/32.

(Registro anterior, 0.13.0:)

**0.13.0 (PR #22, M45):** `i32` (`type_new` pelo NÚCLEO, kind `TK_SINT`, sinal por kind); **uma
chamada devolve o que declara** (D5) — todo `extern` que devolve C `int` passa a `extern i32`
(corrigido em `ngen/tests/surface_overload_free.tk`'s `chmod`); **`p_cp()`** público (o cursor do
lexer sob substituição, usado em `ngen/teko_access.mc`'s `tk_dot_follows`); e o falso positivo
`region crosses a file boundary` no fim de um arquivo incluído, corrigido no núcleo (nada a tirar
aqui — `teko_generic.mc` não tinha contorno algum, só o design região-por-parte). O mesmo release
também respondeu ao lote C5b: `+` unário tem site por `syntax_expr("+")` + `parse_expr(11)` (a
precedência acima de `*`/`/`/`%`, a mais alta do `--dump-rules`), sem linha nova no núcleo —
`ngen/teko_ops.mc`'s `tk_unary_plus`. Baseline local no 0.13.0: 25/25.

(Registro anterior, 0.12.1:)

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
`gh api repos/minicompiler/mc/releases/latest` ao começar o dia.

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
de `minicompiler/mc` — **nada de submodule**, e **não se usa binário de dentro do clone do
mc** (pode estar à frente do que o CI usa). Troque `macos-arm64` pelo seu alvo:

```sh
tag=$(gh api repos/minicompiler/mc/releases/latest --jq .tag_name); ver=${tag#v}
gh release download "$tag" --repo minicompiler/mc --pattern "mc-$ver-macos-arm64.tar.gz*"
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

**Entrega 4 FECHADA, C6 incluso** (desbloqueado pelo `syntax_param` do mc 0.10.3 — ver
o bloco "C6 LANDADO" logo abaixo da fila da entrega 5).

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
  `+ - * / % == != < <= > >= & | ^ << >>`, unários `- ! ~ +`. O `+` unário TEM SÍTIO (M45, entrega
  5 crumb 0): `teko_ops.mc`'s `tk_unary_plus`, `syntax_expr("+")` + `parse_expr(11)`; sobre um tipo
  do NÚCLEO o pass colapsa o nó no próprio operando (`+x == x`), o gen do núcleo nunca vê o `N_UNARY`.
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

**Entrega 4 — C6 LANDADO** (`syntax_param`, `teko_default.mc`, 24 fixtures): default de
parâmetro em FUNÇÃO DE TOPO, `i64 add(i64 a, i64 b = 10)` → `add(1)` completado em
`add(1, 10)` por um `pass()` (`tk_default_pass`), registrado logo antes do `tk_over_pass`.
- **Duas rotas, uma tabela.** `tk_default_pass` resolve sozinho toda chamada a um nome
  declarado UMA VEZ na unidade (`tk_default_decl_count`, varredura de `root` — NÃO a
  tabela de parâmetros, que só tem linha para declaração com ≥1 parâmetro; uma sobrecarga
  de aridade zero, tipo `tally()` ao lado de `tally(i64)`, nunca aciona `syntax_param` e
  ficaria invisível se a contagem fosse pelas linhas). Um nome declarado mais de uma vez
  (C4) é deixado intocado aqui de propósito: `tk_over_pass` ganhou uma QUARTA rodada
  (`tk_ov_fits_default`/`tk_ov_match_default`), tentada só depois das duas de aridade exata
  falharem — o que dá de graça a regra do C# (§12.6.4.5, "candidato sem default vence"):
  `add(1)` com `add(i64)` e `add(i64, i64 = 10)` resolve para o primeiro na rodada exata,
  sem jamais consultar a tabela de defaults.
- **Reuso total do C1** (`tk_param_default(mark)`, a tabela `df_node`/`tk_ndflt` e
  `tk_fill_defaults`, todos de `teko_class.mc`) — zero regra ou mensagem duplicada; a
  função de topo é só mais um chamador da mesma máquina que método/construtor já usam.
- **Achado que exigiu correção:** `tk_over_pass` renomeia a declaração (`tk_ov_rename`,
  para o símbolo com sufixo) ANTES de resolver qualquer chamada — então a quarta rodada
  não pode casar pela `nd_name(d)` corrente (já mangled); casa pelo `od_name_at(i)`, o
  nome ORIGINAL que `tk_ov_collect` guardou no instante da coleta, antes do rename tocar
  o nó. `teko_default.mc` expõe `tk_default_ndef_of_name`/`tk_default_d0_of_name` (por
  NOME, não por nó) exatamente por isso.
- **Recusas:** `params` com `=` no mesmo parâmetro (`teko: a \`params\` list has no
  default`); `extern` com qualquer default, mesmo que nenhum call-site precise dele —
  checado por declaração, não por chamada (`teko: an extern parameter has no default`);
  `na < nreq` (`teko: <fn> takes at least N arguments`); mais as duas regras herdadas do
  C1 (constante, sem-default-após-default), mesma mensagem.
- **`p_decl_name()` distingue membro de função de topo sem precisar de `p_set_decl_name`
  do C1:** `tk_params` (membros, `teko_class.mc`) tem loop PRÓPRIO e nunca chama o
  `parse_params()` do core, então `syntax_param` simplesmente nunca dispara para um
  parâmetro de membro — zero colisão, zero checagem extra necessária.
- **Fixture** `surface_default_free.tk` (1 e 2 defaults, 0/1/2/3 args, sobrecarga
  sem-default vencendo, chamada dentro de método de classe); AST das outras 23 é
  byte-idêntica à base.

**Entrega 5 — LOOPS LANDADO** (D218/D221/D226, plano §29; 25 fixtures): `while`, `do ... while`
e `for` como em C# (`ngen/teko_loop.mc`, novo), rebaixados no PARSE ao `loop`/`if`/`break N` do
núcleo (mantidos, D221) — a mesma forma que `lib/prelude.mc` já mostra, só que via `syntax_stmt`
em vez de `#rule`, para caber o rewrite de saltos abaixo e para um `.tk` cru ganhar as palavras
sem `#include`.
- **`while (c) stmt`** → `loop { if (!(c)) break; stmt }`. O corpo fica na MESMA profundidade que
  o programador escreveu — nenhum `break`/`continue` dentro dele precisa de ajuste.
- **`do stmt while (c);`** → `loop { loop { stmt break; } if (!(c)) break; }`; **`for (init; cond;
  step) stmt`** → `{ init loop { if (!(cond)) break; loop { stmt break; } step; } }`. As duas
  embrulham o corpo num loop-de-uma-volta extra, para que `continue` (que sempre reinicia o loop
  MAIS INTERNO) caia no passo/condição em vez de voltar ao topo do corpo — e é esse loop invisível
  que exige o rewrite: `tk_loop_rewrite_stmt` caminha o corpo ANTES de embrulhar, contando quantos
  `N_LOOP` do PRÓPRIO corpo ficam entre ele e cada `break`/`continue`; um `break k` que já
  ultrapassa o que o corpo abriu (`k > profundidade`) também precisa ultrapassar o embrulho novo
  (`k+1`), e um `continue` na profundidade 0 vira `break 1` (cai onde o `break;` do embrulho
  cairia). Compõe corretamente aninhado (para `for` dentro de `for`, o `break 2` do usuário sai
  com o nível certo mesmo depois de cada camada aplicar seu próprio `+1`) — a fixture
  `surface_loops.tk` prova o caso de dois `for`s com `break 2`.
- **`x++;` `x--;` `x += e;` `x -= e;`**, como statement solto E como passo de `for`: os quatro
  tokens são registrados direto por `word_add` (sem `#token`, evitando o dobro-registro), e a
  forma solta usa a MESMA rota que `+=`/`++` de `lib/prelude.mc` — um `#rule` empurrado por
  `p_push_source` a partir de `user_init()`, antes do primeiro token do programa real (o
  `drv_parse` chama `lex_init` → `user_init()` → `parse_unit()`, nessa ordem, então o push cai
  ANTES do primeiro `next()`). A rota é a mesma proposta como opção A no crumb, mas usando a forma
  do prelúdio em vez de `st64`/`ld64` crus: `x += e;` vira `x = x + e;`, então herda de graça o
  `operator+` que uma classe declarar (C5b) e o RC de `x`, coisa que sintetizar `st64(&x, ...)`
  diretamente NÃO teria (perderia posse/overload). O passo do `for` lê os mesmos tokens
  diretamente (sem passar pelo `#rule`, já que não há `;` de fechamento ali).
- **Fixture** `surface_loops.tk` (25/25 em exit 42): `while` com bloco e com statement único,
  `do...while` com `continue` provando que cai na condição, `for` clássico com `i++`, `for` com
  `continue` que executa o passo, `for (;;)` com `break`, `for` aninhado com `break 2`, `for`
  dentro de método de `class` (o passe de RC caminhando a árvore sintetizada) e `x += 3;`. AST das
  24 fixtures anteriores **byte-idêntica**.

**Entrega 5 — M45 crumb LANDADO** (mc 0.13.0, 25 fixtures): três itens, nenhum novo `.tk`.
- **Adoção do mc 0.13.0**: `p_cp()` público troca a leitura crua de `cp` em `teko_access.mc`'s
  `tk_dot_follows`; `ngen/tests/surface_overload_free.tk`'s `chmod` (C ABI, devolve `int`) passa a
  `extern i32` (D5 — uma chamada devolve o que declara, sign-extended); nenhum outro `extern` do
  `ngen` chama C que devolve `int` (`lib/rt.mc` é 100% `callp`). Sem contorno de "region crosses a
  file boundary" a remover — o núcleo corrigiu o falso positivo, e o `teko_generic.mc` nunca teve um.
- **`+` unário** (fecha a dívida do C5b acima): `teko_ops.mc`'s `tk_unary_plus`, registrado por
  `syntax_expr("+")`, lê o operando por `parse_expr(11)` (uma acima da maior precedência infixa) e
  devolve o MESMO `N_UNARY` que o núcleo constrói para `-v`. `tk_ops_unary` resolve `T.operator+`
  quando o operando é de um tipo teko; sobre um tipo do núcleo colapsa o nó no próprio operando
  (`tk_ops_replace`, `+x == x`) — o gen do núcleo nunca vê um `N_UNARY` de `+`.
- **`true`/`false`**: `teko_type.mc`'s `tk_true`/`tk_false`, `N_INT` de 1/0 tipado `TY_I64` (como
  o oráculo já tipa toda comparação/`!`/`&&`/`||`, não `TY_U8`/`bool` — `bool` é a largura de uma
  DECLARAÇÃO, não o tipo de um valor de verdade). `syntax_expr` reserva as duas palavras: `i64 true
  = 1;` é recusado (`name reserved by a syntax/type_alias registration: true`).
- **Fixtures**: `surface_operator.tk` ganhou `operator+(Vec a)` unário e os sítios `+v`/`+i`;
  `surface_loops.tk` ganhou `while (true) { ... break; }` com dois `bool`. AST das 22 fixtures
  realmente não tocadas (todas menos essas duas e `surface_overload_free.tk`) **byte-idêntica**
  contra o compilador da base em `545b26b5`, os dois no mc 0.13.0.

**Entrega 5 — N1 LANDADO** (D218/D226, plano §31/§32; 26 fixtures): `namespace A.B { ... }` e
`namespace A.B;` (file-scoped, C# 10), `using A.B;` e tipos qualificados, `teko_ns.mc` (novo).
- **Nome real = `A__B__Circle`** (`type_new`, sem alias); o nome CURTO nunca é `type_alias`
  (`alias_find` responde só o último registrado em silêncio — colidiria entre namespaces). É
  registrado como palavra própria (`syntax`/`syntax_stmt`/`syntax_expr`, as mesmas duas últimas
  que `tk_type_word` já usa) e a identidade sai da lista de busca **no sítio de uso**: namespace
  corrente para fora, prefixo a prefixo, e só então os `using` do ARQUIVO — nunca memoizada.
- **Declaração nunca busca:** qualifica com o namespace corrente e procura EXATO
  (`tk_ns_qualify`, chamado uma vez por construto que declara tipo — `class`/`struct`/
  `interface`/`trait`); fora de um namespace é a identidade, então nenhum programa sem
  `namespace` muda de forma. O reopen check de `partial` (`teko_class.mc`) precisou da mesma
  qualificação (read-only, `tk_ns_qualified_name`) para não fundir dois `partial class Foo` de
  namespaces diferentes.
- **Qualificado (`geo.Circle`, `geo.Circle.made`, `new geo.Circle()`)** resolve pelo 1º SEGMENTO
  (`geo`), lido com `p_name()`+`p_next()` — nunca `p_ident()`, pois o segmento é palavra reservada
  a partir do 2º uso. `tk_ns_walk` cresce o nome acumulado por `.` enquanto o que já foi lido é um
  namespace conhecido, e para no instante em que vira um tipo declarado — `tk_static_member`
  (já existente) consome o resto (`.made`, `.tally()`).
- **`tk_struct_find` ganhou um fallback** (namespace corrente + `using`s do arquivo,
  `tk_ns_resolve`) que responde -1 sem custo quando o programa não usa namespace nenhum — mas o
  scan original teve de virar `tk_struct_find_exact`, chamado por todo sítio que testa uma
  string que ELE MESMO construiu (dentro de `teko_ns.mc`, e o reopen check/`tk_gen_close`), pois
  `tk_struct_find` chamando de volta `tk_ns_resolve` sobre seu próprio candidato recursa sem
  convergir (medido: estouro de pilha via `lldb bt`, ver plano §32 item 1).
- **`tk_newname` (teko_struct.mc) aceita uma palavra namespaced curta já reservada** por OUTRO
  namespace (`mesh { class Circle }` depois de `geo { class Circle }`), checando o exato
  qualificado antes de aceitar — o mesmo texto reservado por dois namespaces diferentes não é
  duplicata; pelo mesmo texto no MESMO namespace, ainda é.
- **`tk_ns_top`** é a única posição de tipo que o core lê sem hook nenhum (`Circle f(Circle c)`
  no topo, `syntax(curto, ...)`); `tk_gen_ty` (teko_generic.mc) e `tk_default_param`
  (teko_default.mc) ganharam o mesmo ramo namespaced ANTES de `p_type()`/`type_of_token` para o
  campo/parâmetro/retorno de membro e o parâmetro de função livre, respectivamente.
- **Achados/correções do próprio crumb** (plano §32): `tk_type_stmt`/`tk_type_expr` precisaram
  de um `if (si < 0) err_at2(...)` explícito (liam lixo fora da tabela sem); `tk_ns_seg_stmt` usa
  `p_id() != tk_ns_dot` (não `tk_dot_follows`, que responde outra pergunta depois que o nome
  qualificado inteiro já foi consumido); `tk_new` precisou reler `name = sr_name_at(si)` antes de
  montar o símbolo do alocador (senão `new Circle()` bare, resolvido via `using`, chamava
  `circle_new` em vez de `geo__circle_new`); `tk_gen_declstmt` cedeu seu próprio rabo
  (`tk_var_after_type`, agora em `teko_ns.mc`) para `tk_ns_seg_stmt` reusar — nenhum dos dois pode
  entregar o tipo já resolvido ao `parse_var` do core, que insiste em consumir a palavra-tipo ele
  mesmo, e os dois já consumiram mais de um token antes de saber o tipo.
- **Dívidas registradas (não escondidas):** cast de nome curto namespaced; `global`/`extern`/
  `main` dentro de namespace FILE-SCOPED (só o BLOCO é pego, pelo laço que este módulo controla);
  generic declarado dentro de namespace continua com nome CURTO simples (D31.14); instanciação de
  genérico qualificada; `using`/namespace fora do topo do arquivo sem checagem de ordem.
- **Probes de recusa** (fora de `ngen/tests/`): dois `using` ambíguos
  (`teko: ambiguous name Circle (geo, mesh)`); `namespace` sem `{` nem `;`; tipo curto sem
  `using` nem qualificação (`teko: unresolved name: Circle`); `extern`/global/`main` dentro de
  bloco de namespace; `partial` reaberto em outro namespace (confirmado: NÃO funde, cada um só
  enxerga seu próprio campo); `using` dentro de bloco (recusa natural do core, `using` só existe
  em posição de topo).
- **N1b (errata, plano §32):** a lista `:` de base/interface e o `use` de trait namespaced,
  ambos furos abertos pelo verificador do N1, fechados — `tk_conf_name` e `tk_use` leem o nome
  por `p_name()`+`p_next()` (D31.3) em vez de um único `p_ident()`.

**Entrega 5 — N2 LANDADO** (D218/D226, plano §31/§32/§33; 27 fixtures): função livre declarada
dentro de um namespace, mangled e resolvida por um `pass()` (`teko_ns.mc`'s `tk_ns_pass`).
- **Duas varreduras.** Sweep 1 mangla toda função livre/protótipo namespaced (bloco OU
  file-scoped) para `geo__area`, ANTES de qualquer pass que censa por nome (`params`, oráculo,
  operadores, defaults, sobrecarga); sweep 2 resolve o sítio não-qualificado pela lista de busca
  (o namespace corrente do SÍTIO, prefixo a prefixo, e só então os `using` do arquivo — D31.6),
  reescrevendo só quando o candidato qualificado EXISTE (`decl_find`, D31.10 — `rt_alloc`/uma
  função plana chamada de dentro de um namespace fica achatada). O namespace de um bloco é
  anotado no PARSE (`tk_ns_decl_note`, no laço de `tk_namespace`, por identidade do nó); o de um
  sítio sai de graça do próprio nome, JÁ mangled pelo sweep 1 (`tk_ns_of_name`, novo, extrai
  `geo` de volta por prefixo — o mais específico, para `A`/`A.B` coexistirem).
- **`geo.area(x)` qualificado NÃO passa pelo pass:** `tk_ns_seg_expr`/`tk_ns_seg_stmt` do N1 só
  resolviam tipo; ganharam `tk_ns_qualified_call` — quando o segmento não é um tipo mas É um
  namespace conhecido, o nome cheio é montado DIRETO (sem `decl_find`, que não veria uma
  declaração ainda não mangled nem uma escrita mais abaixo no arquivo).
- **`teko_default.mc` ganhou `tk_default_rename`:** a tabela de defaults de função livre é
  chaveada por PONTEIRO do nome no instante do parse (`fpd_name`); sem mover essa chave junto com
  `set_nd_name`, o default de uma função namespaced sumia (`tk_default_row_of_name` nunca achava
  a linha, e o `teko_over.mc`'s quarto round também não).
- **Achados/correções:** um global SINTETIZADO por uma classe namespaced (o `_vt`, emitido bem
  depois do parse por `tk_class_close`) caía na recusa "global fora de todo namespace" quando a
  varredura de sweep 1 passou a alcançar TODO nó de topo — a guarda usa o MESMO `tk_ns_of_name`
  (só recusa um global que ainda NÃO carrega prefixo); a guarda de colisão de identidade (§31 (c))
  não vale contra `decl_find` para função — duas assinaturas de uma função namespaced (C4)
  aterrissam de propósito no mesmo nome cheio, e só a tabela de TIPOS é checada. O **furo do
  destrutor** que o verificador do N1 achou: `teko_class.mc`'s `tk_member_dtor` comparava o token
  que a fonte escreveu (o nome CURTO, `Base`) contra o nome QUALIFICADO (`geo__Base`) — nunca
  batia. Corrigido com `tk_ns_short_of` (novo, o inverso de `tk_ns_of_name`); o CONSTRUTOR já
  estava correto (`tk_gen_ty` resolve por TIPO, não por palavra), mas a mesma classe de bug
  também escondia o diagnóstico `void Name(...)` (C#'s own mistake) logo abaixo — corrigida junto.
- **Fixture** `surface_namespace_fn.tk` (27/27 em exit 42): qualificado + sobrecarga C4, bare de
  dentro e de fora (via `using`) do namespace, default C6 bare e qualificado, função namespaced
  chamando uma plana (achatada), construtor E destrutor pelo nome curto com `: base(v)`. AST das
  26 fixtures anteriores **byte-idêntico** (prova de no-op). Probes fora de `tests/`:
  `main`/`extern`/global em namespace FILE-SCOPED; dois `using` ambíguos; chamada sem namespace
  nem `using` (erro do core); `rt_live()` bare de dentro de um namespace; `void Name(...)`.

**Entrega 5 — N3 LANDADO, série namespace fechada** (D218/D226, plano §31/§34; 28/28 em exit
esperado — hello.tk + 27 fixtures do glob): `import A.B;`, sugar sobre o `lex_include` do core
mais um `using A.B;` implícito, e dois consertos de dívida do verificador do N2.
- **`teko_ns.mc`'s `tk_import`** — `lex_include(tk_ns_path_of(full), line)` com o contrato do
  lookahead (`lex_include` chamado ainda sobre o `;`, `p_next()` só depois); once-only pelo
  `lex_seen` do próprio core (uma reabertura não empurra o arquivo de novo, só repete a `using`,
  inofensivo). `tk_ns_path_of` reusa o mesmo scanner de `tk_ns_dotted` (`tk_ns_sep_replace`),
  trocando "__" por "/" e sufixando `.tk`. Recusado dentro de um bloco de namespace aberto
  (mesmo guard de `tk_namespace`) e recusado quando o ARQUIVO do `import` já declarou um
  namespace seu (D31.13, "no topo, antes de qualquer namespace") — a tabela nova `nsd_file`
  marca isso por `p_file()` no instante em que `tk_namespace` roda, então um namespace
  declarado dentro do arquivo IMPORTADO (outro arquivo) nunca marca o importador, e o
  once-only da segunda `import` nunca reroda o corpo pra marcar duas vezes.
- **Item 2 (dívida do verificador do N2) — `&f` de função em namespace.** `tk_ns_walk_calls_in`
  (sweep 2) só reescrevia `N_CALL`; estendido a `N_ADDR` (o nó de `&nome`, que carrega o nome
  bare do mesmo jeito que uma chamada) — `tk_ns_rewrite_call` já lê/escreve por `nd_name`,
  então o mesmo rewrite serve os dois sem código novo. A forma QUALIFICADA (`&geo.f`) precisou
  de ensino: o core exige que o operando de `&` seja `N_IDENT`, e `tk_ns_qualified_call` só
  sabia montar `N_CALL`. Agora, quando o nome não é seguido de `(`, devolve `tk_id(full)` (um
  `N_IDENT` bare com o nome cheio) em vez de errar "unresolved qualified name" — mesma filosofia
  do D31.10 (uma referência que não existe chega ao linker faltando, não é checada aqui).
- **Item 3 — convenção de mensagem.** `teko: a constructor is written without a return type:
  geo__Circle` interpolava o nome QUALIFICADO onde o dev escreveu o CURTO (o nome de uma classe
  dentro do próprio corpo dela nunca carrega o namespace) — corrigido com `tk_ns_short_of`.
  Convenção adotada, aplicada em `teko_class.mc` e `teko_trait.mc` (grep completo dos dois):
  **nome da PRÓPRIA declaração** (a classe sendo lida agora, `tk_member`/`tk_base_init`/
  `tk_class_reconf`/`tk_class_reopen`) mostra o **CURTO** (`tk_ns_short_of`), porque o dev nunca
  o escreve qualificado; **nome REFERENCIADO** (o `:` de base/interface em `tk_conf_name`, o
  `use` de trait em `tk_use`, e as mensagens de `base(...)` sobre a base em `tk_base_ctor_call`/
  `tk_base_init`) mostra **`A.B.Nome`** (`tk_ns_dotted`, novo — reusa o mesmo scanner de
  `tk_ns_path_of`), porque é exatamente o que o dev pode ter escrito. `nm`/`full`/`acc` (o texto
  cru, "__"-juntado) seguem dirigindo toda resolução; só o argumento passado à mensagem muda.
  (`teko_access.mc`'s `tk_deny_member`/`tk_check_member` — a mensagem `X.m is private` — usa um
  formato próprio, hífen entre `sr_name_at(owner)` e o membro; fora do grep pedido pelo crumb,
  registrado como achado adjacente, não tocado aqui.)
- **Fixture** `surface_import.tk` + `ngen/tests/parts/geo.tk` (com `namespace parts.geo;`
  file-scoped): `import parts.geo;` DUAS vezes (once-only), `Circle` sem modificador
  (`internal` por D220) alcançada de dentro do projeto, forma qualificada
  (`parts.geo.Circle`/`parts.geo.twice`) e bare via o `using` implícito, `&twice`/
  `&parts.geo.twice` cada um passado a `callp`. AST das **27 fixtures anteriores** (as 26 do
  glob + `hello.tk`) **byte-idêntico** contra o compilador da base `5e401b01`; `mc limits ngen`
  `ok`. Probes fora de `tests/`: `import` de namespace sem arquivo (`mc: cannot open:
  .../nope/here.tk`, a mensagem crua do core); `import` dentro de `namespace { }` (recusa);
  `import` depois de um `namespace` no MESMO arquivo (recusa).

**Entrega 5 — N3b LANDADO** (plano §35, correção de bug real do verificador do N3): a ordem de
resolução de um nome bare era furada em dois pontos -- `&f` reescrevia para a função `geo.f`
mesmo com uma LOCAL `f` em escopo, e uma chamada `f(...)` fora de qualquer namespace perdia, em
silêncio, para o `geo.f` que um `using geo;` trazia mesmo com uma `f` PLANA de topo já visível.
Ordem final: local/parâmetro → namespace corrente e prefixos (D31.6, inalterado) → SÓ se isso não
achar nada, uma declaração plana de topo do nome exato → os `using`s do arquivo (um `using` nunca
vence o que já era visível sem ele). `tk_ns_walk_calls_in` ganhou o mesmo escopo por bloco que
`teko_typeof.mc` já mantém para seu próprio passe (`sc_name`/`tk_nscope`, reusado, não uma
terceira tabela).

**Entrega 5 — N3c LANDADO** (plano §35, correção de bug real do verificador do N3b): um membro do
tipo corrente (método, inclusive herdado da base, inclusive estático) vencia em C#, mas perdia
para o `using` porque `tk_ns_pass` reescrevia a chamada bare ANTES de `tk_this_call` sequer ver o
nome; ordem final: local/parâmetro → membro do tipo corrente → namespace corrente e prefixos →
declaração plana de topo → `using`s (`tk_ns_call_cls`, lido de `teko_class.mc`'s `tk_method_of_fn`/
`tk_method_named_find`, os dois já usados por `teko_this.mc` para o mesmo passe posterior).

**Entrega 5 — CONST LANDADO** (D218, plano §36; 29 fixtures): `const` como açúcar sobre o `#define`
do mc, `ngen/teko_const.mc` (novo).
- **Mecanismo:** NEM `do_directive()` NEM `p_push_source`/laço-de-topo — o handler `syntax("const",
  &tk_const_top)` chama `fold(parse_expr(0))` (o mesmo que `#define` chama) e `def_add(nome, valor,
  linha, arquivo)` DIRETO, o mesmo par que o `enum` demo do próprio mc usa (`mc docs/reference/
  hooks.md` § `syntax()`). Zero indireção: nenhum texto é montado/empurrado.
- **Topo:** `const i64 N = 10;` — fora de namespace o nome do `#define` É o nome curto, então o
  `parse_primary` do NÚCLEO já resolve toda referência bare (inclusive como tamanho de array —
  `parse_dim` chama `fold(parse_expr(0))` também — e em expressão comum) sem código nenhum daqui.
  Dentro de `namespace geo { }` o nome é qualificado ANTES do `#define` (`tk_ns_qualified_name`,
  NÃO `tk_ns_qualify` — esta última reservaria a palavra curta como TIPO, errado para uma
  constante): `geo.N` resolve no PARSE (`tk_ns_qualified_call`, o mesmo segmento de `geo.area(x)`,
  agora também respondendo por const) e `N` bare de dentro de `geo` (ou via `using`) resolve num
  PASSE novo (`tk_ns_rewrite_ident`, estendendo `tk_ns_walk_calls_in` para `N_IDENT` — uma chamada
  é resolvida por RENOME porque o núcleo procura o símbolo em tempo de lowering, mas um const não
  tem essa procura: o nó é SUBSTITUÍDO por um `N_INT`, o mesmo que `parse_primary` teria construído).
- **Genérico:** `Box<T, const N: i64>` instanciado com o NOME de um const (`Box<Circle, N>`) —
  `tk_gen_targs` lia só `T_INT` cru; ganhou um ramo que aceita `T_IDENT`, procura bare e depois
  qualificado pelo namespace corrente na tabela de `teko_const.mc`, e lê o valor já dobrado (sem
  reparsear).
- **Membro:** `public const i64 MAX = 4;` — `const` entra em `tk_member_mods` como mais um modificador
  contextual (`static const` é recusado: "already static"); `tk_member_const` (teko_const.mc) lê
  tipo+nome+valor e registra `Tipo__MAX` (dois underscores, distinto do `Tipo_campo` de um static
  field). `Nome.MAX` de fora entra em `tk_static_member` (checado ANTES de field/método, já que um
  const não ocupa slot nenhum); `MAX`/`STEP` bare de dentro entram em `tk_this_ident`'s fallback
  (`tk_this_const`, mesma prioridade que um field), visibilidade pelo `tk_check_member` de sempre.
  Não há RC nem layout — `const` nunca ocupa slot; herda pela cadeia de base como um field.
- **Local recusado, de propósito:** `teko_stmt.mc`'s `tk_stop_const` (a posição de ESTATUTO, dentro
  de corpo de função) segue reservada, mensagem própria — `#define` é uma tabela única do programa,
  sem escopo de local.
- **Achado (não previsto pelo crumb, registrado):** a redefinição de um `const` de topo NÃO cai no
  `duplicate #define` de `def_add` — `p_ident()` do núcleo já recusa ANTES, com `name already
  defined by #define` (o guard embutido em toda leitura de nome de declaração, `check_def()`).
  Mensagem diferente da esperada, mas 100% do núcleo, igualmente clara.
- **Fixture** `surface_const.tk` (29/29 em exit 42): const de topo em expressão, como tamanho de
  ARRAY GLOBAL (`i64 arr[SIZE];` — um array LOCAL com `[i]=v;` não é suportado por este compilador,
  achado adjacente, não é regressão do `const`: `[` em posição de expressão é `teko_params.mc`'s
  `tk_bracket`, que só resolve um campo-array ou uma lista `params`; um `N_INDEX` sobre array
  comum nunca é lowered — dívida pré-existente, registrada abaixo), como argumento `const N` de
  genérico (`Box<i64, SIZE>`), const em `namespace geo` bare e qualificado, const membro `public`
  acessado `Nome.X` de fora e bare dentro, `private const` bare dentro. AST das **28 fixtures
  anteriores** (as 27 do glob + `hello.tk`) **byte-idêntica** contra o compilador da base
  `8adf0f93`. `mc limits ngen` `ok`. Probes fora de `tests/`: `static const` ("already static; drop
  static"); local const (a mensagem acima); `private const` de fora ("Foo.SECRET is private");
  atribuição a um const de topo (`N = 5;` → o núcleo recusa com `left side of assignment must be a
  name`, porque `N` já virou `N_INT` no parse — nunca chega a ser um `N_IDENT` atribuível);
  redefinição (achado acima); valor não-constante (`const i64 N = g();` → "teko: const requires a
  constant expression", mensagem própria).
- **Dívida nova:** um array LOCAL comum (`i64 arr[N];` dentro de função, fora de struct/`params`) não
  suporta `arr[i] = v;` — o `[` cai em `teko_params.mc`'s `tk_bracket`, que só sabe lowerar um campo
  de array (`this.items[i]`) ou uma lista `params`; sobre qualquer outro array o `N_INDEX` que ele
  constrói nunca é resolvido (nem lido nem escrito), e uma ATRIBUIÇÃO a ele já é recusada no parse
  (`left side of assignment must be a name`, o núcleo exige `N_IDENT`). Pré-existente ao `const`
  (achado ao testar o array de tamanho fixo); registrado, não fechado aqui.
- **Dívidas herdadas dos verificadores N3b/N3c (ainda não constavam, registradas agora):** `&campo`
  bare dentro de um método não resolve (só `&funcao` livre passa por `tk_ns_rewrite_call`/`N_ADDR`);
  um membro `private` da BASE bloqueia com erro de visibilidade em vez de cair no fallback `using`
  quando um `using` traria um candidato de mesmo nome de outro namespace — o gate de acesso roda
  ANTES da decisão "achou member, tenta using", então a mensagem é "is private" em vez de resolver
  pelo `using`. Nenhum dos dois é tocado por este crumb.

**Entrega 5 — TERNÁRIO LANDADO** (D228, plano §37; 30 fixtures): `c ? a : b`, associativo à
direita, mesma precedência de `||` (`ngen/teko_ternary.mc`, novo).
- **Mecanismo:** o núcleo não tem controle de fluxo em posição de expressão, então
  `syntax_infix("?", TK_TERN_PREC, &tk_tern_infix)` só constrói um PLACEHOLDER — chamada a
  `tk_ternary(c, a, b)`, o mesmo truque de `tk_defer_member` (se o passe não rodar, o núcleo
  recusa `call to unknown function`, nunca miscompila). `TK_TERN_PREC` é **1**, não 0 —
  `syntax_infix` recusa precedência fora de 1..100, e 1 já é a linha mais baixa da tabela
  (empatada com `||`); ler `b` com `parse_expr(TK_TERN_PREC)` (o MESMO piso, não piso+1) é o
  que dá a associatividade à direita.
- **Posição do passe: logo DEPOIS de `tk_typeof_pass`, ANTES de `tk_ops_pass`** — não entre `ns`
  e `params` como o crumb sugeria de partida. Motivo: `tk_ty_of` (o oráculo que tipa os braços)
  só responde um `.` sobre receptor que o parser não tipou depois que `tk_typeof_pass` já
  reescreveu o placeholder deferido no load/call que ele representa; rodar antes faria um braço
  com `.` deferido responder "tipo desconhecido" em vez do tipo real do campo. `teko_rc.mc` roda
  por último pelo MESMO motivo ("depois que o oráculo resolveu todo acesso deferido..."). `params`/
  `typeof` não perdem nada rodando antes do ternário — nenhum dos dois olha a FORMA da árvore
  (bloco/if/statement), só censo por nome e tipo — e `ops`/`default`/`over`/`rc`, todos DEPOIS do
  ternário, passam a ver `if`/local comuns, nenhum precisa saber que um ternário existiu.
- **Braços preguiçosos com aninhamento:** `tk_tern_lower` hoista `c` (junto do `if` que ele mesmo
  dirige — roda sempre, então não custa nada) e reduz `a`/`b` cada um DENTRO do seu próprio ramo,
  antes de tipar — um ternário aninhado num braço (`c ? (x?y:z) : w`, ou o encadeamento à direita
  `c1 ? a : c2 ? b : d`, que vira exatamente `tk_ternary(c1, a, tk_ternary(c2, b, d))`) hoista de
  dentro pra fora, mas o `if` interno cai DENTRO do ramo externo — preguiça sobrevive ao
  aninhamento. Provado por probe (não fixture): `0!=0 ? side(1) : (1!=0 ? side(2) : side(3))`
  chama `side` uma vez só.
- **Condição de `while`/`for`:** cai dentro do bloco do corpo do loop (o `if (!c) break;` que
  `teko_loop.mc` já constrói), reavaliada a cada volta — provado na fixture.
- **`return`/`if` sem chaves:** `tk_tern_branch` embrulha o statement solto num bloco só quando ele
  de fato hoistou algo, a mesma cerca que `teko_rc.mc`'s `tk_rc_branch` já usa para um temporário
  parked.
- **Tipo:** os dois braços do MESMO tipo pelo oráculo, senão `teko: the two arms of ?: have
  different types`; objeto teko nos dois braços funciona igual (o temporário é uma local comum, o
  passe de RC — que roda DEPOIS do ternário — trata como qualquer outra).
- **Fixture** `surface_ternary.tk` (30/30 em exit 42): inicializador, argumento, encadeamento à
  direita, preguiça com contador, condição de `while`, braços de objeto (`rt_live()` prova que
  nenhum objeto novo nasce só para a escolha), `return` dentro de `if` sem chaves, dentro de
  método. AST das **29 fixtures anteriores** byte-idêntica contra o compilador da base. `mc limits
  ngen` `ok`. Probes fora de `tests/`: braços de tipos diferentes (`i64`/`f64`) → recusa; `?` sem
  `:` → "expected ':' in a ternary"; `a ? b` sem `:` como statement solto → mesma recusa; ternário
  como lado esquerdo de atribuição → recusa do núcleo ("left side of assignment must be a name").
- **Dívida documental do `const` ainda aberta (registrada no §5, não fechada aqui):** o array LOCAL
  comum sem `[i]=v;` (achado do crumb `const`) segue sem fechamento — fora do escopo do ternário.

**Entrega 5 — SWITCH LANDADO** (D222/D228, plano §19/§38; 31 fixtures): as duas vertentes do C#,
`ngen/teko_switch.mc` (novo).
- **Statement** (`syntax_stmt("switch")`) rebaixa, no PARSE, a um `loop` de uma volta: `x` lido
  UMA vez (`i64 $t = x;`, o 1º statement do loop), um `if` por grupo de rótulos que compartilha um
  corpo (`case 2: case 3: … break;` — rótulo vazio cai no próximo; corpo não-vazio tem de terminar
  em `break`/`return`/`continue`/`break N`, senão `teko: control cannot fall out of a case`),
  `default` (em qualquer posição — movido para o FIM da sequência de `if`s, C#), `case <const> when
  <cond>:` (sem pattern de tipo — só constante+guarda opcional) e um `break;` incondicional final
  que fecha o loop mesmo sem match. `case` duplicado (mesmo valor, sem guarda) e `default` duplicado
  são recusados; `default` combinado com um `case` no MESMO grupo gera as duas coisas (o `if`
  posicional E o corpo clonado como fallback, `tk_clone_list`).
- **`break`/`break N` no corpo do case NÃO são reescritos** — o loop do switch já É o nível que a
  fonte enxerga, então um `break;` cru já sai do switch; um `break N` que alcança mais longe é
  pego pelo rewrite de um `do`/`for` EXTERNO (`tk_loop_rewrite_stmt`), que enxerga o loop do switch
  como só mais um `N_LOOP` descoberto — a MESMA composição que já vale para loop-dentro-de-loop
  (plano §29). Prova: `break 2` atravessando um `switch` dentro de um `for` na fixture.
- **`continue` dentro de um case reescreve, não recusa mais** (mc 0.14.1, `continue N`; crumb
  "adoção do 0.14.1", `tk_switch_rewrite_continue_stmt`): um `continue k` na profundidade 0 do case
  vira `continue k + 1`, a MESMA regra de `break` (`tk_loop_rewrite_stmt`), nunca convertido a
  `break` — o loop do switch é de uma volta só, continuá-lo direto é sempre seguro. Um `switch` sem
  laço envolvente é interceptado com mensagem própria em vez do "continue out of range" cru do
  núcleo. Um `continue` dentro de um loop que o PRÓPRIO corpo do case abre passa normalmente.
- **Errata (crumb "guarda do continue sem laço", 2026-09-05):** a checagem acima corria no PARSE
  (`tk_realloop_depth`, só `while`/`do`/`for`) e dava falso positivo num `loop { }` cru envolvente
  (o núcleo não avisa módulos de laço bare); movida para um `pass()` (`tk_switch_guard_pass`,
  registrado logo após `tk_ternary_pass`, ANTES de `tk_rc_pass` — esse relocaliza o `continue` pra
  um índice de nó novo ao envolvê-lo em release, plano §38 detalha) que enxerga TODO `N_LOOP`
  igual, bare incluso; só olha um `continue` bare marcado (`sw_bare`, `teko_switch.mc` — nunca um
  campo do próprio nó, que corromperia `--dump-ast`/`tk_clone`) contra o `N_LOOP` que o número
  alcança, contando um `loop {}` de switch (`nd_val=TK_SWITCH_LOOP_MARK`) como inválido também.
- **Expression** (`x switch { 1 => a, 2 or 3 => b, _ when c => d, _ => e }`, `syntax_infix("switch",
  TK_TERN_PREC)`, D228): NENHUMA máquina própria — constrói a MESMA cadeia de placeholders
  `tk_ternary(...)` que `teko_ternary.mc`'s `?:` constrói, dobrada da ÚLTIMA armação para trás; a
  condição da última armação NUNCA é testada (é a base incondicional da cadeia) — por isso exige-se
  ao menos um braço `_` em algum lugar (`teko: a switch expression needs a` _` arm` se faltar),
  idealmente o último escrito. `or` só entre constantes (sem patterns). `x`, se não for um nome
  simples, é lido uma vez via um `N_VAR` real embutido NO MEIO da expressão (`teko_switch.mc`'s
  `tk_switch_xleft`) — hoisted pelo MESMO passe do ternário (`teko_ternary.mc` ganhou
  `tk_tern_hoist_var`, reconhecendo um `N_VAR` embutido como um segundo tipo de placeholder, ao
  lado de `tk_ternary`; hoisted incondicionalmente, igual à condição `c` de um ternário comum).
  Prova de avaliação única: `switchval(v) switch {...}` incrementa um contador exatamente 1×.
- **Achado no registro:** `when` já estava reservado (`syntax_stmt("when", &tk_stop_when)`, entrega
  1) — `tk_kw("when")` (que só casa `T_IDENT`) nunca bate contra a palavra reservada; corrigido para
  `tk_word("when")` (`teko_class.mc`, "a mesma pergunta para uma palavra que TAMBÉM pode estar
  reservada"). E `syntax_infix` já entrega o operador CONSUMIDO ao handler (`hooks.md` § syntax_infix
  — "the operator already consumed") — `tk_switch_infix` não deve chamar `p_next()` de novo (o
  `tk_tern_infix` do ternário já não chamava; o erro apareceu como "expected { after switch" comendo
  o `{` de verdade).
- **Fixture** `surface_switch.tk` (31/31 em exit 42): `case` múltiplo, fall-through de rótulos
  vazios, `default` fora de ordem combinado com um `case`, `when`, `const` como rótulo, `break 2`
  atravessando um `for`, `switch` dentro de método (`Bucket.describe`), expression em inicializador
  E em `return`, aninhada (o valor de um braço é outro `x switch {...}`), `or`, `when`, braços de
  objeto (`rt_live()` prova que a escolha não aloca). AST das **30 fixtures anteriores**
  byte-idêntica contra o compilador da base `3f223f9a`. `mc limits ngen` `ok` (`intrin` segue 8/8 —
  nenhum registrado). Probes fora de `tests/`: braço sem `break` → recusa; `case` duplicado →
  recusa; `case` não-constante → recusa; `switch` expression sem `_` → recusa; `continue` dentro de
  `switch` → recusa (decisão acima); `switch` sem `{` → recusa do núcleo.
- **Dívida registrada:** um `case`/braço namespaced (`geo.N`) não resolve como rótulo — o `#define`
  do núcleo dobra um nome BARE no parse, mas um const namespaced só resolve num passe posterior
  (`teko_ns.mc`), depois que o rótulo já teria de estar dobrado; fora do escopo deste crumb. Um
  `when` guardando o braço `_` TEXTUALMENTE ÚLTIMO da expression não é testado (é a base
  incondicional da dobra) — escrever a guarda no último braço é ignorado; documentado, não
  fechado (o núcleo não tem exceção em runtime para cobrir o caso sem match).

**Entrega 5 — ARRAYS FIXOS LANDADO** (plano §39; 32 fixtures): `T a[N];` local e global, `a[i]`,
`a[i] = e`, `a[i] += e`/`-=`/`++`/`--` e `a.Length` — o núcleo já lê a DECLARAÇÃO (`N_VAR`/
`N_GLOBAL` com `nd_val` = a contagem, `language.md` § Locals/§ Globals); o `[`/`.` são só deste
crumb (`ngen/teko_array.mc`, novo). Um fix pequeno do `switch` num commit separado (abaixo).
- **LOCAL, resolvido no parse** — a mesma máquina do campo-array de `teko_struct.mc`. O `N_VAR` de
  um array é observado por um SEGUNDO `on_stmt` (`tk_arr_on_stmt`, ao lado do `tk_on_stmt` que já
  existia) e registrado numa tabela própria (`av_*`/`tk_narr`), com escopo por bloco: `tk_block`
  (`teko_stmt.mc`) ganhou uma SEGUNDA marca/restauração (`amark`/`tk_narr`), ao lado da que já
  cuidava de `tk_nlocal`. `tk_bracket` (`teko_params.mc`, o mesmo dono do `[` desde o C7) checa a
  tabela ANTES do fallback de `params`; achando, resolve tudo ali — leitura, `=`, `+=`/`-=`/`++`/
  `--` (`tk_arr_index_of`) — sem deixar nó pendente.
- **GLOBAL, resolvido num `pass()`** — `on_stmt` não vê declaração de topo (`hooks.md` § on_stmt) e
  não existe hook público sobre uma; um `pass(&tk_array_pass)`, registrado ANTES de
  `tk_params_pass`, varre `nnodes` por `N_GLOBAL` com `nd_val != 0` (`tk_garr_collect`). Uma
  LEITURA que o parser não resolveu já é o `N_INDEX` que o `[` de `params` também deixa (o mesmo
  fallback de sempre, `teko_params.mc`'s próprio cabeçalho) — o passe acha só os que nomeiam um
  array global e reescreve em `node_assign`, deixando os outros (o `xs[i]` de um `params`) intactos
  para o `tk_params_pass` de sempre. Uma ESCRITA não pode esperar o passe — o núcleo recusa
  `g[i] = e;` no PRÓPRIO parse (`left side of assignment must be a name`) — então `tk_bracket` lê
  `=`/`+=`/`-=`/`++`/`--` também no fallback, e devolve um placeholder (`tk_call("tk_unresolved_
  array", 0)`, o MESMO idioma do `.` deferido de `teko_typeof.mc`) que o passe resolve ou recusa
  (`teko: not a known array`, uma recusa estritamente NOVA — antes disso o núcleo já recusava
  qualquer escrita não resolvida, então não há regressão).
- **Largura e sinal:** `ld8`/`ld16`/`ld32`/`ld64`/`st8`/`st16`/`st32`/`st64` por `type_width`
  (`tk_ldn`/`tk_stn`, já de `teko_struct.mc`). `ld32` é sempre zero-extending (`language.md` §2 —
  "the signed read of raw memory... spelled `(i32) ld32(p)`"); um elemento `TK_SINT` mais estreito
  que a palavra — só `i32`, hoje — é envolvido num `CAST` pro próprio tipo depois do load
  (`tk_arr_load`), o MESMO idioma documentado. `i64` não precisa (já é a palavra inteira).
- **Bounds:** um índice LITERAL fora de `[0, N)` é erro de compilação, nas duas rotas
  (`tk_arr_bounds`, mensagem `teko: index K is out of range for NAME[N]`, o formato do próprio
  crumb). Um índice não-literal NÃO tem guard em runtime nesta fatia — dívida abaixo.
- **`a.Length`** — só para um array LOCAL (o `[` já sabe; um global exigiria a mesma deferência da
  leitura, fora do escopo desta fatia). `tk_dot` (`teko_expr.mc`) checa a tabela `av_` ANTES de
  `tk_struct_of_expr`; achando, devolve a constante `N` e recusa qualquer outro membro. `a.Length =
  e` cai na recusa do próprio núcleo (`tk_int` não é um nome).
- **Recusado, não contornado:** um array de tipo struct/classe (`Circle cs[2];`), local OU global —
  o elemento seria um slot de objeto sem nome próprio para `teko_rc.mc` percorrer, vazando a cada
  sobrescrita; mensagem própria nas duas rotas (`tk_arr_on_stmt`/`tk_garr_collect`).
- **Fixture** `surface_arrays.tk` (32/32 em exit 42): array local com leitura/escrita por índice
  variável num `for`, `a.Length` no laço, `a[i] += e`/`-=`, um global sem inicializador (`u8 g[8]`)
  e um com (`i64 t[] = {…}`), as três larguras (`u8`/`u16`/`i32`, a última provando o sinal — um
  valor negativo que voltaria positivo se `ld32` não fosse casteado), e um array local ao corpo de
  um MÉTODO (`Grid.product3`, a mesma prova de que o passe de RC caminha por uma árvore sintetizada
  sem se importar com arrays escalares no meio dela). AST das **31 fixtures anteriores**
  byte-idêntica ao compilador da base `6cf49db1` — EXCETO `surface_switch.tk` (tocada pelo commit
  do fix abaixo; idêntica entre o commit do fix e este). `mc limits ngen` `ok` (`intrin` segue 8/8
  — nada de novo registrado). Probes fora de `tests/`: índice constante fora do range → recusa;
  `Circle cs[2]` → recusa; `a[1] = e` sobre um `i64` escalar → recusa (`teko: not a known array`);
  `a.Length = 3` → recusa do núcleo.
- **Fix do `switch` (commit separado, ANTES deste):** `tk_switch_check_end` só olhava o último nó
  de TOPO do corpo do `case` — um corpo escrito como bloco explícito (`case 1: { …; break; }`, C#
  comum) caía direto no "control cannot fall out of a case" mesmo terminando em `break`. Recursa em
  `N_BLOCK` agora, olhando o último statement DENTRO do bloco — a mesma recursão que
  `tk_switch_no_continue_stmt` já fazia, ao lado. `surface_switch.tk` ganhou um `case` de bloco
  explícito (`case 20: { r = 55; break; }`) provando o conserto.
- **Dívidas registradas:** índice DINÂMICO sem guard em runtime (precisa de `panic` de superfície,
  que este crumb não tem); array de objeto/struct (local ou global) — o pacote inteiro de `T[]` em
  heap com RC próprio, fora de escopo; `T[]` como parâmetro (o nome de um array decai pro endereço,
  como em C, mas `void f(i64 xs[])` na ASSINATURA não foi ensinado); `.Length` sobre um array
  GLOBAL (só o local resolve); um `params xs[i]` dentro do corpo replay-instanciado de outra
  `params` que TAMBÉM usa um array global — o passe de arrays roda uma vez, antes da instanciação
  de `params`, então um global usado só dentro do corpo REPLAYED de um `params` cairia no `[` de
  `params` sem chance de resolver; nenhuma fixture combina os dois, registrado como aresta rara.

**Fila:** closures/`ref`/`out` (D221, architect-first) →
compilador teko de `<mc/core_min>` (plano §26). **Fora:** `var`, `type`, `match`, Variant,
método parcial, nested, `foreach` (precisa de iteráveis), herança de interface, `using G = geo;`/
`using static`, genérico qualificado (D31.14), namespace aninhado (D31.1).

**Dívida achada pelo verificador do C6 (registrada aqui, crumb futuro):** o `ngen` é um parser de
UMA passada — um tipo/classe precisa estar declarado ANTES do primeiro uso no arquivo, o que C#
não exige (ordem livre). Mesma família da limitação de método do §5.1 item 7, mas para
tipo/classe; fechar os dois junto exige record/replay de topo, não só de método.

**Dívida do C8:** `p.items[i]` sobre um receptor que o parser NÃO tipa (um parâmetro,
que só o oráculo do `pass()` resolve) não chega ao `[` de array — cai no `[` do `params`
e é recusado com `teko: \`[\` indexes a \`params\` list only`. Recusa clara, nunca
miscompilação; fechar isso é trabalho no `teko_typeof.mc` (C3b, em voo em paralelo).

**Dívidas conhecidas:** `struct`, pacote de `params` e campo `static` de classe sem reclaim
(acima). Fechadas: a arena bump sem reclaim (D218), o `syntax_infix` sobre operador do core
(mc 0.10.3/M41.5 — mas a rota do C5b segue sendo o `pass()`, pelos dois motivos do cabeçalho
de `teko_ops.mc`), o default em função de topo (C6, acima) e o `+` unário sem sítio (M45,
`syntax_expr("+")` + `parse_expr(11)` em `teko_ops.mc`'s `tk_unary_plus`).

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

**Entrega 5 — PREFIXOS VEEM PÓS-FIXOS LANDADO** (32 fixtures, `surface_operator.tk`
estendida): `!b[1]`, `-a.x`, `~g[2]` — `.`/`[` (`syntax_infix`, prec 12) agora binding
mais apertado que `- ! ~`, como em C#. **Não é `syntax_expr("-", ...)`** (a rota do `+`
não se generaliza): medido com um handler forçado a devolver um valor distinto, ele nunca
disparou para `-x` — `parse_unary()` do núcleo acha `-`/`!`/`~` na sua PRÓPRIA tabela de
prefixo (`ops_init`) e resolve o `N_UNARY` ali, sem nunca chegar em `parse_primary` (onde
`syntax_expr` mora). Só `+` reaparece por `syntax_expr` porque `ops_init` nunca o
registrou. A correção real é em `tk_dot`/`tk_bracket` (`ngen/teko_prefix.mc`, novo):
sinkam pela cadeia de `- ! ~` até o operando de verdade, resolvem o `.`/`[` NELE, e
reembrulham o resultado na mesma cadeia — `-a.x` vira o mesmo `N_UNARY(-, DOT(a,x))` que
o núcleo constrói para `-(a.x)` escrito com parênteses. `tk_bracket` ganhou uma guarda
(`tk_bracket_no_write`) para o operando-base de uma cadeia sinkada nunca virar alvo do
deferral de array GLOBAL (`-arr[i]` é valor, nunca lvalue, como em C#). AST das 31
fixtures não tocadas byte-idêntica à base `e2d4d936`; `mc limits` `ok` (`intrin` 8/8,
nada de novo registrado).

**Errata (crumb "adoção do 0.14.1", 2026-09-05):** a guarda `tk_bracket_no_write` só protegia o `[`
de array GLOBAL deferido — `tk_arr_index_of` (array LOCAL, `teko_array.mc`) e `tk_array_index`
(campo-array, `teko_struct.mc`) aceitavam `=`/`+=`/`-=`/`++`/`--` sem consultá-la, e `!b[1] = 3` só
era recusado por acidente (`value of type void` sobre o `!`, ou pior — `call to unknown function`
no site de instanciação de um genérico, no caso de um campo-array). Os dois agora consultam a
guarda antes de aceitar uma escrita e recusam com mensagem própria (`teko: the left side of = is
not a place`); a declaração de `tk_bracket_no_write` mudou de `teko_params.mc` para
`teko_prefix.mc`, seu dono lógico.

**K1 LANDADO (entrega 5, D221/§41, 2026-09-05):** `delegate` nomeado, ponteiro de função tipado,
`callp` tipado, `null`. Arquivos: `ngen/teko_deleg.mc` (novo — a tabela de assinatura `dg_*`,
`tk_delegate`, o wrap `Op f = fn;`/`new Op(fn)` via thunk memoizado por (delegate, função), a
chamada `f(a, b)` rebaixada num `pass()`), `teko_struct.mc` (`TK_KDELEG`, `tk_is_deleg`,
`tk_is_counted` — zero linha nova em `teko_rc.mc` além de generalizar `tk_rc_needed` para
`tk_is_counted` em vez de `tk_is_class`/`tk_is_iface` hardcoded, o que também é o que faz um
programa SÓ com delegate, sem classe, disparar o reclaim), `teko_type.mc` (`tk_null`), `teko_access.mc`
(`public`/`internal delegate` reaproveita `tk_decl_head`, `tk_delegate` só forward-declarado — ele é
definido depois de `teko_typeof.mc`, de quem `tk_deleg_pass` precisa), `teko_expr.mc` (`tk_new`
ganha `new Op(fn)`; `tk_field_use` ganha a chamada de campo de tipo delegate, `h.cb(2, 3)`),
`lib/rt.mc` (`tk_deleg_code`, `panic` — o `panic` de superfície É o `tk_deleg_code` chamando
`rt_panic`, então a fixture de pânico exercita as duas funções novas juntas, não uma morta).

**Desvio do §41, medido e necessário:** `gadd`, referenciado BARE (não numa chamada) dentro de
`namespace geo { GOp f = gadd; }`, não é tocado por `tk_ns_pass` (esse só reescreve `N_CALL`/
`N_ADDR`/um `N_IDENT` de `const` — nunca um `N_IDENT` de FUNÇÃO usado como valor), então
`decl_find("gadd")` falhava depois da renomeação para `geo__gadd`. `tk_deleg_pass` ganhou seu
PRÓPRIO walk (não o `tk_ty_pass_walk` de `teko_typeof.mc`, que não expõe "em que função estou"),
rastreando o namespace de cada função (`tk_ns_of_name`) e reusando os MESMOS `tk_ns_call_try_prefixes`/
`tk_ns_call_try_usings` que `tk_ns_pass` já usa para uma chamada — `tk_deleg_resolve_fn`. `new
Op(fn)` (tempo de parse, antes da renomeação) não precisou do mesmo tratamento.

Fixtures: `surface_delegate.tk` (`expect-exit: 42` — declaração no topo e em `namespace`, `Op f =
add;` contextual e `new Op(mul)` explícita, `f(3, 4)`, delegate como parâmetro (`apply(Op)`) e como
retorno (`choose`), sobrecarga `apply(Op)` vs `apply(i64)`, campo de classe de tipo delegate chamado
(`h.cb(2, 3)`), `null` guardado por `if`, `rt_live()` de volta ao piso depois de cada bloco) e
`surface_panic_null.tk` (`expect-exit: 70`, delegate nulo chamado).

Gate: 34/34 (32 anteriores + as 2 novas); AST das 32 fixtures anteriores **aditiva apenas** —
`diff` mostra SÓ as duas funções novas de `lib/rt.mc` (`panic`/`tk_deleg_code`, herdadas por TODA
fixture via `#include "../lib/rt.mc"`), ZERO linha removida ou alterada nas 29 que incluem `rt.mc`
(as outras 3 — `hello`, `primitives_ptr`, `primitives_scalar` — não incluem `rt.mc` e saem
byte-idênticas); `mc limits ngen` `ok`, `intrin` 8/16 (crescimento 0, nenhum intrínseco novo).
Probes (fora de `tests/`): delegate de 11 parâmetros recusado (`method with too many parameters`);
`new Op()`/`new Op` (sem alvo) recusados (`expression expected`/`expected ( after the delegate
name`); atribuir `add3` (3 parâmetros) a `Op` (2) recusado (`add3 does not match the delegate
Op(i64, i64)`); `delegate` dentro de corpo de função recusado pelo núcleo (`expression expected`,
a palavra não abre declaração de topo ali).

Fila K2→K5 (§41(d)): **K2** `ref`/`out` (dois `type_new`, tabela de apontado, `syntax_expr`,
`tk_ref_pass` ANTES de `tk_deleg_pass`); **K3** `T[]` de heap (parcialmente bloqueado — pede
`syntax_type` ao mc, dívida registrada em §41(e); `new T[n]`/`xs[i]`/`.Length`/`T[]` como campo e
parâmetro não dependem dele); **K4** lambda/função local/`use` (estende `teko_deleg.mc`); **K5**
`foreach` (`teko_loop.mc`).

**Item 0 LANDADO** (entrega 5, 2026-09-05, commit separado antes do K2): `Op f = 5;` compilava
limpo e segfaultava — `tk_deleg_var` só interceptava um inicializador `N_IDENT`. `tk_deleg_coerce`
(`ngen/teko_deleg.mc`) é o validador único que os quatro sítios de um slot de delegate agora usam
(var, atribuição de nome nu, `return`, argumento de chamada não-sobrecarregada): `null`, um valor
já tipado (local/param/campo/retorno/chamada-aninhada-por-delegate, via `tk_deleg_expr_ty`), ou um
nome de função compatível (embrulhado no mesmo thunk de sempre) passam; qualquer outra coisa é
`teko: Op takes a function, another Op, or null`. Zero fixture mudou (`--dump-ast` byte-idêntico);
a atribuição ganhou de graça a coerção de nome de função (`f = mul;` funciona agora). Ver plano §43.

**K2 LANDADO** (entrega 5, D221/§41, 2026-09-05): `ref T`/`out T`, C#'s by-reference, sobre uma
tabela de apontado chaveada pelo NÓ do parâmetro (não `(owner, idx)` — dois desvios medidos, ver
plano §43). `ngen/teko_ref.mc` (novo) — `type_new("ref"/"out", 8, 8, TK_INT)`, a tabela
`tk_rp_add`/`tk_rp_kind`/`tk_rp_pointee`, o mangling `tk_ty_sfx(p)`, o sítio obrigatório
`syntax_expr("ref"/"out")` (`tk_ref_arg`/`tk_out_arg`, tageado por `tk_rfarg_tag` para o validador
de chamada e o casador de sobrecarga distinguirem um endereço-por-`ref` de um valor que só parece
um), o rebaixamento `tk_ref_pass` (ANTES de `tk_deleg_pass`, DEPOIS do oráculo — `x` vira `ldW(x)`,
`x = e` vira `stW(x, e)` exceto pointee CONTADO, deixado para a exceção de `teko_rc.mc`), o
prólogo DPS de `out` contado (`st64(x, 0);`) e a checagem barata "nunca atribuído". `teko_default.mc`
(`tk_default_param` estendido para função livre), `teko_class.mc` (`tk_params` estendido para
método; `tk_sig_of` usa `tk_ty_sfx`), `teko_over.mc` (`tk_ov_sig` idem; `tk_ov_arg_ty`/
`tk_ov_args_fit` ganham a checagem de KIND, o que faz `f(i64)`/`f(ref i64)` resolverem por
sobrecarga; `tk_ov_judge` recusa `f(ref i64)` + `f(out i64)`), `teko_rc.mc` (`tk_rc_assign` ganha a
ÚNICA exceção: um parâmetro `ref`/`out` de tipo contado escreve por `tk_id(name)`, não
`tk_addr(name)`). `teko_typeof.mc` não mudou NADA — `nd_type` do parâmetro já É o apontado por
construção, então `tk_ty_scope_params` já servia de graça.

Achado que exigiu correção (medido): o guard de entrada de `tk_ref_pass` só olhava `tk_nrp`
(parâmetros `ref`/`out` DECLARADOS) — um `ref`/`out` usado só no ARGUMENTO contra um parâmetro POR
VALOR não registra nenhum parâmetro em lugar nenhum, e o pass saía sem tocar a árvore, deixando
`bump(ref a)` contra `void bump(i64 x)` compilar por engano. Corrigido: o guard também olha `tk_nrf`
(argumentos `ref`/`out` escritos); e o passe caminha TODA função, não só uma que DECLARA `ref`/`out`
— quem CHAMA raramente é uma delas.

Fixture: `surface_refout.tk` (`expect-exit: 42`) — `ref` escalar com sobrecarga por valor, `out`
duplo (`split`), `ref` sobre campo e elemento de array local, `ref` em método, `ref` de pointee
CONTADO com `rt_live()`/destrutor provando a troca. Gate: 35/35; `--dump-ast` das 34 anteriores
**byte-idêntico** a `5e83bb3a`; `mc limits ngen` `ok`, `intrin` 8/16 (zero crescimento). Probes (fora
de `tests/`): `f(a)` sem `ref`; `ref` num parâmetro por valor (também para chamada de MÉTODO);
`ref i64 x = 1` (default); `out` nunca atribuído; `f(ref i64)` + `f(out i64)`; `ref x;` como local.

Dívidas: atribuição-por-caminho para `out` (herdada do §41); `f(out i64 a)` inline; `ref p.inner.x`
(mais de um nível); `ref` sobre campo implícito (`this.x`) dentro do próprio corpo do método.

**K3 LANDADO** (entrega 5, D221/§41, 2026-09-05): `T[]` de heap -- o bloqueio do §41(e) caiu
(mc 0.14.2 trouxe `syntax_type`, o irmão de `syntax_param` na posição de TIPO). Um objeto por
elemento distinto, com o MESMO shape de um delegate: vtable de duas palavras (release + a
palavra de itab que ninguém usa), contagem, e daí `len`/os dados -- `rc_dec` libera um array sem
saber nada sobre ele, através da MESMA máquina que já libera classe/interface/delegate
(`tk_is_counted` estendido, zero linha nova em `teko_rc.mc`).

Arquivos: `ngen/teko_struct.mc` (`TK_KARRAY`, `tk_is_ha`, `tk_ha_row` -- memoizado por elemento,
a palavra reservada é a forma com colchetes `"i64[]"`/`"Circle[]"`, um lexema que o lexer nunca
forma, o mesmo truque de `lib/user_typearr.mc` do mc; `tk_ty_mangle_name`, a forma SEGURA pra um
símbolo mangled, `"arr_i64"`, já que a palavra reservada do tipo carrega `[`/`]` e um símbolo não
pode; a tabela `tk_hp_*`, ver abaixo), `ngen/teko_heaparr.mc` (novo -- `tk_ha_type` o handler
`syntax_type`, `tk_new_array`, `tk_ha_index` leitura/escrita/`+=`/`-=`/`++`/`--`, `tk_ha_member_of`
o `.Length` só-leitura, `tk_ha_deleg_call` pra `ops[i](args)` sobre um `Op[]`, e o gerador
vtable/release/alloc por elemento, lazy no primeiro `new`), `lib/rt.mc` (`tk_arr_at(a, i, w)`, o
endereço guardado -- `panic` fora de `[0, Length)`), `teko_expr.mc` (`tk_new`/`tk_member_of`),
`teko_params.mc` (`tk_bracket`), `teko_default.mc`/`teko_ref.mc` (o tipo de um parâmetro passa a
ler por `p_type()` em vez do `type_of_token()`+`p_next()` manual, pra o sufixo `[]` registrar via
`syntax_type`; `teko_class.mc`'s `tk_params` já caía em `p_type()` de graça, por `tk_gen_ty`).

**§5.1 armadilha nova: sítios do módulo que leem tipo precisam de `p_type()` pra ver `[]`.**
`tk_gen_ty()` (teko_generic.mc) JÁ chamava `p_type()` como fallback (campo/parâmetro/retorno de
MEMBRO, de graça) -- mas `teko_default.mc`'s `tk_default_param` e `teko_ref.mc`'s `tk_ref_param`
liam por `type_of_token(p_id())` seguido de um `p_next()` manual, que NUNCA dispara `syntax_type`
(o hook mora dentro de `take_type`, que só o `p_type()` público chama). Um novo `syntax_type` que
não muda esses dois sítios "funciona" pra tipos de membro e não funciona pra parâmetro de função
livre nem pro apontado de `ref`/`out` -- silenciosamente (`i64[] x` vira `i64` seguido de um `[`
sobrando, que `p_ident()` tenta ler como nome e recusa com uma mensagem confusa). Regra: todo
sítio que lê um tipo fora do `p_type()` genérico do núcleo tem que rotear por ele (ou decair
ANTES de saber se é seu) assim que qualquer `syntax_type` for registrado.

**`T[]` PARÂMETRO resolve no PARSE, não no oráculo -- desvio medido do §41(b).** O §41(b) previa
`teko_array.mc`'s `gd_*`/`decl_param_type` como o caminho de um parâmetro `T[]`; medido, esse
caminho corrompe o `p_decl_name()` da PRÓPRIA declaração sendo lida se o gerador (`top_add`) rodar
no meio do parâmetro (`teko_default.mc`'s teste de "nova declaração" leria 0 pro parâmetro
seguinte), e o `N_INDEX`/o placeholder de escrita ficam invisíveis a qualquer passe ANTES do
oráculo pendurados fora da árvore (o mesmo formato do `tk_pend_recv` de `.`). A saída medida:
`teko_struct.mc`'s `tk_hp_*` -- uma tabela PEQUENA, resetada uma vez por declaração
(`tk_hp_reset`, chamada de `tk_default_param`'s "nova declaração" e do topo de `tk_params`) e
preenchida no MESMO instante em que o parâmetro é lido -- então `tk_bracket`/`tk_dot` respondem
`xs[i]`/`xs.Length` sobre um parâmetro exatamente como respondem sobre um local, no PARSE, sem
placeholder e sem passe extra. Provado pelo caso que quebraria a alternativa: `cs[i].area()` sobre
um parâmetro `Circle[] cs` -- se `cs[i]` ficasse como `N_INDEX` cru até o oráculo, `.area()`
deferiria sobre um RECEPTOR sem tipo e resolveria por NOME (`tk_pend_by_name`), o que "funciona"
com uma única classe declarando `area` e mascara silenciosamente a ambiguidade com duas.

Fixtures: `surface_array_heap.tk` (`expect-exit: 42`) -- `n` de runtime, `xs[i]`/`xs[i]=e`/`+=`/
`-=`/`++`, `.Length` como bound de `while` E de `for`, `u8`/`i32` provando largura e sinal, `T[]`
como parâmetro de função livre E de método e como campo (`this.items`), `Circle[]` com
`rt_live()` provando o piso duas vezes (substituir um elemento libera o antigo; liberar o array
libera os três ainda vivos), `Op[]` chamado por índice com uma função nua coagida no slot.
`surface_panic_index.tk` (`expect-exit: 70`) -- índice além do fim.

Gate: 37/37 (35 anteriores + as 2 novas); `--dump-ast` das 35 anteriores -- as 3 que não incluem
`rt.mc` (`hello`, `primitives_ptr`, `primitives_scalar`) **byte-idênticas**; as 32 que incluem
`--dump-ast` com o diff **puramente ADITIVO**: só a nova `tk_arr_at` aparece, `grep '^<'` vazio
nas 35; `mc limits ngen` `ok`, `intrin` 8/16 (zero crescimento), `passes` 13 (zero pass nova --
tudo resolve no parse ou generico via `tk_is_counted`). Probes (fora de `tests/`): `new i64[-1]`
(`a negative array length`, exit 70); `xs.Length = 3` (`is read-only`); `xs[i]` sobre um `uptr`
cru (recusado pelo núcleo, `expression with no codegen`); `i64[][]` (`an array of arrays is not
taught yet`, a checagem lê o SEGUNDO `[` logo após consumir o primeiro, já que `take_type` só
despacha uma vez por posição); `new i64[]` sem tamanho (`` `new T[]` needs a length``); `ref
i64[] x` (recusa limpa, `not a known array` -- `tk_hp_add` só registra o parâmetro na forma
PLANA, nunca em `ref`/`out`, porque o valor de um `ref T[]` é o ENDEREÇO do slot do caller, não o
objeto, e tratá-lo como se fosse um seria silenciosamente errado, não uma dívida honesta).

Dívidas: `T[]` como GLOBAL -- o TIPO é aceito em toda posição (`p_type()`, extern, cast, retorno,
campo, parâmetro), mas leitura/escrita/`.Length` sobre uma GLOBAL de `T[]` não resolvem (nem
`tk_struct_of_expr` nem `teko_array.mc`'s próprias tabelas rastreiam uma global fora do `nd_val`
de tamanho fixo); `ref`/`out T[]`; `params T[]` (já era dívida do §41); `T[][]`/multidimensional;
`p.items[i]` sem `this.` explícito (herdada do limite de campo-array do D219); array de heap como
elemento de outro array de heap.

**K4 LANDADO** (entrega 5, D221/§41, 2026-09-05): lambda, função local nomeada e `use (a, &b)`,
inteiramente sobre a forma EXPLÍCITA `new Op((T a, T b) [use (...)] => corpo)` -- a mesma máquina de
objeto do K1, com um allocator/release/vtable/corpo gerados por lambda (não memoizados por par
como o thunk do K1, já que cada ocorrência pode capturar valores diferentes).

**Desvio medido do §41, decidido por risco de AST (não por preguiça):** o §41(a) decisão 19 previa
DUAS grafias -- contextual (`Op f = (a,b) => e;`, sem `new`) e explícita (`new Op((a,b) => e)`).
Só a EXPLÍCITA foi ensinada. Motivo medido: `parse_primary`'s ramo de `(` (`mc/src/parse.mc:873`)
decide cast-ou-agrupamento pelo PRIMEIRO token após `(` -- `(i64 a, i64 b) => ...` começa
IDENTICAMENTE a um cast `(i64)`, então `parse_expr(0)` sozinho já morre em `expected ) in cast`
antes de qualquer hook rodar. A única forma de alcançar a grafia contextual seria registrar
`syntax_expr("(", &f)` -- o que INTERCEPTARIA TODO `(` de expressão do programa inteiro (grouping E
cast), sem fallback ao núcleo (`docs/reference/hooks.md` § 3: "an expression position has no empty
node to fall back on"), arriscando as 37 fixtures anteriores que usam `(`/cast livremente. A forma
explícita evita isso por inteiro: `tk_new_deleg` já possui seu PRÓPRIO ponto de parse (depois de
`new Op(`), então o `(` de um parâmetro de lambda nunca passa por `parse_primary`. Cobre TODOS os
casos do fixture (inclusive "função local nomeada", que vira `Op twice = new Op((i64 x) => x*2);`)
sem tocar o núcleo. A forma contextual bare e o `x => e` sem parênteses (decisão 19's forma curta,
"só quando o alvo dá o tipo") ficam como DÍVIDA registrada, não código morto -- os dois são
recusados hoje pelo próprio núcleo (`expected ) in cast` / `expected ; after declaration`), nunca
silenciosamente.

**Como o corpo é lido:** NEM record/replay (`p_skip_balanced`+`p_push_source`, o idioma do C8/K3)
NEM `parse_function` puro -- os DOIS, conforme a grafia. A lista de parâmetros da lambda é lida por
`parse_params()` (o mesmo leitor público de `delegate`/função livre, que já dá de graça `ref`/`out`/
`T[]`/defaults em um parâmetro de lambda, embora não exigido pelo §41). O `use (...)` é lido a
seguir, ANTES do `=>` (posição escolhida: única posição sem ambiguidade -- depois dos parâmetros,
antes do corpo). O corpo: `=> { ... }` chama `parse_function(ret, nome, params)` (o body-depth reset
de M31 é dela, não meu); `=> expr` monta `tk_ret(parse_expr(0))`/`tk_stmt(...)` à mão, no estilo do
`tk_deleg_thunk_fn` do K1. `p_set_decl_name`/`p_decl_name` são salvos e restaurados ao redor de
TUDO isso -- a lambda é uma declaração nova por identidade (gensym `Op__lam0`, `Op__lam1`, ...,
`tk_ns_qualify`da como um `delegate`), então a tabela de defaults (`teko_default.mc`'s
`owner != tk_dflt_owner`) e o `tk_hp_reset` do K3 disparam de graça.

**Layout e captura:** o objeto é o MESMO do K1 (`vt/count/code` + N slots de captura, 8 bytes cada),
mas o allocator agora recebe UM PARÂMETRO POR CAPTURA (o valor, para uma por-valor; o ENDEREÇO cru,
para uma por-referência) -- o call site (`new Op(...)`) fornece `tk_id(nome)`/`tk_addr(nome)` NO
INSTANTE da construção, o que É o "congelamento" que D221 decisão 20 pede: uma cópia por-valor
muda de dono na hora (o allocator faz `rc_inc` antes de gravar, se o tipo for contado -- a MESMA
árvore que o release desfaz, um `rc_dec` por captura por-valor contada, nunca por uma por-referência
que é só um endereço). Dentro do corpo gerado, uma captura por-valor vira um LOCAL DE VERDADE
(`T nome = ld<W>(__env+off);`, a mesma largura de `teko_struct.mc`'s `tk_ldn`) -- o reclaim comum
(`teko_rc.mc`) já borrow-to-own e libera no fim da chamada de graça, ZERO código de RC novo aqui.
Uma captura por-referência vira um `uptr` interno (`__lamrefN`) carregando o ENDEREÇO, e toda leitura/
escrita do nome original dentro do corpo é reescrita para `ld`/`st` através dele (`teko_array.mc`'s
`tk_arr_load`/`tk_arr_store`, os MESMOS dois helpers que `teko_ref.mc`'s `tk_ref_walk` já usa para
dereferenciar `ref`/`out`) -- capturar por referência um tipo CONTADO é recusado (dívida honesta,
não silêncio: "a capture by reference of a counted type is not taught yet"), já que o slot seria o
endereço do PONTEIRO do declarante, não o objeto, e um `rt_store` correto ali pediria a mesma
exceção que `teko_rc.mc` já dá a um parâmetro `ref` contado -- generalizar essa exceção para uma
captura fica fora do K4.

**O nome capturado tem que já ser um local:** `teko_struct.mc`'s `tk_on_stmt` (M21.5's hook) ganhou
uma tabela NOVA, `tk_slv_add`/`tk_slv_find` -- toda declaração `N_VAR`, de QUALQUER tipo (não só
struct/class, que é tudo que `tk_local_add` já rastreava), grava (nome, tipo); `use (nome)` consulta
essa tabela NO INSTANTE em que lê a cláusula. **Dívida honesta:** um PARÂMETRO da função declarante
não é capturável hoje (só uma tabela de PARÂMETROS por-declaração resolveria isso, e nenhuma das
existentes -- K2's `rp_*`, K3's `tk_hp_*` -- serve; ficaria para quem generalizar `tk_slv_add` para
o site de `parse_params`/`teko_class.mc`'s `tk_params` também).

**"não capturado" e as duas recusas de escape:** o corpo recém-construído é percorrido por
`tk_lam_walk` (o MESMO desenho de escopo em pilha de `teko_typeof.mc`'s `tk_ty_scope_*`, reusado
diretamente -- zero tabela de escopo nova): todo `N_IDENT` que não é parâmetro/local/captura E não é
`decl_find`/`tk_struct_find` (função ou tipo global) morre em `teko: X is not captured; add it to
use (...)`, a frase exata do §41. As duas recusas da decisão 21 (`&`-captura escapando por `return`
ou por campo) são UMA função, `tk_lam_escapes(e)` -- "`e` é uma chamada ao allocator de uma lambda
com captura por referência" --, chamada nos TRÊS pontos onde um slot de delegate é ESCRITO:
`tk_deleg_return` (K1, já existia), `teko_expr.mc`'s `tk_field_use` (campo de instância) e
`teko_access.mc`'s `tk_static_use` (campo estático) -- a terceira é gratuita (mesma forma de store),
`tk_lam_escapes` é `forward`-declarada nesse arquivo (incluído ANTES de `teko_deleg.mc`). **Não
verificado:** GLOBAL -- é moto por construção, `parse_global`'s próprio `global initializer must be
constant` já recusa qualquer expressão não-literal antes que a checagem exista para checar.

Fixture: `surface_lambda.tk` (`expect-exit: 42`) -- sem captura, `use (k)` por valor congelado
(mutar `k` depois não muda o que a closure lê), `use (&acc)` mutando o local do declarante entre
duas chamadas, duas lambdas do MESMO delegate com capturas diferentes, `Op twice = new Op(...)`
(função local nomeada), lambda guardada num campo e chamada via `this.cb(x)` + lambda passada como
argumento de MÉTODO e chamada lá dentro, captura de um objeto contado (`Box`) com `rt_live()`
provando que o release do closure solta o objeto só depois que a ÚLTIMA referência (closure OU
local) cai, a mesma máquina dentro de um `namespace`, corpo em BLOCO (`=> { ...; return e; }`), e
`new Op(...)` como argumento de uma função LIVRE. Gate: 38/38 (37 anteriores + a nova); `--dump-ast`
das 37 anteriores **byte-idêntico** ao compilador da base `6ddae6a3` (`same=37 diff=0`); `mc limits
ngen` `verdict ok`, zero linha `grew` (o `intrin` bate igual entre base e K4 -- nenhum intrínseco
novo). Probes (fora de `tests/`): `use` de um nome de FUNÇÃO (não é local, recusado); nome livre no
corpo sem `use` (a frase exata); `(i64 x) => e` sem `new Op(...)` (recusado pelo próprio núcleo,
`expected ) in cast`); `x => e` sem parênteses (recusado, `expected ; after declaration`); 11
parâmetros de lambda contra um delegate de 2 (mismatch); `use (k, k)` duplicado; `&`-captura
devolvida por `return` de uma função que retorna o delegate; `&`-captura atribuída a um campo de
instância dentro de um método -- as duas com a MESMA frase de decisão 21.

Dívidas: a grafia CONTEXTUAL (`Op f = (a,b) => e;`, sem `new`) e a forma curta `x => e` (ambas
pedem hookar `(` em posição de expressão -- risco descrito acima, fora de escopo); captura de um
PARÂMETRO da função declarante; captura por referência de tipo CONTADO; `op.Invoke(x)` (herdada);
`Func<>`/`Action<>` (herdada); `params T[]` embalando uma lambda (herdada); alvo-tipagem de lambda
em argumento de função LIVRE sem `new Op(...)` (a forma explícita já cobre esse caso, então esta
dívida do §41(e) está fechada na prática -- `apply(new Op((i64 x) => x - 1), 43)` já funciona).

**K4b LANDADO** (entrega 5, D221/§41, 2026-09-05): fecha as três ressalvas do verificador do K4.

1. **Captura de delegate POR VALOR quebrava.** O prólogo gerado (`Op inner = ld64(addr);`) é um
   `N_CALL` que `tk_deleg_coerce` (a mesma passada `tk_deleg_pass`) recusava. Corrigido marcando o
   nó do `ld64` com o tipo do delegate (`tk_xt_put`), o mesmo idioma que `tk_field_use` já usa para
   um load de campo delegate — o nó é o FINAL (nada o copia depois), então a marcação resolve sem
   tocar `tk_deleg_coerce`.
2. **A recusa de `&`-captura escapando era por FORMA LITERAL, não por TAINT.** `tk_lam_escapes` só
   reconhecia o `N_CALL` do alocador — `Op f = new Op(...) use (&acc) => ...; return f;`
   (indireção por variável) e `cb = new Op(...) use (&x) => ...;` com `this` implícito (não passava
   por `tk_field_use`) escapavam sem aviso. Corrigido com um taint flow-insensitivo por NOME (um
   novo `on_stmt`, `tk_lam_taint_stmt`, marca o nome escrito por um `N_VAR`/`N_ASSIGN` cujo lado
   direito já escapa) — `tk_lam_escapes` aceita agora o `N_CALL` OU um `N_IDENT` tainted, checado em
   `return`, campo explícito, campo estático, campo implícito via `this` (`tk_this_assign`, novo) e
   elemento de `T[]` (`tk_ha_index`, novo). Array FIXO de delegate não precisou de check — já é
   inalcançável (`teko_array.mc` recusa qualquer linha da tabela de tipos como elemento).
3. **Grafia contextual e curta.** `(` de expressão continua SEM hook (o risco do §46 seguiu
   correto). A contextual entra nos pontos que já conhecem o tipo alvo antes do inicializador:
   `Op f = <init>;` (`tk_type_stmt`, teko_access.mc, desviando para `tk_deleg_var_stmt` quando o
   tipo é um delegate ESCALAR — `Op[] ops` cai fora via `tk_bracket_follows`) e um argumento de
   MÉTODO sem sobrecarga (`tk_call_method_args`/`tk_args_typed`, teko_expr.mc, gated por
   `tk_method_name_count(si, m) == 1` — picar overload por nome só, sem contar argumentos ainda,
   não dá para saber qual sinatura vale). A decisão `(`-é-lambda usa um LOOKAHEAD NÃO-CONSUMIDOR
   (`tk_paren_lambda_follows`, varredura de bytes a partir de `p_cp()`) em vez de
   `p_skip_balanced`+`p_push_source`: medido que o push descarta o lookahead pendente (o `=>` que a
   decisão precisa) assim que uma fonte nova é empurrada — `p_push_source` certo é para replay
   DEPOIS que a decisão já foi tomada por outro meio, não para decidir. `tk_lambda_build` virou um
   wrapper fino sobre `tk_lambda_finish` (o rabo compartilhado), reusado pela forma curta
   (`tk_deleg_short_lambda`, um parâmetro implícito do tipo que o delegate já declara).

Fixture: `surface_lambda.tk` ganha `deleg_byval_check` (item 1), `contextual_check`/`short_check`
(item 3) e uma chamada contextual em `method_check`. Item 2 não ganha fixture — as recusas ficam em
probes fora de `tests/`. Gate: 38/38; `--dump-ast` das 37 anteriores byte-idêntico à base `68b38174`
(`same=37 diff=0`); `mc limits ngen` `verdict ok`, `intrin` 8/16 em ambos os lados.

**Dívidas que seguem abertas** (não fechadas por este crumb, registradas): `return (params) => e;`
(o `return` é palavra do núcleo, sem hook — fica `return new Op(...)`); captura por referência de um
tipo CONTADO (herdada do K4); captura de um PARÂMETRO da função declarante (herdada do K4);
`op.Invoke(x)`/`Func<>`/`Action<>`/`params T[]` embalando lambda (herdadas do §41(e)).

## 5.1 Armadilhas já pagas (não repita)

1. **`mc --exe` emite Mach-O SEMPRE.** `minicompiler/mc` `src/main.mc:227` faz
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

16. **Um lookup com fallback nunca chama a si mesmo (mesma função) sobre uma string que ELE
    PRÓPRIO construiu.** `tk_struct_find` (namespace, entrega 5 N1) ganhou um fallback
    (`tk_ns_resolve`) que tenta candidatos qualificados; um candidato que falha chama de volta a
    função "exact + fallback" original — e o próximo candidato é sempre MAIOR que o anterior
    (mais um prefixo), nunca repete o argumento, então a recursão nunca bate uma base e nunca
    converge. Sintoma: `EXC_BAD_ACCESS`/`SIGSEGV` no meio do parse, sem mensagem — só visível com
    `lldb bt` (a pilha mostra as duas funções alternando centenas de vezes). Correção: separar o
    scan puro (`tk_struct_find_exact`, sem fallback) e fazer todo sítio que testa uma string
    CONSTRUÍDA internamente (o próprio `tk_ns_resolve`, o reopen check de `partial`, uma busca por
    nome já manglado) chamar a versão exata — só o sítio que lê o que a FONTE escreveu chama a
    versão com fallback.

17. **Um prefixo do NÚCLEO nunca vê o pós-fixo do MÓDULO.** `parse_unary()` checa a
    própria tabela de prefixo (`ops_init`: `- ~ ! &`) ANTES de `parse_primary` — onde
    `syntax_expr` mora — e lê o operando por recursão direta em `parse_unary()`, que nunca
    consulta `.`/`[` (`syntax_infix`, prec 12). `!b[1]` chega no `[` já como `N_UNARY(!,
    b)`: o núcleo devolveu o unário ANTES de o `[` do módulo ter a chance de aparecer.
    Registrar `syntax_expr("-", ...)` não conserta nada — é código morto, medido com um
    handler forçado a devolver um valor distinto que nunca disparou para `-x`. Só `+`
    escapa dessa armadilha (M45's `tk_unary_plus`) porque `ops_init` nunca o registrou, daí
    ele cai em `parse_primary` como um token comum. A correção é do lado do PÓS-fixo, não
    do prefixo: `tk_dot`/`tk_bracket` sinkam pela cadeia de `- ! ~` que RECEBERAM como
    `left`, resolvem contra o operando de verdade, e reembrulham (`ngen/teko_prefix.mc`).

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
