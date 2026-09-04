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
- **Entrega 1 (fatia vertical) — VERDE.** `ngen/` com `mc.toml`, `teko.mc`
  (registro `user_init`), `teko_{type,class,stmt,expr}.mc`, `lib/rt.mc`,
  `tests/hello.tk`, e o CI `.github/workflows/ngen.yml`.
  Ensinado até aqui: **`bool`** (`type_alias` → `TY_U8`) e honest-stops para
  `class`/`type`/`interface`/`namespace`/`import`/`using`, `var`/`const`/
  `match`/`when`, `new`. Todo o resto vem do core do mc.
- **Prova de CI (primeiro verde):** baixa `mc-0.10.0-linux-x86_64`, confere o
  checksum, `mc build ngen` constrói o compilador ensinado `build/mc-teko`,
  compila `tests/hello.tk` **com ele**, e o binário sai com **exit 42**.
  Pipeline inteiro em **~11 s**.

## 4. O que a sessão local pode fazer que a remota NÃO pode

A sandbox remota **não consegue rodar o `mc`**: a rede para o GitHub está
bloqueada (403) e o `make bootstrap-linux` do mc exige um `mc` pré-existente
como semente (chicken-and-egg documentado no próprio mc). Lá só dá para
validar **estaticamente** com o `mc0` (o `make stage0` funciona) e depender do
CI como gate real.

**Localmente tu tens o `mc` de verdade** — então o loop é:

```sh
mc build ngen        # da raiz do repo; ou `mc build` de dentro de ngen/
ngen/build/teko-hello; echo $?    # tem que dar 42
```

Comando e opções: `docs/build.md` do repo do mc (seção `[compiler]`).
Isso remove o round-trip de push, e é onde a sessão local rende mais —
sobretudo nos construtos que pedem muita iteração (classes/interfaces).

## 5. EM VOO agora (não duplicar)

- **Entrega 2 — primitivas**, por um agente remoto, na branch
  `feat/ngen-primitivas`: `str` (view sem cópia, D197), `char` (`u32`),
  `isize`/`usize`, `byte`, `ptr`/`uptr` + `wrap`/`unwrap`, e `f32`/`f64` via a
  biblioteca `<float>` do mc (M24).
- **Próxima (entrega 3) — tipos:** `class`, `struct`, `interface`, `trait`.
  Precedente a copiar: `examples/lang/lang_class.mc` e `lang_type.mc` do mc.

Antes de começar, **confira se a entrega 2 já foi drenada** para
`fix/retirement` (`git log --oneline -- ngen/`), para não colidir.

## 6. Consultar a sessão remota

A sessão remota coordenadora guarda o histórico completo da virada (por que o
port existe, o que aposenta, o que já foi decidido) e é quem drena para as duas
branches:

<https://claude.ai/code/session_01VX6NuV7RoBLyW6tBCrwEde>

Consulte-a quando: (a) aparecer um **fork de design** que o `DECISION_LOG` e a
doc de Port não resolvam; (b) houver dúvida sobre se algo **aposenta** ou se
porta; (c) for preciso drenar/alinhar as branches.

## 7. Gate do fecho

O port **começou** (o gatilho M24/floats do mc disparou), mas **só fecha quando
o mc chegar a 1.0.0** — o cálculo automático de arena (M13) e o restante da fila
do mc vêm antes. Até lá: crescer o ensino da superfície, sempre por baixo,
sempre com o CI verde.
