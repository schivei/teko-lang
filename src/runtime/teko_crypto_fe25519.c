// Shared Curve25519 field arithmetic (see header). Portable C23, constant-time.

#include "teko_crypto_fe25519.h"

static void teko_fe_car(teko_fe o) {
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

void teko_fe_copy(teko_fe o, const teko_fe a) {
    int i;
    for (i = 0; i < 16; ++i) o[i] = a[i];
}

void teko_fe_add(teko_fe o, const teko_fe a, const teko_fe b) {
    int i;
    for (i = 0; i < 16; ++i) o[i] = a[i] + b[i];
}

void teko_fe_sub(teko_fe o, const teko_fe a, const teko_fe b) {
    int i;
    for (i = 0; i < 16; ++i) o[i] = a[i] - b[i];
}

void teko_fe_mul(teko_fe o, const teko_fe a, const teko_fe b) {
    int64_t t[31];
    int i, j;
    for (i = 0; i < 31; ++i) t[i] = 0;
    for (i = 0; i < 16; ++i) for (j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (i = 0; i < 16; ++i) o[i] = t[i];
    teko_fe_car(o);
    teko_fe_car(o);
}

void teko_fe_sq(teko_fe o, const teko_fe a) {
    teko_fe_mul(o, a, a);
}

void teko_fe_inv(teko_fe o, const teko_fe a) {
    teko_fe c;
    int i;
    for (i = 0; i < 16; ++i) c[i] = a[i];
    for (i = 253; i >= 0; --i) {
        teko_fe_sq(c, c);
        if (i != 2 && i != 4) teko_fe_mul(c, c, a);
    }
    for (i = 0; i < 16; ++i) o[i] = c[i];
}

void teko_fe_pow2523(teko_fe o, const teko_fe a) {
    teko_fe c;
    int i;
    for (i = 0; i < 16; ++i) c[i] = a[i];
    for (i = 250; i >= 0; --i) {
        teko_fe_sq(c, c);
        if (i != 1) teko_fe_mul(c, c, a);
    }
    for (i = 0; i < 16; ++i) o[i] = c[i];
}

void teko_fe_cswap(teko_fe p, teko_fe q, int b) {
    int64_t t, c = ~((int64_t)b - 1);
    int i;
    for (i = 0; i < 16; ++i) {
        t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void teko_fe_pack(uint8_t out[32], const teko_fe n) {
    teko_fe m, t;
    int i, j, b;
    for (i = 0; i < 16; ++i) t[i] = n[i];
    teko_fe_car(t); teko_fe_car(t); teko_fe_car(t);
    for (j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        teko_fe_cswap(t, m, 1 - b);
    }
    for (i = 0; i < 16; ++i) {
        out[2 * i] = (uint8_t)(t[i] & 0xff);
        out[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

void teko_fe_unpack(teko_fe o, const uint8_t in[32]) {
    int i;
    for (i = 0; i < 16; ++i) o[i] = in[2 * i] + ((int64_t)in[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

int teko_fe_parity(const teko_fe a) {
    uint8_t s[32];
    teko_fe_pack(s, a);
    return s[0] & 1;
}

int teko_fe_neq(const teko_fe a, const teko_fe b) {
    uint8_t sa[32], sb[32];
    int i, d = 0;
    teko_fe_pack(sa, a);
    teko_fe_pack(sb, b);
    for (i = 0; i < 32; ++i) d |= (sa[i] ^ sb[i]);
    return d != 0 ? 1 : 0;
}
