#include "unity.h"
#include "../../src/runtime/teko_crypto_aes_gcm.h"

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

// McGrew-Viega / NIST GCM key, IV, and 64-byte plaintext shared by test cases 3 & 4.
static const uint8_t GCM_KEY[16] = {
    0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
    0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08
};
static const uint8_t GCM_IV[12] = {
    0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce, 0xdb, 0xad, 0xde, 0xca, 0xf8, 0x88
};
static const uint8_t GCM_PT[64] = {
    0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5, 0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26, 0x9a,
    0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda, 0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72,
    0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53, 0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
    0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57, 0xba, 0x63, 0x7b, 0x39, 0x1a, 0xaf, 0xd2, 0x55
};

// GCM Test Case 3: no AAD, full 64-byte plaintext.
void test_teko_crypto_aes_gcm_case3(void) {
    TekoAesKey k;
    uint8_t ct[64];
    uint8_t tag[16];
    uint8_t back[64];
    char hex[129];

    TEST_ASSERT_EQUAL_INT(0, teko_aes_init(&k, GCM_KEY, 16u));

    teko_aes_gcm_encrypt(&k, GCM_IV, 12u, NULL, 0u, GCM_PT, 64u, ct, tag, 16u);
    to_hex(ct, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
        "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985", hex);
    to_hex(tag, 16u, hex);
    TEST_ASSERT_EQUAL_STRING("4d5c2af327cd64a62cf35abd2ba6fab4", hex);

    TEST_ASSERT_EQUAL_INT(0, teko_aes_gcm_decrypt(&k, GCM_IV, 12u, NULL, 0u,
                                                  ct, 64u, tag, 16u, back));
    TEST_ASSERT_EQUAL_MEMORY(GCM_PT, back, 64u);
}

// GCM Test Case 4: 20-byte AAD, 60-byte plaintext (partial final block).
void test_teko_crypto_aes_gcm_case4(void) {
    static const uint8_t aad[20] = {
        0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed,
        0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef, 0xab, 0xad, 0xda, 0xd2
    };
    TekoAesKey k;
    uint8_t ct[60];
    uint8_t tag[16];
    uint8_t back[60];
    char hex[121];

    TEST_ASSERT_EQUAL_INT(0, teko_aes_init(&k, GCM_KEY, 16u));

    teko_aes_gcm_encrypt(&k, GCM_IV, 12u, aad, sizeof(aad), GCM_PT, 60u, ct, tag, 16u);
    to_hex(ct, 60u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
        "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091", hex);
    to_hex(tag, 16u, hex);
    TEST_ASSERT_EQUAL_STRING("5bc94fbc3221a5db94fae95ae7121a47", hex);

    TEST_ASSERT_EQUAL_INT(0, teko_aes_gcm_decrypt(&k, GCM_IV, 12u, aad, sizeof(aad),
                                                  ct, 60u, tag, 16u, back));
    TEST_ASSERT_EQUAL_MEMORY(GCM_PT, back, 60u);

    // Tampering the AAD or ciphertext must fail authentication.
    {
        uint8_t bad_aad[20];
        memcpy(bad_aad, aad, sizeof(aad));
        bad_aad[0] ^= 0x01u;
        TEST_ASSERT_EQUAL_INT(-1, teko_aes_gcm_decrypt(&k, GCM_IV, 12u, bad_aad, sizeof(bad_aad),
                                                       ct, 60u, tag, 16u, back));
    }
    {
        uint8_t bad_ct[60];
        memcpy(bad_ct, ct, sizeof(bad_ct));
        bad_ct[10] ^= 0x80u;
        TEST_ASSERT_EQUAL_INT(-1, teko_aes_gcm_decrypt(&k, GCM_IV, 12u, aad, sizeof(aad),
                                                       bad_ct, 60u, tag, 16u, back));
    }
}
