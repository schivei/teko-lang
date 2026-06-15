// Native Argon2 (RFC 9106), version 0x13. Portable C23, no external libraries. Built on
// BLAKE2b (H0 and the variable-length hash H'). Single-threaded over the lanes (the lane
// loop is sequential; this matches the spec output, parallelism only affects memory layout).

#include "teko_crypto_argon2.h"
#include "teko_crypto_blake2b.h"

#include <stdlib.h>
#include <string.h>

#define TEKO_A2_WORDS 128u            // 1024-byte block = 128 uint64
#define TEKO_A2_SYNC  4u              // segments per lane

typedef uint64_t TekoA2Block[TEKO_A2_WORDS];

static inline uint64_t teko_a2_rotr(uint64_t x, unsigned n) {
    return (x >> n) | (x << (64u - n));
}

static void teko_a2_st32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint64_t teko_a2_ld64(const uint8_t* p) {
    return ((uint64_t)p[0]) | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24)
         | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static void teko_a2_st64(uint8_t* p, uint64_t v) {
    unsigned i;
    for (i = 0u; i < 8u; ++i) p[i] = (uint8_t)(v >> (8u * i));
}

// Argon2 variable-length hash H'.
static void teko_a2_hprime(uint8_t* out, uint32_t out_len, const uint8_t* in, size_t in_len) {
    uint8_t lenpfx[4];
    teko_a2_st32(lenpfx, out_len);

    if (out_len <= 64u) {
        TekoBlake2bCtx ctx;
        teko_blake2b_init(&ctx, out_len, NULL, 0u);
        teko_blake2b_update(&ctx, lenpfx, 4u);
        teko_blake2b_update(&ctx, in, in_len);
        teko_blake2b_final(&ctx, out);
        return;
    }
    {
        uint8_t v[64];
        uint32_t r = (out_len + 31u) / 32u - 2u; // ceil(out_len/32) - 2
        uint32_t i;
        size_t pos = 0u;
        TekoBlake2bCtx ctx;

        teko_blake2b_init(&ctx, 64u, NULL, 0u);
        teko_blake2b_update(&ctx, lenpfx, 4u);
        teko_blake2b_update(&ctx, in, in_len);
        teko_blake2b_final(&ctx, v);
        memcpy(out + pos, v, 32u); pos += 32u;

        for (i = 2u; i <= r; ++i) {
            teko_blake2b(v, 64u, v, 64u);
            memcpy(out + pos, v, 32u); pos += 32u;
        }
        {
            uint32_t last = out_len - 32u * r;
            teko_blake2b(v, 64u, out + pos, last);
        }
    }
}

// GB: the modified BLAKE2 quarter-round with the 64-bit low-half multiply.
#define TEKO_A2_GB(a, b, c, d) do {                                         \
    uint64_t m_;                                                            \
    m_ = (uint64_t)(uint32_t)(a) * (uint64_t)(uint32_t)(b);                 \
    (a) = (a) + (b) + 2u * m_;  (d) = teko_a2_rotr((d) ^ (a), 32);          \
    m_ = (uint64_t)(uint32_t)(c) * (uint64_t)(uint32_t)(d);                 \
    (c) = (c) + (d) + 2u * m_;  (b) = teko_a2_rotr((b) ^ (c), 24);          \
    m_ = (uint64_t)(uint32_t)(a) * (uint64_t)(uint32_t)(b);                 \
    (a) = (a) + (b) + 2u * m_;  (d) = teko_a2_rotr((d) ^ (a), 16);          \
    m_ = (uint64_t)(uint32_t)(c) * (uint64_t)(uint32_t)(d);                 \
    (c) = (c) + (d) + 2u * m_;  (b) = teko_a2_rotr((b) ^ (c), 63);          \
} while (0)

// P: the Argon2 permutation over 16 words (s[0..15]).
static void teko_a2_P(uint64_t* s) {
    TEKO_A2_GB(s[0], s[4], s[8],  s[12]);
    TEKO_A2_GB(s[1], s[5], s[9],  s[13]);
    TEKO_A2_GB(s[2], s[6], s[10], s[14]);
    TEKO_A2_GB(s[3], s[7], s[11], s[15]);
    TEKO_A2_GB(s[0], s[5], s[10], s[15]);
    TEKO_A2_GB(s[1], s[6], s[11], s[12]);
    TEKO_A2_GB(s[2], s[7], s[8],  s[13]);
    TEKO_A2_GB(s[3], s[4], s[9],  s[14]);
}

// Compression: out = (P-rows then P-cols of (X^Y)) XOR (X^Y); when xor_out, fold into out.
static void teko_a2_G(uint64_t* out, const uint64_t* x, const uint64_t* y, int xor_out) {
    TekoA2Block r;
    TekoA2Block q;
    unsigned i, j;

    for (i = 0u; i < TEKO_A2_WORDS; ++i) { r[i] = x[i] ^ y[i]; q[i] = r[i]; }

    // Apply P to each of the 8 rows (16 words each).
    for (i = 0u; i < 8u; ++i) teko_a2_P(q + 16u * i);

    // Apply P to each of the 8 columns (2 words per row, 8 rows -> 16 words).
    for (j = 0u; j < 8u; ++j) {
        uint64_t col[16];
        for (i = 0u; i < 8u; ++i) {
            col[2u * i + 0u] = q[16u * i + 2u * j + 0u];
            col[2u * i + 1u] = q[16u * i + 2u * j + 1u];
        }
        teko_a2_P(col);
        for (i = 0u; i < 8u; ++i) {
            q[16u * i + 2u * j + 0u] = col[2u * i + 0u];
            q[16u * i + 2u * j + 1u] = col[2u * i + 1u];
        }
    }

    if (xor_out) {
        for (i = 0u; i < TEKO_A2_WORDS; ++i) out[i] ^= q[i] ^ r[i];
    } else {
        for (i = 0u; i < TEKO_A2_WORDS; ++i) out[i] = q[i] ^ r[i];
    }
}

typedef struct {
    TekoA2Block* mem;
    uint32_t lanes;
    uint32_t lane_length;
    uint32_t segment_length;
    uint32_t passes;
    TekoArgon2Type type;
} TekoA2Ctx;

static uint32_t teko_a2_index_alpha(const TekoA2Ctx* c, uint32_t pass, uint32_t lane,
                                    uint32_t slice, uint32_t index, uint64_t j1, int same_lane) {
    uint64_t area;
    uint64_t rel, start, pos;

    if (pass == 0u) {
        if (slice == 0u) area = (uint64_t)index - 1u;
        else if (same_lane) area = (uint64_t)slice * c->segment_length + index - 1u;
        else area = (uint64_t)slice * c->segment_length + (index == 0u ? (uint64_t)-1 : 0u);
    } else {
        if (same_lane) area = (uint64_t)c->lane_length - c->segment_length + index - 1u;
        else area = (uint64_t)c->lane_length - c->segment_length + (index == 0u ? (uint64_t)-1 : 0u);
    }
    (void)lane;

    rel = j1;
    rel = (rel * rel) >> 32;
    rel = area - 1u - ((area * rel) >> 32);

    start = 0u;
    if (pass != 0u) start = (slice == TEKO_A2_SYNC - 1u) ? 0u : (uint64_t)(slice + 1u) * c->segment_length;

    pos = (start + rel) % c->lane_length;
    return (uint32_t)pos;
}

static void teko_a2_fill_segment(TekoA2Ctx* c, uint32_t pass, uint32_t lane, uint32_t slice) {
    TekoA2Block addr, input, zero, tmp;
    int data_indep = (c->type == TEKO_ARGON2_I) ||
                     (c->type == TEKO_ARGON2_ID && pass == 0u && slice < 2u);
    uint32_t start_index = 0u;
    uint32_t i;
    uint64_t counter = 0u;

    if (data_indep) {
        memset(zero, 0, sizeof(zero));
        memset(input, 0, sizeof(input));
        input[0] = pass;
        input[1] = lane;
        input[2] = slice;
        input[3] = c->lanes * c->lane_length; // total blocks
        input[4] = c->passes;
        input[5] = (uint64_t)c->type;
        // pre-generate the first address block (covers indices 0..127)
        counter++;
        input[6] = counter;
        teko_a2_G(tmp, zero, input, 0);
        teko_a2_G(addr, zero, tmp, 0);
    }

    if (pass == 0u && slice == 0u) start_index = 2u;

    for (i = start_index; i < c->segment_length; ++i) {
        uint32_t cur_col = slice * c->segment_length + i;
        uint32_t prev_col = (cur_col == 0u) ? (c->lane_length - 1u) : (cur_col - 1u);
        uint64_t* prev_block = c->mem[(size_t)lane * c->lane_length + prev_col];
        uint64_t pseudo_rand;
        uint64_t j1, j2;
        uint32_t ref_lane, ref_index;
        uint64_t* ref_block;
        uint64_t* cur_block;

        if (data_indep) {
            if (i != 0u && (i % TEKO_A2_WORDS) == 0u) {
                counter++;
                input[6] = counter;
                teko_a2_G(tmp, zero, input, 0);
                teko_a2_G(addr, zero, tmp, 0);
            }
            pseudo_rand = addr[i % TEKO_A2_WORDS];
        } else {
            pseudo_rand = prev_block[0];
        }

        j1 = pseudo_rand & 0xffffffffULL;
        j2 = pseudo_rand >> 32;

        ref_lane = (pass == 0u && slice == 0u) ? lane : (uint32_t)(j2 % c->lanes);
        ref_index = teko_a2_index_alpha(c, pass, lane, slice, i, j1, ref_lane == lane);
        ref_block = c->mem[(size_t)ref_lane * c->lane_length + ref_index];
        cur_block = c->mem[(size_t)lane * c->lane_length + cur_col];

        teko_a2_G(cur_block, prev_block, ref_block, (pass == 0u) ? 0 : 1);
    }
}

int teko_argon2(TekoArgon2Type type,
                uint32_t t_cost, uint32_t m_cost, uint32_t parallelism,
                const uint8_t* pwd, size_t pwd_len,
                const uint8_t* salt, size_t salt_len,
                const uint8_t* key, size_t key_len,
                const uint8_t* ad, size_t ad_len,
                uint8_t* tag, size_t tag_len) {
    TekoA2Ctx c;
    TekoBlake2bCtx h;
    uint8_t h0[72];                 // H0 (64) + space for LE32(j) + LE32(lane)
    uint8_t lenbuf[4];
    uint32_t m_prime, lane_length, segment_length, blocks_total;
    uint32_t pass, slice, lane;
    uint8_t* block_bytes = NULL;
    int rc = -1;

    if (t_cost == 0u || parallelism == 0u || tag_len < 4u) return -1;
    if (m_cost < 8u * parallelism) m_cost = 8u * parallelism; // RFC minimum
    if (type != TEKO_ARGON2_D && type != TEKO_ARGON2_I && type != TEKO_ARGON2_ID) return -1;

    segment_length = m_cost / (TEKO_A2_SYNC * parallelism);
    if (segment_length == 0u) return -1;
    lane_length = segment_length * TEKO_A2_SYNC;
    m_prime = lane_length * parallelism;
    blocks_total = m_prime;

    // H0 = BLAKE2b-64 of the parameter/header preimage.
    teko_blake2b_init(&h, 64u, NULL, 0u);
    teko_a2_st32(lenbuf, parallelism);       teko_blake2b_update(&h, lenbuf, 4u);
    teko_a2_st32(lenbuf, (uint32_t)tag_len); teko_blake2b_update(&h, lenbuf, 4u);
    teko_a2_st32(lenbuf, m_cost);            teko_blake2b_update(&h, lenbuf, 4u);
    teko_a2_st32(lenbuf, t_cost);            teko_blake2b_update(&h, lenbuf, 4u);
    teko_a2_st32(lenbuf, 0x13u);             teko_blake2b_update(&h, lenbuf, 4u);
    teko_a2_st32(lenbuf, (uint32_t)type);    teko_blake2b_update(&h, lenbuf, 4u);
    teko_a2_st32(lenbuf, (uint32_t)pwd_len); teko_blake2b_update(&h, lenbuf, 4u);
    teko_blake2b_update(&h, pwd, pwd_len);
    teko_a2_st32(lenbuf, (uint32_t)salt_len); teko_blake2b_update(&h, lenbuf, 4u);
    teko_blake2b_update(&h, salt, salt_len);
    teko_a2_st32(lenbuf, (uint32_t)key_len);  teko_blake2b_update(&h, lenbuf, 4u);
    if (key_len) teko_blake2b_update(&h, key, key_len);
    teko_a2_st32(lenbuf, (uint32_t)ad_len);   teko_blake2b_update(&h, lenbuf, 4u);
    if (ad_len) teko_blake2b_update(&h, ad, ad_len);
    teko_blake2b_final(&h, h0);

    c.mem = (TekoA2Block*)malloc((size_t)blocks_total * sizeof(TekoA2Block));
    block_bytes = (uint8_t*)malloc(1024u);
    if (!c.mem || !block_bytes) goto done;
    c.lanes = parallelism;
    c.lane_length = lane_length;
    c.segment_length = segment_length;
    c.passes = t_cost;
    c.type = type;

    // Initial two blocks of each lane via H'.
    for (lane = 0u; lane < parallelism; ++lane) {
        uint32_t col;
        for (col = 0u; col < 2u; ++col) {
            uint32_t k;
            teko_a2_st32(h0 + 64u, col);
            teko_a2_st32(h0 + 68u, lane);
            teko_a2_hprime(block_bytes, 1024u, h0, 72u);
            for (k = 0u; k < TEKO_A2_WORDS; ++k) {
                c.mem[(size_t)lane * lane_length + col][k] = teko_a2_ld64(block_bytes + k * 8u);
            }
        }
    }

    // Fill the memory.
    for (pass = 0u; pass < t_cost; ++pass) {
        for (slice = 0u; slice < TEKO_A2_SYNC; ++slice) {
            for (lane = 0u; lane < parallelism; ++lane) {
                teko_a2_fill_segment(&c, pass, lane, slice);
            }
        }
    }

    // Final block C = XOR of the last column of every lane; tag = H'(C, tag_len).
    {
        TekoA2Block final_block;
        uint32_t k;
        memcpy(final_block, c.mem[(size_t)0 * lane_length + (lane_length - 1u)], sizeof(final_block));
        for (lane = 1u; lane < parallelism; ++lane) {
            uint64_t* lb = c.mem[(size_t)lane * lane_length + (lane_length - 1u)];
            for (k = 0u; k < TEKO_A2_WORDS; ++k) final_block[k] ^= lb[k];
        }
        for (k = 0u; k < TEKO_A2_WORDS; ++k) teko_a2_st64(block_bytes + k * 8u, final_block[k]);
        teko_a2_hprime(tag, (uint32_t)tag_len, block_bytes, 1024u);
        memset(final_block, 0, sizeof(final_block));
    }
    rc = 0;

done:
    if (c.mem) { memset(c.mem, 0, (size_t)blocks_total * sizeof(TekoA2Block)); free(c.mem); }
    if (block_bytes) { memset(block_bytes, 0, 1024u); free(block_bytes); }
    memset(h0, 0, sizeof(h0));
    return rc;
}
