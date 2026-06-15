// Browser-side loader for the P12-F multiple-nested-args proof. The module under test
// is produced by the teko BINARY compiling runtime/wasm/samples/nested.tks
// (@dom.appendChild(@dom.getElementById("out"), @dom.createElement("span"))). main()
// builds the DOM tree through the auto-generated glue; the harness asserts #out > span.
import { makeTekoDomImports } from "../samples/nested.glue.mjs";

let memory, instance;
const imports = makeTekoDomImports(() => memory, () => instance);
try {
  const bytes = await (await fetch("../samples/nested.wasm")).arrayBuffer();
  ({ instance } = await WebAssembly.instantiate(bytes, imports));
  memory = instance.exports.memory;
  instance.exports.main();
  const span = document.querySelector("#out > span");
  window.__tekoNested = { ok: true, tag: span ? span.tagName.toLowerCase() : null };
} catch (e) {
  window.__tekoNested = { ok: false, error: String(e) };
}
