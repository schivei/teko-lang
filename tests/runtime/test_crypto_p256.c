#include "unity.h"
#include "../../src/runtime/teko_crypto_p256.h"
#include "../../src/runtime/teko_crypto_sha2.h"

#include <string.h>

// Parse a 2*len-char hex string into len bytes.
static void hexbytes(const char* hex, uint8_t* out, size_t len) {
    size_t i;
    for (i = 0; i < len; ++i) {
        char hi = hex[2 * i], lo = hex[2 * i + 1];
        int h = (hi <= '9') ? hi - '0' : (hi | 0x20) - 'a' + 10;
        int l = (lo <= '9') ? lo - '0' : (lo | 0x20) - 'a' + 10;
        out[i] = (uint8_t)((h << 4) | l);
    }
}

// NIST CAVP "KAS_ECC_CDH_PrimitiveTest" [P-256], COUNT 0 and COUNT 1: derive_public(dIUT)
// must equal (QIUTx,QIUTy), and ECDH(dIUT, QCAVS) must equal ZIUT.
static void p256_cdh_case(const char* d, const char* qx, const char* qy,
                          const char* qiutx, const char* qiuty, const char* z) {
    uint8_t priv[32], peer[64], expect_pub[64], expect_z[32];
    uint8_t pub[64], shared[32];

    hexbytes(d, priv, 32u);
    hexbytes(qx, peer, 32u);
    hexbytes(qy, peer + 32, 32u);
    hexbytes(qiutx, expect_pub, 32u);
    hexbytes(qiuty, expect_pub + 32, 32u);
    hexbytes(z, expect_z, 32u);

    TEST_ASSERT_EQUAL_INT(0, teko_p256_derive_public(priv, pub));
    TEST_ASSERT_EQUAL_MEMORY(expect_pub, pub, 64u);

    TEST_ASSERT_EQUAL_INT(0, teko_p256_ecdh(priv, peer, shared));
    TEST_ASSERT_EQUAL_MEMORY(expect_z, shared, 32u);
}

void test_teko_p256_ecdh_cavp_count0(void) {
    p256_cdh_case(
        "7d7dc5f71eb29ddaf80d6214632eeae03d9058af1fb6d22ed80badb62bc1a534",
        "700c48f77f56584c5cc632ca65640db91b6bacce3a4df6b42ce7cc838833d287",
        "db71e509e3fd9b060ddb20ba5c51dcc5948d46fbf640dfe0441782cab85fa4ac",
        "ead218590119e8876b29146ff89ca61770c4edbbf97d38ce385ed281d8a6b230",
        "28af61281fd35e2fa7002523acc85a429cb06ee6648325389f59edfce1405141",
        "46fc62106420ff012e54a434fbdd2d25ccc5852060561e68040dd7778997bd7b");
}

void test_teko_p256_ecdh_cavp_count1(void) {
    p256_cdh_case(
        "38f65d6dce47676044d58ce5139582d568f64bb16098d179dbab07741dd5caf5",
        "809f04289c64348c01515eb03d5ce7ac1a8cb9498f5caa50197e58d43a86a7ae",
        "b29d84e811197f25eba8f5194092cb6ff440e26d4421011372461f579271cda3",
        "119f2f047902782ab0c9e27a54aff5eb9b964829ca99c06b02ddba95b0a3f6d0",
        "8f52b726664cac366fc98ac7a012b2682cbd962e5acb544671d41b9445704d1d",
        "057d636096cb80b67a8c038c890e887d1adfa4195e9b3ce241c8a778c59cda67");
}

// RFC 6979 Appendix A.2.5 — P-256 / SHA-256 deterministic ECDSA. Private key x and the
// (r,s) for messages "sample" and "test". derive_public must match (Ux,Uy); deterministic
// sign must reproduce (r,s) exactly; verify must accept and reject tampering.
static const char* A25_X  = "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721";
static const char* A25_UX = "60fed4ba255a9d31c961eb74c6356d68c049b8923b61fa6ce669622e60f29fb6";
static const char* A25_UY = "7903fe1008b8bc99a41ae9e95628bc64f2f1b20c2d7e9f5177a3c294d4462299";

static void p256_ecdsa_det_case(const char* msg, const char* r_hex, const char* s_hex) {
    uint8_t priv[32], pub[64], expect_pub[64], expect_sig[64], sig[64], hash[32];

    hexbytes(A25_X, priv, 32u);
    hexbytes(A25_UX, expect_pub, 32u);
    hexbytes(A25_UY, expect_pub + 32, 32u);
    hexbytes(r_hex, expect_sig, 32u);
    hexbytes(s_hex, expect_sig + 32, 32u);

    TEST_ASSERT_EQUAL_INT(0, teko_p256_derive_public(priv, pub));
    TEST_ASSERT_EQUAL_MEMORY(expect_pub, pub, 64u);

    teko_sha256((const uint8_t*)msg, strlen(msg), hash);

    TEST_ASSERT_EQUAL_INT(0, teko_p256_ecdsa_sign(priv, hash, 32u, sig));
    TEST_ASSERT_EQUAL_MEMORY(expect_sig, sig, 64u); // RFC 6979 deterministic (r,s)

    TEST_ASSERT_EQUAL_INT(0, teko_p256_ecdsa_verify(pub, hash, 32u, sig));

    // Tamper: flipped hash byte and flipped signature byte must both be rejected.
    hash[0] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-1, teko_p256_ecdsa_verify(pub, hash, 32u, sig));
    hash[0] ^= 0x01;
    sig[10] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-1, teko_p256_ecdsa_verify(pub, hash, 32u, sig));
}

void test_teko_p256_ecdsa_rfc6979_sample(void) {
    p256_ecdsa_det_case("sample",
        "efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716",
        "f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8");
}

void test_teko_p256_ecdsa_rfc6979_test(void) {
    p256_ecdsa_det_case("test",
        "f1abb023518351cd71d881567b1ea663ed3efcf6c5132b354f28d3b0b7d38367",
        "019f4113742a2b14bd25926b49c649155f267e60d3814b4c0cc84250e46f0083");
}

// Negative: a peer point not on the curve must be rejected.
void test_teko_p256_ecdh_rejects_offcurve(void) {
    uint8_t priv[32], peer[64], shared[32];
    hexbytes("7d7dc5f71eb29ddaf80d6214632eeae03d9058af1fb6d22ed80badb62bc1a534", priv, 32u);
    hexbytes("700c48f77f56584c5cc632ca65640db91b6bacce3a4df6b42ce7cc838833d287", peer, 32u);
    hexbytes("db71e509e3fd9b060ddb20ba5c51dcc5948d46fbf640dfe0441782cab85fa4ac", peer + 32, 32u);
    peer[63] ^= 0x01; // corrupt y -> off curve
    TEST_ASSERT_EQUAL_INT(-1, teko_p256_ecdh(priv, peer, shared));
}
