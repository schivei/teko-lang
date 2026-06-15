// Generic a=-3 short-Weierstrass group law (see header). The add/double routines are a
// line-for-line transcription of Renes-Costello-Batina Algorithm 4 / Algorithm 6 (eprint
// 2015/1060), with each numbered step preserved so the code can be checked against the
// paper. Field arithmetic is Montgomery-form via teko_crypto_bn.

#include "teko_crypto_ec.h"

#include <string.h>

#define LIMBS_BYTES(n) (sizeof(uint32_t) * (size_t)(n))

// Shorthands so the formulas read like the paper. d/x/y are limb pointers; aliasing of the
// destination with a source is safe (the bn ops read sources fully before writing dest).
#define MUL(d, x, y) teko_mont_mul(fp, (d), (x), (y))
#define ADD(d, x, y) teko_mont_add(fp, (d), (x), (y))
#define SUB(d, x, y) teko_mont_sub(fp, (d), (x), (y))

void teko_ec_add(const TekoMont* fp, const uint32_t* b,
                 uint32_t* OX3, uint32_t* OY3, uint32_t* OZ3,
                 const uint32_t* iX1, const uint32_t* iY1, const uint32_t* iZ1,
                 const uint32_t* iX2, const uint32_t* iY2, const uint32_t* iZ2) {
    int n = fp->n;
    uint32_t X1[TEKO_EC_MAX_LIMBS], Y1[TEKO_EC_MAX_LIMBS], Z1[TEKO_EC_MAX_LIMBS];
    uint32_t X2[TEKO_EC_MAX_LIMBS], Y2[TEKO_EC_MAX_LIMBS], Z2[TEKO_EC_MAX_LIMBS];
    uint32_t X3[TEKO_EC_MAX_LIMBS], Y3[TEKO_EC_MAX_LIMBS], Z3[TEKO_EC_MAX_LIMBS];
    uint32_t t0[TEKO_EC_MAX_LIMBS], t1[TEKO_EC_MAX_LIMBS], t2[TEKO_EC_MAX_LIMBS];
    uint32_t t3[TEKO_EC_MAX_LIMBS], t4[TEKO_EC_MAX_LIMBS];

    memcpy(X1, iX1, LIMBS_BYTES(n)); memcpy(Y1, iY1, LIMBS_BYTES(n)); memcpy(Z1, iZ1, LIMBS_BYTES(n));
    memcpy(X2, iX2, LIMBS_BYTES(n)); memcpy(Y2, iY2, LIMBS_BYTES(n)); memcpy(Z2, iZ2, LIMBS_BYTES(n));

    // RCB Algorithm 4 (a = -3). b denotes the curve constant (Montgomery form).
    MUL(t0, X1, X2);            //  1. t0 = X1*X2
    MUL(t1, Y1, Y2);            //  2. t1 = Y1*Y2
    MUL(t2, Z1, Z2);            //  3. t2 = Z1*Z2
    ADD(t3, X1, Y1);            //  4. t3 = X1+Y1
    ADD(t4, X2, Y2);            //  5. t4 = X2+Y2
    MUL(t3, t3, t4);            //  6. t3 = t3*t4
    ADD(t4, t0, t1);            //  7. t4 = t0+t1
    SUB(t3, t3, t4);            //  8. t3 = t3-t4
    ADD(t4, Y1, Z1);            //  9. t4 = Y1+Z1
    ADD(X3, Y2, Z2);            // 10. X3 = Y2+Z2
    MUL(t4, t4, X3);            // 11. t4 = t4*X3
    ADD(X3, t1, t2);            // 12. X3 = t1+t2
    SUB(t4, t4, X3);            // 13. t4 = t4-X3
    ADD(X3, X1, Z1);            // 14. X3 = X1+Z1
    ADD(Y3, X2, Z2);            // 15. Y3 = X2+Z2
    MUL(X3, X3, Y3);            // 16. X3 = X3*Y3
    ADD(Y3, t0, t2);            // 17. Y3 = t0+t2
    SUB(Y3, X3, Y3);            // 18. Y3 = X3-Y3
    MUL(Z3, b, t2);             // 19. Z3 = b*t2
    SUB(X3, Y3, Z3);            // 20. X3 = Y3-Z3
    ADD(Z3, X3, X3);            // 21. Z3 = X3+X3
    ADD(X3, X3, Z3);            // 22. X3 = X3+Z3
    SUB(Z3, t1, X3);            // 23. Z3 = t1-X3
    ADD(X3, t1, X3);            // 24. X3 = t1+X3
    MUL(Y3, b, Y3);             // 25. Y3 = b*Y3
    ADD(t1, t2, t2);            // 26. t1 = t2+t2
    ADD(t2, t1, t2);            // 27. t2 = t1+t2
    SUB(Y3, Y3, t2);            // 28. Y3 = Y3-t2
    SUB(Y3, Y3, t0);            // 29. Y3 = Y3-t0
    ADD(t1, Y3, Y3);            // 30. t1 = Y3+Y3
    ADD(Y3, t1, Y3);            // 31. Y3 = t1+Y3
    ADD(t1, t0, t0);            // 32. t1 = t0+t0
    ADD(t0, t1, t0);            // 33. t0 = t1+t0
    SUB(t0, t0, t2);            // 34. t0 = t0-t2
    MUL(t1, t4, Y3);            // 35. t1 = t4*Y3
    MUL(t2, t0, Y3);            // 36. t2 = t0*Y3
    MUL(Y3, X3, Z3);            // 37. Y3 = X3*Z3
    ADD(Y3, Y3, t2);            // 38. Y3 = Y3+t2
    MUL(X3, t3, X3);            // 39. X3 = t3*X3
    SUB(X3, X3, t1);            // 40. X3 = X3-t1
    MUL(Z3, t4, Z3);            // 41. Z3 = t4*Z3
    MUL(t1, t3, t0);            // 42. t1 = t3*t0
    ADD(Z3, Z3, t1);            // 43. Z3 = Z3+t1

    memcpy(OX3, X3, LIMBS_BYTES(n));
    memcpy(OY3, Y3, LIMBS_BYTES(n));
    memcpy(OZ3, Z3, LIMBS_BYTES(n));
}

void teko_ec_double(const TekoMont* fp, const uint32_t* b,
                    uint32_t* OX3, uint32_t* OY3, uint32_t* OZ3,
                    const uint32_t* iX, const uint32_t* iY, const uint32_t* iZ) {
    int n = fp->n;
    uint32_t X[TEKO_EC_MAX_LIMBS], Y[TEKO_EC_MAX_LIMBS], Z[TEKO_EC_MAX_LIMBS];
    uint32_t X3[TEKO_EC_MAX_LIMBS], Y3[TEKO_EC_MAX_LIMBS], Z3[TEKO_EC_MAX_LIMBS];
    uint32_t t0[TEKO_EC_MAX_LIMBS], t1[TEKO_EC_MAX_LIMBS];
    uint32_t t2[TEKO_EC_MAX_LIMBS], t3[TEKO_EC_MAX_LIMBS];

    memcpy(X, iX, LIMBS_BYTES(n)); memcpy(Y, iY, LIMBS_BYTES(n)); memcpy(Z, iZ, LIMBS_BYTES(n));

    // RCB Algorithm 6 (a = -3).
    MUL(t0, X, X);              //  1. t0 = X*X
    MUL(t1, Y, Y);              //  2. t1 = Y*Y
    MUL(t2, Z, Z);              //  3. t2 = Z*Z
    MUL(t3, X, Y);              //  4. t3 = X*Y
    ADD(t3, t3, t3);            //  5. t3 = t3+t3
    MUL(Z3, X, Z);              //  6. Z3 = X*Z
    ADD(Z3, Z3, Z3);            //  7. Z3 = Z3+Z3
    MUL(Y3, b, t2);             //  8. Y3 = b*t2
    SUB(Y3, Y3, Z3);            //  9. Y3 = Y3-Z3
    ADD(X3, Y3, Y3);            // 10. X3 = Y3+Y3
    ADD(Y3, X3, Y3);            // 11. Y3 = X3+Y3
    SUB(X3, t1, Y3);            // 12. X3 = t1-Y3
    ADD(Y3, t1, Y3);            // 13. Y3 = t1+Y3
    MUL(Y3, X3, Y3);            // 14. Y3 = X3*Y3
    MUL(X3, X3, t3);            // 15. X3 = X3*t3
    ADD(t3, t2, t2);            // 16. t3 = t2+t2
    ADD(t2, t2, t3);            // 17. t2 = t2+t3
    MUL(Z3, b, Z3);             // 18. Z3 = b*Z3
    SUB(Z3, Z3, t2);            // 19. Z3 = Z3-t2
    SUB(Z3, Z3, t0);            // 20. Z3 = Z3-t0
    ADD(t3, Z3, Z3);            // 21. t3 = Z3+Z3
    ADD(Z3, Z3, t3);            // 22. Z3 = Z3+t3
    ADD(t3, t0, t0);            // 23. t3 = t0+t0
    ADD(t0, t3, t0);            // 24. t0 = t3+t0
    SUB(t0, t0, t2);            // 25. t0 = t0-t2
    MUL(t0, t0, Z3);            // 26. t0 = t0*Z3
    ADD(Y3, Y3, t0);            // 27. Y3 = Y3+t0
    MUL(t0, Y, Z);              // 28. t0 = Y*Z
    ADD(t0, t0, t0);            // 29. t0 = t0+t0
    MUL(Z3, t0, Z3);            // 30. Z3 = t0*Z3
    SUB(X3, X3, Z3);            // 31. X3 = X3-Z3
    MUL(Z3, t0, t1);            // 32. Z3 = t0*t1
    ADD(Z3, Z3, Z3);            // 33. Z3 = Z3+Z3
    ADD(Z3, Z3, Z3);            // 34. Z3 = Z3+Z3

    memcpy(OX3, X3, LIMBS_BYTES(n));
    memcpy(OY3, Y3, LIMBS_BYTES(n));
    memcpy(OZ3, Z3, LIMBS_BYTES(n));
}

void teko_ec_scalar_mult(const TekoMont* fp, const uint32_t* b,
                         uint32_t* Xo, uint32_t* Yo, uint32_t* Zo,
                         const uint8_t* scalar_be, size_t slen,
                         const uint32_t* Px, const uint32_t* Py, const uint32_t* Pz) {
    int n = fp->n;
    uint32_t Rx[TEKO_EC_MAX_LIMBS], Ry[TEKO_EC_MAX_LIMBS], Rz[TEKO_EC_MAX_LIMBS];
    uint32_t Tx[TEKO_EC_MAX_LIMBS], Ty[TEKO_EC_MAX_LIMBS], Tz[TEKO_EC_MAX_LIMBS];
    uint32_t Qx[TEKO_EC_MAX_LIMBS], Qy[TEKO_EC_MAX_LIMBS], Qz[TEKO_EC_MAX_LIMBS];
    size_t i;
    int bit;

    memcpy(Qx, Px, LIMBS_BYTES(n));
    memcpy(Qy, Py, LIMBS_BYTES(n));
    memcpy(Qz, Pz, LIMBS_BYTES(n));

    // R = O = (0 : R : 0) (Montgomery form: X=0, Y=fp->one, Z=0).
    memset(Rx, 0, LIMBS_BYTES(n));
    memcpy(Ry, fp->one, LIMBS_BYTES(n));
    memset(Rz, 0, LIMBS_BYTES(n));

    // Left-to-right double-and-add, MSB first. Every bit does the same work (double, add,
    // constant-time select) so the trace is independent of the secret scalar.
    for (i = 0; i < slen; ++i) {
        for (bit = 7; bit >= 0; --bit) {
            uint32_t mask;
            teko_ec_double(fp, b, Rx, Ry, Rz, Rx, Ry, Rz);
            teko_ec_add(fp, b, Tx, Ty, Tz, Rx, Ry, Rz, Qx, Qy, Qz);
            mask = teko_bn_mask1((uint32_t)((scalar_be[i] >> bit) & 1u));
            teko_bn_cselect(Rx, Tx, Rx, n, mask);
            teko_bn_cselect(Ry, Ty, Ry, n, mask);
            teko_bn_cselect(Rz, Tz, Rz, n, mask);
        }
    }

    memcpy(Xo, Rx, LIMBS_BYTES(n));
    memcpy(Yo, Ry, LIMBS_BYTES(n));
    memcpy(Zo, Rz, LIMBS_BYTES(n));
}

void teko_ec_fp_inv(const TekoMont* fp, uint32_t* out, const uint32_t* a) {
    int n = fp->n;
    uint32_t e[TEKO_EC_MAX_LIMBS];   // exponent = p - 2
    uint32_t result[TEKO_EC_MAX_LIMBS];
    uint32_t base[TEKO_EC_MAX_LIMBS];
    uint64_t borrow = 2u;
    int i, bit, started = 0;

    // e = m - 2 (m is the field prime, odd and >= 3, so a single small subtraction).
    for (i = 0; i < n; ++i) {
        uint64_t s = (uint64_t)fp->m[i] - (borrow & 0xffffffffu);
        e[i] = (uint32_t)s;
        borrow = (s >> 32) & 1u;
    }

    memcpy(result, fp->one, LIMBS_BYTES(n)); // Montgomery form of 1
    memcpy(base, a, LIMBS_BYTES(n));

    for (i = n - 1; i >= 0; --i) {
        for (bit = 31; bit >= 0; --bit) {
            uint32_t ebit = (e[i] >> bit) & 1u;
            if (started) {
                teko_mont_mul(fp, result, result, result);
            }
            if (ebit) {
                teko_mont_mul(fp, result, result, base);
                started = 1;
            }
        }
    }
    memcpy(out, result, LIMBS_BYTES(n));
}
