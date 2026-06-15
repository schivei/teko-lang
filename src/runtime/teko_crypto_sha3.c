// Native SHA-3 / Keccak (FIPS 202). Portable C23 byte-oriented sponge: the state is
// 25 little-endian lanes, absorbed/squeezed via shifts so the result is independent of
// host endianness. No external libraries.

#include "teko_crypto_sha3.h"

#include <string.h>

// Keccak-f[1600] round constants.
static const uint64_t TEKO_KECCAK_RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};

// Rho rotation offsets and Pi lane permutation, in the canonical packed order.
static const unsigned TEKO_KECCAK_ROTC[24] = {
    1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44
};
static const unsigned TEKO_KECCAK_PILN[24] = {
    10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1
};

static inline uint64_t teko_rotl64(uint64_t x, unsigned n) {
    return (x << n) | (x >> (64u - n));
}

static void teko_keccakf(uint64_t st[25]) {
    uint64_t bc[5];
    uint64_t t;
    unsigned r, i, j;

    for (r = 0u; r < 24u; ++r) {
        // Theta
        for (i = 0u; i < 5u; ++i) {
            bc[i] = st[i] ^ st[i + 5u] ^ st[i + 10u] ^ st[i + 15u] ^ st[i + 20u];
        }
        for (i = 0u; i < 5u; ++i) {
            t = bc[(i + 4u) % 5u] ^ teko_rotl64(bc[(i + 1u) % 5u], 1u);
            for (j = 0u; j < 25u; j += 5u) {
                st[j + i] ^= t;
            }
        }
        // Rho + Pi
        t = st[1];
        for (i = 0u; i < 24u; ++i) {
            j = TEKO_KECCAK_PILN[i];
            bc[0] = st[j];
            st[j] = teko_rotl64(t, TEKO_KECCAK_ROTC[i]);
            t = bc[0];
        }
        // Chi
        for (j = 0u; j < 25u; j += 5u) {
            for (i = 0u; i < 5u; ++i) {
                bc[i] = st[j + i];
            }
            for (i = 0u; i < 5u; ++i) {
                st[j + i] ^= (~bc[(i + 1u) % 5u]) & bc[(i + 2u) % 5u];
            }
        }
        // Iota
        st[0] ^= TEKO_KECCAK_RC[r];
    }
}

void teko_keccak_init(TekoKeccakCtx* ctx, size_t rate, uint8_t delim) {
    memset(ctx->st, 0, sizeof(ctx->st));
    ctx->rate = rate;
    ctx->pos = 0u;
    ctx->delim = delim;
    ctx->squeezing = 0;
}

void teko_keccak_absorb(TekoKeccakCtx* ctx, const uint8_t* data, size_t len) {
    size_t i;
    for (i = 0u; i < len; ++i) {
        // XOR the byte into its little-endian lane position.
        ctx->st[ctx->pos >> 3] ^= (uint64_t)data[i] << (8u * (ctx->pos & 7u));
        ctx->pos++;
        if (ctx->pos == ctx->rate) {
            teko_keccakf(ctx->st);
            ctx->pos = 0u;
        }
    }
}

// pad10*1 with domain separation, then switch to squeezing.
static void teko_keccak_finalize(TekoKeccakCtx* ctx) {
    ctx->st[ctx->pos >> 3] ^= (uint64_t)ctx->delim << (8u * (ctx->pos & 7u));
    ctx->st[(ctx->rate - 1u) >> 3] ^= (uint64_t)0x80u << (8u * ((ctx->rate - 1u) & 7u));
    teko_keccakf(ctx->st);
    ctx->pos = 0u;
    ctx->squeezing = 1;
}

void teko_keccak_squeeze(TekoKeccakCtx* ctx, uint8_t* out, size_t out_len) {
    size_t i;
    if (!ctx->squeezing) {
        teko_keccak_finalize(ctx);
    }
    for (i = 0u; i < out_len; ++i) {
        if (ctx->pos == ctx->rate) {
            teko_keccakf(ctx->st);
            ctx->pos = 0u;
        }
        out[i] = (uint8_t)(ctx->st[ctx->pos >> 3] >> (8u * (ctx->pos & 7u)));
        ctx->pos++;
    }
}

void teko_sha3_256(const uint8_t* data, size_t len, uint8_t out[TEKO_SHA3_256_DIGEST_LEN]) {
    TekoKeccakCtx ctx;
    teko_keccak_init(&ctx, 136u, 0x06u); // rate = 1600 - 2*256 bits = 136 bytes
    teko_keccak_absorb(&ctx, data, len);
    teko_keccak_squeeze(&ctx, out, TEKO_SHA3_256_DIGEST_LEN);
}

void teko_sha3_512(const uint8_t* data, size_t len, uint8_t out[TEKO_SHA3_512_DIGEST_LEN]) {
    TekoKeccakCtx ctx;
    teko_keccak_init(&ctx, 72u, 0x06u);  // rate = 1600 - 2*512 bits = 72 bytes
    teko_keccak_absorb(&ctx, data, len);
    teko_keccak_squeeze(&ctx, out, TEKO_SHA3_512_DIGEST_LEN);
}

void teko_shake128(const uint8_t* data, size_t len, uint8_t* out, size_t out_len) {
    TekoKeccakCtx ctx;
    teko_keccak_init(&ctx, 168u, 0x1fu); // rate = 1600 - 2*128 bits = 168 bytes
    teko_keccak_absorb(&ctx, data, len);
    teko_keccak_squeeze(&ctx, out, out_len);
}

void teko_shake256(const uint8_t* data, size_t len, uint8_t* out, size_t out_len) {
    TekoKeccakCtx ctx;
    teko_keccak_init(&ctx, 136u, 0x1fu); // rate = 1600 - 2*256 bits = 136 bytes
    teko_keccak_absorb(&ctx, data, len);
    teko_keccak_squeeze(&ctx, out, out_len);
}
