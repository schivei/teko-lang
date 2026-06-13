// Layer B worker entry (node worker_threads). Re-instantiate the SAME module
// against the SAME shared memory passed from the parent, then run one routine via
// the module's exported `teko_invoke(fn, arg)` dispatcher. This is a real OS
// thread; the hand-off to the parent happens through the shared memory's atomics.
import { workerData, parentPort } from "node:worker_threads";
import { readFile } from "node:fs/promises";

const { memory, wasmPath, fn, arg } = workerData;
const bytes = await readFile(wasmPath);
// The worker never spawns further threads, so its teko_rt.spawn is a no-op.
const imports = { env: { memory }, teko_rt: { spawn: () => {} } };
const { instance } = await WebAssembly.instantiate(bytes, imports);
instance.exports.teko_invoke(fn, arg);
parentPort.postMessage("done");
