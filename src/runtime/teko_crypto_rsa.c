// Native RSA (see header). RFC 8017. Built on teko_bn_modexp (Montgomery modexp).

#include "teko_crypto_rsa.h"
#include "teko_crypto_bn.h"
#include "teko_crypto_random.h"
#include "teko_crypto_sha2.h"
#include "teko_crypto_sha512.h"

#include <string.h>

#define RSA_MAX_BYTES (TEKO_BN_MAX_LIMBS * 4)  // matches the bignum modulus ceiling
#define RSA_MAX_HASH  64u

// Dispatch a one-shot hash over the supported set.
static void rsa_hash(TekoRsaHash h, const uint8_t* data, size_t len, uint8_t* out) {
    switch (h) {
        case TEKO_RSA_SHA384: teko_sha384(data, len, out); break;
        case TEKO_RSA_SHA512: teko_sha512(data, len, out); break;
        case TEKO_RSA_SHA256:
        default:              teko_sha256(data, len, out); break;
    }
}

// MGF1 (RFC 8017 B.2.1): mask[0..masklen) = T = H(seed || I2OSP(0,4)) || H(seed || ..1..) ...
static int rsa_mgf1(TekoRsaHash h, const uint8_t* seed, size_t seedlen,
                    uint8_t* mask, size_t masklen) {
    size_t hLen = teko_rsa_hash_len(h);
    uint8_t buf[RSA_MAX_BYTES + 4];
    uint8_t dig[RSA_MAX_HASH];
    size_t done = 0u;
    uint32_t counter = 0u;
    if (seedlen + 4u > sizeof buf) return -1;
    memcpy(buf, seed, seedlen);
    while (done < masklen) {
        size_t take;
        buf[seedlen + 0] = (uint8_t)(counter >> 24);
        buf[seedlen + 1] = (uint8_t)(counter >> 16);
        buf[seedlen + 2] = (uint8_t)(counter >> 8);
        buf[seedlen + 3] = (uint8_t)(counter);
        rsa_hash(h, buf, seedlen + 4u, dig);
        take = (masklen - done < hLen) ? (masklen - done) : hLen;
        memcpy(mask + done, dig, take);
        done += take;
        counter++;
    }
    return 0;
}

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

// ----------------------------- PKCS#1 v1.5 encryption -----------------------------------

int teko_rsa_pkcs1v15_encrypt_seeded(const uint8_t* n, size_t nlen, const uint8_t* e,
                                     size_t elen, const uint8_t* msg, size_t msg_len,
                                     const uint8_t* ps, size_t ps_len, uint8_t* ct) {
    uint8_t em[RSA_MAX_BYTES];
    size_t i;
    if (!n || !e || !msg || !ps || !ct) return -1;
    if (nlen == 0u || nlen > RSA_MAX_BYTES) return -1;
    if (msg_len + 11u > nlen) return -1;                 // mLen <= k - 11
    if (ps_len != nlen - msg_len - 3u) return -1;
    for (i = 0; i < ps_len; ++i) if (ps[i] == 0x00) return -1; // PS must be nonzero

    em[0] = 0x00;
    em[1] = 0x02;
    memcpy(em + 2, ps, ps_len);
    em[2 + ps_len] = 0x00;
    memcpy(em + 3 + ps_len, msg, msg_len);
    return teko_bn_modexp(n, nlen, em, nlen, e, elen, ct);
}

int teko_rsa_pkcs1v15_encrypt(const uint8_t* n, size_t nlen, const uint8_t* e, size_t elen,
                              const uint8_t* msg, size_t msg_len, uint8_t* ct) {
    uint8_t ps[RSA_MAX_BYTES];
    size_t ps_len, i;
    if (nlen == 0u || nlen > RSA_MAX_BYTES) return -1;
    if (msg_len + 11u > nlen) return -1;
    ps_len = nlen - msg_len - 3u;
    // Fill PS with nonzero random bytes (resample any zero byte).
    for (i = 0; i < ps_len; ++i) {
        uint8_t b = 0x00;
        do {
            if (teko_csprng_bytes(&b, 1u) != 0) return -1;
        } while (b == 0x00);
        ps[i] = b;
    }
    return teko_rsa_pkcs1v15_encrypt_seeded(n, nlen, e, elen, msg, msg_len, ps, ps_len, ct);
}

int teko_rsa_pkcs1v15_decrypt(const uint8_t* n, size_t nlen, const uint8_t* d, size_t dlen,
                              const uint8_t* ct, size_t ct_len, uint8_t* out, size_t* out_len) {
    uint8_t em[RSA_MAX_BYTES];
    size_t i, sep;
    if (!n || !d || !ct || !out || !out_len) return -1;
    if (nlen == 0u || nlen > RSA_MAX_BYTES || ct_len != nlen) return -1;
    if (teko_bn_modexp(n, nlen, ct, ct_len, d, dlen, em) != 0) return -1;

    // EM = 00 || 02 || PS(>=8 nonzero) || 00 || M.
    if (em[0] != 0x00 || em[1] != 0x02) return -1;
    sep = 0u;
    for (i = 2u; i < nlen; ++i) {
        if (em[i] == 0x00) { sep = i; break; }
    }
    if (sep == 0u) return -1;          // no separator
    if (sep < 10u) return -1;          // PS shorter than 8 bytes
    *out_len = nlen - sep - 1u;
    memcpy(out, em + sep + 1u, *out_len);
    return 0;
}

// ----------------------------------- OAEP ----------------------------------------------

int teko_rsa_oaep_encrypt_seeded(const uint8_t* n, size_t nlen, const uint8_t* e, size_t elen,
                                 TekoRsaHash h, const uint8_t* label, size_t label_len,
                                 const uint8_t* msg, size_t msg_len,
                                 const uint8_t* seed, size_t seed_len, uint8_t* ct) {
    size_t hLen = teko_rsa_hash_len(h);
    size_t k = nlen, dblen, ps_len, i;
    uint8_t em[RSA_MAX_BYTES];
    uint8_t db[RSA_MAX_BYTES];
    uint8_t mask[RSA_MAX_BYTES];
    uint8_t seedmask[RSA_MAX_HASH];
    uint8_t lhash[RSA_MAX_HASH];

    if (!n || !e || !msg || !seed || !ct || hLen == 0u) return -1;
    if (k == 0u || k > RSA_MAX_BYTES) return -1;
    if (seed_len != hLen) return -1;
    if (k < 2u * hLen + 2u) return -1;
    if (msg_len > k - 2u * hLen - 2u) return -1;

    rsa_hash(h, label, label_len, lhash);
    dblen = k - hLen - 1u;
    ps_len = k - msg_len - 2u * hLen - 2u;
    memcpy(db, lhash, hLen);
    memset(db + hLen, 0, ps_len);
    db[hLen + ps_len] = 0x01;
    memcpy(db + hLen + ps_len + 1u, msg, msg_len);

    // maskedDB = DB xor MGF1(seed, dblen)
    if (rsa_mgf1(h, seed, hLen, mask, dblen) != 0) return -1;
    for (i = 0; i < dblen; ++i) db[i] ^= mask[i];
    // maskedSeed = seed xor MGF1(maskedDB, hLen)
    if (rsa_mgf1(h, db, dblen, seedmask, hLen) != 0) return -1;

    em[0] = 0x00;
    for (i = 0; i < hLen; ++i) em[1 + i] = (uint8_t)(seed[i] ^ seedmask[i]);
    memcpy(em + 1 + hLen, db, dblen);
    return teko_bn_modexp(n, nlen, em, k, e, elen, ct);
}

int teko_rsa_oaep_encrypt(const uint8_t* n, size_t nlen, const uint8_t* e, size_t elen,
                          TekoRsaHash h, const uint8_t* label, size_t label_len,
                          const uint8_t* msg, size_t msg_len, uint8_t* ct) {
    uint8_t seed[RSA_MAX_HASH];
    size_t hLen = teko_rsa_hash_len(h);
    if (hLen == 0u) return -1;
    if (teko_csprng_bytes(seed, hLen) != 0) return -1;
    return teko_rsa_oaep_encrypt_seeded(n, nlen, e, elen, h, label, label_len,
                                        msg, msg_len, seed, hLen, ct);
}

int teko_rsa_oaep_decrypt(const uint8_t* n, size_t nlen, const uint8_t* d, size_t dlen,
                          TekoRsaHash h, const uint8_t* label, size_t label_len,
                          const uint8_t* ct, size_t ct_len, uint8_t* out, size_t* out_len) {
    size_t hLen = teko_rsa_hash_len(h);
    size_t k = nlen, dblen, i, one_idx;
    uint8_t em[RSA_MAX_BYTES];
    uint8_t db[RSA_MAX_BYTES];
    uint8_t mask[RSA_MAX_BYTES];
    uint8_t seed[RSA_MAX_HASH];
    uint8_t seedmask[RSA_MAX_HASH];
    uint8_t lhash[RSA_MAX_HASH];
    uint8_t bad, found;

    if (!n || !d || !ct || !out || !out_len || hLen == 0u) return -1;
    if (k == 0u || k > RSA_MAX_BYTES || ct_len != k) return -1;
    if (k < 2u * hLen + 2u) return -1;
    if (teko_bn_modexp(n, nlen, ct, ct_len, d, dlen, em) != 0) return -1;

    dblen = k - hLen - 1u;
    rsa_hash(h, label, label_len, lhash);

    // seed = maskedSeed xor MGF1(maskedDB, hLen); DB = maskedDB xor MGF1(seed, dblen).
    if (rsa_mgf1(h, em + 1 + hLen, dblen, seedmask, hLen) != 0) return -1;
    for (i = 0; i < hLen; ++i) seed[i] = (uint8_t)(em[1 + i] ^ seedmask[i]);
    if (rsa_mgf1(h, seed, hLen, mask, dblen) != 0) return -1;
    memcpy(db, em + 1 + hLen, dblen);
    for (i = 0; i < dblen; ++i) db[i] ^= mask[i];

    // Checks: Y==0, lHash' == lHash, then 0x00* 0x01 separator. Accumulate without early-out.
    bad = em[0];
    for (i = 0; i < hLen; ++i) bad |= (uint8_t)(db[i] ^ lhash[i]);

    found = 0u;
    one_idx = 0u;
    for (i = hLen; i < dblen; ++i) {
        uint8_t is_one = (uint8_t)((db[i] == 0x01) ? 1u : 0u);
        uint8_t is_zero = (uint8_t)((db[i] == 0x00) ? 1u : 0u);
        if (!found && is_one) { found = 1u; one_idx = i; }
        else if (!found && !is_zero) { bad |= 0xFFu; } // nonzero before the 0x01 -> invalid
    }
    if (bad != 0u || !found) return -1;

    *out_len = dblen - (one_idx + 1u);
    memcpy(out, db + one_idx + 1u, *out_len);
    return 0;
}

// ----------------------------------- PSS -----------------------------------------------

// Bit length of the big-endian integer n (nlen bytes).
static size_t rsa_bitlen(const uint8_t* n, size_t nlen) {
    size_t i;
    for (i = 0; i < nlen; ++i) {
        if (n[i] != 0u) {
            unsigned b = n[i];
            size_t bits = (nlen - i) * 8u;
            while ((b & 0x80u) == 0u) { bits--; b = (unsigned)(b << 1); }
            return bits;
        }
    }
    return 0u;
}

// Build EM (emLen bytes) per EMSA-PSS-ENCODE for the given salt.
static int rsa_pss_encode(TekoRsaHash h, const uint8_t* mhash, size_t hLen,
                          const uint8_t* salt, size_t sLen,
                          size_t emBits, size_t emLen, uint8_t* em) {
    uint8_t mprime[8 + RSA_MAX_HASH + RSA_MAX_BYTES];
    uint8_t hh[RSA_MAX_HASH];
    uint8_t dbmask[RSA_MAX_BYTES];
    size_t dblen, ps_len, i, zero_bits;

    if (emLen < hLen + sLen + 2u) return -1;
    // M' = (00)x8 || mHash || salt ; H = Hash(M')
    memset(mprime, 0, 8u);
    memcpy(mprime + 8, mhash, hLen);
    memcpy(mprime + 8 + hLen, salt, sLen);
    rsa_hash(h, mprime, 8u + hLen + sLen, hh);

    dblen = emLen - hLen - 1u;
    ps_len = emLen - sLen - hLen - 2u;
    // DB = PS(0) || 0x01 || salt
    memset(em, 0, dblen);
    em[ps_len] = 0x01;
    memcpy(em + ps_len + 1u, salt, sLen);
    // maskedDB = DB xor MGF1(H, dblen)
    if (rsa_mgf1(h, hh, hLen, dbmask, dblen) != 0) return -1;
    for (i = 0; i < dblen; ++i) em[i] ^= dbmask[i];
    // Clear the leftmost 8*emLen - emBits bits of the first byte.
    zero_bits = 8u * emLen - emBits;
    em[0] &= (uint8_t)(0xFFu >> zero_bits);
    // EM = maskedDB || H || 0xbc
    memcpy(em + dblen, hh, hLen);
    em[emLen - 1u] = 0xbc;
    return 0;
}

int teko_rsa_pss_sign_seeded(const uint8_t* n, size_t nlen, const uint8_t* d, size_t dlen,
                             TekoRsaHash h, const uint8_t* mhash, size_t mhash_len,
                             const uint8_t* salt, size_t salt_len, uint8_t* sig) {
    size_t hLen = teko_rsa_hash_len(h);
    size_t modBits, emBits, emLen;
    uint8_t em[RSA_MAX_BYTES];
    if (!n || !d || !mhash || !sig || hLen == 0u) return -1;
    if (nlen == 0u || nlen > RSA_MAX_BYTES || mhash_len != hLen) return -1;
    if (salt_len > 0u && !salt) return -1;
    modBits = rsa_bitlen(n, nlen);
    if (modBits == 0u) return -1;
    emBits = modBits - 1u;
    emLen = (emBits + 7u) / 8u;
    if (rsa_pss_encode(h, mhash, hLen, salt, salt_len, emBits, emLen, em) != 0) return -1;
    return teko_bn_modexp(n, nlen, em, emLen, d, dlen, sig);
}

int teko_rsa_pss_sign(const uint8_t* n, size_t nlen, const uint8_t* d, size_t dlen,
                      TekoRsaHash h, const uint8_t* mhash, size_t mhash_len,
                      size_t salt_len, uint8_t* sig) {
    uint8_t salt[RSA_MAX_BYTES];
    if (salt_len > RSA_MAX_BYTES) return -1;
    if (salt_len > 0u && teko_csprng_bytes(salt, salt_len) != 0) return -1;
    return teko_rsa_pss_sign_seeded(n, nlen, d, dlen, h, mhash, mhash_len, salt, salt_len, sig);
}

int teko_rsa_pss_verify(const uint8_t* n, size_t nlen, const uint8_t* e, size_t elen,
                        TekoRsaHash h, const uint8_t* mhash, size_t mhash_len,
                        size_t salt_len, const uint8_t* sig, size_t siglen) {
    size_t hLen = teko_rsa_hash_len(h);
    size_t modBits, emBits, emLen, dblen, ps_len, i, zero_bits;
    uint8_t got[RSA_MAX_BYTES];
    uint8_t db[RSA_MAX_BYTES];
    uint8_t dbmask[RSA_MAX_BYTES];
    uint8_t hh[RSA_MAX_HASH];
    uint8_t hprime[RSA_MAX_HASH];
    uint8_t mprime[8 + RSA_MAX_HASH + RSA_MAX_BYTES];
    const uint8_t* em;
    uint8_t bad;

    if (!n || !e || !mhash || !sig || hLen == 0u) return -1;
    if (nlen == 0u || nlen > RSA_MAX_BYTES || siglen != nlen || mhash_len != hLen) return -1;
    modBits = rsa_bitlen(n, nlen);
    if (modBits == 0u) return -1;
    emBits = modBits - 1u;
    emLen = (emBits + 7u) / 8u;
    if (emLen < hLen + salt_len + 2u) return -1;

    if (teko_bn_modexp(n, nlen, sig, siglen, e, elen, got) != 0) return -1;
    // EM = trailing emLen bytes of the nlen-byte result; any leading bytes must be zero.
    if (emLen > nlen) return -1;
    for (i = 0; i < nlen - emLen; ++i) if (got[i] != 0u) return -1;
    em = got + (nlen - emLen);

    if (em[emLen - 1u] != 0xbc) return -1;
    dblen = emLen - hLen - 1u;
    zero_bits = 8u * emLen - emBits;
    // The leftmost zero_bits bits of maskedDB[0] must be zero.
    if (zero_bits != 0u && (em[0] >> (8u - zero_bits)) != 0u) return -1;

    // DB = maskedDB xor MGF1(H, dblen); clear top bits.
    memcpy(hh, em + dblen, hLen);
    if (rsa_mgf1(h, hh, hLen, dbmask, dblen) != 0) return -1;
    for (i = 0; i < dblen; ++i) db[i] = (uint8_t)(em[i] ^ dbmask[i]);
    db[0] &= (uint8_t)(0xFFu >> zero_bits);

    // DB must be PS(0)*  || 0x01 || salt.
    ps_len = emLen - hLen - salt_len - 2u;
    bad = 0u;
    for (i = 0; i < ps_len; ++i) bad |= db[i];
    if (db[ps_len] != 0x01) bad |= 0xFFu;
    if (bad != 0u) return -1;

    // H' = Hash((00)x8 || mHash || salt); accept iff H' == H.
    memset(mprime, 0, 8u);
    memcpy(mprime + 8, mhash, hLen);
    memcpy(mprime + 8 + hLen, db + ps_len + 1u, salt_len);
    rsa_hash(h, mprime, 8u + hLen + salt_len, hprime);
    return (memcmp(hprime, hh, hLen) == 0) ? 0 : -1;
}
