#ifndef TEKO_CRYPTO_SHA3_H
#define TEKO_CRYPTO_SHA3_H

// Native SHA-3 / Keccak (FIPS 202): SHA3-256, SHA3-512, and the SHAKE128/256
// extendable-output functions. Pure, portable C23 — no external libraries.
// KAT-tested against NIST vectors in tests/runtime/test_crypto_sha3.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_SHA3_256_DIGEST_LEN 32u
#define TEKO_SHA3_512_DIGEST_LEN 64u

// Sponge state shared by every Keccak instance. Zero-init via the *_init helpers.
typedef struct {
    uint64_t st[25];   // 1600-bit state as 25 little-endian lanes
    size_t   rate;     // sponge rate in bytes (block size)
    size_t   pos;      // current byte offset within the rate block
    uint8_t  delim;    // domain-separation byte (0x06 SHA-3, 0x1f SHAKE)
    int      squeezing; // 0 while absorbing, 1 after finalize
} TekoKeccakCtx;

// One-shot fixed-length digests.
void teko_sha3_256(const uint8_t* data, size_t len, uint8_t out[TEKO_SHA3_256_DIGEST_LEN]);
void teko_sha3_512(const uint8_t* data, size_t len, uint8_t out[TEKO_SHA3_512_DIGEST_LEN]);

// One-shot extendable-output functions (XOFs): write out_len bytes.
void teko_shake128(const uint8_t* data, size_t len, uint8_t* out, size_t out_len);
void teko_shake256(const uint8_t* data, size_t len, uint8_t* out, size_t out_len);

// Streaming API (any rate/delim): init -> absorb* -> squeeze*.
void teko_keccak_init(TekoKeccakCtx* ctx, size_t rate, uint8_t delim);
void teko_keccak_absorb(TekoKeccakCtx* ctx, const uint8_t* data, size_t len);
void teko_keccak_squeeze(TekoKeccakCtx* ctx, uint8_t* out, size_t out_len);

#endif // TEKO_CRYPTO_SHA3_H
