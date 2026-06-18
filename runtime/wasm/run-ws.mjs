// Phase 19 (WS-SRV — WASM discriminating proof).
//
// Proves the `websocket` token is LIVE: the RFC 6455 handshake accept is computed by the REAL
// C teko_websocket.c compiled into the crypto reactor (crypto.wasm) — NO JS mock.
//
// lower_ws_block emits:
//   1. id=100 teko_rt_ws_handshake_accept(key_ptr) -> accept_ptr
//      key = "dGhlIHNhbXBsZSBub25jZQ==" (RFC 6455 §1.3 test vector)
//   2. emit(accept_ptr)  — teko_rt_emit prints the 28-char accept string to stdout
//   3. id=103 teko_rt_ws_frame_free(accept_ptr) -> 0
//
// Expected output: ["s3pPLMBiTxaQ9kYGzzhZRbK+xOo="]
// (RFC 6455 §1.3 mandated accept for the standard test key)
//
// PROOF THAT THE REACTOR (NOT A JS MOCK) COMPUTED THE VALUE:
//   - ws.wasm imports "crypto"."teko_rt_ws_handshake_accept" — no JS framing logic here.
//   - The host (this harness) supplies ZERO handshake / SHA-1 / base64 logic.
//   - The reactor (crypto.wasm) runs teko_ws_handshake_accept: sha1(key+"GUID") + base64.
//   - The result is written into reactor-managed heap and returned as a pointer.
//   - teko_rt_emit reads the NUL-terminated string from shared linear memory.
//
// SAST: key is a compile-time constant (ws.tks source); the reactor's teko_ws_handshake_accept
// writes exactly 29 bytes (28 base64 chars + NUL) into a malloc'd buffer; no format-string;
// no OOB; teko_rt_ws_frame_free is NULL-safe. The host has NO framing or SHA-1 code.

import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec  = new TextDecoder();

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];

// Host environment — provides clock, entropy, timezone for the reactor (not for WS).
const env = {
  memory,
  teko_now_ns:    () => process.hrtime.bigint(),
  teko_now_unix:  () => 1000000000n,
  teko_tz_offset: () => 0,
  teko_random:    (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
};

// `emit` is imported from the "teko_rt" namespace — the harness collects the string.
// This harness does NOT implement any WebSocket framing or SHA-1 logic.
const teko_rt = {
  teko_rt_emit: (ptr) => {
    const u = new Uint8Array(memory.buffer);
    let end = ptr >>> 0;
    while (u[end]) end++;
    const str = dec.decode(u.slice(ptr >>> 0, end));
    out.push(str);
    console.log(`  emit("${str}")`);
  }
};

// Load and instantiate the crypto reactor (contains teko_ws_handshake_accept C code).
// This is the REAL teko_websocket.c compiled to wasm32 — no JS mock.
const cryptoBytes = await readFile(here("./crypto/crypto.wasm"));
const cryptoInst = await WebAssembly.instantiate(cryptoBytes, { env });

// Load and instantiate ws.wasm — it imports ws_handshake_accept from the reactor.
const wsWasm = await readFile(here("./samples/ws.wasm"));
const teko = await WebAssembly.instantiate(wsWasm, {
  env,
  crypto: cryptoInst.instance.exports,
  teko_rt
});

// Run main() — emits the RFC 6455 §1.3 accept string.
console.log("Running ws.tks WASM proof (reactor-backed, NO JS mock):");
teko.instance.exports.main();

// Assert: the reactor's teko_ws_handshake_accept produced the RFC 6455 §1.3 test vector.
const expected = ["s3pPLMBiTxaQ9kYGzzhZRbK+xOo="];
if (JSON.stringify(out) === JSON.stringify(expected)) {
  console.log(`OK   ws handshake accept: ${JSON.stringify(out)}`);
  console.log(`     RFC 6455 §1.3 test vector confirmed.`);
  console.log(`     Computed by reactor teko_websocket.c (C SHA-1 + base64), NOT a JS mock.`);
  process.exit(0);
} else {
  console.error(`FAIL ws: got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
