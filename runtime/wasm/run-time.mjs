// Phase 14 (wall-clock / timezone surface) — OS-sourced civil time on the WASM target. The teko
// binary compiles samples/time.tks to a module whose time.* (OP_CALL_RUNTIME ids 44-48) import
// teko_rt_time_* from the compiled-C reactor (crypto.wasm); the reactor gets the current timestamp
// + the DST-correct local offset from the host imports env.teko_now_unix / env.teko_tz_offset and
// shares this module's memory. The portable civil-time FORMATTER is the same C source of truth as
// native. To keep the proof deterministic (wall time is non-deterministic), the host clock is
// STUBBED: now = 1000000000 s, local offset = 0 (UTC). env.emit reads result strings from memory.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const readCStr = (ptr) => {
  const u = new Uint8Array(memory.buffer);
  let e = ptr >>> 0; while (u[e] !== 0) e++;
  return dec.decode(u.subarray(ptr >>> 0, e));
};
const env = {
  memory,
  teko_now_unix: () => 1000000000n,        // stubbed real clock (i64) -> deterministic
  teko_tz_offset: (_epoch) => 0,           // stubbed local offset (UTC) -> deterministic
  teko_now_ns: () => process.hrtime.bigint(),
  teko_random: (ptr, len) => { const u = new Uint8Array(memory.buffer); for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0; },
  emit: (ptr) => { out.push(readCStr(ptr)); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/time.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [
  "2001-09-09T01:46:40Z",   // format_utc("1000000000")
  "2001-09-09T01:46:40Z",   // format_local (host offset 0)
  "1000000000",             // now_unix (stubbed)
  "2001-09-09T01:46:40Z",   // now_utc (stubbed)
];
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   time(wasm): OS-sourced civil time via reactor + host clock ${JSON.stringify(out)} (stubbed deterministic)`);
  process.exit(0);
} else {
  console.error(`FAIL time(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
