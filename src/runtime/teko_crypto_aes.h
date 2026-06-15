#ifndef TEKO_CRYPTO_AES_H
#define TEKO_CRYPTO_AES_H

// Native AES-128/192/256 block cipher (FIPS-197), CONSTANT-TIME. Pure, portable C23 — no
// external libraries, no lookup tables, no secret-dependent branches/indexing. The S-box is
// computed arithmetically (GF(2^8) inverse via a fixed addition chain over a branchless
// carryless multiply, then the affine map), so it is immune to cache-timing side channels.
// AES-NI/hardware acceleration is a future optimization, not a correctness requirement.
// KAT-tested against FIPS-197 in tests/runtime/test_crypto_aes.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_AES_BLOCK_LEN 16u

// Expanded key schedule. Initialize with a 16/24/32-byte key.
typedef struct {
    uint8_t round_keys[240]; // 4*(Nr+1) words; AES-256 = 15 round keys = 240 bytes
    int     rounds;          // Nr: 10 / 12 / 14
} TekoAesKey;

// Returns 0 on success, -1 if key_len is not 16, 24, or 32.
int teko_aes_init(TekoAesKey* key, const uint8_t* key_bytes, size_t key_len);

// Single-block ECB primitive (used to build CTR/CBC/GCM). in/out may alias.
void teko_aes_encrypt_block(const TekoAesKey* key, const uint8_t in[16], uint8_t out[16]);
void teko_aes_decrypt_block(const TekoAesKey* key, const uint8_t in[16], uint8_t out[16]);

#endif // TEKO_CRYPTO_AES_H
