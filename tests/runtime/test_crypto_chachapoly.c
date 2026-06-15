#include "unity.h"
#include "../../src/runtime/teko_crypto_chachapoly.h"

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

// RFC 8439 §2.8.2 — the worked ChaCha20-Poly1305 AEAD encryption example.
void test_teko_crypto_chachapoly_rfc8439_aead(void) {
    uint8_t key[32];
    uint8_t nonce[12] = {0x07, 0, 0, 0, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};
    static const uint8_t aad[12] = {
        0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7
    };
    const char* pt =
        "Ladies and Gentlemen of the class of '99: If I could offer you "
        "only one tip for the future, sunscreen would be it.";
    size_t len = strlen(pt); // 114
    uint8_t ct[114];
    uint8_t tag[16];
    uint8_t back[114];
    char hex[114 * 2 + 1];
    char taghex[33];
    unsigned i;

    for (i = 0u; i < 32u; ++i) key[i] = (uint8_t)(0x80u + i);

    teko_chacha20poly1305_encrypt(key, nonce, aad, sizeof(aad),
                                  (const uint8_t*)pt, len, ct, tag);

    to_hex(ct, len, hex);
    TEST_ASSERT_EQUAL_STRING(
        "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
        "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
        "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
        "3ff4def08e4b7a9de576d26586cec64b6116", hex);

    to_hex(tag, 16u, taghex);
    TEST_ASSERT_EQUAL_STRING("1ae10b594f09e26a7e902ecbd0600691", taghex);

    // Decrypt round-trips and authenticates.
    TEST_ASSERT_EQUAL_INT(0, teko_chacha20poly1305_decrypt(key, nonce, aad, sizeof(aad),
                                                           ct, len, tag, back));
    TEST_ASSERT_EQUAL_MEMORY(pt, back, len);
}

// Authentication must reject any tampering (ciphertext, tag, or AAD).
void test_teko_crypto_chachapoly_rejects_tampering(void) {
    uint8_t key[32];
    uint8_t nonce[12] = {0x07, 0, 0, 0, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};
    uint8_t aad[4] = {1, 2, 3, 4};
    const uint8_t pt[40] = {
        9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
        32, 33, 34, 35, 36, 37, 38, 39
    };
    uint8_t ct[40];
    uint8_t tag[16];
    uint8_t out[40];
    unsigned i;

    for (i = 0u; i < 32u; ++i) key[i] = (uint8_t)i;
    teko_chacha20poly1305_encrypt(key, nonce, aad, sizeof(aad), pt, sizeof(pt), ct, tag);

    // Flip a ciphertext bit.
    {
        uint8_t bad[40];
        memcpy(bad, ct, sizeof(ct));
        bad[5] ^= 0x01u;
        TEST_ASSERT_EQUAL_INT(-1, teko_chacha20poly1305_decrypt(key, nonce, aad, sizeof(aad),
                                                               bad, sizeof(bad), tag, out));
    }
    // Flip a tag bit.
    {
        uint8_t badtag[16];
        memcpy(badtag, tag, sizeof(tag));
        badtag[0] ^= 0x80u;
        TEST_ASSERT_EQUAL_INT(-1, teko_chacha20poly1305_decrypt(key, nonce, aad, sizeof(aad),
                                                               ct, sizeof(ct), badtag, out));
    }
    // Flip an AAD byte.
    {
        uint8_t badaad[4];
        memcpy(badaad, aad, sizeof(aad));
        badaad[1] ^= 0xFFu;
        TEST_ASSERT_EQUAL_INT(-1, teko_chacha20poly1305_decrypt(key, nonce, badaad, sizeof(badaad),
                                                               ct, sizeof(ct), tag, out));
    }
    // Untampered still authenticates.
    TEST_ASSERT_EQUAL_INT(0, teko_chacha20poly1305_decrypt(key, nonce, aad, sizeof(aad),
                                                          ct, sizeof(ct), tag, out));
    TEST_ASSERT_EQUAL_MEMORY(pt, out, sizeof(pt));
}
