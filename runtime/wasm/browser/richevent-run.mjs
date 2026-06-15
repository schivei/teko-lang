// Browser-side loader for the MVP-4 rich-event proof. $main registers a dom.on_value
// listener on #inp; when the harness types, the glue marshals the input's value into
// wasm memory via teko_alloc and the Teko callback mirrors it into #echo. The harness
// drives the typing and asserts #echo.
import { makeTekoDomImports } from "../samples/emitted_richevent.glue.mjs";

let memory, instance;
const imports = makeTekoDomImports(() => memory, () => instance);
try {
  const bytes = await (await fetch("../samples/emitted_richevent.wasm")).arrayBuffer();
  ({ instance } = await WebAssembly.instantiate(bytes, imports));
  memory = instance.exports.memory;
  instance.exports.main(); // registers the on_value listener
  window.__tekoRich = { ok: true };
} catch (e) {
  window.__tekoRich = { ok: false, error: String(e) };
}
