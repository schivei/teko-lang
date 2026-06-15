// Native X25519 (RFC 7748) over Curve25519. Portable C23, constant-time Montgomery ladder
// on the shared GF(2^255-19) field arithmetic (teko_crypto_fe25519).

#include "teko_crypto_x25519.h"
#include "teko_crypto_fe25519.h"

static const teko_fe TEKO_X25519_121665 = { 0xDB41, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

int teko_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u_coordinate[32]) {
    uint8_t z[32];
    teko_fe x, a, b, c, d, e, f;
    int i;
    int64_t r;

    for (i = 0; i < 31; ++i) z[i] = scalar[i];
    z[31] = (uint8_t)((scalar[31] & 127) | 64); // clamp
    z[0] &= 248;

    teko_fe_unpack(x, u_coordinate);
    for (i = 0; i < 16; ++i) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;

    for (i = 254; i >= 0; --i) {
        r = (z[i >> 3] >> (i & 7)) & 1;
        teko_fe_cswap(a, b, (int)r);
        teko_fe_cswap(c, d, (int)r);
        teko_fe_add(e, a, c);
        teko_fe_sub(a, a, c);
        teko_fe_add(c, b, d);
        teko_fe_sub(b, b, d);
        teko_fe_sq(d, e);
        teko_fe_sq(f, a);
        teko_fe_mul(a, c, a);
        teko_fe_mul(c, b, e);
        teko_fe_add(e, a, c);
        teko_fe_sub(a, a, c);
        teko_fe_sq(b, a);
        teko_fe_sub(c, d, f);
        teko_fe_mul(a, c, TEKO_X25519_121665);
        teko_fe_add(a, a, d);
        teko_fe_mul(c, c, a);
        teko_fe_mul(a, d, f);
        teko_fe_mul(d, b, x);
        teko_fe_sq(b, e);
        teko_fe_cswap(a, b, (int)r);
        teko_fe_cswap(c, d, (int)r);
    }
    teko_fe_inv(c, c);
    teko_fe_mul(a, a, c);
    teko_fe_pack(out, a);
    return 0;
}

void teko_x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    uint8_t base[32];
    int i;
    base[0] = 9;
    for (i = 1; i < 32; ++i) base[i] = 0;
    (void)teko_x25519(out, scalar, base);
}
