#include "unity.h"
#include "../../src/runtime/teko_crypto_chacha20.h"

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

// RFC 8439 §2.4.2 — the worked ChaCha20 encryption example.
void test_teko_crypto_chacha20_rfc8439_encrypt(void) {
    uint8_t key[32];
    uint8_t nonce[12] = {0, 0, 0, 0, 0, 0, 0, 0x4a, 0, 0, 0, 0};
    const char* pt =
        "Ladies and Gentlemen of the class of '99: If I could offer you "
        "only one tip for the future, sunscreen would be it.";
    size_t len = strlen(pt); // 114
    uint8_t ct[114];
    uint8_t back[114];
    char hex[114 * 2 + 1];
    unsigned i;

    for (i = 0u; i < 32u; ++i) key[i] = (uint8_t)i;

    teko_chacha20_xor(key, 1u, nonce, (const uint8_t*)pt, ct, len);
    to_hex(ct, len, hex);
    TEST_ASSERT_EQUAL_STRING(
        "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b"
        "f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d8"
        "07ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab7793736"
        "5af90bbf74a35be6b40b8eedf2785e42874d", hex);

    // XOR is its own inverse: decrypt round-trips to the plaintext.
    teko_chacha20_xor(key, 1u, nonce, ct, back, len);
    TEST_ASSERT_EQUAL_MEMORY(pt, back, len);
}

// RFC 8439 §2.3.2 — the ChaCha20 block-function keystream (counter = 1).
void test_teko_crypto_chacha20_rfc8439_block(void) {
    uint8_t key[32];
    uint8_t nonce[12] = {0, 0, 0, 9, 0, 0, 0, 0x4a, 0, 0, 0, 0};
    uint8_t block[64];
    char hex[129];
    unsigned i;

    for (i = 0u; i < 32u; ++i) key[i] = (uint8_t)i;

    teko_chacha20_block(key, 1u, nonce, block);
    to_hex(block, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e"
        "d2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e", hex);
}
