// Phase 11 MVP-1 executable proof (Node): instantiate the backend's emitted FFI
// module with a host `env.log(ptr)` import that reads a NUL-terminated string from
// the module's exported linear memory. Asserts the Teko program called the import
// with the pooled string "hello from teko".
import { readFile } from "node:fs/promises";

const wasm = await readFile(new URL("./samples/emitted_ffi.wasm", import.meta.url));
let mem = null;
let captured = null;
const imports = {
  env: {
    log: (ptr) => {
      const u = new Uint8Array(mem.buffer);
      let end = ptr >>> 0;
      while (u[end] !== 0) end++;
      captured = new TextDecoder().decode(u.subarray(ptr >>> 0, end));
    },
  },
};
const { instance } = await WebAssembly.instantiate(wasm, imports);
mem = instance.exports.memory;
instance.exports.main();

const expected = "hello from teko";
if (captured === expected) {
  console.log(`OK   env.log captured "${captured}" (import call + string pool)`);
  process.exit(0);
} else {
  console.error(`FAIL env.log captured ${JSON.stringify(captured)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
