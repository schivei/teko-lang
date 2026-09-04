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
- **Entrega 3 (tipos) — PARCIAL: `struct` e `class` LANDADOS** (SHAs
  `984f268e` e `06db615d`). Cada um define a tabela de tipos compartilhada
  (campos de offset/size/layout em registro, vtable para class). Registrados
  com `type_new(nome, 8, 8, TK_INT)` — identidade estática preservada para `.`
  resolver membro. `interface` e `trait` ainda são honest-stop.
- **CI VERDE, com execução real** (Linux x86_64, ~15 s ponta-a-ponta): baixa
  `mc-0.10.0-linux-x86_64`, confere checksum, `mc build ngen` constrói o
  compilador ensinado `build/mc-teko`, e **7 fixtures compilam, linkam e
  RODAM**: `hello.tk` + `primitives_{float,ptr,scalar,str}.tk` +
  `types_{struct,class}.tk`, todas com exit 42.

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

Hoje isso dá **7/7 em exit 42**. `ngen/mc.macos.toml`, os `ngen/mc.*.toml` transientes
e `ngen/build/` **nunca se commitam**, e `ngen/mc.toml` fica **intacto** — alterá-lo
quebra o CI.

**O `mc` NÃO emite C.** Ele emite objeto nativo e linka; não existe passo de `gcc`
sobre saída do compilador ensinado. Compile sempre por `mc build DIR --config FILE`
(ver armadilha 1 do §5.1).


## 5. Próximo passo — entrega 3: TIPOS (continuação)

**Struct e class LANDADOS** (commits `984f268e` + `06db615d`); `interface` e
`trait` seguem como honest-stop. A sessão remota **parou de despachar** ngen
para a local assumir sem colisão.

**Próxima fase: `interface`.**
- Precedente: `examples/api/oop.mc` no repo do mc (~482 linhas,
  classes+interfaces+methods sem generics pesadas — modelo limpo e certo).
- Mapeamento e hook: `docs/design/port-teko-mc.md` §3.
- `interface` traz **conformance automática** (`type conforms_to`) e
  **dispatch via itab** (method indirection) — ambos já proven no mc.

**`trait` — FORK ABERTO:** não há precedente no mc nem forma mapeada em
port-teko-mc.md. Decisão e forma precisam de input do dono antes de iniciar.

**Enquanto isso:** a forma genérica `<T>` de `wrap`/`unwrap` também destrava
(precisa de generics record/replay — vem junto dos tipos; aguarda M41/M40 do mc).

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
6. **O core NÃO reporta tipo de PARÂMETRO ao módulo.** Quando o tipo estático
   do receptor é desconhecido (chamada cross-unit), o `.` resolve por NOME; dois
   tipos não-relacionados com mesmo nome → erro de compilação limpo (não leitura
   silenciosa em offset errado). Verificado com programa hostil.
7. **Ordem de declaração:** método só chama métodos ACIMA dele (mesma limitação
   do `examples/lang`); consertar exige record/replay. Planejado pra release
   seguinte do mc.
8. **Sem construtor com argumentos** (`new Nome` apenas) e sem `base.m()` —
   ainda não ensinados. Fila de D215.
9. **Achado no repo do mc (não confirmado):** `examples/lang/lang_expr.mc:42-44`
   reutiliza nó do receptor em `ld64` vtable e na lista de args; método virtual
   de aridade ≥1 quebraria. No `ngen/` está contornado por clonagem com guarda.

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
