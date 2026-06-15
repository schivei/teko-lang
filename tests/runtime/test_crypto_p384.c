#include "unity.h"
#include "../../src/runtime/teko_crypto_p384.h"
#include "../../src/runtime/teko_crypto_sha512.h"

#include <string.h>

static void hexbytes(const char* hex, uint8_t* out, size_t len) {
    size_t i;
    for (i = 0; i < len; ++i) {
        char hi = hex[2 * i], lo = hex[2 * i + 1];
        int h = (hi <= '9') ? hi - '0' : (hi | 0x20) - 'a' + 10;
        int l = (lo <= '9') ? lo - '0' : (lo | 0x20) - 'a' + 10;
        out[i] = (uint8_t)((h << 4) | l);
    }
}

// NIST CAVP "KAS_ECC_CDH_PrimitiveTest" [P-384], COUNT 0: derive_public(dIUT) == QIUT and
// ECDH(dIUT, QCAVS) == ZIUT.
void test_teko_p384_ecdh_cavp_count0(void) {
    uint8_t priv[48], peer[96], expect_pub[96], expect_z[48], pub[96], shared[48];
    hexbytes("3cc3122a68f0d95027ad38c067916ba0eb8c38894d22e1b15618b6818a661774"
             "ad463b205da88cf699ab4d43c9cf98a1", priv, 48u);
    hexbytes("a7c76b970c3b5fe8b05d2838ae04ab47697b9eaf52e764592efda27fe7513272"
             "734466b400091adbf2d68c58e0c50066", peer, 48u);
    hexbytes("ac68f19f2e1cb879aed43a9969b91a0839c4c38a49749b661efedf243451915e"
             "d0905a32b060992b468c64766fc8437a", peer + 48, 48u);
    hexbytes("9803807f2f6d2fd966cdd0290bd410c0190352fbec7ff6247de1302df86f25d3"
             "4fe4a97bef60cff548355c015dbb3e5f", expect_pub, 48u);
    hexbytes("ba26ca69ec2f5b5d9dad20cc9da711383a9dbe34ea3fa5a2af75b46502629ad5"
             "4dd8b7d73a8abb06a3a3be47d650cc99", expect_pub + 48, 48u);
    hexbytes("5f9d29dc5e31a163060356213669c8ce132e22f57c9a04f40ba7fcead493b457"
             "e5621e766c40a2e3d4d6a04b25e533f1", expect_z, 48u);

    TEST_ASSERT_EQUAL_INT(0, teko_p384_derive_public(priv, pub));
    TEST_ASSERT_EQUAL_MEMORY(expect_pub, pub, 96u);
    TEST_ASSERT_EQUAL_INT(0, teko_p384_ecdh(priv, peer, shared));
    TEST_ASSERT_EQUAL_MEMORY(expect_z, shared, 48u);
}

// RFC 6979 Appendix A.2.6 — P-384 / SHA-384 deterministic ECDSA.
static const char* A26_X  = "6b9d3dad2e1b8c1c05b19875b6659f4de23c3b667bf297ba9aa47740787137d8"
                            "96d5724e4c70a825f872c9ea60d2edf5";
static const char* A26_UX = "ec3a4e415b4e19a4568618029f427fa5da9a8bc4ae92e02e06aae5286b300c64"
                            "def8f0ea9055866064a254515480bc13";
static const char* A26_UY = "8015d9b72d7d57244ea8ef9ac0c621896708a59367f9dfb9f54ca84b3f1c9db1"
                            "288b231c3ae0d4fe7344fd2533264720";

static void p384_ecdsa_det_case(const char* msg, const char* r_hex, const char* s_hex) {
    uint8_t priv[48], pub[96], expect_pub[96], expect_sig[96], sig[96], hash[48];
    hexbytes(A26_X, priv, 48u);
    hexbytes(A26_UX, expect_pub, 48u);
    hexbytes(A26_UY, expect_pub + 48, 48u);
    hexbytes(r_hex, expect_sig, 48u);
    hexbytes(s_hex, expect_sig + 48, 48u);

    TEST_ASSERT_EQUAL_INT(0, teko_p384_derive_public(priv, pub));
    TEST_ASSERT_EQUAL_MEMORY(expect_pub, pub, 96u);

    teko_sha384((const uint8_t*)msg, strlen(msg), hash);

    TEST_ASSERT_EQUAL_INT(0, teko_p384_ecdsa_sign(priv, hash, 48u, sig));
    TEST_ASSERT_EQUAL_MEMORY(expect_sig, sig, 96u);

    TEST_ASSERT_EQUAL_INT(0, teko_p384_ecdsa_verify(pub, hash, 48u, sig));
    hash[0] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-1, teko_p384_ecdsa_verify(pub, hash, 48u, sig));
    hash[0] ^= 0x01;
    sig[20] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-1, teko_p384_ecdsa_verify(pub, hash, 48u, sig));
}

void test_teko_p384_ecdsa_rfc6979_sample(void) {
    p384_ecdsa_det_case("sample",
        "94edbb92a5ecb8aad4736e56c691916b3f88140666ce9fa73d64c4ea95ad133c"
        "81a648152e44acf96e36dd1e80fabe46",
        "99ef4aeb15f178cea1fe40db2603138f130e740a19624526203b6351d0a3a94f"
        "a329c145786e679e7b82c71a38628ac8");
}

void test_teko_p384_ecdsa_rfc6979_test(void) {
    p384_ecdsa_det_case("test",
        "8203b63d3c853e8d77227fb377bcf7b7b772e97892a80f36ab775d509d7a5feb"
        "0542a7f0812998da8f1dd3ca3cf023db",
        "ddd0760448d42d8a43af45af836fce4de8be06b485e9b61b827c2f13173923e0"
        "6a739f040649a667bf3b828246baa5a5");
}

void test_teko_p384_ecdh_rejects_offcurve(void) {
    uint8_t priv[48], peer[96], shared[48];
    hexbytes("3cc3122a68f0d95027ad38c067916ba0eb8c38894d22e1b15618b6818a661774"
             "ad463b205da88cf699ab4d43c9cf98a1", priv, 48u);
    hexbytes("a7c76b970c3b5fe8b05d2838ae04ab47697b9eaf52e764592efda27fe7513272"
             "734466b400091adbf2d68c58e0c50066", peer, 48u);
    hexbytes("ac68f19f2e1cb879aed43a9969b91a0839c4c38a49749b661efedf243451915e"
             "d0905a32b060992b468c64766fc8437a", peer + 48, 48u);
    peer[95] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-1, teko_p384_ecdh(priv, peer, shared));
}
