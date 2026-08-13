---
section: design
created: 2026-08-13
status: DESIGN — CATALOG (proposal, not crumbs). No product-code edited, no reseed, no build,
        `teko test` NOT run in any form. Read-only over product code; this file is the sole edit.
role: architect — "the architect brings what CAN be added to the language for native developer
      support" (owner mandate). Owner picks phases and argues priorities part-by-part.
supersedes-scope: this is the CONSOLIDATED expansion catalog around the owner-named areas
        (sorting · crypto · networking OSI 4/5/6/7 · databases · protocols SOCKS/AMQP/RPC/gRPC ·
        GUI). It re-frames the pre-reboot roadmaps (TEKO_ROADMAP_NET_CRYPTO.md,
        TEKO_ROADMAP_DB.md, TEKO_ROADMAP_STDLIB_CORE.md) onto the SEALED 0.3.1 framing.
sealed-framing: exp/pub (owner ruling) · self-`.tkh` monolith (§16) · FFI §16 (`extern fn … from`,
        `ref`, `extern type`, hand-declared per-platform constants in `teko::sys`, NO extern-macro) ·
        §10 concurrency (`spawn`/`sched`/`chan<T>`/`await`→`Intent`, journaling) · #254 generics.
non-goals: no ORM, no package manager, no async rewrite (async is additive over sync signatures).
---

# Teko stdlib — EXPANSION CATALOG for native developer support

> **What this is.** A single, ratifiable catalog of *what can be added* to Teko's stdlib to make it a
> productive native-development language. Every entry is a concrete **proposal** (module purpose, `exp`
> surface sketch, dependencies, native-feasibility, phase tier), never a bare open question. The owner
> picks phases and argues priorities part-by-part; where two Laws pull against each other the tension is
> named with a recommended resolution.
>
> **What this is NOT.** Not a crumb plan, not an implementation. It fixes *namespaces + `exp` surface +
> build order*; each entry becomes its own implementation issue later. Nothing here is buildable-today by
> itself — it is the map that the per-module issues execute against.

---

## 0. How to read a catalog entry

Each module proposal carries five fixed fields, so the owner can weigh them uniformly:

| Field | Meaning |
|---|---|
| **Surface** | the `exp` public API sketch (signatures). Internal helpers are `pub`; file-local is private. Sketches are signature-level, not full Javadoc declarations — the implementer writes the full doc-comment per the W15 law when the entry's issue lands. |
| **Deps** | on other stdlib modules · on §16 libc-direct FFI (a specific `extern fn`/`extern type`) · on #254 generics · on §10 concurrency · on the own-linker / self-`.tkh`. |
| **Native-feasibility** | **pure-Teko** (bounded logic over `[]byte`/`str`/`f64`, fully `.tkt`-testable) vs **needs FFI** (a libc/OS `extern fn` per §16 — no C-macro FFI; constants hand-declared per-platform in `teko::sys`). |
| **Tier** | the phase it lands in (§4). |
| **Verify** | `.tkt` (pure logic) and/or a native regression example (inputs → expected native exit code). |

**Standing constraints that shape every entry (sealed — not reopened here):**

- **exp/pub:** everything dev-visible is `exp` (enters the self-`.tkh`); shared inter-module helpers are
  `pub`; file-local is private. New surfaces are curated `exp`, minimal template surface for generics.
- **§16 marshalling is `ptr`-only.** `extern` params/returns are primitives + `ptr`/`uptr`/`void`.
  Aggregates cross via `teko::mem::as_ptr(str|[]byte): ptr` + `.len`; data comes BACK via the bounded
  copy-in primitives (`str_from_cstr`, `bytes_from_ptr`) or a `teko_rt` shim. **Variadic extern is
  forbidden.** Opaque handles are `extern type` (lowers to `void *`). This is why the byte-buffer
  keystone below gates all socket/crypto FFI.
- **No index-assignment.** State arrays (cipher S-boxes, hash blocks, sort scratch) are built
  functionally or through an arena-backed byte region — never `buf[i] = x`.
- **`#os("unix")`/`#os("windows")`** guards per-OS shape differences; name-only differences resolve in
  `.tkp [extern.symbols]`/`[extern.libs.*]`. glibc vs musl is one `"linux"` path.
- **Async is out of scope but additive.** Every blocking signature is shaped so an `await`-prefixed
  `Intent<T>` variant (§10.3) wraps it later without a rewrite.

---

## 1. Overview map — areas × tiers × gating dependencies

```
                                       gated on
area            first module            #254   own-linker/  §10       libc-FFI
                                        gen.   self-.tkh    concur.   (§16)
──────────────────────────────────────────────────────────────────────────────
Foundations     io · iter · sys · mem   part.   —            —         some
Sorting         sort (extend)            gen.API —            —         no (pure)
Crypto          crypto::hash            no      —            —         rand only
Encoding        encoding::json          no      —            —         no (pure)
Compression     compress::deflate       no      —            —         no (pure)
Networking L4   net::tcp                no      —            add-on    YES (sockets)
Networking L5/6 net::tls                no      —            add-on    YES (provider)
Networking L7   net::http              no      —            add-on    via L4
Databases       db + db::postgres       no      link-keyst.  add-on    via L4 / sqlite FFI
Protocols       net::proxy · grpc      no      —            add-on    via L4 + encoding
GUI (research)  ui::*                   part.   —            add-on    YES (windowing)
```

**Legend of the big gates (§2 details each):**

- **KEYSTONE-BUF** — byte-buffer transport across raw externs (`teko::mem::as_ptr` + a `Buf` region).
  Blocks every socket/crypto-rand/FFI-fill unit. Pure-Teko units (sort, hash-over-`[]byte`, encoding,
  compression) need it **not** and can be catalogued/implemented first.
- **KEYSTONE-LINK** — on-demand FFI linking (link `-l<lib>` only for reachable externs) + the
  self-`.tkh` self-link. Blocks the FFI-driver DB path (sqlite) from imposing a lib on non-users.
- **#254** — methods on generic types. Blocks the generic *comparator/collection* surface (a generic
  `sort<T>`, `teko::collections`); the concrete monomorphic surfaces (`[]str`/`[]i64`/`[]byte`) do not
  wait for it.
- **§10** — `spawn`/`chan<T>`/`await`→`Intent`. Networking/DB drivers are designed **sync-first**; the
  concurrent server/pool variants are additive over these signatures.

---

## 2. Foundations & gating keystones (design AROUND these — they are the enablers)

These are not new "areas" but the seams every area rides. They already exist as design in sibling docs;
this catalog re-states them as the enabling layer and fixes their `exp` posture.

### 2.1 `teko::mem` + KEYSTONE-BUF — the byte region across the FFI boundary
**Purpose.** One arena-backed mutable byte region a syscall can fill through a pointer, then Teko reads
back by length — reconciling `recv(fd, buf, len)` with the no-index-assign ruling.
**Surface (sketch).**
```
exp type Buf                                   // extern-type-free: arena-backed region handle
exp fn  buf_make(cap: u64): Buf
exp fn  buf_ptr(b: Buf): ptr                   // address handed to C (write target)
exp fn  buf_slice(b: Buf, len: u64): []byte    // read back the first `len` bytes
exp fn  as_ptr(s: str): ptr                    // §16 legislated marshalling
exp fn  as_ptr_bytes(b: []byte): ptr
exp fn  bytes_from_ptr(p: ptr, len: u64): []byte
exp fn  str_from_cstr(p: ptr): str
```
**Deps.** §16 marshalling clause (already legislated); no #254. **Native-feasibility.** the `as_ptr`/
`bytes_from_ptr`/`str_from_cstr` primitives are the ONE compiler-plumbed seam (KEYSTONE); `Buf` is
arena-backed Teko over them. **Tier P0.** **Verify.** native smoke: `extern` `memset`/`read` into a
`Buf`, read back, exit code = byte count.

### 2.2 `teko::sys` — hand-declared per-platform libc constants (no extern-macro)
**Purpose.** The curated home for the C constants FFI needs (`SOCK_STREAM`, `AF_INET`, `O_RDONLY`,
`SEEK_SET`, errno values) — **hand-declared per platform** behind `#os`, since Fork D (extern-macro /
extern-comptime) is rejected (no C toolchain).
**Surface (sketch).**
```
#os("linux")   exp const SOCK_STREAM: i32 = 1
#os("macos")   exp const SOCK_STREAM: i32 = 1
#os("windows") exp const SOCK_STREAM: i32 = 1
exp const AF_INET: i32 = 2      // (per-OS blocks; values curated from each platform's headers)
exp const AF_INET6: i32 = ...   // 10 on linux, 30 on macos, 23 on windows — genuine #os split
```
**Deps.** `#os` conditional prune (legislated). **Native-feasibility.** pure declarations — no runtime.
**Tier P0** (grows per consumer). **Verify.** `.tkt` asserting the constant a program reads equals the
platform's real value (spot-checked against a native example that `printf`s the libc macro).

> **Law tension (M.3 honesty vs maintenance).** Hand-declaring constants risks a value drifting from a
> platform header. **Resolution:** each `teko::sys` constant carries a Javadoc `@see <header>` and a
> native regression that reads the real macro and asserts equality — the honesty is *tested*, not
> trusted. This is cheaper than a C-toolchain dependency (M.4: refine with real data).

### 2.3 `teko::io` / `teko::iter` (exist; fix `exp`) — the compose seam
**Purpose.** `Reader`/`Writer`/`Seeker`/`Closer` interfaces so `tcp`, `tls`, `gzip`, `crypto`, `file`
compose (`copy(gzip_w, tcp_r)`); `Iterator<T>` lazy adapters. Already in tree (`src/io`, `src/iter`).
**Catalog action.** promote the composing surface to `exp`; wire net/compress/crypto streams to
`Reader`/`Writer` as they land. **Deps.** interfaces (✅), #254 for the fully-generic `Iterator<T>`
terminals (the concrete `str`/`byte` iterators already ship). **Tier P0 (posture) / P1 (wiring).**

### 2.4 KEYSTONE-LINK — on-demand FFI linking + self-`.tkh`
**Purpose.** Link `-l<lib>` for a driver **iff** a reachable `extern` from it survives; the self-`.tkh`
lets the final program link against the pre-compiled monolith. So a dev who never touches SQLite needs
no `libsqlite3`, and unused stdlib is dead-stripped (`--gc-sections`).
**Deps.** reachability over externs (checker) + lib collection (codegen) + linker flags (build). No
#254. **Tier P0** (compiler-side; the ONE compiler unit the FFI-driver DB path needs). **Verify.** smoke
with two extern-backed modules, `main` calls one → the other's `-l` is absent from the link line.

---

## 3. Per-area module proposals

### A. Sorting — `teko::sort` (extend the existing module)

`teko::sort` already ships stable merge sort over `[]str` (natural + lexicographic) and a `cmp` family.
The expansion makes it **complete and generic** without regressing the concrete fast paths.

| # | Module | Surface (sketch) | Deps | Native-feasibility | Tier |
|---|---|---|---|---|---|
| A1 | `teko::sort` primitives | `exp fn sort_i64(xs: []i64): []i64` · `sort_u64` · `sort_f64` · `sort_bytes(xs: []byte): []byte` · stable variants; `exp fn is_sorted_i64(xs: []i64): bool` | pure Teko; `teko::list` | **pure** — bounded, `.tkt` | **P1** |
| A2 | search + select | `exp fn bsearch_i64(xs: []i64, key: i64): u64?` · `exp fn nth_i64(xs: []i64, k: u64): i64` (quickselect) · `exp fn partition_point_i64(...)` | pure Teko | **pure** | **P1** |
| A3 | generic comparator API | `exp interface Ord<T> { fn cmp(self, o: T): i32 }` · `exp fn sort<T: Ord>(xs: []T): []T` · `exp fn sort_by<T>(xs: []T, cmp: fn(T,T): i32): []T` (closure) | **#254** (method on generic type) · closures (✅) | **pure** | **P2** (gated #254) |
| A4 | key/stable helpers | `exp fn sort_by_key<T,K: Ord>(xs: []T, key: fn(T): K): []T` · `exp fn dedup<T: Ord>(xs: []T): []T` | A3, #254 | **pure** | **P2** |

**Native-feasibility note.** Sorting is 100% pure Teko (no OS dep) and `.tkt`-testable against fixtures —
it is the *cheapest first win* and validates the closure/comparator ergonomics that crypto and encoding
reuse. The generic `sort<T>` (A3/A4) is the only part gated on #254; the monomorphic `sort_i64`/… land
now and remain the fast paths even after A3 exists.

**Example (illustrative, monomorphic path — buildable in P1):**
```
/**
 * Stably sort a slice of i64 in ascending order, returning a fresh sorted slice; the input is never
 * mutated in place (M.0 value-thread: one output buffer, no `xs[i] = …`).
 *
 * @param xs  the source slice, read only
 * @return    a new slice with `xs`'s elements in non-decreasing order
 */
exp fn sort_i64(xs: []i64): []i64 { /* top-down merge, mirrors msort_str */ }
```

---

### B. Cryptography — `teko::crypto::*`

`teko::crypto` today has hashes + a CSPRNG seam over `[]byte`. The expansion is a full security surface:
each family is its own child namespace/file. Legacy primitives (MD5, SHA-1, 3DES, PKCS#1 v1.5) ship
ONLY where a live protocol needs them and are Javadoc-`@deprecated`/marked `legacy`. **Wrapping
arithmetic** (crypto must not panic on int overflow) is the `teko::math::checked` family (`wrapping_*`);
**constant-time** is a hand-audited convention per unit (`ct_eq`, no data-dependent table lookups) — a
`ct`-typed language feature is a *separate* proposal, not assumed here.

| # | Module | Surface (sketch) | Deps | Native-feasibility | Tier |
|---|---|---|---|---|---|
| C0 | `teko::crypto` core | `exp fn ct_eq(a: []byte, b: []byte): bool` · `exp fn secure_zero(b: Buf)` · `exp type CryptoError` | KEYSTONE-BUF (for zeroing) | **pure** | **P1** |
| C1 | `teko::crypto::hash` | `exp fn sha256(m: []byte): []byte` · `sha512` · `sha3_256` · `shake128(m, out_len)` · `blake2b`; streaming `exp type Sha256 { fn update(self, b: []byte): self; fn finish(self): []byte }`; `legacy sha1`/`md5` | C0; wrapping-arith | **pure** — NIST/RFC vectors | **P1** |
| C2 | `teko::crypto::mac` | `exp fn hmac_sha256(key, msg: []byte): []byte` · `poly1305` · `cmac`/`gmac` (over AES) | C1, C4 | **pure** | **P1/P2** |
| C3 | `teko::crypto::kdf` | `exp fn hkdf_sha256(ikm, salt, info: []byte, len: u64): []byte` · `pbkdf2_sha256(...)` | C1, C2 | **pure** | **P2** |
| C3b| `teko::crypto::password` | `exp fn argon2id(pw, salt: []byte, ...): []byte` · `scrypt` · `legacy bcrypt`; `exp fn verify(...)` | C3 | **pure** (memory-hard, heavy) | **P2** |
| C4 | `teko::crypto::cipher` | `exp fn aes_ctr(key, nonce, data: []byte): []byte` · CBC/CFB/OFB · `chacha20(...)`; `legacy des3` | C0; wrapping-arith | **pure**, constant-time (audit) | **P2** |
| C5 | `teko::crypto::aead` | `exp fn aes_gcm_seal(key, nonce, aad, pt): []byte` · `open(...): []byte \| error` · `chacha20_poly1305_seal/open` | C4, C2, C6 | **pure** — RFC 8439 vectors | **P2** |
| C6 | `teko::crypto::rand` | `exp fn rand_bytes(n: u64): []byte \| error` (fill a `Buf`) | KEYSTONE-BUF; **FFI** `getrandom`/`getentropy`/`BCryptGenRandom` `#os` | **needs FFI** (no non-native fallback) | **P1** |
| C7 | `teko::crypto::pk` (asymmetric) | `exp fn x25519(sk, pk: []byte): []byte` · `ed25519_sign/verify` · RSA-OAEP/PSS · ECDSA P-256 · ECDH | C0, C1, C6, S-ASN1, `math::bigint` | **pure** for X25519/Ed25519; provider or bigint for RSA/ECDSA | **P2/P3** |
| C8 | `teko::crypto::x509` | `exp fn parse_cert(der: []byte): Cert \| error` · `verify_chain(...)` · SAN match | S-ASN1, C7, C1 | **pure** | **P3** |
| C9 | app security helpers | `exp fn jwt_sign/verify(...)` · `totp(...)`/`hotp(...)` (RFC 6238/4226) · COSE | C1/C2/C3/C7 + S-JSON | **pure** | **P3** |
| C-PGP | `teko::crypto::pgp` | OpenPGP (RFC 9580) encrypt/decrypt/sign/verify, ASCII armor, keyring | C1/C4/C5/C7/C6 + `compress::zlib` | **pure**, top-of-stack | **P3** |

**Native-feasibility note.** The whole hash/MAC/KDF/cipher/AEAD stack is pure Teko over `[]byte` —
bounded, `.tkt`-testable against RFC/NIST vectors, **no OS dependency and no keystone** — so it is the
*second cheapest first win* after sorting. Only two pieces touch the OS: **C6 rand** (a CSPRNG must come
from the OS — one `extern fn` per platform, needs KEYSTONE-BUF) and, optionally, provider-backed
RSA/ECDSA (C7). **Recommendation:** ship the pure-Teko C0–C5 first; C6 rand as soon as KEYSTONE-BUF
lands; X25519/Ed25519 (self-contained, well-specified) as the earliest hand-rolled public-key; RSA/ECDSA
provider-first, pure-Teko later on `math::bigint`.

**Supporting: `teko::math::bigint` + `teko::math::checked`.** `checked` (`wrapping_*`/`checked_*`/
`saturating_*`) already exists (M1). `bigint` — a general constant-time big-integer for C7 — is a
**math** module (not crypto-private): `exp type BigInt` with `add/mul/mod_exp/inverse`. Pure Teko, P2,
gates RSA/ECDSA.

---

### C. Networking — `teko::net::*` (OSI layers 4/5/6/7)

`teko::net` is greenfield (the `src/net` dir is empty). The root holds cross-protocol commons; each
protocol is its own child namespace/file. **Sync-first**; async rides §10 additively. All logic is
pure-Teko *codecs* (`.tkt`-testable); only the socket/TLS syscalls are FFI.

**Layer 4 — transport:**

| # | Module | Surface (sketch) | Deps | Native-feasibility | Tier |
|---|---|---|---|---|---|
| N0 | `teko::net` core | `exp type IpAddr` (v4/v6) · `exp type SocketAddr { ip, port }` · parse/format · `hton16/32`,`ntoh16/32` · `exp type Socket` (extern-type handle) · `exp type NetError` | KEYSTONE-BUF; `teko::sys` (AF_*/SOCK_*) | address/byte-order **pure**; Socket is `extern type` | **P2** |
| N1 | `teko::net::tcp` | `exp type TcpStream { fn read(self, into: Buf): u64\|error; fn write(self, from: []byte): u64\|error; fn close(self): error? }` · `exp fn connect(a: SocketAddr): TcpStream\|error` · `exp type TcpListener { fn accept(self): TcpStream\|error }` | N0; **FFI** `socket/connect/bind/listen/accept/send/recv/close` (+`WSAStartup`/`closesocket` `#os`) | **needs FFI** | **P2** |
| N2 | `teko::net::udp` | `exp type UdpSocket { fn send_to(...); fn recv_from(self, into: Buf): (u64, SocketAddr)\|error }` | N0; **FFI** `sendto/recvfrom` | **needs FFI** | **P2** |
| N2b | `teko::net::unix` | AF_UNIX stream+datagram over a path (POSIX first) | N0; **FFI** `sockaddr_un` | **needs FFI** | **P3** |
| N-QUIC | `teko::net::quic` | connection/stream state machine, loss recovery, TLS-1.3-in-QUIC | N2, N3(TLS 1.3), C5(AEAD) | codec **pure**, large | **P3** |

**Layer 5/6 — session + presentation (TLS/DTLS):**

| # | Module | Surface (sketch) | Deps | Native-feasibility | Tier |
|---|---|---|---|---|---|
| N3 | `teko::net::tls` | `exp type TlsStream { read/write/close }` (same shape as TcpStream) · `exp fn client(tcp: TcpStream, sni: str): TlsStream\|error` · server; ALPN | N1; **FFI** OS provider (Secure Transport / OpenSSL `libssl` / SChannel) `#os` — OR pure-Teko on C4/C5/C7 later | **needs FFI (provider first)** | **P2** |
| N3b | `teko::net::dtls` | datagram TLS over UDP (WebRTC/QUIC-adjacent) | N2, N3 | provider/pure | **P3** |

**Layer 7 — application:**

| # | Module | Surface (sketch) | Deps | Native-feasibility | Tier |
|---|---|---|---|---|---|
| N4 | `teko::net::dns` | `exp fn resolve(host: str): []SocketAddr\|error` | N0; **FFI** `getaddrinfo/freeaddrinfo` | **needs FFI**; later pure DoH/DoT | **P2** |
| N5 | `teko::net::http` | `exp type Request`/`Response` · header map · `exp fn get(url: str): Response\|error` · `exp fn serve(addr, handler: fn(Request): Response): error?` · URL parser | N1 (client), N3 (https), S-JSON, `compress::gzip` | parser/encoder **pure**; transport via N1/N3 | **P2** |
| N6 | `teko::net::ws` | RFC 6455 upgrade + frame codec (fin/opcode/mask) | N5, C1 (SHA-1 accept-key) | codec **pure** | **P2** |
| N7 | `teko::net::sse` | `text/event-stream` codec (client+server) | N5 | **pure** | **P3** |
| N8 | `teko::net::http2` | HPACK + binary framing + stream multiplexing | N3 (ALPN) | codec **pure**, large; gates gRPC | **P3** |
| N10 | `teko::net::http3` | HTTP/3 over QUIC + QPACK | N-QUIC | **pure** codec | **P3** |
| N11 | `teko::net::mqtt` | MQTT 3.1.1/5.0 client (connect/pub/sub/QoS) | N1 (+N3) | codec **pure** | **P3** |
| N13 | `teko::net::redis` | RESP2/RESP3 codec + command layer | N1 (+N3) | **pure** | **P3** |
| N14 | `teko::net::{smtp,imap,pop3}` | mail send/retrieve; STARTTLS via N3 | N1, N3 | **pure** codec | **P3** |
| N16 | `teko::net::ssh` | client transport+auth+channels | N1, C4/C5/C7 (kex/aead) | **pure**, heavy | **P3** |

**Native-feasibility note.** Every protocol splits into a **pure-Teko codec** (parse/encode — the bulk,
`.tkt`-testable against RFC byte vectors) and a **thin FFI transport** (N1/N2 sockets, N3 TLS provider).
So HTTP/WS/Redis/MQTT/mail are *mostly pure* and only inherit FFI through the socket layer. **Concurrency
(§10):** `serve` is designed to `spawn` one coroutine per connection over `chan<T>`; the sync
`accept`→handle loop ships first, the concurrent server is additive.

> **Do NOT source the pre-reboot `feat/phase-19-networking` branches** (C/WASM reactor, pre-reboot
> syntax). The only portable input is RFC/NIST byte vectors, taken from the specs directly.

---

### D. Encoding / serialization — `teko::encoding::*` (feeds net + db)

`teko::encoding::{base64,csv,json,url}` exist. Expansion adds the formats net/db/crypto need. All pure
Teko, `.tkt`-testable, **no keystone** — parallelizable now.

| # | Module | Surface (sketch) | Deps | Feasibility | Tier |
|---|---|---|---|---|---|
| S-JSON | `teko::encoding::json` (extend) | `exp fn parse(s: str): Json\|error` · `exp fn encode(j: Json): str` · streaming | none | **pure** | **P1** |
| S-PB | `teko::encoding::protobuf` | varint/zigzag/length-delimited wire codec; `.proto` compiler is a later tool | none | **pure**; gates gRPC | **P2** |
| S-ASN1 | `teko::encoding::asn1` | DER/BER encode+decode + PEM framing | none | **pure**; gates x509/PK | **P2** |
| S-XML | `teko::encoding::xml` | pull + DOM parser + encoder; namespaces | none | **pure** | **P2** |
| S-YAML | `teko::encoding::yaml` | YAML 1.2 (k8s manifests) | none | **pure**, big grammar | **P3** |
| S-CBOR / S-MSGPACK | `teko::encoding::{cbor,msgpack}` | binary codecs (COSE/RPC payloads) | none | **pure** | **P3** |
| S-BSON | `teko::encoding::bson` | MongoDB document codec | none | **pure**; gates DB-MONGO | **P3** |
| S-MIME | form-urlencoded · multipart · base64-MIME | (ride with N5) | S base64 (✅) | **pure** | **P2** |

---

### E. Compression — `teko::compress::*` (extend the existing module)

`teko::compress` ships CRC-32 + ZIP STORE. All extensions are pure Teko over `[]byte`, `.tkt`-testable,
reused by HTTP `Content-Encoding`, archives, PGP.

| # | Module | Surface (sketch) | Deps | Feasibility | Tier |
|---|---|---|---|---|---|
| Z-DEFLATE | `teko::compress::deflate` | `exp fn deflate(src: []byte, level: i32): []byte` · `exp fn inflate(src: []byte): []byte\|error` | CRC-32 (✅) | **pure**; the keystone others wrap | **P1** |
| Z-GZIP | `teko::compress::gzip` | RFC 1952 framing over DEFLATE | Z-DEFLATE | **pure**; default HTTP encoding | **P1** |
| Z-ZLIB | `teko::compress::zlib` | RFC 1950 framing (Adler-32) | Z-DEFLATE | **pure** | **P1** |
| Z-ZIP+deflate | extend existing ZIP | method=8 entries | Z-DEFLATE | **pure** | **P2** |
| Z-BROTLI / Z-LZMA / Z-ZSTD | `teko::compress::{brotli,lzma,zstd}` | modern encodings | none | **pure**, large | **P3** |

---

### F. Databases — `teko::db::*`

**Native wire-protocol drivers preferred** (pure Teko over `teko::net::tcp`/`tls`, zero external dep);
FFI drivers only where there is no wire protocol (embedded SQLite). Rides KEYSTONE-LINK so unused drivers
cost nothing.

| # | Module | Surface (sketch) | Deps | Feasibility | Tier |
|---|---|---|---|---|---|
| DB0 | `teko::db` core | `exp interface Connection { fn query(self, sql: str, params: []Value): Rows\|error; fn exec(...): ExecResult\|error; fn begin(self): Tx\|error; fn close(self): error? }` · `exp type Value` (Null/Int/Float/Text/Bytes/Bool/Timestamp) · `exp interface Rows { fn next(self): Row? }` · `exp type DbError` | `teko::io`; N1 | value-model **pure** | **P2** |
| DB-PG | `teko::db::postgres` (native) | Postgres v3 protocol: startup, SCRAM-SHA-256 auth, extended query, type decode | DB0, N1(+N3), C1/C2 (SCRAM) | **pure** wire codec | **P2** (flagship) |
| DB-MY | `teko::db::mysql` (native) | handshake, caching_sha2 auth, prepared statements | DB0, N1(+N3) | **pure** | **P3** |
| DB-SQLITE | `teko::db::sqlite` (FFI) | `extern` bindings to `libsqlite3` (`sqlite3_open/prepare/step/column_*/finalize`) | DB0, **KEYSTONE-LINK**; **FFI** | **needs FFI** (embedded, no wire) | **P2** |
| DB-MONGO | `teko::db::mongodb` (native) | OP_MSG + SCRAM | DB0, N1, S-BSON | **pure** | **P3** |
| DB-MSSQL | `teko::db::mssql` (native) | TDS 7.x: pre-login, SQL-auth (SSPI later), RPC/prepared, row tokens | DB0, N1 (+TLS-in-TDS) | **pure** wire codec (TDS documented) | **P3** |
| DB-CASS | `teko::db::cassandra` (native) | CQL binary v4/v5: STARTUP/AUTH/QUERY/PREPARE/EXECUTE, paging | DB0, N1(+N3), C1 | **pure** | **P3** |
| DB-REDIS | `teko::db::redis` (native) | key-value/cache surface over RESP2/RESP3 — **SHARES** the `net::redis` codec (N13) | DB0, N13 | **pure** | **P3** |
| DB-ORA | `teko::db::oracle` (FFI) | `extern` OCI (`libclntsh`: `OCIEnvCreate`/`OCIStmtPrepare`/`OCIStmtExecute`/`OCIDefine…`) — TNS/Net is proprietary, so FFI é o caminho pragmático | DB0, **KEYSTONE-LINK**; **FFI** | **needs FFI** (proprietary wire) | **P3** |
| DB-ODBC | `teko::odbc` (FFI, universal) | superfície `extern` para o Driver Manager ODBC (`SQLDriverConnect`/`SQLPrepare`/`SQLExecute`/`SQLFetch`/`SQLGetData`) — **catch-all** para qualquer engine com driver ODBC (Oracle, MSSQL, DB2, Informix, Sybase…). **Expõe pares de funções por sufixo `_32`/`_64`** (`connect_32`/`connect_64`, …). **NÃO é `#arch`:** o processo roda em x64, mas o **driver instalado** pode ser só-32-bit — a escolha da variante depende do **driver**, não do CPU. Um driver 32-bit sob processo 64-bit fala via **bridge out-of-process**; **ambas as variantes sempre compilam** (não são condicionais de compilação) | DB0, **KEYSTONE-LINK**; **FFI** | **needs FFI** (runtime, por bitness do driver) | **P3** |
| Pool / Tx | `teko::db` shared | `exp type Pool { fn get(self): Connection\|error }` | DB0, §10 (concurrent pool) | **pure** | **P3** |

**Os dois caminhos** (ruling do dono: o set de DB estava fraco — mysql/mariadb/mongodb/mssql/oracle/redis +
outros; FFI liberado onde não há wire aberto):
- **Native wire (puro Teko, preferido):** Postgres · **MySQL/MariaDB** (mesmo protocolo cliente — **DB-MY
  cobre os dois**; `ed25519`/`mysql_native`/`caching_sha2` são variantes do mesmo handshake) · **MSSQL**
  (TDS) · **MongoDB** (OP_MSG + BSON) · **Cassandra** (CQL) · **Redis** (RESP, compartilhado com
  `net::redis`) · ClickHouse (TCP nativo ou HTTP). Um binário, cross-platform de graça, codec `.tkt`-testável.
- **FFI (lib do fornecedor, onde o wire é proprietário ou inexistente):** **SQLite** (embedded, `libsqlite3`)
  · **Oracle** (OCI `libclntsh` — TNS proprietário) · um **bridge universal `teko::odbc`** que alcança
  Oracle/MSSQL/DB2/Sybase/Informix pelos drivers ODBC — o catch-all que o dono sancionou (*"pode até ser por
  FFI"*), com **variantes `_32`/`_64`** escolhidas pela **bitness do driver instalado** (não pela arquitetura
  do CPU). KEYSTONE-LINK garante que um programa que não chama um driver não linka a lib dele.
- **De graça por um driver existente (sem módulo novo):** CockroachDB / YugabyteDB / Redshift falam o **wire
  do Postgres** → DB-PG dirige-os como estão; Elasticsearch / OpenSearch / CouchDB / DynamoDB são
  **HTTP+JSON** → `net::http` + `encoding::json` os dirigem sem código de camada-db.

Placeholder portável (`?`) na camada comum, o driver reescreve para `$1`/`?`/`:name`/`@p1`. **ORM/query
builder é roadmap SEPARADO — não entra na camada de conector.**

---

### G. Protocols — SOCKS · RPC · gRPC · AMQP

Owner-named protocols. Most are L7 codecs over `teko::net` (already in §C); this section makes them
first-class catalog entries so the owner can prioritize the RPC/messaging axis explicitly.

| # | Module | Surface (sketch) | Deps | Feasibility | Tier |
|---|---|---|---|---|---|
| P-SOCKS | `teko::net::proxy` | `exp fn socks5_connect(proxy: SocketAddr, target: SocketAddr): TcpStream\|error` · HTTP `CONNECT` tunneling | N1 | **pure** handshake codec; small, high-leverage | **P2** |
| P-GRPC | `teko::net::grpc` | `exp fn unary(chan, method: str, msg: []byte): []byte\|error` · streaming; status/metadata | N8 (HTTP/2), S-PB (protobuf) | **pure** framing | **P3** |
| P-RPC | `teko::rpc` (JSON-RPC 2.0) | `exp fn call(t: TcpStream, method: str, params: Json): Json\|error` · server dispatch | N1, S-JSON | **pure** | **P2** |
| P-AMQP | `teko::net::amqp` | AMQP 0-9-1 client (channels/exchanges/queues/publish/consume) | N1 (+N3) | **pure** frame codec | **P3** |
| P-MQTT | `teko::net::mqtt` | (see N11) | N1 | **pure** | **P3** |

**Native-feasibility note.** SOCKS5 and JSON-RPC are small, pure codecs with immediate cloud/dev value
(route any client through a corporate proxy; expose a service) — good **P2** wins. gRPC is the big one: it
*gates* on HTTP/2 (N8) + protobuf (S-PB), so it is genuinely **P3** and its priority is really "how soon
do we want HTTP/2." AMQP is an independent P3 frame codec.

---

### H. Desktop / GUI — `teko::ui::*` (research tier, last)

A native windowing surface. This is the largest and least-bounded area: it needs a per-platform windowing
FFI, an event loop, and a rendering path — none of which exist. Proposed as a **research tier** with a
concrete-but-honest seam so it is designed, not hand-waved.

| # | Module | Surface (sketch) | Deps | Feasibility | Tier |
|---|---|---|---|---|---|
| U0 | `teko::ui` core | `exp type Window` (extern-type handle) · `exp fn open(title: str, w: u32, h: u32): Window\|error` · `exp type Event` (Close/Key/Mouse/Resize) · `exp fn poll(win: Window): Event?` | KEYSTONE-BUF; **FFI** platform windowing (Win32 `user32/gdi32` · Cocoa/AppKit · X11/Wayland or `libSDL2`) `#os` | **needs FFI** (heavy, per-platform) | **P4 (research)** |
| U1 | `teko::ui::draw` | immediate-mode 2D: `exp fn fill_rect(...)` · `exp fn draw_text(...)` over a `Canvas` | U0; **FFI** platform 2D or software raster | **needs FFI / partial pure raster** | **P4** |
| U2 | `teko::ui::widget` | retained widgets (Button/Label/TextField) over U1; layout | U1, #254 (generic containers), §10 (event loop) | **pure** over U1 | **P4** |

**Native-feasibility note + recommended framing.** Three honest options for the owner to pick:
1. **Bind an existing cross-platform toolkit (recommended for a first cut):** `extern` to `libSDL2`
   (window+input+GL context) — ONE dependency, ONE `#os`-free code path, immediate pixels. Smallest FFI
   surface, fastest to a running window; the dep is opt-in via KEYSTONE-LINK.
2. **Per-platform native windowing (Win32 / Cocoa / X11-Wayland):** no external dep, but 3× the FFI
   surface and the Cocoa path needs Objective-C message-send shims (a real cost — likely a `teko_rt`
   shim, since §16 forbids variadic externs and `objc_msgSend` is variadic-shaped).
3. **A retained widget toolkit in pure Teko over a thin raster+input seam:** the most Teko-idiomatic, the
   most work; a **P4+** stretch.

**Recommendation:** reserve the `teko::ui` namespace and design U0's `Window`/`Event` seam now (so it is
forward-compatible), implement option 1 (SDL2-backed) as the first research spike, keep it strictly
**P4** — it must not gate any of A–G. GUI is where "e o que mais entender necessário" lands: valuable,
but the last phase.

---

## 4. Recommended SEQUENCE (phases — the owner picks the cut line)

The ordering rule: **pure-Teko-over-`[]byte`/`str`/`f64` first (no keystone, immediate `.tkt` value),
then the FFI transport keystones, then the protocol/driver fan-out, generics-gated surfaces slotting in
when #254 lands, GUI last.**

| Phase | Theme | Contents | Gate |
|---|---|---|---|
| **P0** | Foundations / keystones | `teko::mem`+KEYSTONE-BUF · `teko::sys` · KEYSTONE-LINK · io/iter `exp` posture | compiler-side |
| **P1** | Pure wins (no keystone) | **sort** (A1/A2) · **crypto** C0/C1/C2 · **encoding** S-JSON · **compress** Z-DEFLATE/GZIP/ZLIB · crypto **C6 rand** (needs P0 BUF) | none / P0-BUF |
| **P2** | Transport + web baseline | net N0/N1/N2/N3(provider TLS)/N4/N5/N6 · crypto C3/C4/C5 · encoding S-PB/S-ASN1/S-XML/S-MIME · **db** DB0/DB-PG/DB-SQLITE · protocols P-SOCKS/P-RPC · sort A3/A4 (**when #254 lands**) | P0-BUF, P0-LINK, #254 (A3/A4) |
| **P3** | Cloud-native + breadth | net N8(H2)/N9|P-GRPC/N-QUIC/N10(H3)/N11(MQTT)/N13(Redis)/N14(mail)/N16(ssh) · P-AMQP · crypto C3b/C7/C8/C9/C-PGP + `math::bigint` · db DB-MY(+MariaDB)/DB-MSSQL/DB-MONGO/DB-CASS/DB-REDIS/DB-ORA(FFI)/DB-ODBC(FFI)/Pool · encoding S-YAML/S-CBOR/S-MSGPACK/S-BSON · compress brotli/lzma/zstd | as noted per unit |
| **P4** | GUI (research) | `teko::ui` U0/U1/U2 (SDL2-backed spike first) | own research spike; gates nothing |

**Parallelizable immediately (no keystone, no #254):** `{sort A1, crypto C0/C1, encoding S-JSON,
compress Z-DEFLATE}` — four independent lanes that each ship `.tkt`-green value on day one. This is the
concrete answer to "what lands first": **sorting + crypto-over-`[]byte` + JSON + DEFLATE**, then C6 rand
+ the socket keystone, then TLS/HTTP, then DB, then gRPC/AMQP/QUIC, then GUI.

---

## 5. Dependency / risk notes + law tensions (with recommended resolution)

| # | Risk / tension | Law in play | Recommended resolution |
|---|---|---|---|
| 1 | **TLS needs crypto + networking + x509** — a naive order builds HTTP before a secure transport exists | M.4 (layering) | N3 ships **provider-backed** (OpenSSL/SChannel/SecureTransport) in P2 so HTTPS works before pure-Teko crypto is complete; a pure-Teko TLS 1.3 is a P3 follow-on on C4/C5/C7/C8. Hand-rolling TLS first is a security liability (M.1). |
| 2 | **gRPC needs HTTP/2 + protobuf** — high demand, deep dependency | M.4 | Keep gRPC **P3**, gated on N8 + S-PB. Ship **JSON-RPC (P-RPC)** in P2 as the *early* RPC answer so the RPC axis is not empty while HTTP/2 is built. |
| 3 | **GUI needs a platform windowing FFI** — largest, least bounded | M.4, M.1 | Isolate to **P4 research**; reserve the namespace + `Window`/`Event` seam now; first spike is SDL2-backed (one opt-in dep) so it never gates A–G. |
| 4 | **Hand-declared `teko::sys` constants can drift** from platform headers | M.3 (honest boundary) | Each constant carries `@see <header>` + a native regression asserting equality with the real macro (§2.2). Tested honesty beats a C-toolchain dep (M.4). |
| 5 | **Crypto vs `TEKO_OVERFLOW_DEBUG`** — primitives must wrap, not panic | M.1 (fail-loud) vs crypto correctness | Use the explicit `teko::math::checked` `wrapping_*` family; confirm M1 lands before any C-family primitive. Do NOT rely on release-mode wrap. |
| 6 | **Constant-time** — table lookups / `ct_eq` leak timing | M.1 (safety) | Hand-audited convention per unit (documented in each Javadoc); a `ct`-typed language feature is a **separate proposal**, not assumed by this catalog. |
| 7 | **Generic surfaces (sort<T>, collections) gated on #254** | #254 open | Ship the **monomorphic** fast paths now (`sort_i64`/…); design the generic API (A3/A4) forward-compatible; implement when #254 closes. Do not block P1 on it. |
| 8 | **No index-assignment** vs syscall-fills-buffer / cipher state arrays | ruling (no `buf[i]=x`) | Route ALL fill-in-place through KEYSTONE-BUF (`Buf` region, opaque until read back); build cipher/hash state functionally. This is why KEYSTONE-BUF is P0. |
| 9 | **Variadic extern forbidden** vs `objc_msgSend`/`printf`-shaped APIs (GUI/logging) | §16 (no variadic extern) | Wrap fixed-arity shims in `teko_rt` (maintained C) where a variadic C API is unavoidable (Cocoa message-send) — the ONE sanctioned C seam. Prefer SDL2 (non-variadic) to avoid this entirely (risk 3). |
| 10 | **self-`.tkh` bloat** if every codec is `exp` | exp/pub ruling | Curated `exp`: only the user-called entry points (`http::get`, `sha256`, `sort_i64`) are `exp`; parsers/state helpers stay `pub`. Generic templates expose the class, hide rebuild helpers (`pub`). |
| 11 | **Legacy primitives** (MD5/SHA-1/3DES) invite misuse | M.1/M.3 | Ship ONLY where a live protocol needs them (SHA-1 for WS accept-key, MD5 for HTTP digest), Javadoc-`@deprecated` + `legacy`-marked, never the default of any family. |
| 12 | **Reseed hazard** — a catalog module using a feature absent from the seed | bootstrap seed | This doc adds NO language feature; every entry is a library over existing constructs (`extern fn`, `class<T>`, unions, `Buf`). Sequence per-unit issues so each uses only seeded features; the generic-gated units wait for #254 to be seeded. |

**No genuine unresolved law tension requires a HALT.** The mechanism (self-`.tkh`, §16 FFI, §10
concurrency, exp/pub) is sealed; every area maps onto it library-first. The owner's remaining decisions
are **priority/phase cuts** (which of P2/P3 first, whether GUI is funded at all) — argued part-by-part
from the tables above, not blocked on any Law conflict.

---

## 6. Adjacent findings (REPORTED up — not turned into issues)

- The pre-reboot `TEKO_ROADMAP_NET_CRYPTO.md` / `TEKO_ROADMAP_DB.md` / `TEKO_ROADMAP_STDLIB_CORE.md`
  predate the sealed 0.3.1 framing (they say `.c`/`.h` twins, `interface { }`/`variant`, `try`-era
  examples already corrected). This catalog re-projects their *content* onto exp/pub + self-`.tkh` + §16;
  the integrator may wish to mark those three docs `SUPERSEDED-BY: this file` for the expansion axis.
- `docs/design/plano-stdlib-expansao-e-separacao.md` covers the **separation mechanism** (self-`.tkh`,
  triage of ~1619 `pub`) — complementary to this **content** catalog; the two together are the full
  "expand + separate" phase the owner named in §11.
- `teko::sort` already exists (stable `[]str` merge sort) — A1/A2 extend it, they do not create it.

---

## 7. Safety confirmation

`teko test` was **NOT** run in any form (not `teko test .`, not a subset, not staged) — the `monomorph`
leak that crashes the container was never risked. **No build, no seed, no product-code edit.** All work
was static reading + reasoning over `src/sort/sort.tks`, `src/process/process.tks`, `src/crypto/*`,
`TEKO_LEGISLATION.md` §16 (FFI: `:428-433`), `docs/design/mudancas-superficie-0.3.1.md` (§10 concurrency,
§11 self-`.tkh`), `docs/design/plano-secao12-libc-direct-cond-macro.md` (`teko::sys` constants),
`TEKO_ROADMAP_NET_CRYPTO.md`, `TEKO_ROADMAP_DB.md`, `TEKO_ROADMAP_STDLIB_CORE.md`, and
`docs/design/plano-stdlib-expansao-e-separacao.md`. **The main `/home/user/teko-lang` checkout was not
touched** — all work happened in an isolated worktree off `origin/fix/retirement`. This file is the sole
edit.
