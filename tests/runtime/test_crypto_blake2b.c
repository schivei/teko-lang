#include "unity.h"
#include "../../src/runtime/teko_crypto_blake2b.h"

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

// RFC 7693 + standard BLAKE2 known-answer vectors.
void test_teko_crypto_blake2b_known_answer_vectors(void) {
    uint8_t out[64];
    char hex[129];

    // BLAKE2b-512("") .
    TEST_ASSERT_EQUAL_INT(0, teko_blake2b((const uint8_t*)"", 0u, out, 64u));
    to_hex(out, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419"
        "d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2ce", hex);

    // BLAKE2b-512("abc") — RFC 7693 Appendix A.
    TEST_ASSERT_EQUAL_INT(0, teko_blake2b((const uint8_t*)"abc", 3u, out, 64u));
    to_hex(out, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
        "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923", hex);

    // Keyed BLAKE2b: key = 00 01 .. 3f (64 bytes), empty message (standard test vector).
    {
        uint8_t key[64];
        TekoBlake2bCtx ctx;
        unsigned i;
        for (i = 0u; i < 64u; ++i) key[i] = (uint8_t)i;
        TEST_ASSERT_EQUAL_INT(0, teko_blake2b_init(&ctx, 64u, key, 64u));
        teko_blake2b_update(&ctx, (const uint8_t*)"", 0u);
        teko_blake2b_final(&ctx, out);
        to_hex(out, 64u, hex);
        TEST_ASSERT_EQUAL_STRING(
            "10ebb67700b1868efb4417987acf4690ae9d972fb7a590c2f02871799aaa4786"
            "b5e996e8f0f4eb981fc214b005f42d2ff4233499391653df7aefcbc13fc51568", hex);
    }

    // Variable output length (BLAKE2b-256("abc")) — the out_len is bound into the IV.
    TEST_ASSERT_EQUAL_INT(0, teko_blake2b((const uint8_t*)"abc", 3u, out, 32u));
    to_hex(out, 32u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319", hex);

    // Bad output length is rejected.
    TEST_ASSERT_EQUAL_INT(-1, teko_blake2b((const uint8_t*)"x", 1u, out, 0u));
    TEST_ASSERT_EQUAL_INT(-1, teko_blake2b((const uint8_t*)"x", 1u, out, 65u));
}

// Streaming in odd chunks must equal the one-shot digest (spans the 128-byte block).
void test_teko_crypto_blake2b_streaming_matches_oneshot(void) {
    uint8_t msg[300];
    uint8_t one_shot[64];
    size_t i, chunk;

    for (i = 0u; i < sizeof(msg); ++i) msg[i] = (uint8_t)(i * 3u + 1u);
    TEST_ASSERT_EQUAL_INT(0, teko_blake2b(msg, sizeof(msg), one_shot, 64u));

    for (chunk = 1u; chunk <= 130u; chunk += 43u) {
        TekoBlake2bCtx ctx;
        uint8_t streamed[64];
        size_t off;
        TEST_ASSERT_EQUAL_INT(0, teko_blake2b_init(&ctx, 64u, NULL, 0u));
        for (off = 0u; off < sizeof(msg); off += chunk) {
            size_t take = (sizeof(msg) - off < chunk) ? (sizeof(msg) - off) : chunk;
            teko_blake2b_update(&ctx, msg + off, take);
        }
        teko_blake2b_final(&ctx, streamed);
        TEST_ASSERT_EQUAL_MEMORY(one_shot, streamed, 64u);
    }
}
