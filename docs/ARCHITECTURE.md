# Teko Compiler Architecture

> Canonical reference for the compiler's internal structure and pipeline.
> For the *roadmap* (what is built and what is planned) see [`docs/plan.md`](./plan.md);
> for outstanding maintenance work see [`../TECH_DEBT_BACKLOG.md`](../TECH_DEBT_BACKLOG.md).
> Last updated 2026-06-13.

Teko is an ahead-of-time (AOT) systems language with its own backend: it lowers
source to a compact intermediate language (IL), which can either run on a built-in
development VM or be emitted directly as native machine code for each target — with
no dependency on LLVM/GCC/MSVC for codegen, and a self-contained native linker
(`tld`) that writes ELF / Mach-O / PE-COFF / WASM binaries.

## Pipeline overview

```
  .tks / .tkp source
        │
        ▼
  ┌───────────┐   tokens     ┌───────────┐   AST      ┌──────────────┐
  │  Lexer    │ ───────────▶ │  Parser   │ ─────────▶ │  Semantics / │
  │           │              │ (modular) │            │ Type checker │
  └───────────┘              └───────────┘            └──────┬───────┘
   lexer.c                    parser*.c                       │ validated AST
   lexer_string.c             parser.c                        ▼
                                                       ┌──────────────┐
                                                       │  IL codegen  │  OpCode ISA
                                                       │ (.tkb bytes) │  (codegen_li.h)
                                                       └──────┬───────┘
                                                  codegen_li.c │ teko_il.c
                                  ┌───────────────────────────┴───────────────────────────┐
                                  ▼ (development / `teko run`)                              ▼ (release / AOT)
                          ┌───────────────┐                                       ┌──────────────────┐
                          │  Bytecode VM  │                                       │  Metal backend   │
                          │  switch/      │                                       │  orchestrator    │  fold/DCE/CSE
                          │  computed-goto│                                       └────────┬─────────┘
                          └──────┬────────┘                                  codegen_metal.c │ routes per (os,arch)
                  vm_core.c +    │ runtime/                                                  ▼
                  vm_* + runtime/*                                            ┌───────────────────────────┐
                                                                              │  16 per-target emitters   │  asm / WAT text
                                                                              │  emit_*  (apple/linux/    │
                                                                              │  bsd_unix/windows/bare)   │
                                                                              └────────────┬──────────────┘
                                                                                           ▼
                                                                              ┌───────────────────────────┐
                                                                              │  Native linker `tld`      │  ELF / Mach-O /
                                                                              │  tld_elf/macho/pe/wasm    │  PE-COFF / WASM
                                                                              └────────────┬──────────────┘
                                                                                           ▼
                                                                                   native executable
```

The CLI driver (`src/main.c`) orchestrates this in batch and can stop at
intermediate stages via flags (`-S` for textual assembly, `-c` for an unlinked
object), or link third-party `.s` files directly.

## Stage-by-stage, with key files

### 1. Frontend — lexing
- `src/lexer.c` / `src/lexer.h` — tokenizer and the reserved-keyword table.
- `src/lexer_string.c` / `.h` — string literals, interpolation, and multiline/raw strings.

### 2. Frontend — parsing (modular, one concern per file)
- `src/parser.c` / `src/parser.h` — core driver, token lookahead (`current`/`peek`), program entry.
- `src/parser_statements.c` — functions, variable declarations, `for`, statement blocks.
- `src/parser_types.c` — complex type info (`map<str, mut i32>`, nullables, `func<>`).
- `src/parser_ffi.c` — `extern struct` / `extern fn ... from "lib" as "Alias"` / `extern { ... }` / inline C.
- `src/parser_generics.c` — generic type parameters (`<T, U>`) and `where` constraints.
- `src/parser_extensions.c` — `extend(self: T) { ... }` methods and operator overloads.
- `src/parser_di.c` — dependency-injection constructs (interfaces, services, decorators).
- `src/parser_messaging.c` — CQRS handlers (command/query/notification).
- `src/parser_concurrent.c`, `src/parser_async_control.c` — channels, `await`, `when`/inline switch, `raised` catch.
- `src/parser_visibility.c` — visibility modifiers (`pub`, `exp`, project/service restrictions).
- `src/parser_string.c` — string-expression parsing helpers.

### 3. Semantics
- `src/type_checker.c` — type inference/unification, assignment & mutability rules, async-flow validation.
- `src/symbol_table.c` — scopes and symbol metadata (e.g. `is_mutable`).
- `src/semantic_concurrent.c`, `src/semantic_struct.c` — concurrency and struct semantic checks.

### 4. Intermediate Language (IL)
- `src/codegen_li.h` — the IL ISA: the `OpCode` enum (e.g. `OP_ICONST`, `OP_ADD`, `OP_ARENA_PUSH`, `OP_SPAWN_ASYNC`).
- `src/codegen_li.c` — walks the validated AST and emits the linear IL bytecode buffer.
- `src/teko_il.c` / `.h` — serializes the IL to the compact `.tkb` binary (magic header, constant pool, definitions, opcodes).

### 5a. Development VM (`teko run`)
- `src/vm_core.c` — the interpreter dispatch loop. Fast path uses computed gotos (GCC/Clang); a portable `switch` fallback is compiled for MSVC and any compiler without labels-as-values (guarded by `TEKO_VM_PORTABLE_DISPATCH`).
- `src/vm_arena.c`, `src/vm_scheduler.c`, `src/vm_concurrency.c`, `src/vm_intrinsics.c`, `src/vm_debug.c` — region allocator, M:N green-thread scheduler, channels, intrinsics, and the debug engine.
- `src/runtime/teko_arena.c`, `teko_channel.c`, `teko_thread.c`, `teko_mem_sys.c` — the embedded native runtime (O(1) arenas, channels, cooperative threads, raw page allocation via `mmap`/`VirtualAlloc`).

### 5b. AOT backend (release)
- `src/codegen_aot.c` — transpile-to-C path (emits a standalone C file with an embedded arena runtime).
- `src/codegen/codegen_metal.c` — the "Metal" orchestrator: applies constant folding, dead-code elimination, and common-subexpression elimination over the IL, then **routes each opcode to the right emitter** based on `(os, arch)`.
- `src/codegen/codegen_opt.c` — optimization helpers.

### 6. Per-target emitters (16)
Each implements `void emit_X(MetalContext*, OpCode, int32_t)` — a `switch (op)` that writes the architecture's assembly (or WAT) text:
- `src/codegen/apple/` — `emit_darwin_arm64`, `emit_darwin_x86_64`
- `src/codegen/linux/` — `emit_linux_{x86_64,x86,arm64,arm32,riscv64,riscv32,mips,ppc64}`
- `src/codegen/bsd_unix/` — `emit_freebsd_{x86_64,arm64}`
- `src/codegen/windows/` — `emit_win_{x86_64,x86,arm64}`
- `src/codegen/bare_metal/` — `emit_wasm` (WebAssembly Text)

### 7. Native static linker (`tld`)
Writes object/executable formats directly to disk — no external linker:
- `src/codegen/tld_elf.c`, `tld_elf_arch.c`, `tld_elf_reloc.c` — ELF32/64 headers, machine-opcode dictionary, relocations.
- `src/codegen/tld_macho.c` — Mach-O executables and dylibs.
- `src/codegen/tld_pe.c` — Windows PE/COFF.
- `src/codegen/tld_wasm.c` — binary `.wasm` (LEB128).
- `src/codegen/tld_symbols.c` — symbol resolution and static dependency injection.

### Tooling & support
- `src/main.c` — the CLI compiler driver (`teko build`, `-S`, `-c`).
- `src/teko_target.c` / `.h` — host detection and `--target=` triple parsing.
- `src/project_manager.c` — `.tkp` project manifest parsing and structure validation.
- `src/teko_sdk.c`, `src/teko_lsp.c` — virtual SDK intrinsics (`@`/`teko::`) and the Language Server.
- `sdk/*.tks` — the Teko-side standard-library surface (`strings`, `lists`, `flows`, `marshall`, `fs`, `logger`, `sync`).

## Tests
Unity-based, built into the `teko_tests` target (see `CMakeLists.txt`). Frontend/semantic
tests live in `tests/test_*.c`; backend emission and linker tests in `tests/codegen/`; the
embedded runtime in `tests/runtime/`. The per-architecture emitter output is asserted
conservatively (substrings) in `tests/codegen/test_codegen_{linux,apple,unix,windows,embedded}.c`
plus `test_codegen_emitters_arithmetic.c`.
