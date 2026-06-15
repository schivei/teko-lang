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

// Project Wycheproof rsa_oaep_2048_sha256_mgf1sha256 (SHA-256, MGF1-SHA-256, empty label).
// 2048-bit key (modulus's ASN.1 leading 00 stripped -> 256 bytes).
static const char* OAEP_N =
    "a2b451a07d0aa5f96e455671513550514a8a5b462ebef717094fa1fee82224e6"
    "37f9746d3f7cafd31878d80325b6ef5a1700f65903b469429e89d6eac8845097"
    "b5ab393189db92512ed8a7711a1253facd20f79c15e8247f3d3e42e46e48c98e"
    "254a2fe9765313a03eff8f17e1a029397a1fa26a8dce26f490ed81299615d981"
    "4c22da610428e09c7d9658594266f5c021d0fceca08d945a12be82de4d1ece6b"
    "4c03145b5d3495d4ed5411eb878daf05fd7afc3e09ada0f1126422f590975a19"
    "69816f48698bcbba1b4d9cae79d460d8f9f85e7975005d9bc22c4e5ac0f7c1a4"
    "5d12569a62807d3b9a02e5a530e773066f453d1f5b4c2e9cf7820283f742b9d5";
static const char* OAEP_E = "010001";
static const char* OAEP_D =
    "24cdc62317f5d72a6f6ba6cc9632899b01d1ff28867d72f61688995bc855a4e4"
    "20a8405250089bdb13cf8e09543827b748b9d27fbb2b4d9e20af8c5a6a862796"
    "d1a4cc18ad16ea678bc1bd4a83bbbe9c5e57453b5ce7388e41a3ba4ce2b77b44"
    "38a229e954f720dae0353dc088ac8a76b26dc276f8e1b7851ddd6398ad16ff2e"
    "78195123b9b036e945c38c9d12434f6df76fe22359eb3e1ac9c011678fc926fa"
    "d3ae475a4fffff55feb2d147e9c894f4c0e29a599e762462482d968bf4278094"
    "5fc0d2c31c573c4431b8f4fe8b8c67bec815abd44f7a86edca1c2308737358d2"
    "c2ae5e2e0e2dadf730980262377e58b13b7d9992060a0bc870ccfdb4a9319ee1";

void test_teko_rsa_oaep_sha256_kat(void) {
    uint8_t n[256], e[8], d[256], ct[256], rec[256];
    size_t nlen, elen, dlen, rec_len;
    nlen = hexb(OAEP_N, n);
    elen = hexb(OAEP_E, e);
    dlen = hexb(OAEP_D, d);
    TEST_ASSERT_EQUAL_UINT(256u, nlen);

    // tcId 1: empty message.
    hexb("6e62bf24d95aff6868afec2a92a445b6458f16f688c19fe1212f66a6313783165"
         "3cedd359d8cff4dd485d77dfd55812c181373201f54aafd65730d2a304e62345"
         "5d51125d891e65d97fce52341cae45fb64c38a384a1c621e2713ee6794633f02"
         "9a9fd4d774f56551eac2176162e162640f25eab873a3451c475570f19228bced"
         "e4c67c370a75ed7fabccd538c9819eff182481b10d42f1a9f6a05373b8cf9b71"
         "818d467bd3b8ebacb619e8ad42916e600c043effceb3855bc48a629e60ae886f"
         "51b2a7876b0e623fb2ce68af4b039242f963adb0e4240aed0ed07f65f1ee7c0c"
         "c77d210d0c2d1dc10c81b881aa0c9c9e9499665cf2970d2ccfeeb3191531765", ct);
    TEST_ASSERT_EQUAL_INT(0, teko_rsa_oaep_decrypt(n, nlen, d, dlen, TEKO_RSA_SHA256,
                                                   NULL, 0u, ct, 256u, rec, &rec_len));
    TEST_ASSERT_EQUAL_UINT(0u, rec_len);

    // tcId 2: 20-byte all-zero message.
    hexb("207180c340658b5154ae45d2e4e7326a0997c683a26b595e536a29333c4b6614"
         "9af85e029d5419a39e3a147b221516ffd86b6b4b66c3e0c4c49fe8c57a2f5c37"
         "b8704b9b592b80db9cd788a4ed51ab4f0a1cbed63bd18d1f06a22f225866b0c2"
         "c417cb23473b7ba4250b1353bd2e5b4f0f937cd2efe5fa38db3c295f7748b970"
         "088657db4aa9a76e1ee6fbff166ec1861d00d085326c7384bdd1bc2f400d4f74"
         "dbdfadaf3fdc46073e668573e02030b9eb5af58eb540c66677a771194479ec00"
         "98d858a2ea45d0ba1e6b32440dfbac745000554d51a17684ca964b02a74d479f"
         "1d432ef763ef4059715a4348cfe36a215359712f25b6977903be4adb92febbf6", ct);
    TEST_ASSERT_EQUAL_INT(0, teko_rsa_oaep_decrypt(n, nlen, d, dlen, TEKO_RSA_SHA256,
                                                   NULL, 0u, ct, 256u, rec, &rec_len));
    TEST_ASSERT_EQUAL_UINT(20u, rec_len);
    {
        uint8_t zeros[20] = {0};
        TEST_ASSERT_EQUAL_MEMORY(zeros, rec, 20u);
    }

    // Round-trip with a random seed (public encrypt) and label.
    {
        const uint8_t m[13] = { 'O','A','E','P',' ','r','o','u','n','d',' ','!','!' };
        const uint8_t label[5] = { 'l','a','b','e','l' };
        TEST_ASSERT_EQUAL_INT(0, teko_rsa_oaep_encrypt(n, nlen, e, elen, TEKO_RSA_SHA256,
                                                       label, 5u, m, sizeof(m), ct));
        TEST_ASSERT_EQUAL_INT(0, teko_rsa_oaep_decrypt(n, nlen, d, dlen, TEKO_RSA_SHA256,
                                                       label, 5u, ct, 256u, rec, &rec_len));
        TEST_ASSERT_EQUAL_UINT(sizeof(m), rec_len);
        TEST_ASSERT_EQUAL_MEMORY(m, rec, sizeof(m));
    }
}
