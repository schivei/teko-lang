// Layer B browser producer Worker (a real OS thread). Re-instantiates the same
// module against the SAME shared memory and runs one routine via teko_invoke,
// publishing its value through the atomic channel to wake the runner's main().
self.onmessage = async (e) => {
  const { memory, bytes, fn, arg } = e.data;
  try {
    const { instance } = await WebAssembly.instantiate(bytes, {
      env: { memory },
      teko_rt: { spawn: () => {} },
    });
    instance.exports.teko_invoke(fn, arg);
  } catch (err) {
    // surfaced via the runner timing out
  }
  self.postMessage({ done: true });
};
