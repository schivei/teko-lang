// Phase 17.F.4 — the decimal LANGUAGE SURFACE + casts + auto-`to_string` on the WASM target.
//
// decimal_surface.tks is compiled by the teko BINARY to a WASM module that, beyond the 17.F.3 value
// model, lowers `decimal.to_string` (id 59) / `decimal.parse` (id 60) and the int/float ↔ decimal
// casts (OP_I2D/F2D/D2I/D2F) to the reactor's teko_rt_decimal_from_*/to_*/to_string/parse over the
// SHARED linear memory (the SAME C teko_decimal.c source of truth as native). A decimal in a `+`
// concat / `"{…}"` hole auto-`to_string`s via id 59; mixed `int + decimal` promotes the int (I2D).
// This harness instantiates the reactor + the sample against one shared memory, runs `main`, and
// asserts the output is BYTE-FOR-BYTE identical to the native proof (samples/decimal_surface.tks).
//
// decimal_fail.tks proves the CHECKED/fail-loud path: `decimal.parse("abc")` traps the reactor
// (__builtin_trap -> a WebAssembly RuntimeError), the WASM analogue of native exit 70 — after the
// `before` line, never reaching the post-parse emit.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout (runtime/native/samples/decimal_surface.tks).
const expected = [
  "10.00",
  "total = 10.00",
  "[10.00]",
  "3.50",
  "grand = 15.00",
  "bumped = 13.00",
  "2.5",
  "int 10",
];

function makeEnv(memory, out) {
  return {
    memory,
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
}

async function instantiate(sampleFile, out) {
  const memory = new WebAssembly.Memory({ initial: 64 });
  const env = makeEnv(memory, out);
  const reactorBytes = await readFile(here("./crypto/crypto.wasm"));
  const reactor = await WebAssembly.instantiate(reactorBytes, { env });
  const sample = await readFile(here(sampleFile));
  const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
  return teko.instance;
}

// --- happy path: byte-identical to native ---------------------------------------
{
  const out = [];
  const inst = await instantiate("./samples/decimal_surface.wasm", out);
  inst.exports.main();
  if (JSON.stringify(out) !== JSON.stringify(expected)) {
    console.error("FAIL decimal_surface:");
    for (let i = 0; i < Math.max(out.length, expected.length); i++) {
      const g = out[i], w = expected[i];
      console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
    }
    process.exit(1);
  }
  console.log(`OK   decimal_surface: ${out.length} lines matched native byte-for-byte (decimal.to_string/parse + casts + auto-to_string, reactor-backed)`);
}

// --- fail-loud: decimal.parse("abc") traps, after emitting `before` -------------
{
  const out = [];
  const inst = await instantiate("./samples/decimal_fail.wasm", out);
  let trapped = false;
  try {
    inst.exports.main();
  } catch (e) {
    trapped = true; // a WebAssembly.RuntimeError from the reactor's __builtin_trap
  }
  if (!trapped) {
    console.error("FAIL decimal_fail: expected a trap on decimal.parse(\"abc\"), but main() returned normally");
    process.exit(1);
  }
  if (JSON.stringify(out) !== JSON.stringify(["before"])) {
    console.error(`FAIL decimal_fail: expected pre-trap stdout ["before"], got ${JSON.stringify(out)}`);
    process.exit(1);
  }
  console.log("OK   decimal_fail: trapped after `before` (checked decimal.parse, byte-identical pre-trap to native exit 70)");
}

process.exit(0);
