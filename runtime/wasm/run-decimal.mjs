// Phase 17.F.3 — the 256-byte exact base-10 `decimal` VALUE MODEL on the WASM target.
//
// decimal.tks is compiled by the teko BINARY to a WASM module that carries decimals end to end
// through a SEPARATE 256-byte memory-slot accumulator: fixed $d0/$d1 regions in Teko's linear
// memory (carved from the heap top, gated on wasm_emit_decimal so decimal-free modules stay
// byte-identical) + a $dvN decimal local file. `dec` literals → a (data ...) pool blob + DCONST
// (`memory.copy` into $d0); arithmetic/compares → `call $decimal_add/$decimal_cmp` to the reactor's
// teko_rt_decimal_* over the SHARED memory (i32 slot offsets passed BY POINTER — the SAME C
// teko_decimal.c source of truth as native, fail-loud on overflow/divzero). There is no decimal
// FORMATTER yet (that is 17.F.4), so each comparison's 0/1 result is funneled back through
// `convert.int_to_str` (id 49, reactor-backed) + `emit`. This harness instantiates the reactor +
// the sample against one shared linear memory, runs `main`, and asserts the output is
// BYTE-FOR-BYTE identical to the native proof (runtime/native/samples/decimal.tks) — proving the
// decimal value model lowers consistently on both targets. The exactness is the point: 9.99 + 0.01
// == 10.00 holds in base-10 decimal (it does NOT in binary f64).
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout (eq = 1 / lt = 1).
const expected = ["eq = 1", "lt = 1"];

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
const sample = await readFile(here("./samples/decimal.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL decimal:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   decimal: ${out.length} lines matched native byte-for-byte (256-byte decimal value model, reactor-backed)`);
process.exit(0);
