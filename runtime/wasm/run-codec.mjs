// Phase 12 P12-G — base64/hex codec proof (Node). The module under test is produced by
// the teko BINARY compiling runtime/wasm/samples/codec.tks; it calls the native in-module
// base64/hex codec runtime (no external deps). The host supplies env.emit, reads each
// NUL-terminated result from exported memory, and checks RFC 4648 vectors + a round-trip.
import { readFile } from "node:fs/promises";

const wasm = await readFile(new URL("./samples/codec.wasm", import.meta.url));
let mem = null;
const got = [];
const dec = new TextDecoder();
const imports = {
  env: {
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

const expected = ["TWFu", "4d616e", "Man", "hello from teko"];
if (JSON.stringify(got) === JSON.stringify(expected)) {
  console.log(`OK   base codecs: ${JSON.stringify(got)} (base64/hex encode+decode + round-trip)`);
  process.exit(0);
} else {
  console.error(`FAIL base codecs: got ${JSON.stringify(got)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
