// Phase 17 (17.B) — checked int↔float casts + float modulo on the WASM target, incl. the
// FAIL-LOUD path.
//
// cast.tks / cast_fail.tks are compiled by the teko BINARY to WASM modules. `convert.to_float(int)`
// lowers to f64.convert_i32_s (OP_I2F), `convert.to_int(float)` to i32.trunc_f64_s (OP_F2I — which
// TRAPS on NaN/±Inf/out-of-i32-range, the fail-loud guarantee), and float `%` to the inline IEEE
// remainder a-trunc(a/b)*b (OP_FMOD). The happy module funnels each comparison's 0/1 result through
// convert.int_to_str (id 49, reactor-backed — the SAME teko_convert.c source of truth as native) +
// emit, so it must be BYTE-IDENTICAL to the native proof (runtime/native/run-native.sh). The fail
// module uses NO reactor id (just emit + F2I), so it declares its own memory; it must emit "before"
// then TRAP — proving the cast fails loudly (a value that aborts non-zero on native also traps
// here), never silently truncating/wrapping.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// `getMem` lets emit read from whichever memory the module actually uses — the shared env.memory
// (reactor-backed modules import it) or the module's own exported memory (the float-only fail module
// declares + exports its own).
function makeEnv(getMem, out) {
  return {
    teko_now_ns: () => process.hrtime.bigint(),
    teko_now_unix: () => 1000000000n,
    teko_tz_offset: () => 0,
    teko_random: (ptr, len) => {
      const u = new Uint8Array(getMem().buffer);
      for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = (i * 7 + 1) & 0xff;
    },
    emit: (ptr) => {
      const u = new Uint8Array(getMem().buffer);
      let e = ptr >>> 0; while (u[e] !== 0) e++;
      out.push(dec.decode(u.subarray(ptr >>> 0, e)));
    },
  };
}

const reactorBytes = await readFile(here("./crypto/crypto.wasm"));
let ok = true;

// 1) Happy path — reactor-backed (id 49), shares env.memory; byte-identical to the native proof.
{
  const memory = new WebAssembly.Memory({ initial: 64 });
  const out = [];
  const env = { memory, ...makeEnv(() => memory, out) };
  const reactor = await WebAssembly.instantiate(reactorBytes, { env });
  const sample = await readFile(here("./samples/cast.wasm"));
  const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
  teko.instance.exports.main();
  const expected = ["a = 1", "n = 7", "m = 1"];
  if (JSON.stringify(out) !== JSON.stringify(expected)) {
    console.error("FAIL cast (happy):");
    for (let i = 0; i < Math.max(out.length, expected.length); i++)
      console.error(`  [${i}] ${out[i] === expected[i] ? "ok  " : "DIFF"} got=${out[i]} want=${expected[i]}`);
    ok = false;
  } else {
    console.log(`OK   cast: ${out.length} lines matched native byte-for-byte (to_float / to_int / fmod)`);
  }
}

// 2) Fail-loud path — converting 3e9 (> INT32_MAX) to int must TRAP (not silently truncate/wrap).
//    This module uses no reactor id, so it owns + exports its memory; emit reads that.
{
  const out = [];
  let inst = null;
  const env = makeEnv(() => inst.exports.memory, out);
  const sample = await readFile(here("./samples/cast_fail.wasm"));
  const teko = await WebAssembly.instantiate(sample, { env });
  inst = teko.instance;
  let trapped = false;
  try { inst.exports.main(); }
  catch (e) { trapped = e instanceof WebAssembly.RuntimeError; }
  if (!trapped) {
    console.error(`FAIL cast_fail: expected a trap (fail-loud), but main() returned. emitted=${JSON.stringify(out)}`);
    ok = false;
  } else if (JSON.stringify(out) !== JSON.stringify(["before"])) {
    console.error(`FAIL cast_fail: expected stdout ["before"] before the trap, got ${JSON.stringify(out)}`);
    ok = false;
  } else {
    console.log(`OK   cast_fail: emitted ["before"] then TRAPPED on out-of-i32-range float (fail-loud)`);
  }
}

process.exit(ok ? 0 : 1);
