// Native X25519 (RFC 7748) over Curve25519. Portable C23, constant-time Montgomery ladder.
// Field arithmetic uses a radix-2^16 representation (gf = int64_t[16]) with schoolbook
// multiply — only 16x16->32-bit products in 64-bit accumulators, so it is MSVC-safe (no
// __int128) and portable. (Field-arithmetic approach after the public-domain TweetNaCl.)

#include "teko_crypto_x25519.h"

#include <stdint.h>

typedef int64_t teko_gf[16];

static const teko_gf TEKO_X25519_121665 = { 0xDB41, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static void teko_gf_car(teko_gf o) {
    int i;
    int64_t c;
    for (i = 0; i < 16; ++i) {
        o[i] += (int64_t)1 << 16;
        c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        // c may be negative; shift in unsigned to keep exact two's-complement semantics
        // without the UB of left-shifting a negative signed value.
        o[i] -= (int64_t)((uint64_t)c << 16);
    }
}

static void teko_gf_sel(teko_gf p, teko_gf q, int b) {
    int64_t t, c = ~((int64_t)b - 1);
    int i;
    for (i = 0; i < 16; ++i) {
        t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void teko_gf_add(teko_gf o, const teko_gf a, const teko_gf b) {
    int i;
    for (i = 0; i < 16; ++i) o[i] = a[i] + b[i];
}

static void teko_gf_sub(teko_gf o, const teko_gf a, const teko_gf b) {
    int i;
    for (i = 0; i < 16; ++i) o[i] = a[i] - b[i];
}

static void teko_gf_mul(teko_gf o, const teko_gf a, const teko_gf b) {
    int64_t t[31];
    int i, j;
    for (i = 0; i < 31; ++i) t[i] = 0;
    for (i = 0; i < 16; ++i) for (j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (i = 0; i < 16; ++i) o[i] = t[i];
    teko_gf_car(o);
    teko_gf_car(o);
}

static void teko_gf_sq(teko_gf o, const teko_gf a) {
    teko_gf_mul(o, a, a);
}

static void teko_gf_inv(teko_gf o, const teko_gf i) {
    teko_gf c;
    int a;
    for (a = 0; a < 16; ++a) c[a] = i[a];
    for (a = 253; a >= 0; --a) {
        teko_gf_sq(c, c);
        if (a != 2 && a != 4) teko_gf_mul(c, c, i);
    }
    for (a = 0; a < 16; ++a) o[a] = c[a];
}

static void teko_gf_unpack(teko_gf o, const uint8_t* n) {
    int i;
    for (i = 0; i < 16; ++i) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void teko_gf_pack(uint8_t* o, const teko_gf n) {
    teko_gf m, t;
    int i, j, b;
    for (i = 0; i < 16; ++i) t[i] = n[i];
    teko_gf_car(t); teko_gf_car(t); teko_gf_car(t);
    for (j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        teko_gf_sel(t, m, 1 - b);
    }
    for (i = 0; i < 16; ++i) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

int teko_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u_coordinate[32]) {
    uint8_t z[32];
    teko_gf x, a, b, c, d, e, f;
    int i;
    int64_t r;

    for (i = 0; i < 31; ++i) z[i] = scalar[i];
    z[31] = (uint8_t)((scalar[31] & 127) | 64); // clamp
    z[0] &= 248;

    teko_gf_unpack(x, u_coordinate);
    for (i = 0; i < 16; ++i) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;

    for (i = 254; i >= 0; --i) {
        r = (z[i >> 3] >> (i & 7)) & 1;
        teko_gf_sel(a, b, (int)r);
        teko_gf_sel(c, d, (int)r);
        teko_gf_add(e, a, c);
        teko_gf_sub(a, a, c);
        teko_gf_add(c, b, d);
        teko_gf_sub(b, b, d);
        teko_gf_sq(d, e);
        teko_gf_sq(f, a);
        teko_gf_mul(a, c, a);
        teko_gf_mul(c, b, e);
        teko_gf_add(e, a, c);
        teko_gf_sub(a, a, c);
        teko_gf_sq(b, a);
        teko_gf_sub(c, d, f);
        teko_gf_mul(a, c, TEKO_X25519_121665);
        teko_gf_add(a, a, d);
        teko_gf_mul(c, c, a);
        teko_gf_mul(a, d, f);
        teko_gf_mul(d, b, x);
        teko_gf_sq(b, e);
        teko_gf_sel(a, b, (int)r);
        teko_gf_sel(c, d, (int)r);
    }
    teko_gf_inv(c, c);
    teko_gf_mul(a, a, c);
    teko_gf_pack(out, a);
    return 0;
}

void teko_x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    uint8_t base[32];
    int i;
    base[0] = 9;
    for (i = 1; i < 32; ++i) base[i] = 0;
    (void)teko_x25519(out, scalar, base);
}
