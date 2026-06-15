#ifndef TEKO_CRYPTO_SHA1_H
#define TEKO_CRYPTO_SHA1_H

// Native SHA-1 (FIPS 180). Pure, portable C23 — no external libraries.
//
// ⚠️ LEGACY / INSECURE: SHA-1 is broken against collision attacks (SHAttered). Provided ONLY
// for backward compatibility and interop (e.g. UUID v5, legacy protocols) — NEVER for new
// security uses (signatures, certificates). Use SHA-256/SHA-3/BLAKE3 for security.
//
// KAT-tested against FIPS 180 in tests/runtime/test_crypto_sha1.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_SHA1_DIGEST_LEN 20u
#define TEKO_SHA1_BLOCK_LEN  64u

typedef struct {
    uint32_t state[5];
    uint64_t bit_len;
    uint8_t  buffer[TEKO_SHA1_BLOCK_LEN];
    size_t   buffer_len;
} TekoSha1Ctx;

void teko_sha1_init(TekoSha1Ctx* ctx);
void teko_sha1_update(TekoSha1Ctx* ctx, const uint8_t* data, size_t len);
void teko_sha1_final(TekoSha1Ctx* ctx, uint8_t out[TEKO_SHA1_DIGEST_LEN]);
void teko_sha1(const uint8_t* data, size_t len, uint8_t out[TEKO_SHA1_DIGEST_LEN]);

#endif // TEKO_CRYPTO_SHA1_H
