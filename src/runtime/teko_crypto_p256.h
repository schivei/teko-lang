#ifndef TEKO_CRYPTO_P256_H
#define TEKO_CRYPTO_P256_H

// NIST P-256 (secp256r1) — ECDH (this file) and ECDSA (added in a later increment), built
// on the generic a=-3 Weierstrass group law (teko_crypto_ec) over the Montgomery field
// layer (teko_crypto_bn). Curve: y^2 = x^3 - 3x + b over GF(p). All scalars and coordinates
// are 32-byte big-endian. KAT-tested against NIST CAVP (ECC CDH) vectors.

#include <stdint.h>

// pub = priv * G, written as the uncompressed affine coordinates X||Y (64 bytes). priv must
// be a valid scalar (0 < priv < n). Returns 0 on success, -1 on bad args / point at infinity.
int teko_p256_derive_public(const uint8_t priv[32], uint8_t pub[64]);

// out_x = x-coordinate of (priv * peer_pub), the ECDH (cofactor 1) shared secret. peer_pub
// is the peer's uncompressed affine point X||Y (64 bytes); it is validated to be on the
// curve. Returns 0 on success, -1 on bad args / invalid peer point / infinity result.
int teko_p256_ecdh(const uint8_t priv[32], const uint8_t peer_pub[64], uint8_t out_x[32]);

#endif // TEKO_CRYPTO_P256_H
