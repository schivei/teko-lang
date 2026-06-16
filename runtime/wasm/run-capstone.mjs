// Phase 14 capstone (14.H) — the whole phase in one real program on the WASM target. The teko
// binary compiles samples/capstone.tks to a module that combines: a named background routine with
// a LOOP inside it (14.A + control flow), `while` loops + reassignment in main, an atomic
// accumulator + a delayed channel imported from the compiled-C reactor over shared memory (14.E +
// 14.C), and the `await`/`wait` host-import waiters (14.G). This host wires the reactor + the
// sample against one shared memory, supplies the waiter host imports, captures env.log_int, and
// asserts: atomic sum 1..5 = 15; delayed drain 1+2+3 = 6; the worker loop runs at the await
// (1,2,3); final marker 42 -> [15, 6, 1, 2, 3, 42].
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  teko_random: (ptr, len) => { const u = new Uint8Array(memory.buffer); for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0; },
  teko_now_ns: () => process.hrtime.bigint(), // real monotonic ns — the waiters' time base
  teko_now_unix: () => 1000000000n, teko_tz_offset: () => 0, // reactor civil-time host clock (stubbed)
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/capstone.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [15, 6, 1, 2, 3, 42];
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   capstone(wasm): atomic+delayed+routine-loop+waiters ${JSON.stringify(out)} (reactor + scheduler)`);
  process.exit(0);
} else {
  console.error(`FAIL capstone(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
