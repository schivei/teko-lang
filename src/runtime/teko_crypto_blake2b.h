#ifndef TEKO_CRYPTO_BLAKE2B_H
#define TEKO_CRYPTO_BLAKE2B_H

// Native BLAKE2b (RFC 7693). Pure, portable C23 — no external libraries. Provided primarily
// as the underlying hash for Argon2 (RFC 9106), but also usable standalone. KAT-tested
// against RFC 7693 in tests/runtime/test_crypto_blake2b.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_BLAKE2B_OUT_MAX   64u
#define TEKO_BLAKE2B_BLOCK_LEN 128u

typedef struct {
    uint64_t h[8];
    uint64_t t[2];          // 128-bit message byte counter
    uint8_t  buf[TEKO_BLAKE2B_BLOCK_LEN];
    size_t   buf_len;
    size_t   out_len;
} TekoBlake2bCtx;

// Streaming API. out_len is 1..64; key (optional, 0..64 bytes) keys the hash.
int  teko_blake2b_init(TekoBlake2bCtx* ctx, size_t out_len, const uint8_t* key, size_t key_len);
void teko_blake2b_update(TekoBlake2bCtx* ctx, const uint8_t* data, size_t len);
void teko_blake2b_final(TekoBlake2bCtx* ctx, uint8_t* out);

// One-shot, unkeyed.
int  teko_blake2b(const uint8_t* data, size_t len, uint8_t* out, size_t out_len);

#endif // TEKO_CRYPTO_BLAKE2B_H
