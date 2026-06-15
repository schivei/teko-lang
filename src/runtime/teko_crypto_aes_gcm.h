#ifndef TEKO_CRYPTO_AES_GCM_H
#define TEKO_CRYPTO_AES_GCM_H

// Native AES-GCM AEAD (NIST SP 800-38D) over the constant-time AES core. GHASH uses a
// bit-by-bit GF(2^128) multiply (no tables, no secret-dependent branches). Portable C23,
// no external libraries. KAT-tested against the McGrew–Viega/NIST GCM vectors in
// tests/runtime/test_crypto_aes_gcm.c.

#include <stdint.h>
#include <stddef.h>

#include "teko_crypto_aes.h"

// Seal: ct = GCTR(pt); tag (tag_len <= 16) over (aad, ct). Any iv_len (96-bit is the fast
// path; other lengths use the GHASH derivation). ct may alias pt.
void teko_aes_gcm_encrypt(const TekoAesKey* key,
                          const uint8_t* iv, size_t iv_len,
                          const uint8_t* aad, size_t aad_len,
                          const uint8_t* pt, size_t pt_len,
                          uint8_t* ct, uint8_t* tag, size_t tag_len);

// Open: verify the tag (constant-time) and, only on success, decrypt. Returns 0 on success,
// -1 on authentication failure (pt is then zeroed). tag_len must be 1..16.
int teko_aes_gcm_decrypt(const TekoAesKey* key,
                         const uint8_t* iv, size_t iv_len,
                         const uint8_t* aad, size_t aad_len,
                         const uint8_t* ct, size_t ct_len,
                         const uint8_t* tag, size_t tag_len,
                         uint8_t* pt);

#endif // TEKO_CRYPTO_AES_GCM_H
