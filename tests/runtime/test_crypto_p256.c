#include "unity.h"
#include "../../src/runtime/teko_crypto_p256.h"

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

// Negative: a peer point not on the curve must be rejected.
void test_teko_p256_ecdh_rejects_offcurve(void) {
    uint8_t priv[32], peer[64], shared[32];
    hexbytes("7d7dc5f71eb29ddaf80d6214632eeae03d9058af1fb6d22ed80badb62bc1a534", priv, 32u);
    hexbytes("700c48f77f56584c5cc632ca65640db91b6bacce3a4df6b42ce7cc838833d287", peer, 32u);
    hexbytes("db71e509e3fd9b060ddb20ba5c51dcc5948d46fbf640dfe0441782cab85fa4ac", peer + 32, 32u);
    peer[63] ^= 0x01; // corrupt y -> off curve
    TEST_ASSERT_EQUAL_INT(-1, teko_p256_ecdh(priv, peer, shared));
}
