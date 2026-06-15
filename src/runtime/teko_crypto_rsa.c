// Native RSA (see header). RFC 8017. Built on teko_bn_modexp (Montgomery modexp).

#include "teko_crypto_rsa.h"
#include "teko_crypto_bn.h"

#include <string.h>

#define RSA_MAX_BYTES (TEKO_BN_MAX_LIMBS * 4)  // matches the bignum modulus ceiling

size_t teko_rsa_hash_len(TekoRsaHash h) {
    switch (h) {
        case TEKO_RSA_SHA256: return 32u;
        case TEKO_RSA_SHA384: return 48u;
        case TEKO_RSA_SHA512: return 64u;
        default: return 0u;
    }
}

// ASN.1 DigestInfo prefix (T = prefix || H) for each hash (RFC 8017 §9.2 notes).
static const uint8_t DI_SHA256[] = {
    0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
};
static const uint8_t DI_SHA384[] = {
    0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,0x05,0x00,0x04,0x30
};
static const uint8_t DI_SHA512[] = {
    0x30,0x51,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03,0x05,0x00,0x04,0x40
};

static const uint8_t* rsa_digestinfo(TekoRsaHash h, size_t* prefix_len) {
    switch (h) {
        case TEKO_RSA_SHA256: *prefix_len = sizeof DI_SHA256; return DI_SHA256;
        case TEKO_RSA_SHA384: *prefix_len = sizeof DI_SHA384; return DI_SHA384;
        case TEKO_RSA_SHA512: *prefix_len = sizeof DI_SHA512; return DI_SHA512;
        default: *prefix_len = 0u; return NULL;
    }
}

// Build EM = 0x00 || 0x01 || PS(0xFF..) || 0x00 || T, T = DigestInfo || hash, length emLen.
static int rsa_emsa_pkcs1v15(TekoRsaHash h, const uint8_t* hash, size_t hash_len,
                             uint8_t* em, size_t emLen) {
    size_t prefix_len, tLen, ps_len, i;
    const uint8_t* prefix = rsa_digestinfo(h, &prefix_len);
    if (!prefix) return -1;
    if (hash_len != teko_rsa_hash_len(h)) return -1;
    tLen = prefix_len + hash_len;
    if (emLen < tLen + 11u) return -1;          // need at least 8 bytes of 0xFF padding
    ps_len = emLen - tLen - 3u;

    em[0] = 0x00;
    em[1] = 0x01;
    for (i = 0; i < ps_len; ++i) em[2 + i] = 0xFF;
    em[2 + ps_len] = 0x00;
    memcpy(em + 3 + ps_len, prefix, prefix_len);
    memcpy(em + 3 + ps_len + prefix_len, hash, hash_len);
    return 0;
}

int teko_rsa_pkcs1v15_sign(const uint8_t* n, size_t nlen, const uint8_t* d, size_t dlen,
                           TekoRsaHash h, const uint8_t* hash, size_t hash_len, uint8_t* sig) {
    uint8_t em[RSA_MAX_BYTES];
    if (!n || !d || !hash || !sig) return -1;
    if (nlen == 0u || nlen > RSA_MAX_BYTES) return -1;
    if (rsa_emsa_pkcs1v15(h, hash, hash_len, em, nlen) != 0) return -1;
    // sig = EM^d mod n.
    return teko_bn_modexp(n, nlen, em, nlen, d, dlen, sig);
}

int teko_rsa_pkcs1v15_verify(const uint8_t* n, size_t nlen, const uint8_t* e, size_t elen,
                             TekoRsaHash h, const uint8_t* hash, size_t hash_len,
                             const uint8_t* sig, size_t siglen) {
    uint8_t em[RSA_MAX_BYTES];
    uint8_t got[RSA_MAX_BYTES];
    if (!n || !e || !hash || !sig) return -1;
    if (nlen == 0u || nlen > RSA_MAX_BYTES || siglen != nlen) return -1;
    if (rsa_emsa_pkcs1v15(h, hash, hash_len, em, nlen) != 0) return -1;
    // got = sig^e mod n; valid iff got == EM.
    if (teko_bn_modexp(n, nlen, sig, siglen, e, elen, got) != 0) return -1;
    return (memcmp(got, em, nlen) == 0) ? 0 : -1;
}
