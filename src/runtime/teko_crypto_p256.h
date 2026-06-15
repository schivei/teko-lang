#ifndef TEKO_CRYPTO_P256_H
#define TEKO_CRYPTO_P256_H

// NIST P-256 (secp256r1) — ECDH (this file) and ECDSA (added in a later increment), built
// on the generic a=-3 Weierstrass group law (teko_crypto_ec) over the Montgomery field
// layer (teko_crypto_bn). Curve: y^2 = x^3 - 3x + b over GF(p). All scalars and coordinates
// are 32-byte big-endian. KAT-tested against NIST CAVP (ECC CDH) vectors.

#include <stdint.h>
#include <stddef.h>

// pub = priv * G, written as the uncompressed affine coordinates X||Y (64 bytes). priv must
// be a valid scalar (0 < priv < n). Returns 0 on success, -1 on bad args / point at infinity.
int teko_p256_derive_public(const uint8_t priv[32], uint8_t pub[64]);

// out_x = x-coordinate of (priv * peer_pub), the ECDH (cofactor 1) shared secret. peer_pub
// is the peer's uncompressed affine point X||Y (64 bytes); it is validated to be on the
// curve. Returns 0 on success, -1 on bad args / invalid peer point / infinity result.
int teko_p256_ecdh(const uint8_t priv[32], const uint8_t peer_pub[64], uint8_t out_x[32]);

// ECDSA sign: sig = r||s (32 bytes each, big-endian) over the pre-computed message digest
// `hash` (hash_len bytes), using a deterministic nonce per RFC 6979 with HMAC-SHA-256. priv
// must be in [1, n-1]. The digest is reduced per FIPS 186 (bits2int, byte-aligned qlen).
// Returns 0 on success, -1 on bad args / invalid key.
int teko_p256_ecdsa_sign(const uint8_t priv[32], const uint8_t* hash, size_t hash_len,
                         uint8_t sig[64]);

// ECDSA verify: returns 0 iff sig (r||s, 32 bytes each) is a valid signature of `hash`
// under the public key pub (uncompressed X||Y, 64 bytes); -1 otherwise.
int teko_p256_ecdsa_verify(const uint8_t pub[64], const uint8_t* hash, size_t hash_len,
                           const uint8_t sig[64]);

#endif // TEKO_CRYPTO_P256_H
