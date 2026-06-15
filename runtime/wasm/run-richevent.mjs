// Headless-browser harness for Browser FFI MVP-4 rich event payloads: serve
// runtime/wasm/ with COOP/COEP, drive Chromium, type into #inp, and assert the Teko
// callback received the typed value (materialized via teko_alloc) and mirrored it
// into #echo. Proves JS event DATA — not just a handle — crosses into Teko.
//   usage: node run-richevent.mjs   (requires `npm i` + `npx playwright install chromium`)
import { chromium } from "playwright";
import { startServer } from "./server.mjs";

const PORT = Number(process.env.PORT ?? 8096);
const TYPED = "typed in browser";
const server = await startServer(PORT);
let exitCode = 0;
let browser;
try {
  browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(`http://localhost:${PORT}/browser/richevent.html`, { waitUntil: "load", timeout: 30000 });
  await page.waitForFunction(() => window.__tekoRich !== undefined, { timeout: 40000 });
  const status = await page.evaluate(() => window.__tekoRich);
  await page.fill("#inp", TYPED); // dispatches the "input" event
  const echo = await page.evaluate(() => document.querySelector("#echo")?.textContent ?? null);
  if (status?.ok && echo === TYPED) {
    console.log(`OK   browser rich event: typed "${TYPED}" -> #echo = "${echo}"`);
  } else {
    console.error(`FAIL browser rich event: status=${JSON.stringify(status)} echo=${JSON.stringify(echo)}`);
    exitCode = 1;
  }
} catch (e) {
  console.error("rich-event harness error:", e);
  exitCode = 1;
} finally {
  if (browser) await browser.close().catch(() => {});
  server.close();
}
process.exit(exitCode);
