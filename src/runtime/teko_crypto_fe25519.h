#ifndef TEKO_CRYPTO_FE25519_H
#define TEKO_CRYPTO_FE25519_H

// Shared GF(2^255-19) field arithmetic for Curve25519 (used by X25519 and Ed25519).
// Radix-2^16 representation (int64[16]) with schoolbook multiply — only 16x16->32-bit
// products in 64-bit accumulators, so it is MSVC-safe (no __int128) and portable.
// Constant-time. (Approach after the public-domain TweetNaCl field code.)

#include <stdint.h>

typedef int64_t teko_fe[16];

void teko_fe_copy(teko_fe o, const teko_fe a);
void teko_fe_add(teko_fe o, const teko_fe a, const teko_fe b);
void teko_fe_sub(teko_fe o, const teko_fe a, const teko_fe b);
void teko_fe_mul(teko_fe o, const teko_fe a, const teko_fe b);
void teko_fe_sq(teko_fe o, const teko_fe a);
void teko_fe_inv(teko_fe o, const teko_fe a);       // a^(p-2)
void teko_fe_pow2523(teko_fe o, const teko_fe a);    // a^((p-5)/8)
void teko_fe_cswap(teko_fe p, teko_fe q, int b);     // constant-time conditional swap
void teko_fe_pack(uint8_t out[32], const teko_fe n); // reduce mod p, little-endian
void teko_fe_unpack(teko_fe o, const uint8_t in[32]);// little-endian, clears bit 255
int  teko_fe_parity(const teko_fe a);                // low bit of the reduced value
int  teko_fe_neq(const teko_fe a, const teko_fe b);  // 1 if a != b (after packing)

#endif // TEKO_CRYPTO_FE25519_H
