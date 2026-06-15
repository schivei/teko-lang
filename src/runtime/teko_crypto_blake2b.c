// Native BLAKE2b (RFC 7693). Portable C23, no external libraries.

#include "teko_crypto_blake2b.h"

#include <string.h>

static const uint64_t TEKO_B2B_IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

static const uint8_t TEKO_B2B_SIGMA[12][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
    { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
    { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
    { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
    { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 }
};

static inline uint64_t teko_b2b_rotr(uint64_t x, unsigned n) {
    return (x >> n) | (x << (64u - n));
}

static inline uint64_t teko_b2b_ld64(const uint8_t* p) {
    return ((uint64_t)p[0]) | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24)
         | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

#define TEKO_B2B_G(a, b, c, d, x, y) \
    a = a + b + (x); d = teko_b2b_rotr(d ^ a, 32); \
    c = c + d;       b = teko_b2b_rotr(b ^ c, 24); \
    a = a + b + (y); d = teko_b2b_rotr(d ^ a, 16); \
    c = c + d;       b = teko_b2b_rotr(b ^ c, 63)

static void teko_b2b_compress(TekoBlake2bCtx* ctx, const uint8_t block[128], int last) {
    uint64_t v[16];
    uint64_t m[16];
    unsigned i, r;

    for (i = 0u; i < 16u; ++i) m[i] = teko_b2b_ld64(block + i * 8u);
    for (i = 0u; i < 8u; ++i) { v[i] = ctx->h[i]; v[i + 8u] = TEKO_B2B_IV[i]; }
    v[12] ^= ctx->t[0];
    v[13] ^= ctx->t[1];
    if (last) v[14] ^= 0xffffffffffffffffULL;

    for (r = 0u; r < 12u; ++r) {
        const uint8_t* s = TEKO_B2B_SIGMA[r];
        TEKO_B2B_G(v[0], v[4], v[8],  v[12], m[s[0]],  m[s[1]]);
        TEKO_B2B_G(v[1], v[5], v[9],  v[13], m[s[2]],  m[s[3]]);
        TEKO_B2B_G(v[2], v[6], v[10], v[14], m[s[4]],  m[s[5]]);
        TEKO_B2B_G(v[3], v[7], v[11], v[15], m[s[6]],  m[s[7]]);
        TEKO_B2B_G(v[0], v[5], v[10], v[15], m[s[8]],  m[s[9]]);
        TEKO_B2B_G(v[1], v[6], v[11], v[12], m[s[10]], m[s[11]]);
        TEKO_B2B_G(v[2], v[7], v[8],  v[13], m[s[12]], m[s[13]]);
        TEKO_B2B_G(v[3], v[4], v[9],  v[14], m[s[14]], m[s[15]]);
    }

    for (i = 0u; i < 8u; ++i) ctx->h[i] ^= v[i] ^ v[i + 8u];
}

int teko_blake2b_init(TekoBlake2bCtx* ctx, size_t out_len, const uint8_t* key, size_t key_len) {
    unsigned i;
    if (out_len == 0u || out_len > 64u || key_len > 64u) return -1;

    for (i = 0u; i < 8u; ++i) ctx->h[i] = TEKO_B2B_IV[i];
    ctx->h[0] ^= 0x01010000ULL ^ ((uint64_t)key_len << 8) ^ (uint64_t)out_len;
    ctx->t[0] = 0u;
    ctx->t[1] = 0u;
    ctx->buf_len = 0u;
    ctx->out_len = out_len;
    memset(ctx->buf, 0, sizeof(ctx->buf));

    if (key_len > 0u) {
        uint8_t block[128];
        memset(block, 0, sizeof(block));
        memcpy(block, key, key_len);
        teko_blake2b_update(ctx, block, 128u); // a full keyed first block
        memset(block, 0, sizeof(block));
    }
    return 0;
}

void teko_blake2b_update(TekoBlake2bCtx* ctx, const uint8_t* data, size_t len) {
    while (len > 0u) {
        if (ctx->buf_len == 128u) {
            // Buffer is full and more data follows: count it and compress (not last).
            ctx->t[0] += 128u;
            if (ctx->t[0] < 128u) ctx->t[1] += 1u;
            teko_b2b_compress(ctx, ctx->buf, 0);
            ctx->buf_len = 0u;
        }
        {
            size_t want = 128u - ctx->buf_len;
            size_t take = (len < want) ? len : want;
            memcpy(ctx->buf + ctx->buf_len, data, take);
            ctx->buf_len += take;
            data += take;
            len -= take;
        }
    }
}

void teko_blake2b_final(TekoBlake2bCtx* ctx, uint8_t* out) {
    uint8_t hash[64];
    unsigned i;

    // Count the final (partial) block and compress it as the last block.
    ctx->t[0] += (uint64_t)ctx->buf_len;
    if (ctx->t[0] < (uint64_t)ctx->buf_len) ctx->t[1] += 1u;
    memset(ctx->buf + ctx->buf_len, 0, 128u - ctx->buf_len);
    teko_b2b_compress(ctx, ctx->buf, 1);

    for (i = 0u; i < 8u; ++i) {
        uint64_t w = ctx->h[i];
        hash[i * 8u + 0u] = (uint8_t)(w);
        hash[i * 8u + 1u] = (uint8_t)(w >> 8);
        hash[i * 8u + 2u] = (uint8_t)(w >> 16);
        hash[i * 8u + 3u] = (uint8_t)(w >> 24);
        hash[i * 8u + 4u] = (uint8_t)(w >> 32);
        hash[i * 8u + 5u] = (uint8_t)(w >> 40);
        hash[i * 8u + 6u] = (uint8_t)(w >> 48);
        hash[i * 8u + 7u] = (uint8_t)(w >> 56);
    }
    memcpy(out, hash, ctx->out_len);
    memset(hash, 0, sizeof(hash));
}

int teko_blake2b(const uint8_t* data, size_t len, uint8_t* out, size_t out_len) {
    TekoBlake2bCtx ctx;
    if (teko_blake2b_init(&ctx, out_len, NULL, 0u) != 0) return -1;
    teko_blake2b_update(&ctx, data, len);
    teko_blake2b_final(&ctx, out);
    return 0;
}
