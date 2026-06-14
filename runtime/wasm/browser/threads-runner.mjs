// Layer B browser runner Worker. Runs the module's main() — which blocks on
// memory.atomic.wait32, only permitted off the main thread. Its SPAWN is brokered
// to the page (which starts the producer Worker), so no nested workers are needed.
self.onmessage = async (e) => {
  const { memory, bytes } = e.data;
  const imports = {
    env: { memory },
    teko_rt: { spawn: (fn, arg) => self.postMessage({ spawn: [fn, arg] }) },
  };
  try {
    const { instance } = await WebAssembly.instantiate(bytes, imports);
    const got = instance.exports.main();   // blocks until the producer notifies
    self.postMessage({ result: got });
  } catch (err) {
    self.postMessage({ result: `error: ${err}` });
  }
};
