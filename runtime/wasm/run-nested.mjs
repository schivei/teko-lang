// Headless-browser harness for Browser FFI P12-F (multiple nested @dom handle args):
// serve runtime/wasm/ with COOP/COEP, drive Chromium, load the page whose module was
// compiled by the teko binary from nested.tks, and assert the nested expression built
// the DOM tree — a <span> appended under #out.
//   usage: node run-nested.mjs
import { chromium } from "playwright";
import { startServer } from "./server.mjs";

const PORT = Number(process.env.PORT ?? 8099);
const server = await startServer(PORT);
let exitCode = 0;
let browser;
try {
  browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(`http://localhost:${PORT}/browser/nested.html`, { waitUntil: "load", timeout: 30000 });
  await page.waitForFunction(() => window.__tekoNested !== undefined, { timeout: 40000 });
  const status = await page.evaluate(() => window.__tekoNested);
  const tag = await page.evaluate(() => document.querySelector("#out > span")?.tagName.toLowerCase() ?? null);
  if (status?.ok && status.tag === "span" && tag === "span") {
    console.log(`OK   browser nested args: appendChild(getElementById, createElement) -> #out > <${tag}>`);
  } else {
    console.error(`FAIL browser nested args: status=${JSON.stringify(status)} tag=${JSON.stringify(tag)}`);
    exitCode = 1;
  }
} catch (e) {
  console.error("nested harness error:", e);
  exitCode = 1;
} finally {
  if (browser) await browser.close().catch(() => {});
  server.close();
}
process.exit(exitCode);
