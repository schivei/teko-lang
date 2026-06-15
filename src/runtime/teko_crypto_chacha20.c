// Native ChaCha20 (RFC 8439 §2.1–2.4). Portable C23, constant-time. No external libraries.

#include "teko_crypto_chacha20.h"

#include <string.h>

static inline uint32_t teko_cc_rotl(uint32_t x, unsigned n) {
    return (x << n) | (x >> (32u - n));
}

static inline uint32_t teko_cc_load32_le(const uint8_t* p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define TEKO_CC_QR(a, b, c, d) \
    a += b; d ^= a; d = teko_cc_rotl(d, 16); \
    c += d; b ^= c; b = teko_cc_rotl(b, 12); \
    a += b; d ^= a; d = teko_cc_rotl(d, 8);  \
    c += d; b ^= c; b = teko_cc_rotl(b, 7)

static void teko_chacha20_core(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    unsigned i;
    for (i = 0u; i < 16u; ++i) x[i] = in[i];

    for (i = 0u; i < 10u; ++i) {
        // Column rounds.
        TEKO_CC_QR(x[0], x[4], x[8],  x[12]);
        TEKO_CC_QR(x[1], x[5], x[9],  x[13]);
        TEKO_CC_QR(x[2], x[6], x[10], x[14]);
        TEKO_CC_QR(x[3], x[7], x[11], x[15]);
        // Diagonal rounds.
        TEKO_CC_QR(x[0], x[5], x[10], x[15]);
        TEKO_CC_QR(x[1], x[6], x[11], x[12]);
        TEKO_CC_QR(x[2], x[7], x[8],  x[13]);
        TEKO_CC_QR(x[3], x[4], x[9],  x[14]);
    }

    for (i = 0u; i < 16u; ++i) {
        uint32_t v = x[i] + in[i];
        out[i * 4u + 0u] = (uint8_t)(v);
        out[i * 4u + 1u] = (uint8_t)(v >> 8);
        out[i * 4u + 2u] = (uint8_t)(v >> 16);
        out[i * 4u + 3u] = (uint8_t)(v >> 24);
    }
}

static void teko_chacha20_setup(uint32_t state[16], const uint8_t key[32],
                                uint32_t counter, const uint8_t nonce[12]) {
    unsigned i;
    state[0] = 0x61707865u; // "expa"
    state[1] = 0x3320646eu; // "nd 3"
    state[2] = 0x79622d32u; // "2-by"
    state[3] = 0x6b206574u; // "te k"
    for (i = 0u; i < 8u; ++i) state[4u + i] = teko_cc_load32_le(key + i * 4u);
    state[12] = counter;
    state[13] = teko_cc_load32_le(nonce + 0u);
    state[14] = teko_cc_load32_le(nonce + 4u);
    state[15] = teko_cc_load32_le(nonce + 8u);
}

void teko_chacha20_block(const uint8_t key[32], uint32_t counter,
                         const uint8_t nonce[12], uint8_t out[64]) {
    uint32_t state[16];
    teko_chacha20_setup(state, key, counter, nonce);
    teko_chacha20_core(state, out);
}

void teko_chacha20_xor(const uint8_t key[32], uint32_t counter,
                       const uint8_t nonce[12], const uint8_t* in, uint8_t* out, size_t len) {
    uint32_t state[16];
    uint8_t ks[64];
    size_t off = 0u;

    teko_chacha20_setup(state, key, counter, nonce);

    while (off < len) {
        size_t i;
        size_t take = (len - off < 64u) ? (len - off) : 64u;
        teko_chacha20_core(state, ks);
        for (i = 0u; i < take; ++i) {
            out[off + i] = (uint8_t)(in[off + i] ^ ks[i]);
        }
        off += take;
        state[12]++; // next block counter
    }
}
