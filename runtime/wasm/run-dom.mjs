// Headless-browser harness for Browser FFI MVP-2: serve runtime/wasm/ with
// COOP/COEP, drive Chromium via Playwright, load the DOM proof page, and assert
// the Teko program drove the real DOM through the auto-generated glue —
// #out > span textContent === "hello from teko".
//   usage: node run-dom.mjs   (requires `npm i` + `npx playwright install chromium`)
import { chromium } from "playwright";
import { startServer } from "./server.mjs";

const PORT = Number(process.env.PORT ?? 8093);
const EXPECTED = "hello from teko";
const server = await startServer(PORT);
let exitCode = 0;
let browser;
try {
  browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(`http://localhost:${PORT}/browser/dom.html`, { waitUntil: "load", timeout: 30000 });
  await page.waitForFunction(() => window.__tekoDom !== undefined, { timeout: 40000 });
  const status = await page.evaluate(() => window.__tekoDom);
  // Read the DOM the page itself sees, not just the loader's report.
  const domText = await page.evaluate(() => document.querySelector("#out > span")?.textContent ?? null);
  if (status?.ok && status.text === EXPECTED && domText === EXPECTED) {
    console.log(`OK   browser DOM FFI: #out > span textContent = "${domText}"`);
  } else {
    console.error(`FAIL browser DOM FFI: status=${JSON.stringify(status)} domText=${JSON.stringify(domText)}`);
    exitCode = 1;
  }
} catch (e) {
  console.error("dom harness error:", e);
  exitCode = 1;
} finally {
  if (browser) await browser.close().catch(() => {});
  server.close();
}
process.exit(exitCode);
