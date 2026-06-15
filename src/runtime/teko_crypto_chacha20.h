#ifndef TEKO_CRYPTO_CHACHA20_H
#define TEKO_CRYPTO_CHACHA20_H

// Native ChaCha20 stream cipher (RFC 8439 §2). Pure, portable C23 — no external libraries,
// no hardware dependency. Constant-time (no data-dependent branches/indexing). KAT-tested
// against RFC 8439 in tests/runtime/test_crypto_chacha20.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_CHACHA20_KEY_LEN   32u
#define TEKO_CHACHA20_NONCE_LEN 12u
#define TEKO_CHACHA20_BLOCK_LEN 64u

// Generate one 64-byte ChaCha20 keystream block for the given 32-bit block counter.
void teko_chacha20_block(const uint8_t key[TEKO_CHACHA20_KEY_LEN],
                         uint32_t counter,
                         const uint8_t nonce[TEKO_CHACHA20_NONCE_LEN],
                         uint8_t out[TEKO_CHACHA20_BLOCK_LEN]);

// Encrypt/decrypt (XOR with the keystream). `counter` is the initial 32-bit block counter
// (RFC 8439 uses 1 for the first plaintext block). in/out may alias.
void teko_chacha20_xor(const uint8_t key[TEKO_CHACHA20_KEY_LEN],
                       uint32_t counter,
                       const uint8_t nonce[TEKO_CHACHA20_NONCE_LEN],
                       const uint8_t* in, uint8_t* out, size_t len);

#endif // TEKO_CRYPTO_CHACHA20_H
