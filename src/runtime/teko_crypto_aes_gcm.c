// Native AES-GCM (NIST SP 800-38D). Constant-time GHASH (bit-by-bit GF(2^128) multiply,
// no tables). Portable C23, no external libraries.

#include "teko_crypto_aes_gcm.h"

#include <string.h>

// GF(2^128) multiply Z = X * H, GCM bit/byte order (bit 0 = MSB of byte 0), reduction
// polynomial R = 0xe1||0^120. Constant-time: branchless mask-selected XOR/reduction.
static void teko_gcm_gfmul(uint8_t x[16], const uint8_t h[16]) {
    uint8_t z[16];
    uint8_t v[16];
    unsigned i, j;

    memset(z, 0, 16u);
    memcpy(v, h, 16u);

    for (i = 0u; i < 128u; ++i) {
        uint8_t bit = (uint8_t)((x[i >> 3] >> (7u - (i & 7u))) & 1u);
        uint8_t mask = (uint8_t)(0u - bit);
        uint8_t lsb;
        for (j = 0u; j < 16u; ++j) z[j] ^= mask & v[j];

        // v >>= 1 (big-endian, across bytes), then reduce if the shifted-out bit was set.
        lsb = (uint8_t)(v[15] & 1u);
        for (j = 15u; j > 0u; --j) v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1u] << 7));
        v[0] = (uint8_t)(v[0] >> 1);
        v[0] ^= (uint8_t)(0u - lsb) & 0xe1u;
    }
    memcpy(x, z, 16u);
}

// GHASH-fold `len` bytes (zero-padded to a 16-byte block) into the accumulator Y.
static void teko_gcm_ghash(uint8_t y[16], const uint8_t h[16], const uint8_t* data, size_t len) {
    size_t off = 0u;
    while (off < len) {
        uint8_t block[16];
        size_t take = (len - off < 16u) ? (len - off) : 16u;
        unsigned j;
        memset(block, 0, 16u);
        memcpy(block, data + off, take);
        for (j = 0u; j < 16u; ++j) y[j] ^= block[j];
        teko_gcm_gfmul(y, h);
        off += take;
    }
}

static void teko_gcm_be64(uint8_t out[8], uint64_t v) {
    unsigned i;
    for (i = 0u; i < 8u; ++i) out[i] = (uint8_t)(v >> (56u - 8u * i));
}

// Increment the rightmost 32 bits (big-endian) of a counter block.
static void teko_gcm_inc32(uint8_t ctr[16]) {
    uint32_t c = ((uint32_t)ctr[12] << 24) | ((uint32_t)ctr[13] << 16)
               | ((uint32_t)ctr[14] << 8) | (uint32_t)ctr[15];
    c++;
    ctr[12] = (uint8_t)(c >> 24);
    ctr[13] = (uint8_t)(c >> 16);
    ctr[14] = (uint8_t)(c >> 8);
    ctr[15] = (uint8_t)(c);
}

static void teko_gcm_gctr(const TekoAesKey* key, const uint8_t icb[16],
                          const uint8_t* in, uint8_t* out, size_t len) {
    uint8_t cb[16];
    size_t off = 0u;
    if (len == 0u) return;
    memcpy(cb, icb, 16u);
    while (off < len) {
        uint8_t ks[16];
        size_t take = (len - off < 16u) ? (len - off) : 16u;
        unsigned j;
        teko_aes_encrypt_block(key, cb, ks);
        for (j = 0u; j < take; ++j) out[off + j] = (uint8_t)(in[off + j] ^ ks[j]);
        teko_gcm_inc32(cb);
        off += take;
    }
}

// Derive H and the pre-counter block J0.
static void teko_gcm_setup(const TekoAesKey* key, const uint8_t* iv, size_t iv_len,
                           uint8_t h[16], uint8_t j0[16]) {
    static const uint8_t ZERO[16] = {0};
    teko_aes_encrypt_block(key, ZERO, h);

    if (iv_len == 12u) {
        memcpy(j0, iv, 12u);
        j0[12] = 0u; j0[13] = 0u; j0[14] = 0u; j0[15] = 1u;
    } else {
        uint8_t lenblk[16];
        memset(j0, 0, 16u);
        teko_gcm_ghash(j0, h, iv, iv_len);
        memset(lenblk, 0, 8u);
        teko_gcm_be64(lenblk + 8, (uint64_t)iv_len * 8u);
        {
            unsigned j;
            for (j = 0u; j < 16u; ++j) j0[j] ^= lenblk[j];
        }
        teko_gcm_gfmul(j0, h);
    }
}

// S = GHASH(H, AAD || pad || C || pad || [len(AAD)]_64 || [len(C)]_64).
static void teko_gcm_tag_hash(const uint8_t h[16],
                              const uint8_t* aad, size_t aad_len,
                              const uint8_t* ct, size_t ct_len, uint8_t s[16]) {
    uint8_t lenblk[16];
    unsigned j;
    memset(s, 0, 16u);
    teko_gcm_ghash(s, h, aad, aad_len);
    teko_gcm_ghash(s, h, ct, ct_len);
    teko_gcm_be64(lenblk + 0, (uint64_t)aad_len * 8u);
    teko_gcm_be64(lenblk + 8, (uint64_t)ct_len * 8u);
    for (j = 0u; j < 16u; ++j) s[j] ^= lenblk[j];
    teko_gcm_gfmul(s, h);
}

void teko_aes_gcm_encrypt(const TekoAesKey* key,
                          const uint8_t* iv, size_t iv_len,
                          const uint8_t* aad, size_t aad_len,
                          const uint8_t* pt, size_t pt_len,
                          uint8_t* ct, uint8_t* tag, size_t tag_len) {
    uint8_t h[16], j0[16], s[16], ej0[16], ctr0[16];
    unsigned j;

    teko_gcm_setup(key, iv, iv_len, h, j0);

    memcpy(ctr0, j0, 16u);
    teko_gcm_inc32(ctr0);
    teko_gcm_gctr(key, ctr0, pt, ct, pt_len);

    teko_gcm_tag_hash(h, aad, aad_len, ct, pt_len, s);
    teko_aes_encrypt_block(key, j0, ej0);
    for (j = 0u; j < 16u; ++j) s[j] ^= ej0[j];

    if (tag_len > 16u) tag_len = 16u;
    memcpy(tag, s, tag_len);
}

int teko_aes_gcm_decrypt(const TekoAesKey* key,
                         const uint8_t* iv, size_t iv_len,
                         const uint8_t* aad, size_t aad_len,
                         const uint8_t* ct, size_t ct_len,
                         const uint8_t* tag, size_t tag_len,
                         uint8_t* pt) {
    uint8_t h[16], j0[16], s[16], ej0[16], ctr0[16];
    uint8_t diff = 0u;
    unsigned j;

    if (tag_len == 0u || tag_len > 16u) return -1;

    teko_gcm_setup(key, iv, iv_len, h, j0);

    // Authenticate over the received ciphertext first.
    teko_gcm_tag_hash(h, aad, aad_len, ct, ct_len, s);
    teko_aes_encrypt_block(key, j0, ej0);
    for (j = 0u; j < 16u; ++j) s[j] ^= ej0[j];

    for (j = 0u; j < (unsigned)tag_len; ++j) diff |= (uint8_t)(s[j] ^ tag[j]);
    if (diff != 0u) {
        if (pt && ct_len > 0u) memset(pt, 0, ct_len);
        return -1;
    }

    memcpy(ctr0, j0, 16u);
    teko_gcm_inc32(ctr0);
    teko_gcm_gctr(key, ctr0, ct, pt, ct_len);
    return 0;
}
