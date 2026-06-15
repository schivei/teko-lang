// NIST P-256 ECDH (see header). Curve constants are the standard secp256r1 parameters
// (FIPS 186-4 / SEC 2), big-endian.

#include "teko_crypto_p256.h"
#include "teko_crypto_ec.h"
#include "teko_crypto_bn.h"

#include <string.h>

#define P256_LIMBS 8

// p = 2^256 - 2^224 + 2^192 + 2^96 - 1
static const uint8_t P256_P[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
};
static const uint8_t P256_B[32] = {
    0x5a,0xc6,0x35,0xd8,0xaa,0x3a,0x93,0xe7,0xb3,0xeb,0xbd,0x55,0x76,0x98,0x86,0xbc,
    0x65,0x1d,0x06,0xb0,0xcc,0x53,0xb0,0xf6,0x3b,0xce,0x3c,0x3e,0x27,0xd2,0x60,0x4b
};
static const uint8_t P256_GX[32] = {
    0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,
    0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96
};
static const uint8_t P256_GY[32] = {
    0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,
    0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5
};

typedef struct {
    TekoMont fp;                  // field prime p
    uint32_t b[P256_LIMBS];       // curve b, Montgomery form
    uint32_t gx[P256_LIMBS];      // generator x, Montgomery form
    uint32_t gy[P256_LIMBS];      // generator y, Montgomery form
} P256;

static int p256_setup(P256* c) {
    uint32_t t[P256_LIMBS];
    if (teko_mont_init(&c->fp, P256_P, 32u) != 0) return -1;
    teko_bn_load_be(t, P256_LIMBS, P256_B, 32u);  teko_mont_to(&c->fp, c->b, t);
    teko_bn_load_be(t, P256_LIMBS, P256_GX, 32u); teko_mont_to(&c->fp, c->gx, t);
    teko_bn_load_be(t, P256_LIMBS, P256_GY, 32u); teko_mont_to(&c->fp, c->gy, t);
    return 0;
}

// 1 iff the limb array v (n limbs) is strictly less than the modulus m, else 0.
static int p256_lt_mod(const uint32_t* v, const uint32_t* m, int n) {
    int i;
    for (i = n - 1; i >= 0; --i) {
        if (v[i] != m[i]) return (v[i] < m[i]) ? 1 : 0;
    }
    return 0; // equal -> not less
}

// Verify the affine point (xm:ym) (Montgomery form) satisfies y^2 = x^3 - 3x + b.
static int p256_on_curve(const P256* c, const uint32_t* xm, const uint32_t* ym) {
    const TekoMont* fp = &c->fp;
    uint32_t y2[P256_LIMBS], x2[P256_LIMBS], x3[P256_LIMBS], three_x[P256_LIMBS], rhs[P256_LIMBS];
    teko_mont_mul(fp, y2, ym, ym);
    teko_mont_mul(fp, x2, xm, xm);
    teko_mont_mul(fp, x3, x2, xm);
    teko_mont_add(fp, three_x, xm, xm);
    teko_mont_add(fp, three_x, three_x, xm);   // 3x
    teko_mont_sub(fp, rhs, x3, three_x);        // x^3 - 3x
    teko_mont_add(fp, rhs, rhs, c->b);          // + b
    return teko_bn_eq(y2, rhs, P256_LIMBS);
}

// Project (X:Y:Z) -> affine, writing x (and y if non-NULL) as 32-byte big-endian.
static int p256_to_affine(const P256* c, const uint32_t* X, const uint32_t* Y,
                          const uint32_t* Z, uint8_t* out_x, uint8_t* out_y) {
    const TekoMont* fp = &c->fp;
    uint32_t zinv[P256_LIMBS], am[P256_LIMBS], ao[P256_LIMBS];
    if (teko_bn_is_zero(Z, P256_LIMBS)) return -1; // point at infinity
    teko_ec_fp_inv(fp, zinv, Z);
    teko_mont_mul(fp, am, X, zinv);
    teko_mont_from(fp, ao, am);
    teko_bn_store_be(out_x, 32u, ao, P256_LIMBS);
    if (out_y) {
        teko_mont_mul(fp, am, Y, zinv);
        teko_mont_from(fp, ao, am);
        teko_bn_store_be(out_y, 32u, ao, P256_LIMBS);
    }
    return 0;
}

int teko_p256_derive_public(const uint8_t priv[32], uint8_t pub[64]) {
    P256 c;
    uint32_t one[P256_LIMBS], Rx[P256_LIMBS], Ry[P256_LIMBS], Rz[P256_LIMBS];
    if (!priv || !pub) return -1;
    if (p256_setup(&c) != 0) return -1;
    memcpy(one, c.fp.one, sizeof one);          // Z = 1 in Montgomery form
    teko_ec_scalar_mult(&c.fp, c.b, Rx, Ry, Rz, priv, 32u, c.gx, c.gy, one);
    return p256_to_affine(&c, Rx, Ry, Rz, pub, pub + 32);
}

int teko_p256_ecdh(const uint8_t priv[32], const uint8_t peer_pub[64], uint8_t out_x[32]) {
    P256 c;
    uint32_t px[P256_LIMBS], py[P256_LIMBS], one[P256_LIMBS];
    uint32_t Pxm[P256_LIMBS], Pym[P256_LIMBS];
    uint32_t Rx[P256_LIMBS], Ry[P256_LIMBS], Rz[P256_LIMBS];
    if (!priv || !peer_pub || !out_x) return -1;
    if (p256_setup(&c) != 0) return -1;

    teko_bn_load_be(px, P256_LIMBS, peer_pub, 32u);
    teko_bn_load_be(py, P256_LIMBS, peer_pub + 32, 32u);
    if (!p256_lt_mod(px, c.fp.m, P256_LIMBS)) return -1; // coords must be < p
    if (!p256_lt_mod(py, c.fp.m, P256_LIMBS)) return -1;
    teko_mont_to(&c.fp, Pxm, px);
    teko_mont_to(&c.fp, Pym, py);
    if (!p256_on_curve(&c, Pxm, Pym)) return -1;

    memcpy(one, c.fp.one, sizeof one);          // Z = 1
    teko_ec_scalar_mult(&c.fp, c.b, Rx, Ry, Rz, priv, 32u, Pxm, Pym, one);
    return p256_to_affine(&c, Rx, Ry, Rz, out_x, NULL);
}
