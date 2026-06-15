#ifndef TEKO_CRYPTO_EC_H
#define TEKO_CRYPTO_EC_H

// Generic short-Weierstrass group law for the prime-order NIST curves with a = -3
// (Phase 13.3b), shared by P-256 and P-384. Uses the Renes-Costello-Batina *complete*
// (exception-free) formulas — Algorithm 4 (addition) and Algorithm 6 (doubling) of
// "Complete addition formulas for prime order elliptic curves" (eprint 2015/1060). Because
// the formulas are exception-free, the point at infinity, P == Q (doubling via add), and
// P == -Q are all handled without branches — which is what makes a constant-time ladder
// safe. All coordinate arrays are fp->n little-endian limbs in *Montgomery form*; b_mont is
// the curve constant b in Montgomery form. Projective (X:Y:Z); infinity is (0 : R : 0)
// (X=0, Y=fp->one, Z=0). Built on the teko_crypto_bn Montgomery field ops.

#include <stdint.h>
#include <stddef.h>
#include "teko_crypto_bn.h"

#define TEKO_EC_MAX_LIMBS 12  // P-384 (12 x 32 = 384 bits); P-256 uses 8

// (X3:Y3:Z3) = (X1:Y1:Z1) + (X2:Y2:Z2). Outputs may alias inputs.
void teko_ec_add(const TekoMont* fp, const uint32_t* b_mont,
                 uint32_t* X3, uint32_t* Y3, uint32_t* Z3,
                 const uint32_t* X1, const uint32_t* Y1, const uint32_t* Z1,
                 const uint32_t* X2, const uint32_t* Y2, const uint32_t* Z2);

// (X3:Y3:Z3) = 2*(X1:Y1:Z1). Outputs may alias inputs.
void teko_ec_double(const TekoMont* fp, const uint32_t* b_mont,
                    uint32_t* X3, uint32_t* Y3, uint32_t* Z3,
                    const uint32_t* X1, const uint32_t* Y1, const uint32_t* Z1);

// Constant-time scalar multiplication: (Xo:Yo:Zo) = scalar * (Px:Py:Pz). scalar is
// big-endian, slen bytes. The bit count (8*slen) and the operations per bit are fixed
// (one double + one add + one constant-time select), independent of the scalar value.
void teko_ec_scalar_mult(const TekoMont* fp, const uint32_t* b_mont,
                         uint32_t* Xo, uint32_t* Yo, uint32_t* Zo,
                         const uint8_t* scalar_be, size_t slen,
                         const uint32_t* Px, const uint32_t* Py, const uint32_t* Pz);

// out = a^{-1} mod p  (field inverse via Fermat's little theorem: a^(p-2) mod p). The
// exponent is the public modulus, so the square-and-multiply schedule is data-independent
// (constant-time w.r.t. the secret value a). Montgomery-form in and out.
void teko_ec_fp_inv(const TekoMont* fp, uint32_t* out, const uint32_t* a);

#endif // TEKO_CRYPTO_EC_H
