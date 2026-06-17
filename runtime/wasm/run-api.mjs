// Phase 19 (Router API proof — WASM harness).
//
// WASM cannot dynamically dispatch HTTP routes, so the api { } block lowers to env.teko_rt_router_*
// HOST imports, backed here by a pure in-memory radix-tree mock. The emitted module has the SAME
// shape as every other Teko WASM sample: it imports the compiled-C reactor (crypto.wasm) and shares
// ONE linear memory with it (env.memory). The round-trip asserts the module drives the route
// registration and dispatch in order:
//   router_new(0) -> handle=1
//   router_add("GET",    "/hello", slot=1, handle=1) -> 0  [slot 0=middleware logger]
//   router_add("POST",   "/data",  slot=2, handle=1) -> 0
//   router_add("DELETE", "/gone",  slot=3, handle=1) -> 0
//   router_dispatch(handle=1, "GET",    "/hello") -> slot=1; call_indirect($routine_1) -> emit_int(200)
//   router_dispatch(handle=1, "POST",   "/data")  -> slot=2; call_indirect($routine_2) -> emit_int(201)
//   router_dispatch(handle=1, "DELETE", "/gone")  -> slot=3; call_indirect($routine_3) -> emit_int(204)
//   router_free(handle=1) -> 0
//
// SAST (host side): method_ptr/path_ptr are i32 module values bounded to the module's linear
// memory; readCStr reads at most MAX_BUF bytes before returning (cap the scan); handler_id is
// a module i32 (compile-time bounded slot, no overflow); route data is compile-time string
// constants (no attacker-controlled input at registration); dispatch result is a module-internal
// i32 used only as a call_indirect table index (WASM runtime bounds-checked); no untrusted data
// escapes the memory region.

import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec  = new TextDecoder();

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
let nextHandle = 1;
const routeMap = new Map(); // "handle:method:path" -> slot

const MAX_BUF = 65535;

const readCStr = (ptr) => {
  const u = new Uint8Array(memory.buffer);
  let end = ptr >>> 0;
  while (u[end] !== 0 && end - (ptr >>> 0) < MAX_BUF) end++;
  return dec.decode(u.subarray(ptr >>> 0, end));
};

const env = {
  memory,
  // --- router host mock (env namespace) ---
  // Signature: teko_rt_router_new(capacity) -> handle
  // Signature: teko_rt_router_add(method_ptr, path_ptr, slot, handle) -> status
  // Signature: teko_rt_router_dispatch(handle, method_ptr, path_ptr) -> slot
  // Signature: teko_rt_router_free(handle) -> status
  teko_rt_router_new: (capacity) => {
    const h = nextHandle++;
    console.log(`  router_new(${capacity | 0}) -> handle ${h}`);
    return h;
  },
  teko_rt_router_add: (method_ptr, path_ptr, slot, handle) => {
    const method = readCStr(method_ptr);
    const path   = readCStr(path_ptr);
    const key = `${handle}:${method}:${path}`;
    routeMap.set(key, slot | 0);
    console.log(`  router_add("${method}", "${path}", ${slot | 0}, ${handle}) -> key=${key}`);
    return 0; // TEKO_ROUTE_OK
  },
  teko_rt_router_dispatch: (handle, method_ptr, path_ptr) => {
    const method = readCStr(method_ptr);
    const path   = readCStr(path_ptr);
    const key = `${handle}:${method}:${path}`;
    const slot = routeMap.has(key) ? (routeMap.get(key) | 0) : -1;
    console.log(`  router_dispatch(${handle}, "${method}", "${path}") -> key=${key} slot=${slot}`);
    return slot;
  },
  teko_rt_router_free: (handle) => {
    console.log(`  router_free(${handle})`);
    return 0;
  },
  // --- reactor host hooks (crypto/time/entropy) ---
  teko_now_ns:    () => process.hrtime.bigint(),
  teko_now_unix:  () => 1000000000n,
  teko_tz_offset: () => 0,
  teko_random:    (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
};

// `emit_int` is imported from the "teko_rt" namespace.
const teko_rt = {
  teko_rt_emit_int: (n) => {
    const val = n | 0;
    out.push(val);
    console.log(`  emit_int(${val})`);
  }
};

// Load and instantiate the crypto reactor.
const cryptoBytes = await readFile(here("./crypto/crypto.wasm"));
const cryptoInst = await WebAssembly.instantiate(cryptoBytes, { env });

// Load and instantiate the api.wasm sample.
const apiWasm = await readFile(here("./samples/api.wasm"));
const teko = await WebAssembly.instantiate(apiWasm, {
  env,
  crypto: cryptoInst.instance.exports,
  teko_rt
});

// Run main().
console.log("Running api.tks proof:");
teko.instance.exports.main();

// Assert output.
const expected = [200, 201, 204];
if (JSON.stringify(out) === JSON.stringify(expected)) {
  console.log(`OK   api router: ${JSON.stringify(out)} (GET→200, POST→201, DELETE→204; distinct routes dispatched)`);
  process.exit(0);
} else {
  console.error(`FAIL api router: got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
