// Layer B runner worker (node worker_threads). Runs the module's main() off the
// node main thread: main() busy-polls the channel flag (a spinning poll on the
// main thread would peg it and starve the broker that must spawn the producer).
// The module's SPAWN is brokered back to the parent, which starts the producer
// thread. Failures are surfaced as a message so the harness fails clearly.
import { workerData, parentPort } from "node:worker_threads";

try {
  const { memory, bytes } = workerData;
  const imports = {
    env: { memory },
    teko_rt: { spawn: (fn, arg) => parentPort.postMessage({ spawn: [fn, arg] }) },
  };
  const { instance } = await WebAssembly.instantiate(bytes, imports);
  parentPort.postMessage({ result: instance.exports.main() });
} catch (e) {
  parentPort.postMessage({ error: String((e && e.stack) || e) });
}
