---
seq: 0128
crumb-id: NAT-B4
milestone: M4
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RT-ENTRY]      # entry _start / stack_ptr; rides F7a + entry-native (0125)
sources:
  - "docs/design/native-lowering-cobertura-zero-libc-0.3.1.md:0-999"   # §2 eixo-A, §3 P3.3/P3.4
  - "src/codegen/codegen.tks:9606"                                      # cg_emit_thread_clone_helper_text_x86_64 (x86 done)
  - "src/codegen/codegen.tks:9447-9451"                                 # aarch64 #error arch guard
  - "src/lir/lower.tks:6684-6730"                                       # native_entry_stub / wrap_native_entry
  - ".crumbs/0126-NAT-B0-graceful-stop-capture.md:0-999"               # capture = graceful-stop (owner 2026-08-26); not touched here
---

# 0128 · NAT-B4 — espelho NATIVE das emissões próprias da campanha zero-libc

> Escrever (write-only) o lado native de cada emissão C que a campanha ELA MESMA produziu e ainda não
> tem espelho: clone-aarch64 (hoje `#error`), `stack_ptr`/`_start` per-OS (0125), e o glue de
> spawn-mmap. Capture native é construído EM CIMA do que o agente D45/0064 escreve — NÃO redesenhar.
> Native write-only; rides os reseeds F7a + entry-native (0125). Byte-preserving na rota C.

## Goal

O eixo-A do §16 já emite C novo (F7 threads, F8 capture, entry-native). A lei "todo C expressável em
native" exige o espelho ESCRITO. O que falta (verificado @ `4f488589`): (1) o helper `thread_clone`
existe só em x86_64 (`codegen.tks:9606`), aarch64 é `#error` (`:9451`); (2) o `stack_ptr` intrínseco e
o `_start` naked per-OS estão desenhados (0125) mas a EMISSÃO native (isel `mov reg,sp`) precisa ser
escrita; (3) o glue de spawn (thread stack via SYS_MMAP/VirtualAlloc) precisa do espelho native. Os
`cov_*`/`load_u8`/`store_u8`/syscallN JÁ têm espelho (`lower.tks:1771/1786/1970-1984`) — nada a fazer.
Capture unwind native é do agente D45; aqui só se garante que a cobertura o inclui.

## Where

- `src/codegen/codegen.tks:9393-9451` — o braço aarch64 dos helpers: escrever
  `cg_emit_thread_clone_helper_text_aarch64` (espelho do `_x86_64` em `:9606`) com o `svc #0` do
  `clone` (syscall 220 no arm64) + o trampolim de entrada (empilha entry/arg, `br`); remover o
  `#error` de arch quando aarch64 fechar.
- `src/lir/lower.tks:6684-6730` `native_entry_stub`/`wrap_native_entry` — a LFunc `_start` per-OS: Linux
  primeiro inst = intrínseco `stack_ptr`; macOS/Windows uma LFunc que chama os externs de ABI (0125).
- `src/backend/isel_x86_64.tks` / `isel_arm64.tks` / `minst*.tks` — baixar `stack_ptr` a
  `mov reg,%rsp` / `mov reg,sp` como primeiro inst frameless do `_start` (Linux native).
- `src/backend/objfile_elf.tks` / `objfile_macho.tks` / `objfile_coff.tks` — símbolo de entrada:
  ELF `_start`; Mach-O LC_MAIN; PE `AddressOfEntryPoint`/`/entry:`.
- Capture: o mecanismo é REDESENHADO como graceful-stop (0126) — controle de fluxo puro, MESMA lógica
  nas duas rotas; o native NÃO precisa de unwind/PC-SP-restore. Este crumb não reescreve o capture (0126
  o faz); só garante que a cobertura o registra como control-flow, não como jump/unwind-machinery.

## How

1. **clone-aarch64 (P3.4).** Espelho do helper x86_64 em asm-inline aarch64: `mov x8, #220` (clone),
   `svc #0`, teste do retorno, no filho `ldp`/`br` para o trampolim. Emitido como STRING (mesmo
   mecanismo do `_x86_64`). Remover o `#error` `codegen.tks:9451` quando fechar.

```teko
/**
 * cg_emit_thread_clone_helper_text_aarch64 — the aarch64 mirror of the x86_64 raw-`clone` helper: a
 * naked `svc #0` (syscall 220) trampoline that stacks entry/arg, clones onto the fresh stack, and in
 * the child `br`s to the entry then `svc`-exits. Emitted as a preamble string, guarded by the aarch64
 * `#elif` arm; removes the `#error` at `codegen.tks:9451` once landed.
 *
 * @return the C preamble text of the aarch64 `tk_thread_clone` helper
 * @since 0.3.1
 */
fn cg_emit_thread_clone_helper_text_aarch64(): str
```

2. **`stack_ptr` intrínseco native (P3.3).** No `native_entry_stub`, primeiro inst do `_start` Linux =
   `stack_ptr` LInst; isel baixa a `mov reg,%rsp`/`mov reg,sp`. Único ponto onde as duas rotas divergem
   na EMISSÃO; o valor flui para o `start_linux` idêntico (0125).
3. **`_start`/entry per-OS native.** ELF default `_start`; Mach-O LC_MAIN (entra da libSystem, lê
   `_NSGet*`); PE `/entry:` (kernel32). O corpo Teko (`start_run`/`start_linux`/`start_macos`/
   `start_windows`) é o mesmo das duas rotas — só o stub de entrada difere (0125).
4. **spawn-mmap native.** O glue de stack-de-thread (`thread.tks` SYS_MMAP + SYS_MPROTECT) é chamada de
   syscall/extern — já lowerável pelos intrínsecos existentes; confirmar o threading do retorno e o
   `thread_clone` intrínseco (já em `scope.tks`/`lower`).
5. **Capture = graceful-stop (0126), não aqui.** O capture native é controle de fluxo puro (check-flag
   + defers/drops + return), escrito em 0126 nas duas rotas idênticas; este crumb não o toca — só
   confirma que a cobertura o trata como control-flow normal, sem unwind-machinery.

## Rulings & laws

- **Teko-only:** `src/codegen/*.tks` (emit), `src/lir/*.tks` + `src/backend/*.tks` (native); asm vai
  como STRING emitida (mecanismo dos helpers de syscall/clone). C-twin congelado intocado.
- **Comment convention (W15):** `/** */` só em `exp`; sem `//`; doc nunca maior que o código.
- **Fork protocol:** entry per-OS + `stack_ptr` + clone-aarch64 são ratificados (0125, D101 ABI-por-SO,
  D105 clone-arm64 = T3). macOS thread-create = `pthread_create from "System"` (D105-T2, Apple proíbe
  clone cru) — NÃO clone no mac. Sem fork; NÃO HALT.
- **Native write-only:** ESCREVER o clone-aarch64 / `stack_ptr` / `_start` native; NÃO rodar
  `TEKO_BACKEND=native` — a validação de runtime é pós-F9.
- **Capture separado (0126):** o capture (graceful-stop, dono 2026-08-26) é escrito em 0126 nas duas
  rotas; este crumb não o toca.
- **Safety:** NUNCA `teko test .`; build subshell `ulimit -v 4718592`; reseed só no [fixpoint] de F7a/
  entry-native; `gen2==gen3` (rota C) byte-idêntico; MEM_PARANOID 0; ratchet D100.

## Fixtures

`none — the fixpoint self-build exercises this` — a rota C do clone-aarch64/entry compila no self-build
(o CI arm64/mac/win prova a compilação); a EMISSÃO native é write-only (medida por "gen1 emite gen2
native completo", não executada). Sem fixture afirmativo novo (lei do dono).

## Gate

`[fixpoint]` — rides F7a (clone/spawn) + entry-native 0125 (`stack_ptr`/`_start`). "Green" = o
`#error` de arch some (aarch64 clone escrito); o `native_entry_stub` emite `_start` per-OS + `stack_ptr`
baixado a `mov reg,sp`; gen1-emite-gen2-native não para nesses construtos; reseed de fase C
`gen2==gen3` byte-idêntico. Reseed-class: `fixpoint-rebuild`.

## Deps

`RT-ENTRY` (0125) — o entry per-OS e o `stack_ptr` são a fundação; clone-aarch64 rides F7a.

## Done when

O clone-aarch64, o `stack_ptr`/`_start` per-OS native e o spawn-mmap têm espelho native ESCRITO (não
rodado); o capture native (D45) está coberto sem ser redesenhado; gen1-emite-gen2-native passa desses
construtos nas 4 pernas; reseed de fase byte-idêntico.
