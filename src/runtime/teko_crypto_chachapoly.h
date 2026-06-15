#ifndef TEKO_CRYPTO_CHACHAPOLY_H
#define TEKO_CRYPTO_CHACHAPOLY_H

// Native ChaCha20-Poly1305 AEAD (RFC 8439 §2.8). Pure, portable C23 — no external
// libraries, no hardware dependency. KAT-tested against RFC 8439 §2.8.2 in
// tests/runtime/test_crypto_chachapoly.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_CHACHAPOLY_KEY_LEN   32u
#define TEKO_CHACHAPOLY_NONCE_LEN 12u
#define TEKO_CHACHAPOLY_TAG_LEN   16u

// Seal: encrypt plaintext -> ciphertext (same length) and produce the 16-byte tag over
// (aad, ciphertext). ct may alias pt.
void teko_chacha20poly1305_encrypt(const uint8_t key[TEKO_CHACHAPOLY_KEY_LEN],
                                   const uint8_t nonce[TEKO_CHACHAPOLY_NONCE_LEN],
                                   const uint8_t* aad, size_t aad_len,
                                   const uint8_t* pt, size_t pt_len,
                                   uint8_t* ct, uint8_t tag[TEKO_CHACHAPOLY_TAG_LEN]);

// Open: verify the tag (constant-time) and, only on success, decrypt ct -> pt.
// Returns 0 on success, -1 on authentication failure (pt is then zeroed).
int teko_chacha20poly1305_decrypt(const uint8_t key[TEKO_CHACHAPOLY_KEY_LEN],
                                  const uint8_t nonce[TEKO_CHACHAPOLY_NONCE_LEN],
                                  const uint8_t* aad, size_t aad_len,
                                  const uint8_t* ct, size_t ct_len,
                                  const uint8_t tag[TEKO_CHACHAPOLY_TAG_LEN],
                                  uint8_t* pt);

#endif // TEKO_CRYPTO_CHACHAPOLY_H
