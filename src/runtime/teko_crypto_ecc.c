// Curve-generic ECDH / ECDSA (see header). Generalises the per-curve glue: a Montgomery
// field context for p (point math) and one for n (scalar math), the RCB complete group law
// from teko_crypto_ec, and RFC 6979 deterministic nonces via the curve's HMAC. Limb counts
// run up to TEKO_EC_MAX_LIMBS (P-384). Built once; P-256 and P-384 differ only by descriptor.

#include "teko_crypto_ecc.h"
#include "teko_crypto_ec.h"
#include "teko_crypto_bn.h"

#include <string.h>

#define LB(n) (sizeof(uint32_t) * (size_t)(n))

// 1 iff limb array v (n limbs) is strictly less than modulus m.
static int ecc_lt(const uint32_t* v, const uint32_t* m, int n) {
    int i;
    for (i = n - 1; i >= 0; --i) {
        if (v[i] != m[i]) return (v[i] < m[i]) ? 1 : 0;
    }
    return 0;
}

// Per-call working state: the two Montgomery contexts and curve constants in Montgomery form.
typedef struct {
    const TekoEccCurve* cv;
    TekoMont fp;                       // field prime p
    TekoMont nm;                       // group order n
    uint32_t b[TEKO_EC_MAX_LIMBS];     // curve b, Montgomery (field)
    uint32_t gx[TEKO_EC_MAX_LIMBS];    // generator x, Montgomery (field)
    uint32_t gy[TEKO_EC_MAX_LIMBS];    // generator y, Montgomery (field)
    uint32_t one[TEKO_EC_MAX_LIMBS];   // Z = 1, Montgomery (field)
} EccCtx;

static int ecc_setup(EccCtx* c, const TekoEccCurve* cv) {
    uint32_t t[TEKO_EC_MAX_LIMBS];
    c->cv = cv;
    if (teko_mont_init(&c->fp, cv->p, (size_t)cv->bytes) != 0) return -1;
    if (teko_mont_init(&c->nm, cv->n, (size_t)cv->bytes) != 0) return -1;
    teko_bn_load_be(t, cv->limbs, cv->b, (size_t)cv->bytes);  teko_mont_to(&c->fp, c->b, t);
    teko_bn_load_be(t, cv->limbs, cv->gx, (size_t)cv->bytes); teko_mont_to(&c->fp, c->gx, t);
    teko_bn_load_be(t, cv->limbs, cv->gy, (size_t)cv->bytes); teko_mont_to(&c->fp, c->gy, t);
    memcpy(c->one, c->fp.one, LB(cv->limbs));
    return 0;
}

// y^2 == x^3 - 3x + b  (Montgomery-form affine coords).
static int ecc_on_curve(const EccCtx* c, const uint32_t* xm, const uint32_t* ym) {
    const TekoMont* fp = &c->fp;
    int n = c->cv->limbs;
    uint32_t y2[TEKO_EC_MAX_LIMBS], x2[TEKO_EC_MAX_LIMBS], x3[TEKO_EC_MAX_LIMBS];
    uint32_t tx[TEKO_EC_MAX_LIMBS], rhs[TEKO_EC_MAX_LIMBS];
    teko_mont_mul(fp, y2, ym, ym);
    teko_mont_mul(fp, x2, xm, xm);
    teko_mont_mul(fp, x3, x2, xm);
    teko_mont_add(fp, tx, xm, xm);
    teko_mont_add(fp, tx, tx, xm);     // 3x
    teko_mont_sub(fp, rhs, x3, tx);    // x^3 - 3x
    teko_mont_add(fp, rhs, rhs, c->b); // + b
    return teko_bn_eq(y2, rhs, n);
}

// Project (X:Y:Z) -> affine x (and y if requested), as `bytes` big-endian.
static int ecc_to_affine(const EccCtx* c, const uint32_t* X, const uint32_t* Y,
                         const uint32_t* Z, uint8_t* out_x, uint8_t* out_y) {
    const TekoMont* fp = &c->fp;
    int n = c->cv->limbs;
    uint32_t zinv[TEKO_EC_MAX_LIMBS], am[TEKO_EC_MAX_LIMBS], ao[TEKO_EC_MAX_LIMBS];
    if (teko_bn_is_zero(Z, n)) return -1;
    teko_ec_fp_inv(fp, zinv, Z);
    teko_mont_mul(fp, am, X, zinv);
    teko_mont_from(fp, ao, am);
    teko_bn_store_be(out_x, (size_t)c->cv->bytes, ao, n);
    if (out_y) {
        teko_mont_mul(fp, am, Y, zinv);
        teko_mont_from(fp, ao, am);
        teko_bn_store_be(out_y, (size_t)c->cv->bytes, ao, n);
    }
    return 0;
}

// out = a*b mod n (ordinary residues).
static void ecc_mulmod_n(const EccCtx* c, uint32_t* out, const uint32_t* a, const uint32_t* b) {
    uint32_t am[TEKO_EC_MAX_LIMBS], bm[TEKO_EC_MAX_LIMBS], pm[TEKO_EC_MAX_LIMBS];
    teko_mont_to(&c->nm, am, a);
    teko_mont_to(&c->nm, bm, b);
    teko_mont_mul(&c->nm, pm, am, bm);
    teko_mont_from(&c->nm, out, pm);
}

// out = a^{-1} mod n.
static void ecc_invmod_n(const EccCtx* c, uint32_t* out, const uint32_t* a) {
    uint32_t am[TEKO_EC_MAX_LIMBS], im[TEKO_EC_MAX_LIMBS];
    teko_mont_to(&c->nm, am, a);
    teko_ec_fp_inv(&c->nm, im, am);
    teko_mont_from(&c->nm, out, im);
}

// FIPS 186 bits2int as `bytes` octets (byte-aligned qlen): top `bytes` of in if longer,
// else left-zero-padded.
static void ecc_bits2octets_raw(const uint8_t* in, size_t inlen, uint8_t* out, int bytes) {
    if (inlen >= (size_t)bytes) {
        memcpy(out, in, (size_t)bytes);
    } else {
        memset(out, 0, (size_t)bytes - inlen);
        memcpy(out + ((size_t)bytes - inlen), in, inlen);
    }
}

// e = bits2int(hash) mod n, as limbs (and optionally as `bytes` big-endian = bits2octets).
static void ecc_hash_to_scalar(const EccCtx* c, const uint8_t* hash, size_t hlen,
                               uint32_t* e, uint8_t* e_be) {
    uint8_t oct[TEKO_EC_MAX_LIMBS * 4];
    int bytes = c->cv->bytes;
    ecc_bits2octets_raw(hash, hlen, oct, bytes);
    teko_bn_load_be(e, c->cv->limbs, oct, (size_t)bytes);
    teko_mont_reduce_once(&c->nm, e);  // bits2int < 2^(8*bytes) < 2n -> one subtraction
    if (e_be) teko_bn_store_be(e_be, (size_t)bytes, e, c->cv->limbs);
}

// RFC 6979 deterministic nonce k (`bytes` big-endian) in [1, n-1]. Requires hmac_len == bytes.
static void ecc_rfc6979_k(const EccCtx* c, const uint8_t* priv,
                          const uint8_t* hash, size_t hlen, uint8_t* out_k) {
    const TekoEccCurve* cv = c->cv;
    int bytes = cv->bytes;
    uint8_t V[TEKO_EC_MAX_LIMBS * 4], K[TEKO_EC_MAX_LIMBS * 4];
    uint8_t h1oct[TEKO_EC_MAX_LIMBS * 4];
    uint8_t buf[TEKO_EC_MAX_LIMBS * 4 + 1 + TEKO_EC_MAX_LIMBS * 4 + TEKO_EC_MAX_LIMBS * 4];
    uint32_t e[TEKO_EC_MAX_LIMBS], k[TEKO_EC_MAX_LIMBS];

    ecc_hash_to_scalar(c, hash, hlen, e, h1oct);  // h1oct = bits2octets(hash) = (h mod n)

    memset(V, 0x01, (size_t)bytes);
    memset(K, 0x00, (size_t)bytes);

    // K = HMAC_K(V || 0x00 || priv || h1oct); V = HMAC_K(V)
    memcpy(buf, V, (size_t)bytes); buf[bytes] = 0x00;
    memcpy(buf + bytes + 1, priv, (size_t)bytes);
    memcpy(buf + 2 * bytes + 1, h1oct, (size_t)bytes);
    cv->hmac(K, (size_t)bytes, buf, (size_t)(3 * bytes + 1), K);
    cv->hmac(K, (size_t)bytes, V, (size_t)bytes, V);
    // K = HMAC_K(V || 0x01 || priv || h1oct); V = HMAC_K(V)
    memcpy(buf, V, (size_t)bytes); buf[bytes] = 0x01;
    memcpy(buf + bytes + 1, priv, (size_t)bytes);
    memcpy(buf + 2 * bytes + 1, h1oct, (size_t)bytes);
    cv->hmac(K, (size_t)bytes, buf, (size_t)(3 * bytes + 1), K);
    cv->hmac(K, (size_t)bytes, V, (size_t)bytes, V);

    for (;;) {
        cv->hmac(K, (size_t)bytes, V, (size_t)bytes, V); // T = V (hmac_len == bytes)
        teko_bn_load_be(k, cv->limbs, V, (size_t)bytes);
        if (!teko_bn_is_zero(k, cv->limbs) && ecc_lt(k, c->nm.m, cv->limbs)) {
            memcpy(out_k, V, (size_t)bytes);
            (void)e;
            return;
        }
        memcpy(buf, V, (size_t)bytes); buf[bytes] = 0x00;
        cv->hmac(K, (size_t)bytes, buf, (size_t)(bytes + 1), K);
        cv->hmac(K, (size_t)bytes, V, (size_t)bytes, V);
    }
}

int teko_ecc_derive_public(const TekoEccCurve* cv, const uint8_t* priv, uint8_t* pub) {
    EccCtx c;
    uint32_t Rx[TEKO_EC_MAX_LIMBS], Ry[TEKO_EC_MAX_LIMBS], Rz[TEKO_EC_MAX_LIMBS];
    if (!cv || !priv || !pub) return -1;
    if (ecc_setup(&c, cv) != 0) return -1;
    teko_ec_scalar_mult(&c.fp, c.b, Rx, Ry, Rz, priv, (size_t)cv->bytes, c.gx, c.gy, c.one);
    return ecc_to_affine(&c, Rx, Ry, Rz, pub, pub + cv->bytes);
}

int teko_ecc_ecdh(const TekoEccCurve* cv, const uint8_t* priv,
                  const uint8_t* peer_pub, uint8_t* out_x) {
    EccCtx c;
    uint32_t px[TEKO_EC_MAX_LIMBS], py[TEKO_EC_MAX_LIMBS];
    uint32_t Pxm[TEKO_EC_MAX_LIMBS], Pym[TEKO_EC_MAX_LIMBS];
    uint32_t Rx[TEKO_EC_MAX_LIMBS], Ry[TEKO_EC_MAX_LIMBS], Rz[TEKO_EC_MAX_LIMBS];
    if (!cv || !priv || !peer_pub || !out_x) return -1;
    if (ecc_setup(&c, cv) != 0) return -1;

    teko_bn_load_be(px, cv->limbs, peer_pub, (size_t)cv->bytes);
    teko_bn_load_be(py, cv->limbs, peer_pub + cv->bytes, (size_t)cv->bytes);
    if (!ecc_lt(px, c.fp.m, cv->limbs) || !ecc_lt(py, c.fp.m, cv->limbs)) return -1;
    teko_mont_to(&c.fp, Pxm, px);
    teko_mont_to(&c.fp, Pym, py);
    if (!ecc_on_curve(&c, Pxm, Pym)) return -1;

    teko_ec_scalar_mult(&c.fp, c.b, Rx, Ry, Rz, priv, (size_t)cv->bytes, Pxm, Pym, c.one);
    return ecc_to_affine(&c, Rx, Ry, Rz, out_x, NULL);
}

int teko_ecc_ecdsa_sign(const TekoEccCurve* cv, const uint8_t* priv,
                        const uint8_t* hash, size_t hash_len, uint8_t* sig) {
    EccCtx c;
    int n;
    uint32_t d[TEKO_EC_MAX_LIMBS], e[TEKO_EC_MAX_LIMBS], k[TEKO_EC_MAX_LIMBS];
    uint32_t r[TEKO_EC_MAX_LIMBS], s[TEKO_EC_MAX_LIMBS], kinv[TEKO_EC_MAX_LIMBS];
    uint32_t rd[TEKO_EC_MAX_LIMBS], erd[TEKO_EC_MAX_LIMBS];
    uint32_t Rx[TEKO_EC_MAX_LIMBS], Ry[TEKO_EC_MAX_LIMBS], Rz[TEKO_EC_MAX_LIMBS];
    uint8_t k_be[TEKO_EC_MAX_LIMBS * 4], rx_be[TEKO_EC_MAX_LIMBS * 4];

    if (!cv || !priv || !hash || !sig) return -1;
    if (ecc_setup(&c, cv) != 0) return -1;
    n = cv->limbs;

    teko_bn_load_be(d, n, priv, (size_t)cv->bytes);
    if (teko_bn_is_zero(d, n) || !ecc_lt(d, c.nm.m, n)) return -1; // 0 < d < n

    ecc_hash_to_scalar(&c, hash, hash_len, e, NULL);
    ecc_rfc6979_k(&c, priv, hash, hash_len, k_be);
    teko_bn_load_be(k, n, k_be, (size_t)cv->bytes);

    teko_ec_scalar_mult(&c.fp, c.b, Rx, Ry, Rz, k_be, (size_t)cv->bytes, c.gx, c.gy, c.one);
    if (ecc_to_affine(&c, Rx, Ry, Rz, rx_be, NULL) != 0) return -1;
    teko_bn_load_be(r, n, rx_be, (size_t)cv->bytes);
    teko_mont_reduce_once(&c.nm, r);             // x < p < 2n
    if (teko_bn_is_zero(r, n)) return -1;

    ecc_invmod_n(&c, kinv, k);
    ecc_mulmod_n(&c, rd, r, d);
    teko_mont_add(&c.nm, erd, e, rd);
    ecc_mulmod_n(&c, s, kinv, erd);
    if (teko_bn_is_zero(s, n)) return -1;

    teko_bn_store_be(sig, (size_t)cv->bytes, r, n);
    teko_bn_store_be(sig + cv->bytes, (size_t)cv->bytes, s, n);
    return 0;
}

int teko_ecc_ecdsa_verify(const TekoEccCurve* cv, const uint8_t* pub,
                          const uint8_t* hash, size_t hash_len, const uint8_t* sig) {
    EccCtx c;
    int n;
    uint32_t r[TEKO_EC_MAX_LIMBS], s[TEKO_EC_MAX_LIMBS], e[TEKO_EC_MAX_LIMBS], w[TEKO_EC_MAX_LIMBS];
    uint32_t u1[TEKO_EC_MAX_LIMBS], u2[TEKO_EC_MAX_LIMBS];
    uint32_t qx[TEKO_EC_MAX_LIMBS], qy[TEKO_EC_MAX_LIMBS], Qxm[TEKO_EC_MAX_LIMBS], Qym[TEKO_EC_MAX_LIMBS];
    uint32_t G1x[TEKO_EC_MAX_LIMBS], G1y[TEKO_EC_MAX_LIMBS], G1z[TEKO_EC_MAX_LIMBS];
    uint32_t G2x[TEKO_EC_MAX_LIMBS], G2y[TEKO_EC_MAX_LIMBS], G2z[TEKO_EC_MAX_LIMBS];
    uint32_t Rx[TEKO_EC_MAX_LIMBS], Ry[TEKO_EC_MAX_LIMBS], Rz[TEKO_EC_MAX_LIMBS], v[TEKO_EC_MAX_LIMBS];
    uint8_t u1_be[TEKO_EC_MAX_LIMBS * 4], u2_be[TEKO_EC_MAX_LIMBS * 4], rx_be[TEKO_EC_MAX_LIMBS * 4];

    if (!cv || !pub || !hash || !sig) return -1;
    if (ecc_setup(&c, cv) != 0) return -1;
    n = cv->limbs;

    teko_bn_load_be(r, n, sig, (size_t)cv->bytes);
    teko_bn_load_be(s, n, sig + cv->bytes, (size_t)cv->bytes);
    if (teko_bn_is_zero(r, n) || !ecc_lt(r, c.nm.m, n)) return -1;
    if (teko_bn_is_zero(s, n) || !ecc_lt(s, c.nm.m, n)) return -1;

    teko_bn_load_be(qx, n, pub, (size_t)cv->bytes);
    teko_bn_load_be(qy, n, pub + cv->bytes, (size_t)cv->bytes);
    if (!ecc_lt(qx, c.fp.m, n) || !ecc_lt(qy, c.fp.m, n)) return -1;
    teko_mont_to(&c.fp, Qxm, qx);
    teko_mont_to(&c.fp, Qym, qy);
    if (!ecc_on_curve(&c, Qxm, Qym)) return -1;

    ecc_hash_to_scalar(&c, hash, hash_len, e, NULL);
    ecc_invmod_n(&c, w, s);
    ecc_mulmod_n(&c, u1, e, w);
    ecc_mulmod_n(&c, u2, r, w);
    teko_bn_store_be(u1_be, (size_t)cv->bytes, u1, n);
    teko_bn_store_be(u2_be, (size_t)cv->bytes, u2, n);

    teko_ec_scalar_mult(&c.fp, c.b, G1x, G1y, G1z, u1_be, (size_t)cv->bytes, c.gx, c.gy, c.one);
    teko_ec_scalar_mult(&c.fp, c.b, G2x, G2y, G2z, u2_be, (size_t)cv->bytes, Qxm, Qym, c.one);
    teko_ec_add(&c.fp, c.b, Rx, Ry, Rz, G1x, G1y, G1z, G2x, G2y, G2z);

    if (ecc_to_affine(&c, Rx, Ry, Rz, rx_be, NULL) != 0) return -1;
    teko_bn_load_be(v, n, rx_be, (size_t)cv->bytes);
    teko_mont_reduce_once(&c.nm, v);
    return teko_bn_eq(v, r, n) ? 0 : -1;
}
