// Native ChaCha20-Poly1305 AEAD (RFC 8439 §2.8). Portable C23, no external libraries.

#include "teko_crypto_chachapoly.h"
#include "teko_crypto_chacha20.h"
#include "teko_crypto_poly1305.h"

#include <string.h>

static const uint8_t TEKO_AEAD_ZEROS[16] = {0};

// Feed Poly1305 the zero padding that rounds `len` up to a 16-byte boundary.
static void teko_aead_pad16(TekoPoly1305Ctx* mac, size_t len) {
    size_t rem = len % 16u;
    if (rem != 0u) {
        teko_poly1305_update(mac, TEKO_AEAD_ZEROS, 16u - rem);
    }
}

static void teko_aead_le64(uint8_t out[8], uint64_t v) {
    unsigned i;
    for (i = 0u; i < 8u; ++i) {
        out[i] = (uint8_t)(v >> (8u * i));
    }
}

// Derive the one-time Poly1305 key and authenticate (aad, ct) per RFC 8439 §2.8.
static void teko_aead_tag(const uint8_t key[32], const uint8_t nonce[12],
                          const uint8_t* aad, size_t aad_len,
                          const uint8_t* ct, size_t ct_len,
                          uint8_t tag[16]) {
    uint8_t poly_block[TEKO_CHACHA20_BLOCK_LEN];
    uint8_t lengths[16];
    TekoPoly1305Ctx mac;

    // One-time key = first 32 bytes of the ChaCha20 keystream block at counter 0.
    teko_chacha20_block(key, 0u, nonce, poly_block);
    teko_poly1305_init(&mac, poly_block);

    teko_poly1305_update(&mac, aad, aad_len);
    teko_aead_pad16(&mac, aad_len);
    teko_poly1305_update(&mac, ct, ct_len);
    teko_aead_pad16(&mac, ct_len);

    teko_aead_le64(lengths + 0, (uint64_t)aad_len);
    teko_aead_le64(lengths + 8, (uint64_t)ct_len);
    teko_poly1305_update(&mac, lengths, 16u);

    teko_poly1305_finish(&mac, tag);

    memset(poly_block, 0, sizeof(poly_block)); // wipe key material
}

void teko_chacha20poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                                   const uint8_t* aad, size_t aad_len,
                                   const uint8_t* pt, size_t pt_len,
                                   uint8_t* ct, uint8_t tag[16]) {
    // Ciphertext uses the ChaCha20 keystream starting at block counter 1.
    teko_chacha20_xor(key, 1u, nonce, pt, ct, pt_len);
    teko_aead_tag(key, nonce, aad, aad_len, ct, pt_len, tag);
}

// Constant-time 16-byte tag comparison: returns 0 iff equal.
static int teko_ct_eq16(const uint8_t* a, const uint8_t* b) {
    uint8_t diff = 0u;
    unsigned i;
    for (i = 0u; i < 16u; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    // diff==0 -> 0 ; diff!=0 -> -1, without a data-dependent branch on the bytes.
    return (int)((0u - (uint32_t)diff) >> 31) * -1;
}

int teko_chacha20poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                                  const uint8_t* aad, size_t aad_len,
                                  const uint8_t* ct, size_t ct_len,
                                  const uint8_t tag[16], uint8_t* pt) {
    uint8_t expected[16];

    teko_aead_tag(key, nonce, aad, aad_len, ct, ct_len, expected);

    if (teko_ct_eq16(expected, tag) != 0) {
        if (pt && ct_len > 0u) memset(pt, 0, ct_len); // do not release unauthenticated data
        return -1;
    }

    teko_chacha20_xor(key, 1u, nonce, ct, pt, ct_len);
    return 0;
}
