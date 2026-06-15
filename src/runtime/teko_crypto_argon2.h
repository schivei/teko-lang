#ifndef TEKO_CRYPTO_ARGON2_H
#define TEKO_CRYPTO_ARGON2_H

// Native Argon2 memory-hard KDF (RFC 9106): Argon2d / Argon2i / Argon2id, version 0x13.
// Pure, portable C23 — no external libraries. Built on Teko's BLAKE2b (the variable-length
// H' and H0). KAT-tested against RFC 9106 §5 in tests/runtime/test_crypto_argon2.c.

#include <stdint.h>
#include <stddef.h>

typedef enum {
    TEKO_ARGON2_D = 0,
    TEKO_ARGON2_I = 1,
    TEKO_ARGON2_ID = 2
} TekoArgon2Type;

// Derive a tag of tag_len bytes (tag_len >= 4). m_cost is in KiB (1 KiB blocks);
// t_cost = iterations (>= 1); parallelism = lanes (>= 1). key/ad are optional (may be NULL).
// Returns 0 on success, -1 on invalid parameters or allocation failure.
int teko_argon2(TekoArgon2Type type,
                uint32_t t_cost, uint32_t m_cost, uint32_t parallelism,
                const uint8_t* pwd, size_t pwd_len,
                const uint8_t* salt, size_t salt_len,
                const uint8_t* key, size_t key_len,
                const uint8_t* ad, size_t ad_len,
                uint8_t* tag, size_t tag_len);

#endif // TEKO_CRYPTO_ARGON2_H
