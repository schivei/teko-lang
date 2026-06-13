// Standalone-engine runner (Node / wasmtime-equivalent): loads each compiled WASM
// fixture and exercises its in-module `test()` export. Layer A needs no host
// runtime. Fixtures and their expected results:
//   channels.wasm  -> 42 (channel round-trip, Phase 10.1)
//   scheduler.wasm -> 15 (cooperative scheduler + spawn + blocking channel, 10.2)
import { readFile } from "node:fs/promises";

const fixtures = [
  { file: "./samples/channels.wasm", expected: 42, name: "channels (10.1)" },
  { file: "./samples/scheduler.wasm", expected: 15, name: "scheduler (10.2)" },
];

let failures = 0;
for (const { file, expected, name } of fixtures) {
  try {
    const bytes = await readFile(new URL(file, import.meta.url));
    const { instance } = await WebAssembly.instantiate(bytes, {});
    const got = instance.exports.test();
    if (got !== expected) {
      console.error(`FAIL ${name}: test() = ${got}, expected ${expected}`);
      failures++;
    } else {
      console.log(`OK   ${name}: test() = ${got}`);
    }
  } catch (e) {
    console.error(`FAIL ${name}: ${e}`);
    failures++;
  }
}
process.exit(failures === 0 ? 0 : 1);
