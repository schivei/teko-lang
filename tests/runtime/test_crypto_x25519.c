#include "unity.h"
#include "../../src/runtime/teko_crypto_x25519.h"

#include <string.h>

static void from_hex(uint8_t* out, const char* hex, size_t n) {
    size_t i;
    for (i = 0u; i < n; ++i) {
        unsigned hi = hex[i * 2u], lo = hex[i * 2u + 1u];
        hi = (hi <= '9') ? (hi - '0') : ((hi | 0x20u) - 'a' + 10u);
        lo = (lo <= '9') ? (lo - '0') : ((lo | 0x20u) - 'a' + 10u);
        out[i] = (uint8_t)((hi << 4) | lo);
    }
}

static void check_x25519(const char* scalar_hex, const char* u_hex, const char* expect_hex) {
    uint8_t scalar[32], u[32], out[32], expect[32];
    from_hex(scalar, scalar_hex, 32u);
    from_hex(u, u_hex, 32u);
    from_hex(expect, expect_hex, 32u);
    TEST_ASSERT_EQUAL_INT(0, teko_x25519(out, scalar, u));
    TEST_ASSERT_EQUAL_MEMORY(expect, out, 32u);
}

// RFC 7748 §5.2 known-answer vectors.
void test_teko_crypto_x25519_rfc7748_vectors(void) {
    check_x25519(
        "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
        "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",
        "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");

    check_x25519(
        "4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d",
        "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493",
        "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957");
}

// RFC 7748 §6.1 — a full X25519 Diffie-Hellman agreement (base-point derivation + shared
// secret from both sides must match).
void test_teko_crypto_x25519_dh_agreement(void) {
    uint8_t a_priv[32], b_priv[32];
    uint8_t a_pub[32], b_pub[32];
    uint8_t a_pub_expect[32], b_pub_expect[32], shared_expect[32];
    uint8_t shared_a[32], shared_b[32];

    from_hex(a_priv, "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", 32u);
    from_hex(b_priv, "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", 32u);
    from_hex(a_pub_expect, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", 32u);
    from_hex(b_pub_expect, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", 32u);
    from_hex(shared_expect, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", 32u);

    teko_x25519_base(a_pub, a_priv);
    teko_x25519_base(b_pub, b_priv);
    TEST_ASSERT_EQUAL_MEMORY(a_pub_expect, a_pub, 32u);
    TEST_ASSERT_EQUAL_MEMORY(b_pub_expect, b_pub, 32u);

    teko_x25519(shared_a, a_priv, b_pub);
    teko_x25519(shared_b, b_priv, a_pub);
    TEST_ASSERT_EQUAL_MEMORY(shared_expect, shared_a, 32u);
    TEST_ASSERT_EQUAL_MEMORY(shared_a, shared_b, 32u); // both sides agree
}
