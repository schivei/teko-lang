// Headless-browser harness for Browser FFI FE-E (@dom intrinsics from source): serve
// runtime/wasm/ with COOP/COEP, drive Chromium, load the page whose module was
// compiled by the teko binary from dom.tks, and assert the @dom intrinsics drove the
// real DOM — #out textContent === "hello from dom source".
//   usage: node run-dom-source.mjs
import { chromium } from "playwright";
import { startServer } from "./server.mjs";

const PORT = Number(process.env.PORT ?? 8097);
const EXPECTED = "hello from dom source";
const server = await startServer(PORT);
let exitCode = 0;
let browser;
try {
  browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(`http://localhost:${PORT}/browser/dom-source.html`, { waitUntil: "load", timeout: 30000 });
  await page.waitForFunction(() => window.__tekoDomSource !== undefined, { timeout: 40000 });
  const status = await page.evaluate(() => window.__tekoDomSource);
  const domText = await page.evaluate(() => document.querySelector("#out")?.textContent ?? null);
  if (status?.ok && status.text === EXPECTED && domText === EXPECTED) {
    console.log(`OK   browser @dom source: teko-compiled dom.tks set #out = "${domText}"`);
  } else {
    console.error(`FAIL browser @dom source: status=${JSON.stringify(status)} domText=${JSON.stringify(domText)}`);
    exitCode = 1;
  }
} catch (e) {
  console.error("dom-source harness error:", e);
  exitCode = 1;
} finally {
  if (browser) await browser.close().catch(() => {});
  server.close();
}
process.exit(exitCode);
