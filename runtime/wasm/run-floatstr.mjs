// Phase 17 (17.D) — `convert.float_to_str` (id 50) + auto-`to_string` for floats on the WASM target.
//
// floatstr.tks is compiled by the teko BINARY to a WASM module that formats f64 values through id
// 50 — the ONLY OP_CALL_RUNTIME id whose argument is a `double`: the import is declared
// `(func $crypto_50 (param f64) (result i32))` and the call lowers `local.get $f0 / call $crypto_50
// / local.set $w0` (the value rides the parallel float accumulator $f0, the result char* lands in
// $w0). The formatter is the 17.C Ryu shortest-round-trip core (teko_convert_f64.c), now EXPORTED
// from the compiled-C reactor (crypto.wasm) as teko_rt_float_to_string — the SAME source compiled
// for native, so the bytes are byte-for-byte identical to the native proof (run-native.sh). This
// harness instantiates the reactor + the sample against one shared linear memory, runs `main`, and
// asserts the output matches the native bytes (auto-to_string in concat — both right- and
// left-float — interpolation with a float hole + a float-EXPRESSION hole, and the explicit surface).
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout (the ACTUAL Ryu formatter bytes).
const expected = ["f = 3.14", "0.1", "pi~ 3.14, dbl 6.28", "2.5", "3.14 = pi"];

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
const sample = await readFile(here("./samples/floatstr.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL floatstr:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   floatstr: ${out.length} lines matched native byte-for-byte (float->string id 50, f64-arg ABI)`);
process.exit(0);
