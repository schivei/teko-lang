// Browser-side loader for the MVP-4 facade proof. Uses ONLY the auto-generated
// ergonomic facade: instantiate() wires the glue+module, then mod.showMessage(str)
// marshals the JS string into wasm memory via teko_alloc and dispatches the Teko
// routine that writes it to #out. The harness reads window.__tekoFacade.
import { instantiate } from "../samples/emitted_alloc.mjs";

try {
  const bytes = await (await fetch("../samples/emitted_alloc.wasm")).arrayBuffer();
  const mod = await instantiate(bytes);
  mod.main();
  mod.showMessage("hello from JS via alloc");
  window.__tekoFacade = { ok: true, text: document.querySelector("#out")?.textContent ?? null };
} catch (e) {
  window.__tekoFacade = { ok: false, error: String(e) };
}
