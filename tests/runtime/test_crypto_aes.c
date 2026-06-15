#include "unity.h"
#include "../../src/runtime/teko_crypto_aes.h"

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

static void check_block(const uint8_t* key, size_t key_len,
                        const uint8_t pt[16], const char* expect_ct_hex) {
    TekoAesKey k;
    uint8_t ct[16];
    uint8_t back[16];
    char hex[33];

    TEST_ASSERT_EQUAL_INT(0, teko_aes_init(&k, key, key_len));

    teko_aes_encrypt_block(&k, pt, ct);
    to_hex(ct, 16u, hex);
    TEST_ASSERT_EQUAL_STRING(expect_ct_hex, hex);

    // Decrypt round-trips back to the plaintext.
    teko_aes_decrypt_block(&k, ct, back);
    TEST_ASSERT_EQUAL_MEMORY(pt, back, 16u);
}

// FIPS-197 Appendix B/C single-block known-answer vectors for AES-128/192/256.
void test_teko_crypto_aes_fips197_blocks(void) {
    static const uint8_t pt[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    uint8_t key[32];
    unsigned i;
    for (i = 0u; i < 32u; ++i) key[i] = (uint8_t)i; // 000102...1f

    // AES-128, key = 000102...0f.
    check_block(key, 16u, pt, "69c4e0d86a7b0430d8cdb78070b4c55a");
    // AES-192, key = 000102...17.
    check_block(key, 24u, pt, "dda97ca4864cdfe06eaf70a0ec0d7191");
    // AES-256, key = 000102...1f.
    check_block(key, 32u, pt, "8ea2b7ca516745bfeafc49904b496089");

    // Bad key length is rejected.
    {
        TekoAesKey k;
        TEST_ASSERT_EQUAL_INT(-1, teko_aes_init(&k, key, 20u));
    }
}

// FIPS-197 Appendix A key-expansion sanity via the canonical AES-128 example: the
// classic key 2b7e151628aed2a6abf7158809cf4f3c encrypts the all-zero ... actually use the
// well-known AES-128 ECB vector (NIST): key=2b7e..., pt=6bc1bee2..., ct=3ad77bb40d7a3660.
void test_teko_crypto_aes128_nist_vector(void) {
    static const uint8_t key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t pt[16] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
    };
    check_block(key, 16u, pt, "3ad77bb40d7a3660a89ecaf32466ef97");
}
