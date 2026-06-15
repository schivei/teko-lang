#include "unity.h"
#include "../../src/runtime/teko_crypto_argon2.h"

#include <string.h>

static void to_hex(const uint8_t* d, size_t n, char* out) {
    static const char* HEX = "0123456789abcdef";
    size_t i;
    for (i = 0u; i < n; ++i) {
        out[i * 2u + 0u] = HEX[(d[i] >> 4) & 0xFu];
        out[i * 2u + 1u] = HEX[d[i] & 0xFu];
    }
    out[n * 2u] = '\0';
}

// RFC 9106 §5 known-answer vectors. All use version 0x13, t=3, m=32, p=4, tag=32, with
// P = 0x01 x 32, S = 0x02 x 16, K = 0x03 x 8, X = 0x04 x 12.
static void run_rfc9106(TekoArgon2Type type, const char* expect_hex) {
    uint8_t pwd[32], salt[16], key[8], ad[12];
    uint8_t tag[32];
    char hex[65];
    memset(pwd, 0x01, sizeof(pwd));
    memset(salt, 0x02, sizeof(salt));
    memset(key, 0x03, sizeof(key));
    memset(ad, 0x04, sizeof(ad));

    TEST_ASSERT_EQUAL_INT(0, teko_argon2(type, 3u, 32u, 4u,
                                         pwd, sizeof(pwd), salt, sizeof(salt),
                                         key, sizeof(key), ad, sizeof(ad), tag, sizeof(tag)));
    to_hex(tag, 32u, hex);
    TEST_ASSERT_EQUAL_STRING(expect_hex, hex);
}

void test_teko_crypto_argon2_rfc9106_vectors(void) {
    run_rfc9106(TEKO_ARGON2_D,
        "512b391b6f1162975371d30919734294f868e3be3984f3c1a13a4db9fabe4acb");
}

void test_teko_crypto_argon2i_rfc9106_vector(void) {
    run_rfc9106(TEKO_ARGON2_I,
        "c814d9d1dc7f37aa13f0d77f2494bda1c8de6b016dd388d29952a4c4672b6ce8");
}

void test_teko_crypto_argon2id_rfc9106_vector(void) {
    run_rfc9106(TEKO_ARGON2_ID,
        "0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659");
}
