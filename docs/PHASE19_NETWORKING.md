# Phase 19 — Native Networking & Web Architecture (native server + client; WASM client-only)

> **Status:** IN PROGRESS — branch `feat/phase-19-networking` (principal Draft PR #13 → `main`).
> Planned/managed under the **Orchestration Doctrine** (`docs/ORCHESTRATION_DOCTRINE.md`): the Master
> Agent (Opus) decomposes into breadcrumbs and delegates to Tech Lead (Sonnet) / Developer (Haiku)
> subagents; each breadcrumb ships as a sub-PR **into this phase branch** (PM merges after review +
> SAST + CI); the **owner (PO) merges the principal PR into `main`** at the end.

References — never duplicates — `docs/plan.md` §Phase 19, `CLAUDE.md` (bars), and the parallel
TLS-track doc `docs/PHASE19_TLS_TRACK.md`.

---

## 0. Owner-locked decisions (current revision)

- **A1 — pragmatic MVP.** Make every reserved web token live with native+WASM proofs; advanced
  protocols become **parallel tracks**, not blanket deferrals.
- **C1 — cooperative async.** I/O integrates with the Phase-14 green-thread scheduler (a routine
  blocked on `accept`/`recv` yields). Real async backends (io_uring/kqueue/IOCP) are a later perf
  track behind the *same* surface — no new tokens.
- **B — split by target (corrected):**
  - **NATIVE = full SERVER + CLIENT, at every layer (TCP/UDP → HTTP → WebSocket → router).** The
    socket foundation includes **both** the **server path** (`bind`/`listen`/`accept` loop) **and**
    the **client path** (`connect`/`send`/`recv`/`close`). The web router runs a **real listening
    server** with AOT static radix-tree dispatch.
  - **WASM = CLIENT-ONLY.** Host-import client (`fetch`/`ws`); **no listening server** — a platform
    reality, **honestly marked**. Server socket I/O (`bind`/`listen`/`accept`) is **NATIVE-ONLY**.
- **CI-trigger fix** kept in the setup commit (owner-approved).
- **TLS** deferred from the MVP critical path but runs as a **parallel track** with all SSL/TLS
  versions enumerated — see `docs/PHASE19_TLS_TRACK.md`.
- **Compression** folded in (owner request) as a tiered track — see §4.

## 1. Token / surface semantics (server vs client, and how each is proven on BOTH targets)

The "no dead tokens" bar binds the reserved lexer tokens
`api middleware get post put delete patch head options rpc websocket` (+ bandwidth units
`kbps`/`mbps`/`gbps`). The reserved **web router tokens are SERVER route-definition constructs**;
client HTTP/WS/RPC usage rides **dotted-identifier surfaces** (not tokens). Both must satisfy the
**no-dead-token bar AND the native+WASM proof bar**, resolved as follows:

| Token / surface | Kind | Native | WASM | Proof strategy |
|---|---|---|---|---|
| `api`, `middleware`, `get/post/put/delete/patch/head/options` | **SERVER route-definition tokens** | real listening server + AOT static **radix-tree** dispatch; `middleware` = server interceptor chain | **client-only platform** has no server | **Target-agnostic routing:** build the radix tree and dispatch a **SYNTHETIC in-module request** (method+path+headers, no real socket) on BOTH targets → proves grammar + emission + routing everywhere; real `bind/listen/accept` is **native-only**, honestly marked |
| `websocket` | **SERVER WS endpoint** token | real server WS endpoint (accept + RFC 6455) | synthetic | same target-agnostic proof (handshake/route built in-module; real accept native-only) |
| `rpc` | RPC | native: server **and** client; minimal length-prefixed | client (host-import) | native server+client `.tks`; WASM client `.mjs` + synthetic server-route proof |
| `http.*` (`http.get/post/...`) | **client** dotted surface | real client over socket | real client (host-import `fetch`) | live on BOTH (native socket / WASM host) |
| `net.*` (`tcp_connect/...` + native `tcp_listen/accept`) | dotted surface | client **and** server primitives | **client only** (connect/send/recv); listen/accept **absent**, marked native-only | native server+client `.tks`; WASM client `.mjs` |
| `ws.*` (client WebSocket) | **client** dotted surface | real client | host-import client | live on BOTH |
| `kbps/mbps/gbps` | client/server throttle | `limit(10mbps)` | same | normalized bps literal consumed by a throttle surface |

`net`/`http`/`tls`/`compress`/`ws` are **dotted surfaces, not tokens** — so QUIC/HTTP-2/3/4/full-gRPC
sequence as later tracks with **no dead tokens**. Greenfield: no socket/IO code exists today.

House pattern per feature: **portable C runtime (single source of truth + Unity KATs) →
`OP_CALL_RUNTIME` id → native `teko_rt_*` wrapper + WASM reactor import → `.tks` proof native AND
WASM** (server-token surfaces additionally use the synthetic-request proof on WASM).

### 1.1 Why the synthetic-request proof is legitimate (not a stubbed token)
The server tokens emit two separable things: (a) the **AOT radix tree + middleware chain + handler
dispatch** (target-agnostic compiler/runtime logic), and (b) the **socket accept loop** (native OS
syscalls). The WASM proof exercises (a) end-to-end with a real in-module request value and asserts
the correct handler/middleware run and response is produced — the token is **fully lowered and
executed**, only the OS accept syscall (b) is native-only. This keeps the token live and proven on
WASM without pretending WASM can listen.

---

## 2. Parallelization — the dependency DAG (owner priority: MAXIMIZE concurrency)

Every codec / protocol state machine is a pure, KAT-testable C runtime with **zero socket
dependency**; only *integration* (wiring onto live sockets / the accept loop) serializes, funnelling
through **T2** (socket wiring). The native server path adds one enabler node (**T1b**, the
accept-loop primitives) parallel to the client foundation.

### Legend: ⟂ independent (concurrent now) · → depends on · ⏸ deferred (reason)

### Wave 0 — start NOW, fully parallel (separate `teko_*.c` + KAT files)
- **T1a — socket CLIENT foundation** `teko_socket.c`: `tcp_connect`/`udp_connect`/`send`/`recv`/
  `close`, handle table (calloc, fail-loud bounds), state machine, POSIX + Winsock. ⟂ *(enabler)*
- **T1b — socket SERVER foundation (NATIVE-ONLY)** `teko_socket_server.c`: `bind`/`listen`/`accept`
  loop, connection handles, integrated with the Phase-14 scheduler (accept/recv yield). Guarded so
  WASM never references it. ⟂ *(enabler; native-only TU)*
- **HTTP-CODEC** — request **and** response build/parse (status/headers/body, chunked) as pure
  byte↔struct functions, KAT'd. Serves both client (build req / parse resp) and server (parse req /
  build resp). ⟂
- **WS-CODEC** — RFC 6455 frame encode/decode + masking + UTF-8 validate (client and server
  masking rules), KAT'd. ⟂
- **ROUTER-CORE** — AOT static **radix-tree** builder + middleware chain + handler dispatch over a
  **synthetic request struct** (no socket), KAT'd. Target-agnostic — this is what proves the server
  tokens on WASM. ⟂
- **C-CORE compression** — DEFLATE (RFC 1951) + zlib (RFC 1950) + gzip (RFC 1952) + CRC32/Adler-32,
  KAT'd. Lands the `compress.*` surface plumbing. ⟂
- **C-BR / C-ZSTD / C-LZMA / C-LZ4** — brotli (RFC 7932), zstd (RFC 8878), lzma/lzma2 (xz), lz4 —
  four independent codec tracks, each KAT'd. ⟂
- **TLS-RT** — TLS 1.3 (+1.2) key schedule + handshake state machine + record AEAD, driven by
  **RFC 8448 vectors with no socket**. ⟂
- **PROTOBUF-CODEC** — protobuf wire encode/decode (for gRPC), KAT'd. ⟂
- **H2-CODEC** — HTTP/2 framing + HPACK (RFC 7541), KAT'd. ⟂
- **QUIC-CODEC** — QUIC packet/frame codec (RFC 9000), KAT'd. ⟂

### Wave 1 — after **T1a/T1b → T2**; parallel among themselves
- **T2 — socket wiring**: CMake + `teko_rt_socket_*` (client) + `teko_rt_server_*` (native-only) +
  reactor SRCS/EXPORTS (client subset only) + frontend `net.*` surface + native/WASM emit + proofs.
  → T1a, T1b
- **HTTP-INT (client)** `http.get/post/...` over the socket client.  → T2 + HTTP-CODEC
- **HTTP-SRV (native)** HTTP/1.1 server: accept → parse request → router → build response → send.
  → T2(T1b) + HTTP-CODEC + ROUTER-CORE
- **WS-INT (client)** client `ws.*` connect/send/recv.  → T2 + HTTP-CODEC + WS-CODEC
- **RPC** native server+client + WASM client.  → T2
- **UNITS** `limit(10mbps)` throttle.  → T2
- **TLS-INT** `tls.client` (+ native `tls.server`) over socket.  → T2 + TLS-RT
- **COMPRESS-INT** Content-Encoding/Transfer-Encoding (client+server) + permessage-deflate.
  → HTTP-INT/HTTP-SRV + WS-INT + C-CORE

### Wave 2 — after Wave-1 deps; parallel
- **ROUTER-NATIVE** — `api`/`middleware`/verbs as a **real listening server** (ROUTER-CORE over
  HTTP-SRV); **WASM proof = ROUTER-CORE synthetic request**.  → HTTP-SRV (native) / ROUTER-CORE (WASM)
- **WS-SRV (native)** — `websocket` server endpoint (accept + handshake + RFC 6455); **WASM proof =
  synthetic**.  → HTTP-SRV + WS-CODEC
- **H2-INT** — HTTP/2 (client; native server optional).  → TLS-INT + H2-CODEC
- **QUIC-INT** — QUIC (client; native server optional).  → T1(UDP) + TLS-RT + QUIC-CODEC

### Wave 3 — after Wave-2; parallel
- **GRPC** (full) — gRPC.  → H2-INT + PROTOBUF-CODEC   *(minimal `rpc` already live in Wave 1)*
- **H3-INT** — HTTP/3.  → QUIC-INT + QPACK-CODEC

### ⏸ Truly deferred (cannot be meaningfully parallelized now)
- **HTTP/4** — **no published standard / RFC exists.** Hard defer (spec gap, not effort).
- **Async io_uring/kqueue/IOCP backends** — C1 locks cooperative-over-scheduler; the async backend
  only replaces the blocking syscalls behind the same surface, adds **no token/surface** → perf
  follow-up.
- **WASM listening server** — platform impossibility (no sockets); honestly marked, never attempted.

### Capstone
- **CAPSTONE** — NATIVE: a real routed `api` server + a `websocket` endpoint + an outbound `http.get`
  client + a `compress.gzip` round-trip in one program. WASM: the same client calls + the
  synthetic-request routing proof. Both green. → Wave 0–2.

### 2.1 Concurrency hygiene
Shared files: `CMakeLists.txt` (`CORE_SOURCES` + `teko_rt` lib), `build-crypto-reactor.sh`
(`SRCS`/`EXPORTS` — **client subset only**, server TUs excluded), and the runtime-id→symbol table
(`emit_native_hosted.c`). Mitigations set up front by the Master:
- **Pre-allocated `OP_CALL_RUNTIME` id ranges per track:** net-client 60–69 · net-server(native)
  70–79 · http 80–99 · ws 100–109 · rpc 110–119 · compress-core 120–139 · brotli 140–149 ·
  zstd 150–159 · lzma 160–169 · lz4 170–179 · router 175–179 *(see note)* · TLS 180–219 ·
  H2/HPACK 220–239 · QUIC 240–259 · H3/QPACK 260–279 · gRPC/protobuf 280–299.
  *(router/throttle ids finalized at T2; ranges are reserved, not yet emitted.)*
- **Append-only** edits to the CMake lists and reactor `SRCS`/`EXPORTS` at distinct anchors;
  **native-only server TUs are added to `teko_rt`/`CORE` but NOT to the reactor `SRCS`** (gated
  `#if !defined(__wasm__)` as a second guard).
- Prefer `OP_CALL_RUNTIME` ids over new `OP_*` opcodes (less shared-file contention).

---

## 3. Breadcrumb table (medium → Tech Lead; small → Developer)

| Track | Wave | Role | Native | WASM | Makes live | Deps |
|---|---|---|---|---|---|---|
| T1a socket client | 0 | Tech Lead | ✓ | ✓ | enabler | — |
| T1b socket server (native-only) | 0 | Tech Lead | ✓ | ✗ (marked) | enabler | — |
| HTTP-CODEC | 0 | Tech Lead | ✓ | ✓ | (codec) | — |
| WS-CODEC | 0 | Developer | ✓ | ✓ | (codec) | — |
| ROUTER-CORE (radix + synthetic dispatch) | 0 | Tech Lead | ✓ | ✓ | (router engine) | — |
| C-CORE compression | 0 | Tech Lead | ✓ | ✓ | `compress.*` core | — |
| C-BR / C-ZSTD / C-LZMA / C-LZ4 | 0 | TL×3 + Dev | ✓ | ✓ | `compress.{brotli,zstd,lzma,lz4}` | C-CORE surface |
| TLS-RT | 0 | Tech Lead | ✓ | ✓ | (runtime) | — |
| PROTOBUF-CODEC | 0 | Developer | ✓ | ✓ | (codec) | — |
| H2-CODEC | 0 | Tech Lead | ✓ | ✓ | (codec) | — |
| QUIC-CODEC | 0 | Tech Lead | ✓ | ✓ | (codec) | — |
| T2 socket wiring | 1 | Tech Lead | client+server | client | `net.*` | T1a,T1b |
| HTTP-INT (client) | 1 | Tech Lead | ✓ | ✓ | `http.*` | T2,HTTP-CODEC |
| HTTP-SRV (native server) | 1 | Tech Lead | ✓ | synthetic | (server engine) | T2,HTTP-CODEC,ROUTER-CORE |
| WS-INT (client) | 1 | Tech Lead | ✓ | ✓ | `ws.*` client | T2,HTTP-CODEC,WS-CODEC |
| RPC | 1 | Developer | server+client | client | `rpc` | T2 |
| UNITS throttle | 1 | Developer | ✓ | ✓ | `kbps/mbps/gbps` | T2 |
| TLS-INT | 1 | Tech Lead | client+server | client | `tls.*` | T2,TLS-RT |
| COMPRESS-INT | 1 | Tech Lead | ✓ | ✓ | Content-Encoding/permessage-deflate | HTTP/WS-INT,C-CORE |
| ROUTER-NATIVE (api/middleware/verbs) | 2 | Tech Lead | real server | synthetic | `api middleware get post put delete patch head options` | HTTP-SRV/ROUTER-CORE |
| WS-SRV (native) | 2 | Tech Lead | real server | synthetic | `websocket` | HTTP-SRV,WS-CODEC |
| H2-INT | 2 | Tech Lead | ✓ | client | `http2.*` | TLS-INT,H2-CODEC |
| QUIC-INT | 2 | Tech Lead | ✓ | client | `quic.*` | T1(UDP),TLS-RT,QUIC-CODEC |
| GRPC full | 3 | Tech Lead | ✓ | client | `grpc.*` | H2-INT,PROTOBUF |
| H3-INT | 3 | Tech Lead | ✓ | client | `http3.*` | QUIC-INT,QPACK |
| CAPSTONE | — | Master | ✓ | ✓ | — | 0–2 |

**Token-completeness milestone** (every reserved web token + units live, native real + WASM
synthetic/host proof) = **ROUTER-NATIVE + WS-SRV (Wave 2) + RPC/UNITS (Wave 1)** — reachable without
TLS/H2/QUIC/H3/gRPC.

---

## 4. Compression tiering (owner request)

`src/runtime/teko_compress.c` (+ siblings) = single C source of truth, KAT'd vs RFC/zlib/reference
vectors. **`compress.*` is a free dotted surface** (verified: no reserved compression token —
`bundle`/`minify` are Phase-20 asset tooling, unrelated).

- **CORE (Phase 19, Wave 0):** DEFLATE (RFC 1951 inflate+deflate) + zlib (RFC 1950) + gzip
  (RFC 1952) + CRC32 + Adler-32 — needed for a credible HTTP/WS client AND server (Content-Encoding,
  Transfer-Encoding, permessage-deflate).
- **PARALLEL tracks:** brotli (RFC 7932, HTTP `br`), zstd (RFC 8878), lzma/lzma2 (xz), lz4.

### SAST / security (gate + docs)
Compression + encryption enables **CRIME/BREACH** compression-oracle attacks. Therefore: (1) **never
auto-compress secret/encrypted payloads by default** — compression is a **separate, opt-in layer**;
(2) **TLS 1.3 carries no record compression** (anti-CRIME) and we keep it so; (3) **safe composition
only — does NOT change the Phase-13 crypto primitives.** Decompression is the highest-risk
untrusted-input surface: enforce output size caps (**decompression-bomb** guard), bounds-checked
window/dictionary access, and integer-overflow-safe length arithmetic in every inflate path.

---

## 5. Size estimate (revised — native server adds ~1.5–2.5k)

- **A1 token-completeness core** (T1a+T1b+T2, HTTP codec, HTTP client+native server, ROUTER core +
  native, WS codec+client+native server, RPC, UNITS, compression CORE): **~7,000–9,500 LOC** — the
  milestone where every reserved web token + units is live (native real server/client + WASM
  client/synthetic).
- **Compression parallel codecs** (brotli/zstd/lzma/lz4 + KATs): **~6,000–9,000 LOC**.
- **TLS track** (TLS-RT + TLS-INT, 1.3 primary + 1.2 secondary, client + native server):
  **~3,000–4,500 LOC**.
- **Advanced protocol tracks** (protobuf, H2+HPACK, gRPC, QUIC, H3+QPACK): **~10,000–14,000 LOC**.
- **Full parallel ambition** (everything except truly-deferred HTTP/4 + async backends + WASM
  server): **~26,000–37,000 LOC** across ~12 concurrent Wave-0 tracks + downstream waves.

Max-parallelism holds: the ~12 Wave-0 tracks have no inter-dependencies; serialization is confined
to the integration nodes (T2 and the `*-INT`/`*-SRV` steps).

---

## 6. Bars (unchanged) + per-breadcrumb SAST gate
One increment/commit; ASan+UBSan **both** dispatch paths + TSan; **16 native goldens byte-identical**
(all new emission gated behind `uses_*`; native-only server TUs additionally `#if !defined(__wasm__)`
guarded and excluded from the reactor); 4 CI gates green incl. Windows MSVC (x86_64+arm64);
executable `.tks` proof per surface on **native AND WASM** (server tokens: native real + WASM
synthetic-request); no dead tokens; the human merges the principal PR; **English only**.

**SAST focus for this phase:** both the **server** (accepted connections, parsed requests) and the
**client** (server responses) handle **attacker-controlled** bytes; compressed input too. Every
parser (HTTP request/response, WS frame, every inflate path, TLS records, protobuf, HPACK/QPACK,
QUIC packets, the router's path/method match) is an injection/overflow surface. Enforce: bounds/
length checks before every copy; decompression output caps (bomb guard); integer-overflow-safe size
arithmetic; `calloc` zero-init + clear ownership (no UAF/double-free); width-correct casts incl.
Windows LLP64 `intptr_t`/`int32_t`; no format-string/path-traversal on URLs/headers/routes; the
accept loop hardened against fd exhaustion / slowloris (timeouts, caps); new emission gated +
feature-free output byte-identical; compression kept opt-in, never auto-applied to encrypted/secret
payloads.
