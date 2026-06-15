#ifndef TEKO_CRYPTO_P384_H
#define TEKO_CRYPTO_P384_H

// NIST P-384 (secp384r1) — ECDH + ECDSA, built on the curve-generic core (teko_crypto_ecc)
// over the complete a=-3 group law and the Montgomery bignum layer. Curve: y^2 = x^3 - 3x + b
// over GF(p), 384-bit (12 x 32 limbs). Scalars/coordinates are 48-byte big-endian. RFC 6979
// nonces use HMAC-SHA-384. KAT-tested against NIST CAVP (ECC CDH) and RFC 6979 A.2.6.

#include <stdint.h>
#include <stddef.h>

// pub = priv*G (uncompressed X||Y, 96 bytes). Returns 0, or -1 on bad key / infinity.
int teko_p384_derive_public(const uint8_t priv[48], uint8_t pub[96]);

// out_x = x of priv*peer (ECDH); peer is uncompressed X||Y (96 bytes), validated on-curve.
int teko_p384_ecdh(const uint8_t priv[48], const uint8_t peer_pub[96], uint8_t out_x[48]);

// ECDSA sign (RFC 6979 / HMAC-SHA-384): sig = r||s (48 bytes each). Returns 0/-1.
int teko_p384_ecdsa_sign(const uint8_t priv[48], const uint8_t* hash, size_t hash_len,
                         uint8_t sig[96]);

// ECDSA verify: 0 iff sig (r||s) verifies under pub (uncompressed X||Y); -1 otherwise.
int teko_p384_ecdsa_verify(const uint8_t pub[96], const uint8_t* hash, size_t hash_len,
                           const uint8_t sig[96]);

#endif // TEKO_CRYPTO_P384_H
