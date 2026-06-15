// Browser-side loader for the FE-E @dom-from-source proof. The module under test is
// produced by the teko BINARY compiling runtime/wasm/samples/dom.tks
// (@dom.setText(@dom.getElementById("out"), "hello from dom source")) — a real source
// → WASM path, not an emit-demo. main() drives the DOM through the auto-generated glue.
import { makeTekoDomImports } from "../samples/dom.glue.mjs";

let memory, instance;
const imports = makeTekoDomImports(() => memory, () => instance);
try {
  const bytes = await (await fetch("../samples/dom.wasm")).arrayBuffer();
  ({ instance } = await WebAssembly.instantiate(bytes, imports));
  memory = instance.exports.memory;
  instance.exports.main();
  window.__tekoDomSource = { ok: true, text: document.querySelector("#out")?.textContent ?? null };
} catch (e) {
  window.__tekoDomSource = { ok: false, error: String(e) };
}
