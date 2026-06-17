# Phase 19 — Native Networking & Web Architecture

> **Status:** IN PROGRESS — branch `feat/phase-19-networking` (principal Draft PR → `main`).
> Planned and managed under the project-wide **Orchestration Doctrine**
> (`docs/ORCHESTRATION_DOCTRINE.md`): the Master Agent (Opus) decomposes this phase into
> breadcrumbs and delegates to Tech Lead (Sonnet) / Developer (Haiku) subagents; each breadcrumb
> ships as a sub-PR **into this phase branch** (PM merges after review + SAST + CI); the **owner
> (PO) merges the principal PR into `main`** at the end.

This document is the canonical plan for Phase 19. It references — never duplicates — `docs/plan.md`
(§"PHASE 19: Native Networking & Web Architecture") and `CLAUDE.md` (non-negotiable bars).

---

## 1. Scope (from `docs/plan.md` §Phase 19)

The roadmap describes "comprehensive networking from OSI Layer 4 to Layer 7, plus the native web
keyword surface", and lists, in prose:

- **Networking stack:** raw sockets; async TCP, UDP, **QUIC** via io_uring/kqueue/IOCP; native
  **TLS 1.3** built on the Phase-13 crypto primitives; polyglot routing over HTTP/1.x, **HTTP/2,
  HTTP/3, HTTP/4**, bidirectional **WebSockets** (`ws_chan`), and **gRPC** (`rpc`).
- **Native web architecture by keywords:** `api`, `middleware`, `get`, `post`, `put`, `delete`,
  `rpc`, `websocket`, `use` — AOT-compiled to **static Radix trees**.

### 1.1 The binding constraint = the reserved token matrix (NOT the protocol zoo)

The non-negotiable **"no dead tokens"** bar applies to **lexer tokens**. The reserved web tokens
already present in the lexer (Phase 12, `src/lexer.h` / `src/lexer.c`) that Phase 19 **must make
live with an executable `.tks` proof on native AND WASM** are exactly:

```
api  middleware  get  post  put  delete  patch  head  options  rpc  websocket
```

(`use` is already live — it is the FFI/import keyword, `src/parser.c:36,106`.) The **bandwidth
literal units** `kbps` / `mbps` / `gbps` (`LIT_UNIT_*`) are also currently captured-but-dead and
must be consumed by a live surface.

Crucially: **`net`, `socket`, `tcp`, `udp`, `tls`, `http`, `quic`, `ws_chan`, `grpc` are NOT
tokens.** QUIC / HTTP-2/3/4 are *prose ambitions*, not reserved keywords — **deferring them creates
no dead token**. The low-level surfaces therefore use **dotted-identifier lexemes**
(`net.tcp_open(...)`, `http.get(...)`, `tls.client(...)`) exactly like the Phase-14/15 `duplex.*`,
`delayed.*`, `time.*` surfaces. This is what makes Phase 19 tractable: the binding obligation is the
finite token matrix above, not the full protocol set.

### 1.2 Greenfield

There is **no** pre-existing networking/socket/IO code in the repo (`grep` for
`sys/socket`/`io_uring`/`kqueue`/`epoll`/`IOCP`/`WSASocket` → nothing). Phase 19 starts from zero and
follows the established per-feature pattern: **portable C runtime = single source of truth → opcode
family or `OP_CALL_RUNTIME` id → native `teko_rt_*` wrapper + WASM reactor import → executable
`.tks` proof on native AND WASM**.

---

## 2. Design forks requiring the owner's decision

> These are surfaced to the PO/PM **before** implementation. The recommended option is marked.

- **Fork A — phase ambition.** **(A1, recommended)** ship a pragmatic, fully-tested MVP that makes
  every reserved token live: cooperative TCP+UDP sockets, TLS 1.3 (ECDHE+AEAD), HTTP/1.1
  client+server, the `api`/`middleware`/verb router (AOT radix tree), a minimal `websocket`
  (RFC 6455 framing) and a minimal length-prefixed `rpc` — and **document** QUIC, HTTP/2/3/4, full
  gRPC wire format, and async io_uring/kqueue/IOCP as deferred follow-ups. **(A2)** build the full
  async event-loop matrix first, fewer protocols. **(A3)** leaner still: sockets + router only,
  defer TLS/HTTP/WS.
- **Fork B — WASM networking model.** WASM cannot open raw sockets. **(B1, recommended)** host-import
  model: the emitted module imports `env.teko_net_*`; the `.mjs` proof provides a Node-backed host
  (loopback echo/HTTP server), mirroring how entropy (`env.teko_random`) and time
  (`env.teko_now_*`) are already host imports. **(B2)** WASI-Preview-2 sockets only (wasmtime), no
  browser. **(B3)** WASM net surface stubbed, proofs native-only — **rejected**: violates the
  "proof on native AND WASM" bar.
- **Fork C — native async model.** **(C1, recommended)** cooperative/blocking sockets integrated
  with the existing Phase-14 green-thread scheduler (a routine blocked on `recv` yields); real
  async backends (io_uring/kqueue/IOCP) deferred. **(C2)** implement the full async abstraction now.
- **Fork D — TLS 1.3 posture (note, not a blocker).** Phase 13 RSA private ops are **not**
  constant-time and PKCS#1 v1.5 decrypt is padding-oracle-prone (documented). Phase 19 TLS 1.3 will
  therefore prefer the **CT-clean** Phase-13 primitives: **X25519 / P-256 ECDHE + AES-GCM /
  ChaCha20-Poly1305 + HKDF + Ed25519 / ECDSA verify**; RSA is used only for certificate signature
  *verification* (public-key, acceptable). Same "future work" posture as AES-NI.

---

## 3. Decomposition (dependency-ordered) — filled per the assumed A1/B1/C1/D path

> Pending owner confirmation of the forks. Medium tasks → Tech Lead (Sonnet); small/standalone →
> Developer (Haiku). Each row becomes one or more breadcrumb sub-PRs into this branch. Detailed
> BLOCK-2 specs live in each sub-PR body.

| # | Deliverable | Owner role | Makes live | Depends on |
|---|-------------|-----------|-----------|-----------|
| 0 | **CI-trigger gating** (this setup commit) — widen the 4 workflows' `pull_request` filter to `feat/**` so sub-PRs into the phase branch are gated | Master setup | — | — |
| 1 | **Socket runtime foundation** `teko_socket.c` (handle table, state machine, POSIX+Winsock TCP/UDP) + Unity KATs | Tech Lead | (enabler) | 0 |
| 2 | **Socket wiring** — CMake + `teko_rt_socket_*` + reactor SRCS/EXPORTS + frontend `net.*` + native/WASM emit + `.tks`/`.mjs` proofs | Tech Lead | `net.*` (dotted) | 1 |
| 3 | **HTTP/1.1 runtime** `teko_http.c` (request parser + response builder + chunked) + KATs, wired over sockets | Tech Lead | `http.*` (dotted) | 2 |
| 4 | **Web keyword surface / router** — `api`/`middleware`/verb grammar + AOT static radix tree + dispatch over HTTP runtime | Tech Lead | `api middleware get post put delete patch head options` | 3 |
| 5 | **WebSocket** `teko_websocket.c` (RFC 6455 framing + handshake via Phase-13 SHA-1+base64) + surface | Tech Lead | `websocket` | 3 |
| 6 | **Minimal `rpc`** — length-prefixed request/response over the socket runtime (full gRPC/HTTP-2 deferred) | Developer | `rpc` | 2 |
| 7 | **Bandwidth units** — consume `kbps`/`mbps`/`gbps` in a `limit(...)` throttle surface | Developer | `LIT_UNIT_*bps` | 2 |
| 8 | **TLS 1.3** `teko_tls13.c` (key schedule + handshake + record over Phase-13 AEAD) — *size/defer is Fork A/D* | Tech Lead | `tls.*` (dotted) | 2, (3) |
| 9 | **Capstone + deferred doc** — one `.tks` combining socket+HTTP+routed `api`+WS on native AND WASM; honest "deferred" section | Master | — | 1–8 |

---

## 4. Bars (unchanged, in force for every breadcrumb)

One increment per commit; ASan+UBSan on **both** dispatch paths + TSan clean; **16 native emitter
goldens byte-identical** (all new emission gated behind `uses_*` flags); 4 CI gates green incl.
Windows MSVC (x86_64 + arm64); executable `.tks` proof per surface on **native AND WASM**; no dead
tokens; the human merges the principal PR. All content (code AND docs) in **English**.

### Per-breadcrumb SAST gate (mandatory before releasing the next)
Injection / format-string / path-traversal on every input surface (network input is **untrusted** —
this phase's surfaces parse attacker-controlled bytes, so bounds + length checks are paramount);
C-runtime memory safety (buffer overflow/OOB, UAF/double-free via `calloc` + clear ownership,
integer overflow on length/size arithmetic, unsafe casts incl. Windows LLP64 `intptr_t`/`int32_t`);
confirm new emission stays gated and feature-free output byte-identical.

---

## 5. Deferred (documented, no dead tokens created)
QUIC; HTTP/2, HTTP/3, HTTP/4; full gRPC wire format (HTTP/2 framing); async io_uring/kqueue/IOCP
event-loop backends; browser-native WASM sockets beyond the Node/WASI host-import proof. These are
prose ambitions in `docs/plan.md`, not reserved tokens — they remain future work.
