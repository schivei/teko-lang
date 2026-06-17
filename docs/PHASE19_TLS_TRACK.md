# Phase 19 — TLS / SSL Track (parallel; native client+server, WASM client-only)

> Parallel track under Phase 19 (`docs/PHASE19_NETWORKING.md`). TLS is **not** on the MVP critical
> path but runs as an **independent, KAT-driven track** (TLS-RT) that can start immediately — the
> handshake/key-schedule/record state machine is exercised with **RFC 8448 test vectors and no
> socket**; integration (TLS-INT, `tls.*` over the socket) joins at Wave 1. **Target split (matches
> the phase decision):** TLS-RT implements **both** the client and server halves of the handshake
> (shared key schedule + record layer); **native** wires both `tls.client` and `tls.server` (the
> latter over the native accept loop); **WASM** wires `tls.client` only (host-import; no listening
> server on WASM). Built entirely on the **CT-clean Phase-13 primitives** — this track adds **no** new
> crypto and does **not** modify Phase 13.

## 1. All SSL/TLS versions — enumerated, with posture

| Protocol | Year / RFC | Posture in Teko | Rationale |
|---|---|---|---|
| **SSL 2.0** | 1995 | **PROHIBITED — never implement** | Catastrophically broken; **RFC 6176 prohibits** negotiating SSLv2. No client support. |
| **SSL 3.0** | 1996 / RFC 6101 | **INSECURE — never implement** | **POODLE** (CVE-2014-3566); **RFC 7568 prohibits** SSLv3. No client support. |
| **TLS 1.0** | 1999 / RFC 2246 | **LEGACY/INSECURE — do not implement** | BEAST; CBC/MAC-then-encrypt; **deprecated by RFC 8996**. Not offered. |
| **TLS 1.1** | 2006 / RFC 4346 | **LEGACY/INSECURE — do not implement** | Superseded; **deprecated by RFC 8996**. Not offered. |
| **TLS 1.2** | 2008 / RFC 5246 | **TARGET (secondary)** | Still widely required by servers. Implement **client** with **AEAD-only** suites (ECDHE + AES-GCM / ChaCha20-Poly1305); **exclude** CBC, RC4, static-RSA key exchange, and compression. |
| **TLS 1.3** | 2018 / RFC 8446 | **TARGET (primary)** | Preferred. 1-RTT handshake, HKDF key schedule, AEAD-only, **no record compression** (anti-CRIME by design). Built on Phase-13 X25519/P-256 ECDHE, AES-GCM/ChaCha20-Poly1305, HKDF, Ed25519/ECDSA verify. |

### Datagram TLS (relevant to the UDP/QUIC tracks — noted, not MVP)
| Protocol | Year / RFC | Posture |
|---|---|---|
| **DTLS 1.0** | 2006 / RFC 4347 | **INSECURE — do not implement** (maps to TLS 1.1). |
| **DTLS 1.2** | 2012 / RFC 6347 | Optional future (UDP security), AEAD-only if ever added. |
| **DTLS 1.3** | 2022 / RFC 9147 | Optional future; relevant adjacent to the QUIC track. **QUIC itself uses the TLS 1.3 handshake (RFC 9001), not DTLS.** |

**Net target set:** **TLS 1.3 (primary) + TLS 1.2 (secondary), client-only, AEAD-only.** Everything
≤ TLS 1.1 and all SSL is explicitly **not implemented** and never negotiated (downgrade-safe: we
simply never offer them).

## 2. Cipher-suite posture (CT-clean Phase-13 primitives only)
- **Key exchange:** ECDHE over **X25519** / **P-256** (Phase-13 CT scalarmult). No static RSA kex.
- **AEAD:** **AES-128/256-GCM**, **ChaCha20-Poly1305** (Phase-13, CT).
- **Signatures (cert verify):** **Ed25519**, **ECDSA P-256/P-384** (Phase-13). **RSA PKCS#1 v1.5 /
  PSS** used **only for certificate signature *verification*** (public-key op — acceptable); we do
  **not** use RSA key exchange or RSA decryption (Phase-13 RSA private ops are non-CT /
  padding-oracle-prone — documented; verify-only avoids that surface).
- **KDF:** HKDF-SHA-256/384 (Phase-13). HKDF-Expand-Label per RFC 8446 §7.1.

## 3. TLS-RT breadcrumbs (KAT-driven, no socket — Wave 0, parallel)
1. **Key schedule** — HKDF-Expand-Label + the TLS 1.3 secret tree (early/handshake/master,
   traffic keys), KAT'd against **RFC 8448**.
2. **Handshake transcript + state machine** — ClientHello/ServerHello, EncryptedExtensions,
   Certificate, CertificateVerify, Finished; transcript hash. **Both halves** (client + server) share
   this machine; the server half drives the same messages from the responder side.
3. **Record layer** — AEAD record protection/deprotection, nonce construction, key update.
4. **Certificate-chain verification** — signature verify via Phase-13 (Ed25519/ECDSA/RSA-verify);
   validity/name checks. (Trust-store policy is a documented sub-decision.) The native server half
   additionally needs a configured cert+key (server-side signing uses Phase-13 Ed25519/ECDSA;
   RSA-PSS server signing is allowed since signing is the private op the server owns locally.)
5. **TLS 1.2** — AEAD-only suites, ECDHE; shares the cert-verify + record plumbing.

## 4. TLS-INT (Wave 1) — `tls.*` over the socket layer
- **`tls.client(host, port)`** (native + WASM) → handshake over `net.tcp_connect` →
  `tls.send`/`tls.recv` (AEAD records). `https` client = HTTP-INT over `tls.client`.
- **`tls.server(...)`** (**native only**) → TLS over the native accept loop (T1b) → an `https`
  server = HTTP-SRV over `tls.server`. No WASM server (platform reality, honestly marked).
- **Proofs.** Native: `tls.client` handshake against a replayed **RFC 8448** transcript and/or a
  local `tls.server` loopback; `tls.server` accept+handshake loopback. WASM: `tls.client` via
  host-import TLS (Node TLS) or replayed transcript through the reactor.

## 5. SAST notes (gate)
TLS records and certificates are **attacker-controlled** parse surfaces: bounds-check every record/
extension/certificate field; integer-overflow-safe length arithmetic; constant-time tag/Finished
comparison; reject all ≤TLS-1.1/SSL versions and non-AEAD suites at parse time (no downgrade); **no
record compression** (anti-CRIME). `calloc` zero-init + clear ownership across the handshake state.
