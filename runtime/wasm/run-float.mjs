// Phase 17 (17.A) — the f64 VALUE MODEL on the WASM target.
//
// float.tks is compiled by the teko BINARY to a WASM module that carries floats end to end through
// a PARALLEL float accumulator: `(local $f0 f64)`/`(local $f1 f64)` + a `$fvN` float local file,
// additive to the integer `$w0`/`$w1`. Float literals → `f64.const`, arithmetic → `f64.add/mul`,
// compares → `f64.gt/ge` (i32 0/1 → $w0), int→float promotion → `f64.convert_i32_s`. There is no
// float FORMATTER yet (that is 17.C/17.D), so each comparison's 0/1 result is funneled back through
// `convert.int_to_str` (id 49, reactor-backed — the SAME teko_convert.c source of truth as native)
// + `emit`. This harness instantiates the reactor + the sample against one shared linear memory,
// runs `main`, and asserts the output is BYTE-FOR-BYTE identical to the native proof
// (runtime/native/run-native.sh) — proving the float value model lowers consistently on both
// targets.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout (a = 0 / b = 1 / c = 1).
const expected = ["a = 0", "b = 1", "c = 1"];

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  // The reactor links the whole runtime; stub the host facts it imports so it instantiates.
  teko_now_ns: () => process.hrtime.bigint(),
  teko_now_unix: () => 1000000000n,
  teko_tz_offset: () => 0,
  teko_random: (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = (i * 7 + 1) & 0xff;
  },
  emit: (ptr) => {
    const u = new Uint8Array(memory.buffer);
    let e = ptr >>> 0;
    while (u[e] !== 0) e++;
    out.push(dec.decode(u.subarray(ptr >>> 0, e)));
  },
};

const reactorBytes = await readFile(here("./crypto/crypto.wasm"));
const reactor = await WebAssembly.instantiate(reactorBytes, { env });
const sample = await readFile(here("./samples/float.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL float:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   float: ${out.length} lines matched native byte-for-byte (f64 value model, reactor-backed)`);
process.exit(0);
