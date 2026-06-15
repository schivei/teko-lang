#include "unity.h"
#include "../../src/runtime/teko_crypto_sha1.h"

#include <string.h>
#include <stdlib.h>

static void assert_sha1(const uint8_t* data, size_t len, const char* expect_hex) {
    uint8_t digest[TEKO_SHA1_DIGEST_LEN];
    char got[TEKO_SHA1_DIGEST_LEN * 2u + 1u];
    static const char* HEX = "0123456789abcdef";
    unsigned i;
    teko_sha1(data, len, digest);
    for (i = 0u; i < TEKO_SHA1_DIGEST_LEN; ++i) {
        got[i * 2u + 0u] = HEX[(digest[i] >> 4) & 0xFu];
        got[i * 2u + 1u] = HEX[digest[i] & 0xFu];
    }
    got[TEKO_SHA1_DIGEST_LEN * 2u] = '\0';
    TEST_ASSERT_EQUAL_STRING(expect_hex, got);
}

// FIPS 180 known-answer vectors. (SHA-1 is legacy/insecure — interop only.)
void test_teko_crypto_sha1_fips180_vectors(void) {
    assert_sha1((const uint8_t*)"", 0u, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    assert_sha1((const uint8_t*)"abc", 3u, "a9993e364706816aba3e25717850c26c9cd0d89d");
    assert_sha1((const uint8_t*)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56u,
                "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    {
        uint8_t* big = (uint8_t*)malloc(1000000u);
        TEST_ASSERT_NOT_NULL(big);
        memset(big, 'a', 1000000u);
        assert_sha1(big, 1000000u, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
        free(big);
    }
}

void test_teko_crypto_sha1_streaming_matches_oneshot(void) {
    uint8_t msg[200];
    uint8_t one_shot[TEKO_SHA1_DIGEST_LEN];
    size_t i, chunk;
    for (i = 0u; i < sizeof(msg); ++i) msg[i] = (uint8_t)(i * 9u + 4u);
    teko_sha1(msg, sizeof(msg), one_shot);

    for (chunk = 1u; chunk <= 19u; ++chunk) {
        TekoSha1Ctx ctx;
        uint8_t streamed[TEKO_SHA1_DIGEST_LEN];
        size_t off;
        teko_sha1_init(&ctx);
        for (off = 0u; off < sizeof(msg); off += chunk) {
            size_t take = (sizeof(msg) - off < chunk) ? (sizeof(msg) - off) : chunk;
            teko_sha1_update(&ctx, msg + off, take);
        }
        teko_sha1_final(&ctx, streamed);
        TEST_ASSERT_EQUAL_MEMORY(one_shot, streamed, TEKO_SHA1_DIGEST_LEN);
    }
}
