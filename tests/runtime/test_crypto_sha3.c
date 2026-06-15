#include "unity.h"
#include "../../src/runtime/teko_crypto_sha3.h"

#include <string.h>
#include <stdlib.h>

static void to_hex(const uint8_t* d, size_t n, char* out) {
    static const char* HEX = "0123456789abcdef";
    size_t i;
    for (i = 0u; i < n; ++i) {
        out[i * 2u + 0u] = HEX[(d[i] >> 4) & 0xFu];
        out[i * 2u + 1u] = HEX[d[i] & 0xFu];
    }
    out[n * 2u] = '\0';
}

// FIPS 202 known-answer vectors for SHA3-256 and SHA3-512.
void test_teko_crypto_sha3_known_answer_vectors(void) {
    uint8_t d32[TEKO_SHA3_256_DIGEST_LEN];
    uint8_t d64[TEKO_SHA3_512_DIGEST_LEN];
    char hex[129];

    teko_sha3_256((const uint8_t*)"", 0u, d32);
    to_hex(d32, sizeof(d32), hex);
    TEST_ASSERT_EQUAL_STRING(
        "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a", hex);

    teko_sha3_256((const uint8_t*)"abc", 3u, d32);
    to_hex(d32, sizeof(d32), hex);
    TEST_ASSERT_EQUAL_STRING(
        "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532", hex);

    teko_sha3_512((const uint8_t*)"", 0u, d64);
    to_hex(d64, sizeof(d64), hex);
    TEST_ASSERT_EQUAL_STRING(
        "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6"
        "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26", hex);

    teko_sha3_512((const uint8_t*)"abc", 3u, d64);
    to_hex(d64, sizeof(d64), hex);
    TEST_ASSERT_EQUAL_STRING(
        "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e"
        "10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0", hex);

    // Multi-block absorb: 'a' x 200 spans >1 SHA3-256 rate block (136). Chunked absorb
    // in odd sizes must equal the one-shot digest (no fabricated constant).
    {
        uint8_t buf[200];
        uint8_t one_shot[TEKO_SHA3_256_DIGEST_LEN];
        size_t chunk;
        memset(buf, 'a', sizeof(buf));
        teko_sha3_256(buf, sizeof(buf), one_shot);
        for (chunk = 1u; chunk <= 70u; chunk += 13u) {
            TekoKeccakCtx ctx;
            uint8_t streamed[TEKO_SHA3_256_DIGEST_LEN];
            size_t off;
            teko_keccak_init(&ctx, 136u, 0x06u);
            for (off = 0u; off < sizeof(buf); off += chunk) {
                size_t take = (sizeof(buf) - off < chunk) ? (sizeof(buf) - off) : chunk;
                teko_keccak_absorb(&ctx, buf + off, take);
            }
            teko_keccak_squeeze(&ctx, streamed, TEKO_SHA3_256_DIGEST_LEN);
            TEST_ASSERT_EQUAL_MEMORY(one_shot, streamed, TEKO_SHA3_256_DIGEST_LEN);
        }
    }
}

// FIPS 202 SHAKE128/256 extendable-output vectors (empty message).
void test_teko_crypto_shake_known_answer_vectors(void) {
    uint8_t out[64];
    char hex[129];

    teko_shake128((const uint8_t*)"", 0u, out, 32u);
    to_hex(out, 32u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26", hex);

    teko_shake256((const uint8_t*)"", 0u, out, 64u);
    to_hex(out, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f"
        "d75dc4ddd8c0f200cb05019d67b592f6fc821c49479ab48640292eacb3b7c4be", hex);

    // Streaming the squeeze byte-by-byte must equal the one-shot XOF output.
    {
        TekoKeccakCtx ctx;
        uint8_t streamed[64];
        size_t i;
        teko_keccak_init(&ctx, 136u, 0x1fu); // SHAKE256
        teko_keccak_absorb(&ctx, (const uint8_t*)"", 0u);
        for (i = 0u; i < 64u; ++i) {
            teko_keccak_squeeze(&ctx, &streamed[i], 1u);
        }
        TEST_ASSERT_EQUAL_MEMORY(out, streamed, 64u);
    }
}
