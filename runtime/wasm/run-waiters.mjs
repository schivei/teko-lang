// Phase 14 (real-time clock) — timespan waiters (`wait`/`await`) on the WASM target. The teko
// binary compiles samples/waiters.tks to a module whose OP_WAIT/OP_AWAIT_FOR read the host
// MONOTONIC ns clock (env.teko_now_ns) and spin cooperatively until the real deadline — `await`
// also drains the in-module scheduler ($teko_sched_run). Because the time source is REAL (not a
// logical clock), this asserts a LOWER BOUND on real elapsed time + the cooperative interleave
// order — not exact counters (the clock is non-deterministic). No reactor: the module owns memory.
//
// teko_now_ns = process.hrtime.bigint() (real ns). The sample: routines{worker}; log 1; await 5ms
// (drains -> worker logs 2); log 3; wait 10ms. So order is [1,2,3] and real elapsed >= ~15ms.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const out = [];
const env = {
  teko_now_ns: () => process.hrtime.bigint(), // real monotonic nanoseconds
  log_int: (n) => { out.push(n | 0); },
};

const t0 = process.hrtime.bigint();
const sample = await readFile(here("./samples/waiters.wasm"));
const teko = await WebAssembly.instantiate(sample, { env });
teko.instance.exports.main();
const elapsedMs = Number(process.hrtime.bigint() - t0) / 1e6;

const okOrder = JSON.stringify(out) === JSON.stringify([1, 2, 3]); // await drained the worker
const okTime  = elapsedMs >= 12; // await 5ms + wait 10ms ≈ 15ms; lower bound 12ms (clock tolerance)
if (okOrder && okTime) {
  console.log(`OK   waiters(wasm): real-time await+wait, order ${JSON.stringify(out)}, ` +
              `elapsed ${elapsedMs.toFixed(1)}ms (>= 12ms, real monotonic clock)`);
  process.exit(0);
} else {
  console.error(`FAIL waiters(wasm): out=${JSON.stringify(out)} (want [1,2,3]), ` +
                `elapsed=${elapsedMs.toFixed(1)}ms (want >= 12ms)`);
  process.exit(1);
}
