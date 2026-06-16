// Phase 14 (14.G) — timespan waiters (`wait`/`await`) on the WASM target. The teko binary
// compiles samples/waiters.tks to a module whose OP_WAIT/OP_AWAIT_FOR lower to the host imports
// env.teko_sleep / env.teko_await (the ms delay, already unit-normalized to canonical
// milliseconds at compile time) plus a cooperative scheduler drain ($teko_sched_run) for await.
// This host captures those calls and asserts: (1) the await drained the queued worker AT the
// await point (output order 1,2,3 — not the at-exit 1,3,2), and (2) the host observed the exact
// normalized ms (await 5ms -> 5, wait 10ms -> 10). No reactor: the module owns its memory.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const out = [];
const sleeps = [];
const awaits = [];
const env = {
  log_int: (n) => { out.push(n | 0); },
  teko_sleep: (ms) => { sleeps.push(ms | 0); }, // record (no real sleep — deterministic)
  teko_await: (ms) => { awaits.push(ms | 0); },
};

const sample = await readFile(here("./samples/waiters.wasm"));
const teko = await WebAssembly.instantiate(sample, { env });
teko.instance.exports.main();

const okOrder  = JSON.stringify(out) === JSON.stringify([1, 2, 3]);
const okAwait  = JSON.stringify(awaits) === JSON.stringify([5]);   // 5ms normalized
const okSleep  = JSON.stringify(sleeps) === JSON.stringify([10]);  // 10ms normalized
if (okOrder && okAwait && okSleep) {
  console.log(`OK   waiters(wasm): await drained worker (order ${JSON.stringify(out)}), ` +
              `await=${JSON.stringify(awaits)}ms wait=${JSON.stringify(sleeps)}ms (normalized)`);
  process.exit(0);
} else {
  console.error(`FAIL waiters(wasm): out=${JSON.stringify(out)} (want [1,2,3]), ` +
                `awaits=${JSON.stringify(awaits)} (want [5]), sleeps=${JSON.stringify(sleeps)} (want [10])`);
  process.exit(1);
}
