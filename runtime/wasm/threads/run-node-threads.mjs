// Phase 10.4 Layer B harness (node worker_threads): genuine multicore. For each
// module, main() runs on this thread and blocks on memory.atomic.wait32; its
// SPAWN calls the host teko_rt.spawn, which starts a REAL Worker (separate OS
// thread) that re-instantiates the same module against the shared memory and runs
// the routine via teko_invoke — publishing the value through the atomic channel.
//
// Modules:
//   samples/threads.wasm          -> 777 (hand-written Layer B reference)
//   samples/emitted_threads.wasm  -> 99  (REAL compiler output, --target wasm-threads)
import { Worker } from "node:worker_threads";
import { readFile } from "node:fs/promises";

const here = (p) => new URL(p, import.meta.url);
const workerURL = here("./worker.mjs");

async function runModule(wasmRel, expected) {
  const wasmPath = here(wasmRel).pathname;
  const bytes = await readFile(wasmPath);
  const memory = new WebAssembly.Memory({ initial: 1, maximum: 1, shared: true });
  const pending = [];
  // SPAWN -> start a real Worker thread that runs routine `fn` against the shared memory.
  const imports = {
    env: { memory },
    teko_rt: {
      spawn: (fn, arg) => {
        const w = new Worker(workerURL, { workerData: { memory, wasmPath, fn, arg } });
        pending.push(new Promise((res) => w.on("message", () => w.terminate().then(res, res))));
      },
    },
  };
  const { instance } = await WebAssembly.instantiate(bytes, imports);
  const got = instance.exports.main();      // blocks until the Worker notifies
  await Promise.all(pending);
  return got;
}

const fixtures = [
  { file: "../samples/threads.wasm", expected: 777, name: "threads reference (10.4)" },
  { file: "../samples/emitted_threads.wasm", expected: 99, name: "emitted wasm-threads (10.4)", optional: true },
];

let failures = 0;
for (const { file, expected, name, optional } of fixtures) {
  try {
    const got = await runModule(file, expected);
    if (got === expected) console.log(`OK   ${name}: main() = ${got} (real worker_threads hand-off)`);
    else { console.error(`FAIL ${name}: main() = ${got}, expected ${expected}`); failures++; }
  } catch (e) {
    if (optional && /no such file|ENOENT/.test(String(e))) { console.log(`SKIP ${name}: not built`); continue; }
    console.error(`FAIL ${name}: ${e}`); failures++;
  }
}
process.exit(failures === 0 ? 0 : 1);
