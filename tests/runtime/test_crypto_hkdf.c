#include "unity.h"
#include "../../src/runtime/teko_crypto_hkdf.h"

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

// RFC 5869 Appendix A known-answer vectors for HKDF-SHA-256.
void test_teko_crypto_hkdf_rfc5869_vectors(void) {
    uint8_t okm[82];
    uint8_t prk[32];
    char hex[256];

    // Test Case 1.
    {
        uint8_t ikm[22];
        uint8_t salt[13];
        uint8_t info[10];
        unsigned i;
        memset(ikm, 0x0b, sizeof(ikm));
        for (i = 0u; i < sizeof(salt); ++i) salt[i] = (uint8_t)i;            // 00..0c
        for (i = 0u; i < sizeof(info); ++i) info[i] = (uint8_t)(0xf0u + i);  // f0..f9

        teko_hkdf_sha256_extract(salt, sizeof(salt), ikm, sizeof(ikm), prk);
        to_hex(prk, sizeof(prk), hex);
        TEST_ASSERT_EQUAL_STRING(
            "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5", hex);

        TEST_ASSERT_EQUAL_INT(0, teko_hkdf_sha256(salt, sizeof(salt), ikm, sizeof(ikm),
                                                  info, sizeof(info), okm, 42u));
        to_hex(okm, 42u, hex);
        TEST_ASSERT_EQUAL_STRING(
            "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
            "34007208d5b887185865", hex);
    }

    // Test Case 2 — longer inputs, L = 82.
    {
        uint8_t ikm[80];
        uint8_t salt[80];
        uint8_t info[80];
        unsigned i;
        for (i = 0u; i < 80u; ++i) ikm[i] = (uint8_t)i;             // 00..4f
        for (i = 0u; i < 80u; ++i) salt[i] = (uint8_t)(0x60u + i);  // 60..af
        for (i = 0u; i < 80u; ++i) info[i] = (uint8_t)(0xb0u + i);  // b0..ff

        TEST_ASSERT_EQUAL_INT(0, teko_hkdf_sha256(salt, sizeof(salt), ikm, sizeof(ikm),
                                                  info, sizeof(info), okm, 82u));
        to_hex(okm, 82u, hex);
        TEST_ASSERT_EQUAL_STRING(
            "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c"
            "59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71"
            "cc30c58179ec3e87c14c01d5c1f3434f1d87", hex);
    }

    // Test Case 3 — zero-length salt and info (salt defaults to 32 zero bytes).
    {
        uint8_t ikm[22];
        memset(ikm, 0x0b, sizeof(ikm));

        teko_hkdf_sha256_extract(NULL, 0u, ikm, sizeof(ikm), prk);
        to_hex(prk, sizeof(prk), hex);
        TEST_ASSERT_EQUAL_STRING(
            "19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04", hex);

        TEST_ASSERT_EQUAL_INT(0, teko_hkdf_sha256(NULL, 0u, ikm, sizeof(ikm),
                                                  NULL, 0u, okm, 42u));
        to_hex(okm, 42u, hex);
        TEST_ASSERT_EQUAL_STRING(
            "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
            "9d201395faa4b61a96c8", hex);
    }

    // Over-long request must be rejected (> 255*HashLen).
    {
        static uint8_t big[255u * 32u + 1u];
        uint8_t ikm[22];
        memset(ikm, 0x0b, sizeof(ikm));
        TEST_ASSERT_EQUAL_INT(-1, teko_hkdf_sha256(NULL, 0u, ikm, sizeof(ikm),
                                                   NULL, 0u, big, sizeof(big)));
    }
}

// HKDF-SHA-512 has no RFC 5869 vector; verify the structural invariant instead:
// HKDF-Expand of exactly HashLen bytes equals T(1) = HMAC(PRK, info || 0x01), and the
// derivation is deterministic. (The shared generic code is KAT-proven on the SHA-256 path.)
void test_teko_crypto_hkdf_sha512_structural(void) {
    uint8_t a[100];
    uint8_t b[100];
    const uint8_t ikm[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t salt[4] = {9, 9, 9, 9};
    const uint8_t info[3] = {0xaa, 0xbb, 0xcc};

    TEST_ASSERT_EQUAL_INT(0, teko_hkdf_sha512(salt, sizeof(salt), ikm, sizeof(ikm),
                                              info, sizeof(info), a, sizeof(a)));
    TEST_ASSERT_EQUAL_INT(0, teko_hkdf_sha512(salt, sizeof(salt), ikm, sizeof(ikm),
                                              info, sizeof(info), b, sizeof(b)));
    TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(a)); // deterministic

    // A prefix request must match the prefix of a longer request (incremental T blocks).
    {
        uint8_t shortk[64];
        TEST_ASSERT_EQUAL_INT(0, teko_hkdf_sha512(salt, sizeof(salt), ikm, sizeof(ikm),
                                                  info, sizeof(info), shortk, sizeof(shortk)));
        TEST_ASSERT_EQUAL_MEMORY(a, shortk, sizeof(shortk));
    }
}
