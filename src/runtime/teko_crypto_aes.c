// Native constant-time AES (FIPS-197). No tables, no secret-dependent branches/indexing.
// The S-box is computed per byte as affine(inverse(x)); see the header for the rationale.

#include "teko_crypto_aes.h"

#include <string.h>

// Constant-time GF(2^8) carryless multiply mod the AES polynomial x^8+x^4+x^3+x+1 (0x11b).
// No branches: the conditional XORs are mask-selected from the low/high bits.
static uint8_t teko_aes_gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0u;
    unsigned i;
    for (i = 0u; i < 8u; ++i) {
        p ^= (uint8_t)(0u - (b & 1u)) & a;          // if (b&1) p ^= a
        {
            uint8_t hi = (uint8_t)(0u - ((a >> 7) & 1u)); // 0xff if high bit set
            a = (uint8_t)(a << 1);
            a ^= hi & 0x1bu;                          // reduce mod 0x11b
        }
        b = (uint8_t)(b >> 1);
    }
    return p;
}

static inline uint8_t teko_aes_rotl8(uint8_t x, unsigned n) {
    return (uint8_t)((x << n) | (x >> (8u - n)));
}

// Multiplicative inverse in GF(2^8): x^254 (= x^-1 for x!=0; 0 maps to 0). Fixed addition
// chain of squarings/multiplies — constant-time, no table.
static uint8_t teko_aes_ginv(uint8_t x) {
    uint8_t p2  = teko_aes_gmul(x, x);     // x^2
    uint8_t p4  = teko_aes_gmul(p2, p2);   // x^4
    uint8_t p8  = teko_aes_gmul(p4, p4);   // x^8
    uint8_t p16 = teko_aes_gmul(p8, p8);   // x^16
    uint8_t p32 = teko_aes_gmul(p16, p16); // x^32
    uint8_t p64 = teko_aes_gmul(p32, p32); // x^64
    uint8_t p128 = teko_aes_gmul(p64, p64); // x^128
    uint8_t r = teko_aes_gmul(p2, p4);     // x^6
    r = teko_aes_gmul(r, p8);              // x^14
    r = teko_aes_gmul(r, p16);             // x^30
    r = teko_aes_gmul(r, p32);             // x^62
    r = teko_aes_gmul(r, p64);             // x^126
    r = teko_aes_gmul(r, p128);            // x^254
    return r;
}

static uint8_t teko_aes_sbox(uint8_t x) {
    uint8_t inv = teko_aes_ginv(x);
    return (uint8_t)(inv ^ teko_aes_rotl8(inv, 1) ^ teko_aes_rotl8(inv, 2)
                     ^ teko_aes_rotl8(inv, 3) ^ teko_aes_rotl8(inv, 4) ^ 0x63u);
}

static uint8_t teko_aes_inv_sbox(uint8_t x) {
    uint8_t t = (uint8_t)(teko_aes_rotl8(x, 1) ^ teko_aes_rotl8(x, 3) ^ teko_aes_rotl8(x, 6) ^ 0x05u);
    return teko_aes_ginv(t);
}

int teko_aes_init(TekoAesKey* key, const uint8_t* key_bytes, size_t key_len) {
    size_t nk, total_words, i;
    int nr;
    uint8_t rcon = 1u;

    if (key_len == 16u) { nk = 4u; nr = 10; }
    else if (key_len == 24u) { nk = 6u; nr = 12; }
    else if (key_len == 32u) { nk = 8u; nr = 14; }
    else return -1;

    key->rounds = nr;
    total_words = 4u * ((size_t)nr + 1u);
    memcpy(key->round_keys, key_bytes, key_len);

    for (i = nk; i < total_words; ++i) {
        uint8_t t[4];
        memcpy(t, key->round_keys + (i - 1u) * 4u, 4u);

        if ((i % nk) == 0u) {
            uint8_t tmp = t[0];           // RotWord
            t[0] = t[1]; t[1] = t[2]; t[2] = t[3]; t[3] = tmp;
            t[0] = teko_aes_sbox(t[0]);   // SubWord
            t[1] = teko_aes_sbox(t[1]);
            t[2] = teko_aes_sbox(t[2]);
            t[3] = teko_aes_sbox(t[3]);
            t[0] ^= rcon;
            rcon = teko_aes_gmul(rcon, 2u); // next round constant
        } else if (nk > 6u && (i % nk) == 4u) {
            t[0] = teko_aes_sbox(t[0]);
            t[1] = teko_aes_sbox(t[1]);
            t[2] = teko_aes_sbox(t[2]);
            t[3] = teko_aes_sbox(t[3]);
        }

        key->round_keys[i * 4u + 0u] = key->round_keys[(i - nk) * 4u + 0u] ^ t[0];
        key->round_keys[i * 4u + 1u] = key->round_keys[(i - nk) * 4u + 1u] ^ t[1];
        key->round_keys[i * 4u + 2u] = key->round_keys[(i - nk) * 4u + 2u] ^ t[2];
        key->round_keys[i * 4u + 3u] = key->round_keys[(i - nk) * 4u + 3u] ^ t[3];
    }
    return 0;
}

static void teko_aes_add_round_key(uint8_t s[16], const uint8_t* rk) {
    unsigned i;
    for (i = 0u; i < 16u; ++i) s[i] ^= rk[i];
}

static void teko_aes_sub_bytes(uint8_t s[16]) {
    unsigned i;
    for (i = 0u; i < 16u; ++i) s[i] = teko_aes_sbox(s[i]);
}

static void teko_aes_inv_sub_bytes(uint8_t s[16]) {
    unsigned i;
    for (i = 0u; i < 16u; ++i) s[i] = teko_aes_inv_sbox(s[i]);
}

// State is column-major: byte(row r, col c) = s[r + 4*c].
static void teko_aes_shift_rows(uint8_t s[16]) {
    uint8_t t;
    // Row 1 <<< 1
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    // Row 2 <<< 2
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    // Row 3 <<< 3 (== >>> 1)
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

static void teko_aes_inv_shift_rows(uint8_t s[16]) {
    uint8_t t;
    // Row 1 >>> 1
    t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
    // Row 2 >>> 2
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    // Row 3 >>> 3 (== <<< 1)
    t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
}

static void teko_aes_mix_columns(uint8_t s[16]) {
    unsigned c;
    for (c = 0u; c < 4u; ++c) {
        uint8_t* col = s + c * 4u;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = teko_aes_gmul(a0, 2u) ^ teko_aes_gmul(a1, 3u) ^ a2 ^ a3;
        col[1] = a0 ^ teko_aes_gmul(a1, 2u) ^ teko_aes_gmul(a2, 3u) ^ a3;
        col[2] = a0 ^ a1 ^ teko_aes_gmul(a2, 2u) ^ teko_aes_gmul(a3, 3u);
        col[3] = teko_aes_gmul(a0, 3u) ^ a1 ^ a2 ^ teko_aes_gmul(a3, 2u);
    }
}

static void teko_aes_inv_mix_columns(uint8_t s[16]) {
    unsigned c;
    for (c = 0u; c < 4u; ++c) {
        uint8_t* col = s + c * 4u;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = teko_aes_gmul(a0, 14u) ^ teko_aes_gmul(a1, 11u) ^ teko_aes_gmul(a2, 13u) ^ teko_aes_gmul(a3, 9u);
        col[1] = teko_aes_gmul(a0, 9u) ^ teko_aes_gmul(a1, 14u) ^ teko_aes_gmul(a2, 11u) ^ teko_aes_gmul(a3, 13u);
        col[2] = teko_aes_gmul(a0, 13u) ^ teko_aes_gmul(a1, 9u) ^ teko_aes_gmul(a2, 14u) ^ teko_aes_gmul(a3, 11u);
        col[3] = teko_aes_gmul(a0, 11u) ^ teko_aes_gmul(a1, 13u) ^ teko_aes_gmul(a2, 9u) ^ teko_aes_gmul(a3, 14u);
    }
}

void teko_aes_encrypt_block(const TekoAesKey* key, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    int round;
    memcpy(s, in, 16u);

    teko_aes_add_round_key(s, key->round_keys);
    for (round = 1; round < key->rounds; ++round) {
        teko_aes_sub_bytes(s);
        teko_aes_shift_rows(s);
        teko_aes_mix_columns(s);
        teko_aes_add_round_key(s, key->round_keys + round * 16);
    }
    teko_aes_sub_bytes(s);
    teko_aes_shift_rows(s);
    teko_aes_add_round_key(s, key->round_keys + key->rounds * 16);

    memcpy(out, s, 16u);
}

void teko_aes_decrypt_block(const TekoAesKey* key, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    int round;
    memcpy(s, in, 16u);

    teko_aes_add_round_key(s, key->round_keys + key->rounds * 16);
    for (round = key->rounds - 1; round >= 1; --round) {
        teko_aes_inv_shift_rows(s);
        teko_aes_inv_sub_bytes(s);
        teko_aes_add_round_key(s, key->round_keys + round * 16);
        teko_aes_inv_mix_columns(s);
    }
    teko_aes_inv_shift_rows(s);
    teko_aes_inv_sub_bytes(s);
    teko_aes_add_round_key(s, key->round_keys);

    memcpy(out, s, 16u);
}
