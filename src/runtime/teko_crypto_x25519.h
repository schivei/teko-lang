#ifndef TEKO_CRYPTO_X25519_H
#define TEKO_CRYPTO_X25519_H

// Native X25519 (RFC 7748) Diffie-Hellman over Curve25519. Pure, portable C23 — no external
// libraries, fixed prime field (no general bignum). The scalar multiplication is a constant-
// time Montgomery ladder with a constant-time conditional swap. KAT-tested against RFC 7748
// in tests/runtime/test_crypto_x25519.c.

#include <stdint.h>

#define TEKO_X25519_LEN 32u

// out = X25519(scalar, u_coordinate). All buffers are 32 bytes, little-endian.
// Returns 0 always (the all-zero output / small-subgroup case is the caller's policy).
int teko_x25519(uint8_t out[TEKO_X25519_LEN],
                const uint8_t scalar[TEKO_X25519_LEN],
                const uint8_t u_coordinate[TEKO_X25519_LEN]);

// Compute the public key for a secret scalar: X25519(scalar, 9).
void teko_x25519_base(uint8_t out[TEKO_X25519_LEN], const uint8_t scalar[TEKO_X25519_LEN]);

#endif // TEKO_CRYPTO_X25519_H
