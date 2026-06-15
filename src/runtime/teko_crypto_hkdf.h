#ifndef TEKO_CRYPTO_HKDF_H
#define TEKO_CRYPTO_HKDF_H

// Native HKDF (RFC 5869) over the Teko HMAC primitives. Extract-then-expand key
// derivation. Pure, portable C23 — no external libraries. KAT-tested against the RFC 5869
// SHA-256 vectors in tests/runtime/test_crypto_hkdf.c.

#include <stdint.h>
#include <stddef.h>

// HKDF-SHA-256 / -SHA-512: derive okm_len bytes from (salt, ikm, info).
// A NULL/zero salt defaults to a string of HashLen zero bytes (per RFC 5869).
// Returns 0 on success, -1 if okm_len > 255*HashLen.
int teko_hkdf_sha256(const uint8_t* salt, size_t salt_len,
                     const uint8_t* ikm, size_t ikm_len,
                     const uint8_t* info, size_t info_len,
                     uint8_t* okm, size_t okm_len);

int teko_hkdf_sha512(const uint8_t* salt, size_t salt_len,
                     const uint8_t* ikm, size_t ikm_len,
                     const uint8_t* info, size_t info_len,
                     uint8_t* okm, size_t okm_len);

// Exposed for tests / advanced callers: the two HKDF stages for SHA-256.
// prk must be 32 bytes; HKDF-Extract writes the pseudorandom key.
void teko_hkdf_sha256_extract(const uint8_t* salt, size_t salt_len,
                              const uint8_t* ikm, size_t ikm_len,
                              uint8_t prk[32]);

#endif // TEKO_CRYPTO_HKDF_H
