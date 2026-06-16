// Phase 14 (14.I) — REAL concurrent producer/consumer on the WASM target. The teko binary compiles
// samples/producer_consumer.tks to a module where a background routine takes MULTIPLE arguments
// (Go-style): a shared duplex channel handle + a count. The duplex channel lives in the compiled-C
// reactor (crypto.wasm) over shared memory; the producer task (run at the await) fills it and the
// consumer drains it with a poll loop. This host wires the reactor + the sample against one shared
// memory, supplies the waiter host imports, captures env.log_int, and asserts the sum (1..5 = 15).
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  teko_random: (ptr, len) => { const u = new Uint8Array(memory.buffer); for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0; },
  teko_now_ns: () => process.hrtime.bigint(), // real monotonic ns — the await's time base
  teko_now_unix: () => 1000000000n, teko_tz_offset: () => 0, // reactor civil-time host clock (stubbed)
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/producer_consumer.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const ok = JSON.stringify(out) === JSON.stringify([15]);
if (ok) {
  console.log(`OK   producer_consumer(wasm): multi-arg task (handle+count) shared a channel; consumer drained ${JSON.stringify(out)} (1..5=15)`);
  process.exit(0);
} else {
  console.error(`FAIL producer_consumer(wasm): got ${JSON.stringify(out)}, expected [15]`);
  process.exit(1);
}
