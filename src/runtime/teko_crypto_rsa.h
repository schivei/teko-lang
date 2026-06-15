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

#endif // TEKO_CRYPTO_RSA_H
