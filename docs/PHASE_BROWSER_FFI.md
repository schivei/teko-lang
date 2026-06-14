# Phase — Browser FFI / JS-DOM Interop (design & plan)

> Branch: `feat/browser-ffi-interop`. Goal: when targeting browser WASM, give Teko an
> ergonomic, low-friction two-way bridge to JS/DOM. This document is the design + the
> incremental plan; MVP-1 (foundation) is implemented first.

## Roadmap numbering (confirmed — Phase 11; `docs/plan.md` renumbered)

This is a dedicated **Phase 11 — Browser FFI / JS-DOM Interop**; the memorandum roadmap
was pushed **+1** in `docs/plan.md` (former Phases 11–19 → 12–20):

| Was | Now |
|-----|-----|
| Phase 10 — WASM Concurrency Backend | Phase 10 (unchanged, merged) |
| — | **Phase 11 — Browser FFI / JS-DOM Interop (NEW)** |
| Phase 11 — Frontend Grammar & Lexer Extension | Phase 12 |
| Phase 12 — Advanced Concurrency, Signaling & Duplex Channels | Phase 13 |
| Phase 13 — Bare-Metal OOP | Phase 14 |
| Phase 14 — Optionals & Compile-Time Metaprogramming | Phase 15 |
| Phase 15 — Native Networking, Web & Cryptography | Phase 16 |
| Phase 16 — Enterprise Parsers & Template Compiler | Phase 17 |
| Phase 17 — Interoperability & Rich Metadata (`.teko_meta`) | Phase 18 |
| Phase 18 — Native Testing (`.tkt`) & Coverage | Phase 19 |
| Phase 19 — Self-Containment (Self-Hosting) | Phase 20 |

`docs/plan.md` (intro, ASCII diagram, headers, Documentation Map, cross-refs) is renumbered
accordingly; Self-Containment is now Phase 20.

## Current state (verified in code, June 2026)

- `extern fn … from "ns" as "name"` is **parsed** (`src/parser_ffi.c`) into a full
  `FFIFunctionNode` (fn_name, return_type, alias, params[]) but **discarded** after
  visibility wrapping (`src/parser_visibility.c:99`) — nothing lowers it.
- The IL has a deduplicated `ConstantStringPool` in `BytecodeBuffer`
  (`src/codegen_li.h`), but it is **siloed**: the metal emitter
  (`teko_metal_emit_program(ctx, bytecode, size)`) only receives a raw byte array, so
  `OP_SCONST` emits a placeholder `i32.const (idx*32)` and the only `(data …)` is a
  hardcoded `"Hello Teko"`.
- There is **no call opcode** and **no real `.tks → .wat` driver** (`main.c` emits from
  a mock bytecode array). Executable proof therefore drives the emitter directly via an
  `emit-demo` (the pattern established in Phase 10).

## Architecture (target)

- **Teko → JS/DOM (imports):** `extern fn print(x: i32) from "env" as "log"` lowers to a
  WASM `(import "env" "log" (func $import_0 (param i32)))` and a new `OP_CALL_IMPORT`.
  `@dom`/`@js` intrinsics become imports in a `dom`/`js` namespace, wired by an
  auto-generated JS glue that marshals strings (a `(ptr,len)` pair over linear memory),
  DOM-node handles (an integer handle table on the JS side), and events (a Teko
  function-table index registered as a listener).
- **JS → Teko (exports):** named `pub fn` exports + a JS facade.
- **Strings:** the constant pool is emitted as a real `(data …)` segment; `OP_SCONST`
  yields the actual byte offset. JS reads `(ptr,len)` via `TextDecoder`.

## Incremental plan

- **MVP-1 — foundation (this PR).** ← *in progress*
  - String pool → real `(data …)` segment; `OP_SCONST` → correct offset (thread the
    pool through `MetalContext`).
  - `OP_CALL_IMPORT` + an import table in `MetalContext`; emit `(import "ns" "name"
    (func …))` in the prologue and `call $import_N` at the call site.
  - Executable proof: an `emit-demo` builds IL that imports a host `log` and calls it;
    Node/wasmtime capture the call. Golden tests pin the `(import …)` and `(data …)`.
  - Native emitters (the 16) must not regress; the new opcode is below the native label
    threshold and ignored by them.
- **MVP-1b — frontend lowering (gated).** Lower the parsed `extern` AST → import table
  + a call-expression IL path so a real `.tks` (once a source driver exists) emits the
  import automatically. Needs the call-expression IL path, which does not exist yet.
- **MVP-2 — DOM (gated).** `dom` import namespace + JS handle table + auto-generated
  glue; a Playwright test that creates/updates a DOM element from Teko.
- **MVP-3 — events/callbacks (gated).** JS→Teko via the function table (listeners).
- **MVP-4 — ergonomic facades (gated).** Generated JS facade + Teko-side `string`
  ergonomics; a real allocator (the bump arena does not free).

Discipline: 1 increment per commit; Release + ASan/UBSan on both dispatch paths; native
emitter goldens unchanged; the 4 CI workflows green; patient watcher; no merge/force-push.
