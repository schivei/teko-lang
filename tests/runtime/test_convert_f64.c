#include "unity.h"
#include "../../src/runtime/teko_convert.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

// Phase 17.C — KATs for the Ryu shortest-round-trip f64 formatter (the single C source
// of truth, teko_convert_f64.c). Three layers:
//   1. exact-string catalog        — pin the renderer policy byte-for-byte
//   2. round-trip property test     — format then re-parse with the HOST libc strtod
//                                     (NOT the runtime) and assert bit-identical: the
//                                     broad safety net over a wide vector of doubles.
//   3. shortest spot-checks         — confirm no extra/padded digits leak.
//
// Everything is deterministic: no locale, no clock, no rand — native == WASM.

// strtod is allowed HERE (the test harness is hosted; the freestanding constraint is on
// the runtime, not the tests).
#include <stdlib.h> // strtod

// --- helpers -------------------------------------------------------------------------
static void expect_f64(double v, const char* want) {
    char* s = teko_convert_f64_to_string(v);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING(want, s);
    free(s);
}

// Bit-exact double equality (so -0.0 != 0.0 and NaN handled separately).
static int double_bits_equal(double a, double b) {
    uint64_t ba, bb;
    memcpy(&ba, &a, sizeof(double));
    memcpy(&bb, &b, sizeof(double));
    return ba == bb;
}

// Format v, re-parse the result with host strtod, assert bit-identical round trip.
static void assert_roundtrip(double v) {
    char* s = teko_convert_f64_to_string(v);
    TEST_ASSERT_NOT_NULL(s);
    // Our renderer never emits NaN/Inf in the round-trip vector (caller skips them).
    char* end = NULL;
    double back = strtod(s, &end);
    if (!double_bits_equal(v, back)) {
        char msg[160];
        // Build a readable failure message (no printf-of-double needed for the assert).
        // TEST_ASSERT will surface `s`; include it.
        snprintf(msg, sizeof(msg), "round-trip mismatch for emitted \"%s\"", s);
        free(s);
        TEST_FAIL_MESSAGE(msg);
        return;
    }
    free(s);
}

// =====================================================================================
// 1. Exact-string catalog
// =====================================================================================
void test_teko_convert_f64_catalog(void) {
    // zeros (sign preserved, round-trips)
    expect_f64(0.0,  "0.0");
    expect_f64(-0.0, "-0.0");

    // small whole values -> ".0" (float reads as float, NOT "1")
    expect_f64(1.0,   "1.0");
    expect_f64(2.0,   "2.0");
    expect_f64(-1.0,  "-1.0");
    expect_f64(10.0,  "10.0");
    expect_f64(100.0, "100.0");
    expect_f64(123456789.0, "123456789.0");
    expect_f64(9007199254740992.0, "9007199254740992.0"); // 2^53

    // simple fractions (shortest)
    expect_f64(0.5,  "0.5");
    expect_f64(0.25, "0.25");
    expect_f64(0.1,  "0.1");
    expect_f64(0.2,  "0.2");
    expect_f64(0.3,  "0.3");
    expect_f64(1.5,  "1.5");
    expect_f64(-0.5, "-0.5");

    // pi (full 17-significant-digit shortest)
    expect_f64(3.141592653589793, "3.141592653589793");

    // powers of ten across the e10 threshold (sci iff e10 < -4 OR e10 >= 21).
    // 1e-4 (e10 = -4) is plain; 1e-5 (e10 = -5) is scientific.
    expect_f64(1e-1, "0.1");
    expect_f64(1e-2, "0.01");
    expect_f64(1e-3, "0.001");
    expect_f64(1e-4, "0.0001");
    expect_f64(1e-5, "1e-5");
    // 1e20 (e10 = 20) is plain; 1e21 (e10 = 21) is scientific.
    expect_f64(1e0,  "1.0");
    expect_f64(1e1,  "10.0");
    expect_f64(1e2,  "100.0");
    expect_f64(1e15, "1000000000000000.0");
    expect_f64(1e19, "10000000000000000000.0");
    expect_f64(1e20, "100000000000000000000.0");
    expect_f64(1e21, "1e+21");
    expect_f64(1e22, "1e+22");
    expect_f64(1e23, "1e+23");

    // subnormal minimum and max double (both scientific per the threshold)
    expect_f64(5e-324, "5e-324");
    expect_f64(1.7976931348623157e308, "1.7976931348623157e+308");

    // specials
    {
        double nan = (double)0.0;
        uint64_t qnan = 0x7ff8000000000000ull; // a quiet NaN bit pattern
        memcpy(&nan, &qnan, sizeof(double));
        expect_f64(nan, "NaN");
    }
    {
        double inf, ninf;
        uint64_t pi = 0x7ff0000000000000ull, ni = 0xfff0000000000000ull;
        memcpy(&inf, &pi, sizeof(double));
        memcpy(&ninf, &ni, sizeof(double));
        expect_f64(inf,  "Infinity");
        expect_f64(ninf, "-Infinity");
    }
}

// =====================================================================================
// 2. Round-trip property test (the broad safety net)
// =====================================================================================
void test_teko_convert_f64_roundtrip(void) {
    // (a) the catalog values
    static const double catalog[] = {
        0.0, -0.0, 1.0, 2.0, -1.0, 10.0, 100.0, 123456789.0, 9007199254740992.0,
        0.5, 0.25, 0.1, 0.2, 0.3, 1.5, -0.5, 3.141592653589793,
        1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e0, 1e1, 1e2, 1e15, 1e19, 1e20, 1e21, 1e22, 1e23,
        5e-324, 1.7976931348623157e308, 2.2250738585072014e-308 /* min normal */,
        4.9e-324, 1.0e-300, 9.999999999999999e22, 1234.5678, -98765.4321,
    };
    size_t catalog_n = sizeof(catalog) / sizeof(catalog[0]);
    for (size_t k = 0; k < catalog_n; k++) {
        assert_roundtrip(catalog[k]);
    }

    // (b) exponent sweep: for each binary exponent, a few representative mantissas.
    size_t sweep_n = 0;
    for (int e = -1074; e <= 1023; e += 1) {
        // Build a double as 2^e scaled by a few mantissa fractions, via ldexp-free
        // construction: assemble the bit pattern directly.
        // Normalized: biased exponent eb in [1,2046]; subnormals handled by the LCG step.
        int eb = e + 1023;
        if (eb < 1 || eb > 2046) {
            continue;
        }
        static const uint64_t mantissas[] = {
            0x0000000000000ull, 0x8000000000000ull, 0x5555555555555ull,
            0xFFFFFFFFFFFFFull, 0x123456789ABCDull,
        };
        for (size_t mi = 0; mi < sizeof(mantissas) / sizeof(mantissas[0]); mi++) {
            uint64_t bp = ((uint64_t)eb << 52) | mantissas[mi];
            double d;
            memcpy(&d, &bp, sizeof(double));
            assert_roundtrip(d);
            sweep_n++;
        }
    }

    // (c) fixed-seed LCG over raw 64-bit patterns (skip NaN/Inf).
    uint64_t state = 0x0123456789ABCDEFull; // fixed seed -> deterministic
    size_t lcg_n = 0;
    const size_t LCG_ITERS = 20000;
    for (size_t k = 0; k < LCG_ITERS; k++) {
        // 64-bit LCG (Knuth MMIX constants).
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        uint64_t bp = state;
        // Classify: skip NaN/Inf (exponent all ones).
        uint32_t exp = (uint32_t)((bp >> 52) & 0x7FFu);
        if (exp == 0x7FFu) {
            continue;
        }
        double d;
        memcpy(&d, &bp, sizeof(double));
        assert_roundtrip(d);
        lcg_n++;
    }

    // Sanity: the vector actually exercised a large set.
    TEST_ASSERT_TRUE(sweep_n > 5000);
    TEST_ASSERT_TRUE(lcg_n > 19000);
}

// =====================================================================================
// 3. Shortest spot-checks (no extra/padded digits)
// =====================================================================================
void test_teko_convert_f64_shortest(void) {
    // 0.1 must be exactly "0.1", NOT "0.10000000000000001".
    {
        char* s = teko_convert_f64_to_string(0.1);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_EQUAL_STRING("0.1", s);
        TEST_ASSERT_TRUE(strlen(s) == 3);
        free(s);
    }
    // 0.3 must be exactly "0.3".
    {
        char* s = teko_convert_f64_to_string(0.3);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_EQUAL_STRING("0.3", s);
        free(s);
    }
    // 1.0 stays "1.0" (3 chars), never 17 digits.
    {
        char* s = teko_convert_f64_to_string(1.0);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) == 3);
        free(s);
    }
    // A value with a genuinely long shortest form is still shortest (pi: 17 sig digits).
    {
        char* s = teko_convert_f64_to_string(3.141592653589793);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_EQUAL_STRING("3.141592653589793", s);
        free(s);
    }
}
