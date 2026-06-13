// Browser-side loader: instantiate each compiled WASM fixture and run its
// in-module test() in-page. The headless-browser harness reads
// `window.__tekoResults` to assert behavioral parity with Node/wasmtime, and
// `window.__crossOriginIsolated` (COOP/COEP) — the prerequisite for Layer B
// (SharedArrayBuffer + Web Workers).
const out = document.getElementById("out");
const fixtures = [
  { file: "../samples/channels.wasm", expected: 42, name: "channels" },
  { file: "../samples/scheduler.wasm", expected: 15, name: "scheduler" },
];
const results = {};
let lines = [];
for (const { file, expected, name } of fixtures) {
  try {
    const res = await WebAssembly.instantiateStreaming(fetch(file), {});
    const got = res.instance.exports.test();
    results[name] = got;
    lines.push(`${name}: test() = ${got} (expected ${expected})`);
  } catch (e) {
    results[name] = `error: ${e}`;
    lines.push(`${name}: error: ${e}`);
  }
}
window.__tekoResults = results;
window.__crossOriginIsolated = self.crossOriginIsolated === true;
out.textContent = lines.join("\n") + `\ncrossOriginIsolated = ${window.__crossOriginIsolated}`;
