// NIST P-256 ECDH (see header). Curve constants are the standard secp256r1 parameters
// (FIPS 186-4 / SEC 2), big-endian.

#include "teko_crypto_p256.h"
#include "teko_crypto_ec.h"
#include "teko_crypto_bn.h"
#include "teko_crypto_hmac.h"
#include "teko_crypto_sha2.h"

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
// n = group order (number of points)
static const uint8_t P256_N[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51
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

// ------------------------------- ECDSA (RFC 6979 nonces) ---------------------------------

// FIPS 186 bits2int as octets, for byte-aligned qlen (256 here). Takes the leftmost qlen
// bits of `in`: the top 32 bytes if longer, else left-zero-padded to 32 bytes.
static void p256_bits2octets_raw(const uint8_t* in, size_t inlen, uint8_t out[32]) {
    if (inlen >= 32u) {
        memcpy(out, in, 32u);
    } else {
        memset(out, 0, 32u - inlen);
        memcpy(out + (32u - inlen), in, inlen);
    }
}

// e = bits2int(hash) reduced mod n, as a scalar (limbs) and optionally as 32-byte big-endian.
static void p256_hash_to_scalar(const TekoMont* nm, const uint8_t* hash, size_t hlen,
                                uint32_t e[P256_LIMBS], uint8_t* e_be32) {
    uint8_t oct[32];
    p256_bits2octets_raw(hash, hlen, oct);
    teko_bn_load_be(e, P256_LIMBS, oct, 32u);
    teko_mont_reduce_once(nm, e);               // bits2int < 2^256 < 2n -> one subtraction
    if (e_be32) teko_bn_store_be(e_be32, 32u, e, P256_LIMBS);
}

// out = a*b mod n (a,b ordinary residues < n).
static void p256_mulmod_n(const TekoMont* nm, uint32_t* out,
                          const uint32_t* a, const uint32_t* b) {
    uint32_t am[P256_LIMBS], bm[P256_LIMBS], pm[P256_LIMBS];
    teko_mont_to(nm, am, a);
    teko_mont_to(nm, bm, b);
    teko_mont_mul(nm, pm, am, bm);
    teko_mont_from(nm, out, pm);
}

// out = a^{-1} mod n (a ordinary residue, 0 < a < n).
static void p256_invmod_n(const TekoMont* nm, uint32_t* out, const uint32_t* a) {
    uint32_t am[P256_LIMBS], im[P256_LIMBS];
    teko_mont_to(nm, am, a);
    teko_ec_fp_inv(nm, im, am);                 // a^(n-2) in Montgomery form
    teko_mont_from(nm, out, im);
}

// RFC 6979 deterministic nonce k (32-byte big-endian) with HMAC-SHA-256, for order n.
// hlen here equals the HMAC output length (32). Returns k in [1, n-1].
static void p256_rfc6979_k(const TekoMont* nm, const uint8_t priv[32],
                           const uint8_t* hash, size_t hlen, uint8_t out_k[32]) {
    uint8_t V[32], K[32], h1oct[32];
    uint8_t buf[32 + 1 + 32 + 32];
    uint32_t e[P256_LIMBS], k[P256_LIMBS];

    p256_hash_to_scalar(nm, hash, hlen, e, h1oct); // bits2octets(h1) = (h1 mod n) as 32 bytes

    memset(V, 0x01, 32u);
    memset(K, 0x00, 32u);

    // K = HMAC_K(V || 0x00 || int2octets(x) || bits2octets(h1)); V = HMAC_K(V)
    memcpy(buf, V, 32u); buf[32] = 0x00; memcpy(buf + 33, priv, 32u); memcpy(buf + 65, h1oct, 32u);
    teko_hmac_sha256(K, 32u, buf, sizeof buf, K);
    teko_hmac_sha256(K, 32u, V, 32u, V);
    // K = HMAC_K(V || 0x01 || ...); V = HMAC_K(V)
    memcpy(buf, V, 32u); buf[32] = 0x01; memcpy(buf + 33, priv, 32u); memcpy(buf + 65, h1oct, 32u);
    teko_hmac_sha256(K, 32u, buf, sizeof buf, K);
    teko_hmac_sha256(K, 32u, V, 32u, V);

    for (;;) {
        // T = HMAC_K(V) (one block: hlen == qlen/8). k = bits2int(T) = T.
        teko_hmac_sha256(K, 32u, V, 32u, V);
        teko_bn_load_be(k, P256_LIMBS, V, 32u);
        if (!teko_bn_is_zero(k, P256_LIMBS) && p256_lt_mod(k, nm->m, P256_LIMBS)) {
            memcpy(out_k, V, 32u);
            (void)e;
            return;
        }
        // K = HMAC_K(V || 0x00); V = HMAC_K(V)
        memcpy(buf, V, 32u); buf[32] = 0x00;
        teko_hmac_sha256(K, 32u, buf, 33u, K);
        teko_hmac_sha256(K, 32u, V, 32u, V);
    }
}

int teko_p256_ecdsa_sign(const uint8_t priv[32], const uint8_t* hash, size_t hash_len,
                         uint8_t sig[64]) {
    P256 c;
    TekoMont nm;
    uint32_t d[P256_LIMBS], e[P256_LIMBS], k[P256_LIMBS];
    uint32_t r[P256_LIMBS], s[P256_LIMBS], kinv[P256_LIMBS], rd[P256_LIMBS], erd[P256_LIMBS];
    uint32_t Rx[P256_LIMBS], Ry[P256_LIMBS], Rz[P256_LIMBS], one[P256_LIMBS];
    uint8_t k_be[32], rx_be[32];

    if (!priv || !hash || !sig) return -1;
    if (p256_setup(&c) != 0) return -1;
    if (teko_mont_init(&nm, P256_N, 32u) != 0) return -1;

    teko_bn_load_be(d, P256_LIMBS, priv, 32u);
    if (teko_bn_is_zero(d, P256_LIMBS) || !p256_lt_mod(d, nm.m, P256_LIMBS)) return -1; // 0<d<n

    p256_hash_to_scalar(&nm, hash, hash_len, e, NULL);

    p256_rfc6979_k(&nm, priv, hash, hash_len, k_be);
    teko_bn_load_be(k, P256_LIMBS, k_be, 32u);

    // R = k*G ; r = R.x mod n
    memcpy(one, c.fp.one, sizeof one);
    teko_ec_scalar_mult(&c.fp, c.b, Rx, Ry, Rz, k_be, 32u, c.gx, c.gy, one);
    if (p256_to_affine(&c, Rx, Ry, Rz, rx_be, NULL) != 0) return -1;
    teko_bn_load_be(r, P256_LIMBS, rx_be, 32u);
    teko_mont_reduce_once(&nm, r);               // x < p < 2n -> one subtraction
    if (teko_bn_is_zero(r, P256_LIMBS)) return -1;

    // s = k^-1 (e + r*d) mod n
    p256_invmod_n(&nm, kinv, k);
    p256_mulmod_n(&nm, rd, r, d);
    teko_mont_add(&nm, erd, e, rd);              // e, rd < n -> (e+rd) mod n
    p256_mulmod_n(&nm, s, kinv, erd);
    if (teko_bn_is_zero(s, P256_LIMBS)) return -1;

    teko_bn_store_be(sig, 32u, r, P256_LIMBS);
    teko_bn_store_be(sig + 32, 32u, s, P256_LIMBS);
    return 0;
}

int teko_p256_ecdsa_verify(const uint8_t pub[64], const uint8_t* hash, size_t hash_len,
                           const uint8_t sig[64]) {
    P256 c;
    TekoMont nm;
    uint32_t r[P256_LIMBS], s[P256_LIMBS], e[P256_LIMBS], w[P256_LIMBS];
    uint32_t u1[P256_LIMBS], u2[P256_LIMBS];
    uint32_t qx[P256_LIMBS], qy[P256_LIMBS], Qxm[P256_LIMBS], Qym[P256_LIMBS], one[P256_LIMBS];
    uint32_t G1x[P256_LIMBS], G1y[P256_LIMBS], G1z[P256_LIMBS];
    uint32_t G2x[P256_LIMBS], G2y[P256_LIMBS], G2z[P256_LIMBS];
    uint32_t Rx[P256_LIMBS], Ry[P256_LIMBS], Rz[P256_LIMBS], v[P256_LIMBS];
    uint8_t u1_be[32], u2_be[32], rx_be[32];

    if (!pub || !hash || !sig) return -1;
    if (p256_setup(&c) != 0) return -1;
    if (teko_mont_init(&nm, P256_N, 32u) != 0) return -1;

    teko_bn_load_be(r, P256_LIMBS, sig, 32u);
    teko_bn_load_be(s, P256_LIMBS, sig + 32, 32u);
    if (teko_bn_is_zero(r, P256_LIMBS) || !p256_lt_mod(r, nm.m, P256_LIMBS)) return -1;
    if (teko_bn_is_zero(s, P256_LIMBS) || !p256_lt_mod(s, nm.m, P256_LIMBS)) return -1;

    // Load and validate the public key.
    teko_bn_load_be(qx, P256_LIMBS, pub, 32u);
    teko_bn_load_be(qy, P256_LIMBS, pub + 32, 32u);
    if (!p256_lt_mod(qx, c.fp.m, P256_LIMBS) || !p256_lt_mod(qy, c.fp.m, P256_LIMBS)) return -1;
    teko_mont_to(&c.fp, Qxm, qx);
    teko_mont_to(&c.fp, Qym, qy);
    if (!p256_on_curve(&c, Qxm, Qym)) return -1;

    p256_hash_to_scalar(&nm, hash, hash_len, e, NULL);
    p256_invmod_n(&nm, w, s);                    // w = s^-1 mod n
    p256_mulmod_n(&nm, u1, e, w);
    p256_mulmod_n(&nm, u2, r, w);
    teko_bn_store_be(u1_be, 32u, u1, P256_LIMBS);
    teko_bn_store_be(u2_be, 32u, u2, P256_LIMBS);

    // R = u1*G + u2*Q
    memcpy(one, c.fp.one, sizeof one);
    teko_ec_scalar_mult(&c.fp, c.b, G1x, G1y, G1z, u1_be, 32u, c.gx, c.gy, one);
    teko_ec_scalar_mult(&c.fp, c.b, G2x, G2y, G2z, u2_be, 32u, Qxm, Qym, one);
    teko_ec_add(&c.fp, c.b, Rx, Ry, Rz, G1x, G1y, G1z, G2x, G2y, G2z);

    if (p256_to_affine(&c, Rx, Ry, Rz, rx_be, NULL) != 0) return -1; // R == infinity -> invalid
    teko_bn_load_be(v, P256_LIMBS, rx_be, 32u);
    teko_mont_reduce_once(&nm, v);               // v = R.x mod n
    return teko_bn_eq(v, r, P256_LIMBS) ? 0 : -1;
}
