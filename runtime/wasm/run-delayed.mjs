// Phase 14 (14.C) — delayed (timed) channel on the WASM target. The teko binary compiles
// samples/delayed.tks to a module whose OP_DELAYED_* ops import teko_rt_delayed_* from the
// compiled-C reactor (crypto.wasm) and share ONE linear memory with it — the SAME delayed C
// runtime as the native runner (runtime/native/samples/delayed.tks). This host instantiates
// the reactor + the sample against shared memory, captures env.log_int, and asserts the
// delivery-time-ordered release as the logical clock advances.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  teko_random: (ptr, len) => { // reactor (crypto) import; unused by delayed — stub
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/delayed.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [1, 10, 20, 30]; // NOT_READY, then time-ordered release
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   delayed(wasm): NOT_READY then time-ordered release ${JSON.stringify(out.slice(1))} (reactor-backed)`);
  process.exit(0);
} else {
  console.error(`FAIL delayed(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
