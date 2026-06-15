#include "unity.h"
#include "../../src/runtime/teko_crypto_rsa.h"
#include "../../src/runtime/teko_crypto_sha2.h"

#include <string.h>

// Parse a hex string (strlen/2 bytes) into out; returns the byte count.
static size_t hexb(const char* hex, uint8_t* out) {
    size_t n = strlen(hex) / 2u, i;
    for (i = 0; i < n; ++i) {
        char hi = hex[2 * i], lo = hex[2 * i + 1];
        int h = (hi <= '9') ? hi - '0' : (hi | 0x20) - 'a' + 10;
        int l = (lo <= '9') ? lo - '0' : (lo | 0x20) - 'a' + 10;
        out[i] = (uint8_t)((h << 4) | l);
    }
    return n;
}

// NIST FIPS 186 CAVP "SigGen PKCS#1 Ver1.5", [mod = 1024], SHA-256 (first vector). Full key
// (n, e, d), message Msg, expected signature S. Deterministic sign must reproduce S; verify
// must accept and reject tampering.
static const char* RSA1024_N =
    "9d922c405da68a55993ca248a4a4ea93b2a7aeaf5fc6ed0a68e1adf7c2fd2765"
    "ea16275a1d72753140c8a513fbb50656769ba59caa4a963d4c268bf0f46c7e80"
    "a7c7e62b7601a2291f578c8eef06f11837c69c514b11cbca127c382610e6f0ba"
    "666209ab4ba8ea068e021f53eba105f963be30f9e74d00901a86c139a72f8e25";
static const char* RSA1024_E = "7aed3d";
static const char* RSA1024_D =
    "1176953856856f80646106f56654bb2f630652e67dd5ea94d7429175b2613baa"
    "b880a429fcc2482970a764a179154a3280fe502b8a37e83fa5785f5fbaf68930"
    "093a8992cf786fe9b1542162244a1c3e583cae936aa9cbde8251fd69b48cd1f9"
    "08cc90ab8a9e7a593b350e1c85b2edd97756cb31596ac9835277bfe9c1d2bb55";
static const char* RSA1024_MSG =
    "d427cdac8da1aa706db4967b1e722b939706b65b3f488285558e5a438fc449fe"
    "01bfa44b41ce4a1bc9922b319c1007da2d0578d87c79339f82978bc4cfbc37b2"
    "72ce549f19cfaebe79b23bb6f06de854694f987c81946a66e8373b0709076dcc"
    "edfde68e5ccd5bc346bbcef162b05569158d1e3195bfb0424e1473f385cc73b6";
static const char* RSA1024_S =
    "8f21b2f2c8f3b87617813730134b2de9b75d0a47ee7ddf0b8afedb23961a0c52"
    "9f5f0a80c9c473da991833fcc671e8ac97a400bef64658cfab195b506362f45a"
    "b4b10e04c4357e7ed0111cf38e60704e10ab0e287f34780162ca1164c0313abf"
    "d04ad543e2981d35f3c9c135bea8cc378182fc107b6f49622fc9228eea6c6124";

void test_teko_rsa_pkcs1v15_sha256_kat(void) {
    uint8_t n[128], e[8], d[128], msg[128], expect_sig[128];
    uint8_t hash[32], sig[128];
    size_t nlen, elen, dlen, mlen, slen;

    nlen = hexb(RSA1024_N, n);
    elen = hexb(RSA1024_E, e);
    dlen = hexb(RSA1024_D, d);
    mlen = hexb(RSA1024_MSG, msg);
    slen = hexb(RSA1024_S, expect_sig);
    TEST_ASSERT_EQUAL_UINT(128u, nlen);
    TEST_ASSERT_EQUAL_UINT(128u, slen);

    teko_sha256(msg, mlen, hash);

    TEST_ASSERT_EQUAL_INT(0, teko_rsa_pkcs1v15_sign(n, nlen, d, dlen, TEKO_RSA_SHA256,
                                                    hash, 32u, sig));
    TEST_ASSERT_EQUAL_MEMORY(expect_sig, sig, 128u); // deterministic — must equal S

    TEST_ASSERT_EQUAL_INT(0, teko_rsa_pkcs1v15_verify(n, nlen, e, elen, TEKO_RSA_SHA256,
                                                      hash, 32u, sig, 128u));

    // Tamper: flipped hash and flipped signature must both be rejected.
    hash[0] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-1, teko_rsa_pkcs1v15_verify(n, nlen, e, elen, TEKO_RSA_SHA256,
                                                       hash, 32u, sig, 128u));
    hash[0] ^= 0x01;
    sig[64] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-1, teko_rsa_pkcs1v15_verify(n, nlen, e, elen, TEKO_RSA_SHA256,
                                                       hash, 32u, sig, 128u));
}

// PKCS#1 v1.5 encryption: seeded encrypt -> decrypt round-trip (deterministic), plus the
// CSPRNG-padded public path. Uses the same 1024-bit FIPS key (n,e,d).
void test_teko_rsa_pkcs1v15_encrypt_roundtrip(void) {
    uint8_t n[128], e[8], d[128];
    uint8_t ps[128], ct[128], rec[128];
    size_t nlen, elen, dlen, rec_len, i;
    const uint8_t msg[11] = { 'h','e','l','l','o',' ','r','s','a','!','!' };
    size_t ps_len;

    nlen = hexb(RSA1024_N, n);
    elen = hexb(RSA1024_E, e);
    dlen = hexb(RSA1024_D, d);

    // Seeded (deterministic) path: PS = 0x01..0x01 (all nonzero).
    ps_len = nlen - sizeof(msg) - 3u;
    for (i = 0; i < ps_len; ++i) ps[i] = 0x01;
    TEST_ASSERT_EQUAL_INT(0, teko_rsa_pkcs1v15_encrypt_seeded(n, nlen, e, elen, msg,
                                                              sizeof(msg), ps, ps_len, ct));
    TEST_ASSERT_EQUAL_INT(0, teko_rsa_pkcs1v15_decrypt(n, nlen, d, dlen, ct, nlen,
                                                       rec, &rec_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(msg), rec_len);
    TEST_ASSERT_EQUAL_MEMORY(msg, rec, sizeof(msg));

    // CSPRNG-padded public path round-trips too (ct differs run to run; plaintext recovers).
    TEST_ASSERT_EQUAL_INT(0, teko_rsa_pkcs1v15_encrypt(n, nlen, e, elen, msg, sizeof(msg), ct));
    TEST_ASSERT_EQUAL_INT(0, teko_rsa_pkcs1v15_decrypt(n, nlen, d, dlen, ct, nlen,
                                                       rec, &rec_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(msg), rec_len);
    TEST_ASSERT_EQUAL_MEMORY(msg, rec, sizeof(msg));
}
