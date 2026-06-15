#ifndef TEKO_CRYPTO_RSA_H
#define TEKO_CRYPTO_RSA_H

// Native RSA (Phase 13.3b) — sits directly on the Montgomery modexp bignum layer
// (teko_crypto_bn). Implements PKCS#1 v1.5 signatures (this header section), and — added in
// later increments — PKCS#1 v1.5 encryption, OAEP, and PSS, per RFC 8017 (PKCS #1 v2.2).
// Keys are passed as big-endian byte slices (no ASN.1 key parsing here; the modulus n is the
// public/private modulus, e the public exponent, d the private exponent). KAT-tested against
// NIST FIPS 186 / RFC 8017 / Project Wycheproof vectors. Constant-time is NOT claimed for
// RSA private operations (modexp is square-and-multiply); this is a from-scratch, KAT-correct
// implementation — side-channel hardening (blinding, CRT) is a future optimization.

#include <stdint.h>
#include <stddef.h>

// Hash choices for the PKCS#1 encodings (DigestInfo / MGF1 / mHash length).
typedef enum {
    TEKO_RSA_SHA256 = 0,
    TEKO_RSA_SHA384 = 1,
    TEKO_RSA_SHA512 = 2
} TekoRsaHash;

// Digest byte length for a TekoRsaHash (32 / 48 / 64), or 0 if unknown.
size_t teko_rsa_hash_len(TekoRsaHash h);

// --- PKCS#1 v1.5 signatures (EMSA-PKCS1-v1_5) -------------------------------------------

// sig = RSASP1(EMSA-PKCS1-v1_5(hash)). `hash` is the message digest (length must match h).
// sig must have room for nlen bytes (the modulus byte length). Returns 0, or -1 on bad args
// / key too small for the encoding.
int teko_rsa_pkcs1v15_sign(const uint8_t* n, size_t nlen, const uint8_t* d, size_t dlen,
                           TekoRsaHash h, const uint8_t* hash, size_t hash_len, uint8_t* sig);

// Returns 0 iff sig (siglen bytes) is a valid PKCS#1 v1.5 signature of `hash` under (n, e);
// -1 otherwise.
int teko_rsa_pkcs1v15_verify(const uint8_t* n, size_t nlen, const uint8_t* e, size_t elen,
                             TekoRsaHash h, const uint8_t* hash, size_t hash_len,
                             const uint8_t* sig, size_t siglen);

// --- PKCS#1 v1.5 encryption (EME-PKCS1-v1_5) -------------------------------------------
// LEGACY padding (Bleichenbacher padding-oracle prone); provided for interop. Prefer OAEP.

// ct = (00 || 02 || PS || 00 || M)^e mod n, PS = nonzero random padding from the CSPRNG.
// ct must hold nlen bytes; requires msg_len <= nlen - 11. Returns 0/-1.
int teko_rsa_pkcs1v15_encrypt(const uint8_t* n, size_t nlen, const uint8_t* e, size_t elen,
                              const uint8_t* msg, size_t msg_len, uint8_t* ct);

// Same, but PS is supplied by the caller (ps_len must equal nlen - msg_len - 3, all nonzero).
// Used for deterministic KAT/round-trip testing. Returns 0/-1.
int teko_rsa_pkcs1v15_encrypt_seeded(const uint8_t* n, size_t nlen, const uint8_t* e,
                                     size_t elen, const uint8_t* msg, size_t msg_len,
                                     const uint8_t* ps, size_t ps_len, uint8_t* ct);

// Decrypt EME-PKCS1-v1_5: out receives the recovered message (caller buffer >= nlen), *out_len
// is set to the message length. Returns 0 on valid padding, -1 otherwise.
int teko_rsa_pkcs1v15_decrypt(const uint8_t* n, size_t nlen, const uint8_t* d, size_t dlen,
                              const uint8_t* ct, size_t ct_len, uint8_t* out, size_t* out_len);

#endif // TEKO_CRYPTO_RSA_H
