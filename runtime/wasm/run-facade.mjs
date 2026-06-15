// Headless-browser harness for Browser FFI MVP-4 (allocator + JS→Teko string +
// ergonomic facade): serve runtime/wasm/ with COOP/COEP, drive Chromium, load the
// facade page, and assert mod.showMessage(str) materialized the JS string in wasm
// memory and a Teko routine wrote it to the DOM — #out === "hello from JS via alloc".
//   usage: node run-facade.mjs   (requires `npm i` + `npx playwright install chromium`)
import { chromium } from "playwright";
import { startServer } from "./server.mjs";

const PORT = Number(process.env.PORT ?? 8095);
const EXPECTED = "hello from JS via alloc";
const server = await startServer(PORT);
let exitCode = 0;
let browser;
try {
  browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(`http://localhost:${PORT}/browser/facade.html`, { waitUntil: "load", timeout: 30000 });
  await page.waitForFunction(() => window.__tekoFacade !== undefined, { timeout: 40000 });
  const status = await page.evaluate(() => window.__tekoFacade);
  const domText = await page.evaluate(() => document.querySelector("#out")?.textContent ?? null);
  if (status?.ok && status.text === EXPECTED && domText === EXPECTED) {
    console.log(`OK   browser facade: mod.showMessage(...) -> #out = "${domText}"`);
  } else {
    console.error(`FAIL browser facade: status=${JSON.stringify(status)} domText=${JSON.stringify(domText)}`);
    exitCode = 1;
  }
} catch (e) {
  console.error("facade harness error:", e);
  exitCode = 1;
} finally {
  if (browser) await browser.close().catch(() => {});
  server.close();
}
process.exit(exitCode);
