---
seq: 0126
crumb-id: NAT-B0
milestone: M4
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RT-L6]        # rides the F8/RT-L6 (0064) capture reseed; SUPERSEDES the D45 longjmp C-leg
sources:
  - "docs/design/native-lowering-cobertura-zero-libc-0.3.1.md:0-999"   # §2 eixo-A, §3 P3.1/P3.2
  - "src/codegen/codegen.tks:3215-3253"                                 # emit_capture_panic / emit_capture_longjmp (to REWRITE)
  - "src/codegen/codegen.tks:3513-3514"                                 # capture dispatch
  - "DECISION_LOG.md:455"                                               # D45 (SUPERSEDED here by the owner graceful-stop ruling)
  - "docs/design/arena-em-teko.md:0-999"                                # per-task capture area (panic-pending flag lives here)
---

# 0126 · NAT-B0 — capture/exit/cancel = GRACEFUL-STOP (unwind cooperativo por return; elimina longjmp)

> Redesenho do dono (2026-08-26): exit/panic/cancel **NÃO PULAM**. O setjmp/longjmp salta direto pro
> frame de captura ignorando os `defer`/`drop` intermediários → deixa memória não-limpa e ponteiros
> abertos. O novo mecanismo é **controle de fluxo puro**: um flag de panic-pendente (na área de captura
> por-task, já em `arena.tks`) + após cada call que pode entrar em panic, `if (panicking()) { <roda
> defers/drops do frame>; return <sentinela>; }`; o frame de captura testa/consome. **Elimina
> `__builtin_setjmp`/`__builtin_longjmp`** — portável Linux/Windows/Mac de graça, zero-libc. **Supersede
> o C-leg longjmp de D45/0064.** Byte-mover → `fixpoint-rebuild`; rides o reseed do F8/RT-L6 (0064).

## Goal

O capture cooperativo de D45 pousou com `__builtin_setjmp`/`__builtin_longjmp` — mecanismo ERRADO por
design (salta, não limpa) e insuportável no clang macOS arm64 (`teko.c:191627` nem compila; sintoma da
causa-raiz). O dono já deliberou o desenho certo (não é fork): **graceful-stop** — o panic/cancel sobe
na árvore como um `return` que borbulha, cada frame rodando seus defers/drops antes de retornar, até o
frame de captura consumir. É o MESMO caminho que um return normal já usa para rodar defers/drops —
reusa, não inventa. Elimina toda a maquinaria de jump/PC-SP-restore. As duas rotas (codegen C e
`lower.tks` native) emitem a MESMA lógica de controle de fluxo — nada de unwind especial no native.

## Where

- `src/codegen/codegen.tks:3215` `emit_capture_panic` — REESCREVER: em vez de `__builtin_setjmp` +
  `jb[5]`, emitir o frame de captura como um bloco que chama o corpo e, ao retornar, TESTA o flag
  `panicking()` e consome (lê a mensagem via `capture_msg`, limpa o flag).
- `src/codegen/codegen.tks:3249` `emit_capture_longjmp` — REESCREVER→REMOVER: `capture_longjmp` deixa de
  existir como jump; `panic`/`exit`/`cancel` passam a SETAR o flag panic-pendente e retornar (o
  bubbling é emitido nos call-sites, não num builtin de salto).
- `src/codegen/codegen.tks` — no lowering de STATEMENT de cada rota (após cada call que pode entrar em
  panic, dentro de uma função que roda sob captura): emitir o check `if (panicking()) { <defers/drops
  do frame>; return <sentinela>; }`. Reusa a maquinaria de defer/drop que o return normal já emite.
- `src/lir/lower.tks` — a MESMA lógica no native: o check-flag + cleanup + return é controle de fluxo
  normal (branch + os defers/drops que `lower.tks` já emite no return). **Some qualquer honest-stop /
  primitiva de "unwind"/PC-SP-restore** — não há.
- `src/runtime/arena.tks` (ou o bloco de controle por-task) — o flag `panic_pending` + `panic_msg`
  vivem na área de captura por-task que já existe; helpers `panicking()`/`set_panic()`/`take_panic()`.

## How

1. **Flag por-task.** No bloco de controle por-task (o mesmo `_Thread_local` que ancora arena/task,
   `teko_rt`→Teko), dois campos: `panic_pending: bool` + `panic_msg: str`. `panic(m)` = `set_panic(m)`
   e retorna; NÃO aborta, NÃO salta.

```teko
/**
 * panicking / set_panic / take_panic — the per-task graceful-stop flag. `set_panic` records a pending
 * panic/cancel and returns (never jumps); `panicking` is tested after each call that may panic so the
 * caller runs its defers/drops and returns; `take_panic` is the capture frame consuming the pending
 * message and clearing the flag. Lives in the per-task control block (no setjmp/longjmp, no libc).
 *
 * @return  panicking(): whether a panic/cancel is pending on this task
 * @since 0.3.1
 */
fn panicking(): bool
```

2. **Bubbling nos call-sites.** Após cada call que pode entrar em panic, dentro de uma função que
   participa da captura, emitir (as DUAS rotas, idêntico): `if (panicking()) { <run frame defers/drops>;
   return <zero/sentinela do tipo de retorno>; }`. Os defers/drops são EXATAMENTE os que o return normal
   já roda — reusa `DeferCtx`/o caminho de drop existente.
3. **Frame de captura.** `capture_panic(body)` chama `body()`; ao retornar, `if (panicking()) { return
   take_panic() }` (a mensagem) `else { return null }` — consome o flag. É a única fronteira que PARA o
   bubbling.
4. **exit/cancel.** `exit(code)` e `cancel` seguem o MESMO caminho (setam pendente + código/motivo) —
   sobem limpando cada frame até o topo (ou até um capture que os consuma), então terminam via
   `rt_exit` no `_start` tail (0125). Nada de `atexit`/salto.
5. **Remover o jump.** Apagar a emissão de `__builtin_setjmp`/`__builtin_longjmp` e o `void *jb[5]`
   (`codegen.tks:3220-3252`). Zero builtin, zero `<setjmp.h>`, zero libc.

## Rulings & laws

- **Desenho do dono (2026-08-26), já deliberado — NÃO é fork:** graceful-stop cooperativo; exit/panic/
  cancel sobem como `return` rodando defers/drops por frame; nada pula. **SUPERSEDE** o setjmp/longjmp
  de D45/0064 (registrar a supersessão no DECISION_LOG ao drenar).
- **Zero-libc / portável:** controle de fluxo puro compila nos 3 SOs sem builtin (mata o erro macOS
  `__builtin_longjmp` na raiz, não com asm).
- **As duas direções idênticas:** codegen C e `lower.tks` native emitem a MESMA lógica; o native não
  precisa de unwind-machinery — é branch+defer+return normal.
- **Reusa defer/drop:** o unwind é o caminho que o return já roda; não inventa mecanismo.
- **Teko-only:** `src/codegen/*.tks` + `src/lir/*.tks` + `src/runtime/arena.tks` (área por-task Teko).
- **Comment convention (W15):** `/** */` só em `exp`; sem `//`; doc nunca maior que o código.
- **Native write-only:** ESCREVER o native; NÃO rodar `TEKO_BACKEND=native`.
- **Safety:** NUNCA `teko test .`; build subshell `ulimit -v 4718592`; reseed só no [fixpoint] do
  F8/RT-L6; `gen2==gen3` byte-idêntico; MEM_PARANOID 0 (o graceful-stop LIMPA cada frame → menos
  vazamento, não mais); sweep `.tkt`.

## Fixtures

`none — the fixpoint self-build exercises this` — o harness do compilador usa `capture_panic` e o
self-build o compila nas 4 pernas (macos-arm64 hoje RED em `__builtin_longjmp` → passa a compilar). O
RUNTIME (panic realmente disparado + defers rodando) só sob `teko test` (pós-marco) — sem fixture
afirmativo novo (lei do dono). A limpeza-por-frame é raciocinada + provada por MEM_PARANOID no build.

## Gate

`[fixpoint]` — rides o reseed do F8/RT-L6 (0064). "Green" = `capture_panic` sem `__builtin_setjmp/
longjmp` (grep vazio no `teko.c`); `bootstrap/teko.c` compila nas 4 pernas incl. macos-arm64;
`gen2==gen3` byte-idêntico; MEM_PARANOID 0; `nm -u` sem `_setjmp`/`longjmp`. Reseed-class:
`fixpoint-rebuild`.

## Deps

`RT-L6` (0064) — o capture é parte do F8/RT-L6; este crumb REDESENHA o mecanismo que 0064/D45 landou errado.

## Done when

exit/panic/cancel sobem como return graceful (defers/drops por frame, flag por-task, zero setjmp/
longjmp), C e native emitem a MESMA lógica, `teko.c` compila nas 4 pernas incl. macos-arm64, e o reseed
do F8 é `gen2==gen3` byte-idêntico com MEM_PARANOID 0.
