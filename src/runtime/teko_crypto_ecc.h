#ifndef TEKO_CRYPTO_ECC_H
#define TEKO_CRYPTO_ECC_H

// Curve-generic ECDH / ECDSA over the prime-order NIST curves (a = -3), shared by P-256 and
// P-384 (Phase 13.3b). The error-prone group law lives in teko_crypto_ec (RCB complete
// formulas); this layer adds key derivation, ECDH, and RFC 6979 deterministic ECDSA on top,
// parameterised by a curve descriptor so each curve is just constants + a hash choice. All
// scalars/coordinates are `bytes`-wide big-endian. The thin public wrappers live in
// teko_crypto_p256.* / teko_crypto_p384.*.

#include <stdint.h>
#include <stddef.h>

// RFC 6979 HMAC signature: out = HMAC_hash(key, msg). Matches teko_hmac_sha256/384.
typedef void (*teko_ecc_hmac_fn)(const uint8_t* key, size_t key_len,
                                 const uint8_t* msg, size_t msg_len, uint8_t* out);

typedef struct {
    int limbs;                 // 32-bit limbs in the field/scalar (8 for P-256, 12 for P-384)
    int bytes;                 // scalar/coordinate byte length (= 4*limbs)
    const uint8_t* p;          // field prime (big-endian, bytes long)
    const uint8_t* n;          // group order (big-endian, bytes long)
    const uint8_t* b;          // curve constant b (big-endian)
    const uint8_t* gx;         // base point x (big-endian)
    const uint8_t* gy;         // base point y (big-endian)
    teko_ecc_hmac_fn hmac;     // RFC 6979 HMAC (the curve's standard hash)
    int hmac_len;              // HMAC digest length (must equal `bytes` for these curves)
} TekoEccCurve;

// pub = priv*G as uncompressed X||Y (2*bytes). Returns 0, or -1 on bad key / infinity.
int teko_ecc_derive_public(const TekoEccCurve* cv, const uint8_t* priv, uint8_t* pub);

// out_x = x-coordinate of priv*peer (ECDH). peer is uncompressed X||Y, validated on-curve.
int teko_ecc_ecdh(const TekoEccCurve* cv, const uint8_t* priv,
                  const uint8_t* peer_pub, uint8_t* out_x);

// sig = r||s (each `bytes`) over digest `hash`, RFC 6979 deterministic nonce. Returns 0/-1.
int teko_ecc_ecdsa_sign(const TekoEccCurve* cv, const uint8_t* priv,
                        const uint8_t* hash, size_t hash_len, uint8_t* sig);

// 0 iff sig (r||s) verifies under pub (uncompressed X||Y) for digest `hash`; -1 otherwise.
int teko_ecc_ecdsa_verify(const TekoEccCurve* cv, const uint8_t* pub,
                          const uint8_t* hash, size_t hash_len, const uint8_t* sig);

#endif // TEKO_CRYPTO_ECC_H
