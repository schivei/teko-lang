// Native HKDF (RFC 5869). Generic over HMAC-SHA-256/512; portable C23, no external libs.
//
//   PRK         = HMAC(salt, IKM)                          (Extract)
//   T(0)        = empty
//   T(i)        = HMAC(PRK, T(i-1) || info || byte(i))     (Expand)
//   OKM         = first L bytes of T(1) || T(2) || ...

#include "teko_crypto_hkdf.h"
#include "teko_crypto_hmac.h"

#include <string.h>

#define TEKO_HKDF_MAX_HASH 64u // SHA-512 digest length

static void teko_hkdf_hmac(size_t hash_len,
                           const uint8_t* key, size_t key_len,
                           const uint8_t* msg, size_t msg_len,
                           uint8_t* out) {
    if (hash_len == 32u) {
        teko_hmac_sha256(key, key_len, msg, msg_len, out);
    } else {
        teko_hmac_sha512(key, key_len, msg, msg_len, out);
    }
}

static void teko_hkdf_extract(size_t hash_len,
                              const uint8_t* salt, size_t salt_len,
                              const uint8_t* ikm, size_t ikm_len,
                              uint8_t* prk) {
    uint8_t zero_salt[TEKO_HKDF_MAX_HASH];
    if (salt == NULL || salt_len == 0u) {
        memset(zero_salt, 0, hash_len); // default salt = HashLen zero bytes
        salt = zero_salt;
        salt_len = hash_len;
    }
    teko_hkdf_hmac(hash_len, salt, salt_len, ikm, ikm_len, prk);
}

static int teko_hkdf_expand(size_t hash_len,
                            const uint8_t* prk,
                            const uint8_t* info, size_t info_len,
                            uint8_t* okm, size_t okm_len) {
    uint8_t t[TEKO_HKDF_MAX_HASH];
    size_t t_len = 0u;            // 0 for T(0)
    size_t done = 0u;
    unsigned counter = 1u;

    if (okm_len > 255u * hash_len) return -1;

    while (done < okm_len) {
        // Build msg = T(prev) || info || counter into a stack buffer, then HMAC it.
        uint8_t msg[TEKO_HKDF_MAX_HASH + 1024u + 1u];
        size_t mlen = 0u;
        size_t take;
        // info_len is bounded by the buffer; HKDF info is small in practice. Guard anyway.
        if (info_len > 1024u) return -1;

        if (t_len > 0u) {
            memcpy(msg, t, t_len);
            mlen += t_len;
        }
        if (info_len > 0u) {
            memcpy(msg + mlen, info, info_len);
            mlen += info_len;
        }
        msg[mlen++] = (uint8_t)counter;

        teko_hkdf_hmac(hash_len, prk, hash_len, msg, mlen, t);
        t_len = hash_len;

        take = (okm_len - done < hash_len) ? (okm_len - done) : hash_len;
        memcpy(okm + done, t, take);
        done += take;
        counter++;
    }
    return 0;
}

static int teko_hkdf(size_t hash_len,
                     const uint8_t* salt, size_t salt_len,
                     const uint8_t* ikm, size_t ikm_len,
                     const uint8_t* info, size_t info_len,
                     uint8_t* okm, size_t okm_len) {
    uint8_t prk[TEKO_HKDF_MAX_HASH];
    teko_hkdf_extract(hash_len, salt, salt_len, ikm, ikm_len, prk);
    return teko_hkdf_expand(hash_len, prk, info, info_len, okm, okm_len);
}

int teko_hkdf_sha256(const uint8_t* salt, size_t salt_len,
                     const uint8_t* ikm, size_t ikm_len,
                     const uint8_t* info, size_t info_len,
                     uint8_t* okm, size_t okm_len) {
    return teko_hkdf(32u, salt, salt_len, ikm, ikm_len, info, info_len, okm, okm_len);
}

int teko_hkdf_sha512(const uint8_t* salt, size_t salt_len,
                     const uint8_t* ikm, size_t ikm_len,
                     const uint8_t* info, size_t info_len,
                     uint8_t* okm, size_t okm_len) {
    return teko_hkdf(64u, salt, salt_len, ikm, ikm_len, info, info_len, okm, okm_len);
}

void teko_hkdf_sha256_extract(const uint8_t* salt, size_t salt_len,
                              const uint8_t* ikm, size_t ikm_len,
                              uint8_t prk[32]) {
    teko_hkdf_extract(32u, salt, salt_len, ikm, ikm_len, prk);
}
