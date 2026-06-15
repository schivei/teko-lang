// Phase 13 Sub-phase C — CSPRNG-via-host-entropy proof (Node). The module under test is
// produced by the teko BINARY compiling runtime/wasm/samples/random.tks; `random.bytes(n)`
// lowers to the in-module $teko_random_hex wrapper, which calls the host import
// `env.teko_random(ptr, len)` for entropy and hex-encodes the result in-module. This host
// fills the buffer DETERMINISTICALLY (buf[i] = i & 0xff) so the emitted hex is exactly
// predictable — an executable KAT of the import ABI + the in-module hex encoding. (In
// production the embedder fills `env.teko_random` with real CSPRNG bytes.)
import { readFile } from "node:fs/promises";

const wasm = await readFile(new URL("./samples/random.wasm", import.meta.url));
let mem = null;
const got = [];
const dec = new TextDecoder();
const imports = {
  env: {
    // Deterministic counter fill so the proof asserts exact bytes.
    teko_random: (ptr, len) => {
      const u = new Uint8Array(mem.buffer);
      for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = i & 0xff;
    },
    emit: (ptr) => {
      const u = new Uint8Array(mem.buffer);
      let end = ptr >>> 0;
      while (u[end] !== 0) end++;
      got.push(dec.decode(u.subarray(ptr >>> 0, end)));
    },
  },
};
const { instance } = await WebAssembly.instantiate(wasm, imports);
mem = instance.exports.memory;
instance.exports.main();

const expected = [
  "000102030405060708090a0b0c0d0e0f", // random.bytes(16), counter fill
  "00010203",                         // random.bytes(4),  counter fill
];
if (JSON.stringify(got) === JSON.stringify(expected)) {
  console.log(`OK   random: ${got.length} vectors matched — random.bytes via env.teko_random host import (deterministic fill)`);
  process.exit(0);
} else {
  console.error(`FAIL random: got ${JSON.stringify(got)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
