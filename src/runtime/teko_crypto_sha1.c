// Native SHA-1 (FIPS 180). Portable C23. LEGACY/INSECURE — see the header.

#include "teko_crypto_sha1.h"

#include <string.h>

static inline uint32_t teko_sha1_rotl(uint32_t x, unsigned c) {
    return (x << c) | (x >> (32u - c));
}

void teko_sha1_init(TekoSha1Ctx* ctx) {
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xEFCDAB89u;
    ctx->state[2] = 0x98BADCFEu;
    ctx->state[3] = 0x10325476u;
    ctx->state[4] = 0xC3D2E1F0u;
    ctx->bit_len = 0u;
    ctx->buffer_len = 0u;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

static void teko_sha1_compress(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    unsigned i;

    for (i = 0u; i < 16u; ++i) {
        w[i] = ((uint32_t)block[i * 4u + 0u] << 24) | ((uint32_t)block[i * 4u + 1u] << 16)
             | ((uint32_t)block[i * 4u + 2u] << 8) | ((uint32_t)block[i * 4u + 3u]);
    }
    for (i = 16u; i < 80u; ++i) {
        w[i] = teko_sha1_rotl(w[i - 3u] ^ w[i - 8u] ^ w[i - 14u] ^ w[i - 16u], 1);
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];

    for (i = 0u; i < 80u; ++i) {
        uint32_t f, k, t;
        if (i < 20u) { f = (b & c) | (~b & d); k = 0x5A827999u; }
        else if (i < 40u) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
        else if (i < 60u) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6u; }

        t = teko_sha1_rotl(a, 5) + f + e + k + w[i];
        e = d; d = c; c = teko_sha1_rotl(b, 30); b = a; a = t;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void teko_sha1_update(TekoSha1Ctx* ctx, const uint8_t* data, size_t len) {
    ctx->bit_len += (uint64_t)len * 8u;

    if (ctx->buffer_len != 0u) {
        size_t need = TEKO_SHA1_BLOCK_LEN - ctx->buffer_len;
        size_t take = (len < need) ? len : need;
        memcpy(ctx->buffer + ctx->buffer_len, data, take);
        ctx->buffer_len += take;
        data += take;
        len -= take;
        if (ctx->buffer_len == TEKO_SHA1_BLOCK_LEN) {
            teko_sha1_compress(ctx->state, ctx->buffer);
            ctx->buffer_len = 0u;
        }
    }

    while (len >= TEKO_SHA1_BLOCK_LEN) {
        teko_sha1_compress(ctx->state, data);
        data += TEKO_SHA1_BLOCK_LEN;
        len -= TEKO_SHA1_BLOCK_LEN;
    }

    if (len != 0u) {
        memcpy(ctx->buffer + ctx->buffer_len, data, len);
        ctx->buffer_len += len;
    }
}

void teko_sha1_final(TekoSha1Ctx* ctx, uint8_t out[20]) {
    const uint64_t bit_len = ctx->bit_len;
    unsigned i;

    ctx->buffer[ctx->buffer_len++] = 0x80u;
    if (ctx->buffer_len > 56u) {
        memset(ctx->buffer + ctx->buffer_len, 0, TEKO_SHA1_BLOCK_LEN - ctx->buffer_len);
        teko_sha1_compress(ctx->state, ctx->buffer);
        ctx->buffer_len = 0u;
    }
    memset(ctx->buffer + ctx->buffer_len, 0, 56u - ctx->buffer_len);

    for (i = 0u; i < 8u; ++i) ctx->buffer[56u + i] = (uint8_t)(bit_len >> (56u - 8u * i)); // big-endian
    teko_sha1_compress(ctx->state, ctx->buffer);

    for (i = 0u; i < 5u; ++i) {
        out[i * 4u + 0u] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4u + 3u] = (uint8_t)(ctx->state[i]);
    }
}

void teko_sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
    TekoSha1Ctx ctx;
    teko_sha1_init(&ctx);
    teko_sha1_update(&ctx, data, len);
    teko_sha1_final(&ctx, out);
}
