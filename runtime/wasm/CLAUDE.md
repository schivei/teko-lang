# CLAUDE.md — `runtime/wasm/` (WASM harness)

See the root `CLAUDE.md` for the backend design and CI. This is the host-side test harness.

## Important: no `.tks → .wat` pipeline yet
The CLI (`src/main.c`) emits from mock bytecode, not real source. So executable proof drives
the emitter **directly**: each `emit-demo/*.c` links `build/libteko_core.a`, builds an IL byte
program, and calls `teko_metal_emit_program` to write a `.wat`. `wat2wasm` assembles it; the
runners execute and assert. (Locally, `npm i wabt` gives a JS `wat2wasm` to validate output.)

## Layout
- `emit-demo/` — C drivers: `emit_spawn_channel.c` (→7), `emit_suspend.c` (→30),
  `emit_multi.c` (→15), `emit_threads.c` (Layer B →99), `emit_ffi.c` (Browser FFI MVP-1:
  `(import "env" "log")` + `OP_CALL_IMPORT` + pooled string), `emit_dom.c` (MVP-2: multi-arg
  `dom.*` imports + `OP_SETARG` + auto-generated `.glue.mjs`). Output `.wat`/`.glue.mjs` gitignored.
- `samples/` — hand-written reference fixtures: `channels.wat` (42), `scheduler.wat` (15),
  `threads.wat` (Layer B reference, 777). `emitted*.wat`/`*.wasm` are generated/gitignored.
- `run-node.mjs`, `run-browser.mjs` — Layer A under Node / headless Chromium.
- `run-ffi.mjs` — Browser FFI MVP-1 proof: supplies host `env.log(ptr)`, reads the
  NUL-terminated pooled string back from exported memory, asserts "hello from teko".
- `run-dom.mjs` + `browser/dom.html` + `browser/dom-run.mjs` — Browser FFI MVP-2 proof
  (Playwright, COOP/COEP): Teko builds a `<span>"hello from teko"` and appends it to `#out`
  entirely through the auto-generated `dom.*` glue; the harness asserts `#out > span` text.
  Strings cross as `(ptr,len)`; DOM nodes as `i32` handles (Teko→JS read direction only).
- `threads/` (`run-node-threads.mjs`, `runner.mjs`, `worker.mjs`) — Layer B via
  `worker_threads`. `browser/threads-*.mjs` + `run-threads-browser.mjs` — Layer B via Web Workers.
- `server.mjs` — static server with COOP/COEP (required for SharedArrayBuffer / Layer B).

## Layer B note
`main()` blocks, so it runs in a **runner worker** (the node main thread must stay free to
bootstrap the producer). The channel **receive busy-polls** the flag (bounded, `unreachable`
cap) — cross-instance `memory.atomic.notify` is unreliable on the GitHub runner; plain shared
atomic loads are not. Harnesses always carry a watchdog so a hang fails fast.
