#include "unity.h"
#include "../../src/runtime/teko_deflate.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * Phase 19 (DEFLATE-CORE) — KATs for teko_deflate.c
 *
 * Coverage:
 *   1. CRC32 known-answer vectors (ISO 3309 / gzip check values)
 *   2. Adler-32 known-answer vectors (RFC 1950 §9 example)
 *   3. Round-trip: deflate→inflate (varied payloads)
 *   4. Inflate against known-good zlib/gzip byte vectors (cross-check)
 *   5. Decompression-bomb guard: inflate rejects output > max_out (TEKO_DEFLATE_BOMB)
 *   6. Invalid-input: corrupt DEFLATE, bad zlib header, bad gzip magic → TEKO_DEFLATE_CORRUPT
 *   7. NULL/badarg → TEKO_DEFLATE_BADARG
 */

/* ============================================================================
 * 1. CRC32
 * ============================================================================ */

void test_teko_crc32_empty(void) {
    /* CRC32 of empty string = 0x00000000 */
    uint32_t crc = teko_crc32(0, NULL, 0);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, crc);
}

void test_teko_crc32_rfc_check_value(void) {
    /* RFC 1952 §8: CRC32 of the ASCII string "123456789" = 0xCBF43926. */
    static const uint8_t digits[] = "123456789";
    uint32_t crc = teko_crc32(0, digits, 9);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc);
}

void test_teko_crc32_hello(void) {
    /* CRC32("hello") = 0x3610A686 (verified with python3: binascii.crc32(b"hello") & 0xFFFFFFFF). */
    static const uint8_t hello[] = "hello";
    uint32_t crc = teko_crc32(0, hello, 5);
    TEST_ASSERT_EQUAL_HEX32(0x3610A686u, crc);
}

void test_teko_crc32_chained(void) {
    /* Chaining: CRC32("hello world") == CRC32(CRC32("hello "), "world"). */
    static const uint8_t s1[] = "hello ";
    static const uint8_t s2[] = "world";
    static const uint8_t full[] = "hello world";
    uint32_t c1 = teko_crc32(0,  s1,   6);
    uint32_t c2 = teko_crc32(c1, s2,   5);
    uint32_t cf = teko_crc32(0,  full, 11);
    TEST_ASSERT_EQUAL_HEX32(cf, c2);
}

/* ============================================================================
 * 2. Adler-32
 * ============================================================================ */

void test_teko_adler32_empty(void) {
    /* Adler-32 of empty: init value 1. */
    uint32_t a = teko_adler32(1, NULL, 0);
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, a);
}

void test_teko_adler32_rfc_example(void) {
    /* RFC 1950 §9.1 example: Adler-32("Wikipedia") = 0x11E60398. */
    static const uint8_t wiki[] = "Wikipedia";
    uint32_t a = teko_adler32(1, wiki, 9);
    TEST_ASSERT_EQUAL_HEX32(0x11E60398u, a);
}

void test_teko_adler32_mark_adler_example(void) {
    /* Mark Adler's reference: Adler-32("Mark Adler") = 0x13070394. */
    static const uint8_t s[] = "Mark Adler";
    uint32_t a = teko_adler32(1, s, 10);
    TEST_ASSERT_EQUAL_HEX32(0x13070394u, a);
}

void test_teko_adler32_chained(void) {
    /* Chaining: adler32(1, "ab") == chain adler32(1,"a") → adler32(prev,"b"). */
    static const uint8_t a_b[] = "ab";
    uint32_t c1 = teko_adler32(1, a_b,     1);
    uint32_t c2 = teko_adler32(c1, a_b + 1, 1);
    uint32_t cf = teko_adler32(1,  a_b,     2);
    TEST_ASSERT_EQUAL_HEX32(cf, c2);
}

/* ============================================================================
 * 3. Round-trip (deflate compress → decompress)
 * ============================================================================ */

static void rt_deflate(const uint8_t *data, size_t len, const char *label) {
    uint8_t *comp = NULL;
    size_t   comp_len = 0;
    TekoDeflateStatus st = teko_deflate_compress(data, len, &comp, &comp_len);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TEKO_DEFLATE_OK, st, label);
    TEST_ASSERT_NOT_NULL_MESSAGE(comp, label);

    uint8_t *plain = NULL;
    size_t   plain_len = 0;
    st = teko_deflate_decompress(comp, comp_len, &plain, &plain_len, TEKO_DEFLATE_MAX_OUT);
    free(comp);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TEKO_DEFLATE_OK, st, label);
    TEST_ASSERT_NOT_NULL_MESSAGE(plain, label);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(len, plain_len, label);
    if (len > 0) TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, plain, len, label);
    free(plain);
}

static void rt_zlib(const uint8_t *data, size_t len, const char *label) {
    uint8_t *comp = NULL;
    size_t   comp_len = 0;
    TekoDeflateStatus st = teko_zlib_compress(data, len, &comp, &comp_len);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TEKO_DEFLATE_OK, st, label);
    TEST_ASSERT_NOT_NULL_MESSAGE(comp, label);

    uint8_t *plain = NULL;
    size_t   plain_len = 0;
    st = teko_zlib_decompress(comp, comp_len, &plain, &plain_len, TEKO_DEFLATE_MAX_OUT);
    free(comp);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TEKO_DEFLATE_OK, st, label);
    TEST_ASSERT_NOT_NULL_MESSAGE(plain, label);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(len, plain_len, label);
    if (len > 0) TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, plain, len, label);
    free(plain);
}

static void rt_gzip(const uint8_t *data, size_t len, const char *label) {
    uint8_t *comp = NULL;
    size_t   comp_len = 0;
    TekoDeflateStatus st = teko_gzip_compress(data, len, &comp, &comp_len);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TEKO_DEFLATE_OK, st, label);
    TEST_ASSERT_NOT_NULL_MESSAGE(comp, label);

    uint8_t *plain = NULL;
    size_t   plain_len = 0;
    st = teko_gzip_decompress(comp, comp_len, &plain, &plain_len, TEKO_DEFLATE_MAX_OUT);
    free(comp);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TEKO_DEFLATE_OK, st, label);
    TEST_ASSERT_NOT_NULL_MESSAGE(plain, label);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(len, plain_len, label);
    if (len > 0) TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, plain, len, label);
    free(plain);
}

void test_teko_deflate_roundtrip_empty(void) {
    /* Empty input: must produce a valid (empty) DEFLATE stream and round-trip. */
    rt_deflate(NULL, 0, "deflate/empty");
    static const uint8_t empty[] = "";
    rt_deflate(empty, 0, "deflate/empty2");
}

void test_teko_deflate_roundtrip_single_byte(void) {
    static const uint8_t b[] = { 0x41 };
    rt_deflate(b, 1, "deflate/1byte");
}

void test_teko_deflate_roundtrip_hello(void) {
    static const uint8_t hello[] = "Hello, World!";
    rt_deflate(hello, sizeof(hello) - 1, "deflate/hello");
}

void test_teko_deflate_roundtrip_repetitive(void) {
    /* Highly compressible run: 1024 'a' bytes. LZ77 should compress these well. */
    uint8_t buf[1024];
    memset(buf, 'a', sizeof(buf));
    rt_deflate(buf, sizeof(buf), "deflate/rep1024");
}

void test_teko_deflate_roundtrip_binary(void) {
    /* 256 bytes covering every byte value. */
    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (uint8_t)i;
    rt_deflate(buf, sizeof(buf), "deflate/allbytes");
}

void test_teko_deflate_roundtrip_lorem(void) {
    static const uint8_t lorem[] =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
        "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris.";
    rt_deflate(lorem, sizeof(lorem) - 1, "deflate/lorem");
}

/* zlib round-trip */
void test_teko_zlib_roundtrip_hello(void) {
    static const uint8_t hello[] = "Hello, World!";
    rt_zlib(hello, sizeof(hello) - 1, "zlib/hello");
}

void test_teko_zlib_roundtrip_repetitive(void) {
    uint8_t buf[1024];
    memset(buf, 'Z', sizeof(buf));
    rt_zlib(buf, sizeof(buf), "zlib/rep1024");
}

/* gzip round-trip */
void test_teko_gzip_roundtrip_hello(void) {
    static const uint8_t hello[] = "Hello, World!";
    rt_gzip(hello, sizeof(hello) - 1, "gzip/hello");
}

void test_teko_gzip_roundtrip_repetitive(void) {
    uint8_t buf[1024];
    memset(buf, 'G', sizeof(buf));
    rt_gzip(buf, sizeof(buf), "gzip/rep1024");
}

void test_teko_gzip_roundtrip_binary(void) {
    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (uint8_t)(255 - i);
    rt_gzip(buf, sizeof(buf), "gzip/allbytes");
}

/* ============================================================================
 * 4. Inflate against known byte vectors
 *
 * These vectors were generated with Python's zlib module and are stable
 * RFC-compliant streams.  They exercise the inflate path independently of
 * our compress path.
 * ============================================================================ */

void test_teko_deflate_inflate_known_raw(void) {
    /*
     * Raw DEFLATE of "Hello, World!" generated with:
     *   import zlib, binascii
     *   data = b"Hello, World!"
     *   raw = zlib.compress(data)[2:-4]  # strip 2-byte zlib header and 4-byte trailer
     *   print(list(raw))
     *
     * Python zlib raw deflate of "Hello, World!" (level 9):
     * zlib.decompress(bytes([0x78,0x9c]+raw+[...adler32...]), -15) == b"Hello, World!"
     *
     * The following is the fixed-Huffman encoding of "Hello, World!"
     * produced by RFC-compliant zlib (byte-for-byte reproducible):
     */
    /* zlib.compress(b"Hello, World!", 9) = 78 9c f3 48 cd c9 c9 d7 51 08 cf 2f ca 49 51 04 00
     * 1f 9e 04 6d
     * Raw DEFLATE bytes (between header and adler): f3 48 cd c9 c9 d7 51 08 cf 2f ca 49 51 04 00
     */
    static const uint8_t raw[] = {
        0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0x08,
        0xcf, 0x2f, 0xca, 0x49, 0x51, 0x04, 0x00
    };
    static const uint8_t expected[] = "Hello, World!";
    uint8_t *out = NULL;
    size_t   out_len = 0;
    TekoDeflateStatus st = teko_deflate_decompress(raw, sizeof(raw), &out, &out_len,
                                                    TEKO_DEFLATE_MAX_OUT);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_OK, st);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(sizeof(expected) - 1, out_len);
    TEST_ASSERT_EQUAL_MEMORY(expected, out, out_len);
    free(out);
}

void test_teko_zlib_decompress_known_vector(void) {
    /*
     * Full zlib stream: zlib.compress(b"Hello, World!", 9)
     * Python: list(zlib.compress(b"Hello, World!", 9))
     * = [120, 218, 243, 72, 205, 201, 201, 215, 81, 8, 207, 47, 202, 73, 81, 4, 0, 31, 158, 4, 106]
     * CMF=0x78 (CM=8, CINFO=7), FLG=0xda (FLEVEL=3, fcheck OK: 0x78da % 31 == 0),
     * Adler-32("Hello, World!") = 0x1f9e046a (big-endian trailer).
     */
    static const uint8_t zlib_stream[] = {
        0x78, 0xda, 0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0xd7,
        0x51, 0x08, 0xcf, 0x2f, 0xca, 0x49, 0x51, 0x04,
        0x00, 0x1f, 0x9e, 0x04, 0x6a
    };
    static const uint8_t expected[] = "Hello, World!";
    uint8_t *out = NULL;
    size_t   out_len = 0;
    TekoDeflateStatus st = teko_zlib_decompress(zlib_stream, sizeof(zlib_stream),
                                                 &out, &out_len, TEKO_DEFLATE_MAX_OUT);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_OK, st);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(sizeof(expected) - 1, out_len);
    TEST_ASSERT_EQUAL_MEMORY(expected, out, out_len);
    free(out);
}

void test_teko_gzip_decompress_known_vector(void) {
    /*
     * gzip of b"Hello, World!" with level=9, mtime=0, xfl=2, os=255:
     *   import gzip, io
     *   buf = io.BytesIO(); f = gzip.GzipFile(fileobj=buf, mode='wb', mtime=0); f.write(b"Hello, World!"); f.close()
     *   print(list(buf.getvalue()))
     *
     * Python output (verified):
     * [31, 139, 8, 0, 0, 0, 0, 0, 2, 255,
     *  243, 72, 205, 201, 201, 215, 81, 8, 207, 47, 202, 73, 81, 4, 0,
     *  208, 195, 74, 236, 13, 0, 0, 0]
     * CRC32("Hello, World!") = 0xec4ac3d0 (LE: d0 c3 4a ec)
     * ISIZE = 13 (LE: 0d 00 00 00)
     */
    static const uint8_t gzip_stream[] = {
        /* Header */
        0x1f, 0x8b,                   /* ID1 ID2 */
        0x08,                          /* CM=8 */
        0x00,                          /* FLG=0 */
        0x00, 0x00, 0x00, 0x00,        /* MTIME=0 */
        0x02,                          /* XFL=2 (max compression) */
        0xff,                          /* OS=255 (unknown) */
        /* DEFLATE payload */
        0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0x08,
        0xcf, 0x2f, 0xca, 0x49, 0x51, 0x04, 0x00,
        /* CRC32("Hello, World!") = 0xec4ac3d0 (LE) */
        0xd0, 0xc3, 0x4a, 0xec,
        /* ISIZE = 13 (LE) */
        0x0d, 0x00, 0x00, 0x00
    };
    static const uint8_t expected[] = "Hello, World!";
    uint8_t *out = NULL;
    size_t   out_len = 0;
    TekoDeflateStatus st = teko_gzip_decompress(gzip_stream, sizeof(gzip_stream),
                                                 &out, &out_len, TEKO_DEFLATE_MAX_OUT);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_OK, st);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(sizeof(expected) - 1, out_len);
    TEST_ASSERT_EQUAL_MEMORY(expected, out, out_len);
    free(out);
}

/* ============================================================================
 * 5. Decompression-bomb guard
 *
 * A valid DEFLATE stream that decompresses to more than max_out bytes MUST
 * return TEKO_DEFLATE_BOMB (not OOM, not a partial result, never a crash).
 * The test uses a small max_out cap and a repetitive source that would expand
 * beyond the cap if the guard were absent.
 * ============================================================================ */

void test_teko_deflate_bomb_guard(void) {
    /* Compress 2048 'A' bytes, then attempt to inflate with max_out=512. */
    uint8_t src[2048];
    memset(src, 'A', sizeof(src));

    uint8_t *comp = NULL;
    size_t   comp_len = 0;
    TekoDeflateStatus st = teko_deflate_compress(src, sizeof(src), &comp, &comp_len);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_OK, st);
    TEST_ASSERT_NOT_NULL(comp);

    uint8_t *out = NULL;
    size_t   out_len = 0;
    st = teko_deflate_decompress(comp, comp_len, &out, &out_len, 512);
    free(comp);

    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_BOMB, st);
    /* On BOMB the implementation must NOT return a partial buffer. */
    TEST_ASSERT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(0, out_len);
}

void test_teko_zlib_bomb_guard(void) {
    uint8_t src[2048];
    memset(src, 'B', sizeof(src));

    uint8_t *comp = NULL;
    size_t   comp_len = 0;
    TekoDeflateStatus st = teko_zlib_compress(src, sizeof(src), &comp, &comp_len);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_OK, st);
    TEST_ASSERT_NOT_NULL(comp);

    uint8_t *out = NULL;
    size_t   out_len = 0;
    st = teko_zlib_decompress(comp, comp_len, &out, &out_len, 512);
    free(comp);

    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_BOMB, st);
    TEST_ASSERT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(0, out_len);
}

void test_teko_gzip_bomb_guard(void) {
    uint8_t src[2048];
    memset(src, 'C', sizeof(src));

    uint8_t *comp = NULL;
    size_t   comp_len = 0;
    TekoDeflateStatus st = teko_gzip_compress(src, sizeof(src), &comp, &comp_len);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_OK, st);
    TEST_ASSERT_NOT_NULL(comp);

    uint8_t *out = NULL;
    size_t   out_len = 0;
    st = teko_gzip_decompress(comp, comp_len, &out, &out_len, 512);
    free(comp);

    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_BOMB, st);
    TEST_ASSERT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(0, out_len);
}

void test_teko_deflate_bomb_guard_exact_boundary(void) {
    /* Exactly max_out bytes should succeed; max_out+1 should BOMB. */
    uint8_t src[100];
    memset(src, 'X', sizeof(src));

    uint8_t *comp = NULL;
    size_t   comp_len = 0;
    TekoDeflateStatus st = teko_deflate_compress(src, sizeof(src), &comp, &comp_len);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_OK, st);
    TEST_ASSERT_NOT_NULL(comp);

    /* Exactly 100 bytes must succeed. */
    uint8_t *out = NULL;
    size_t   out_len = 0;
    st = teko_deflate_decompress(comp, comp_len, &out, &out_len, 100);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_OK, st);
    TEST_ASSERT_EQUAL_size_t(100, out_len);
    free(out);

    /* 99 bytes must BOMB. */
    out = NULL; out_len = 0;
    st = teko_deflate_decompress(comp, comp_len, &out, &out_len, 99);
    free(comp);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_BOMB, st);
    TEST_ASSERT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(0, out_len);
}

/* ============================================================================
 * 6. Invalid / corrupt input
 * ============================================================================ */

void test_teko_deflate_corrupt_input(void) {
    /* Random garbage bytes → CORRUPT. */
    static const uint8_t garbage[] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0xFF, 0x00, 0x12, 0x34
    };
    uint8_t *out = NULL;
    size_t   out_len = 0;
    TekoDeflateStatus st = teko_deflate_decompress(garbage, sizeof(garbage),
                                                    &out, &out_len,
                                                    TEKO_DEFLATE_MAX_OUT);
    TEST_ASSERT_NOT_EQUAL(TEKO_DEFLATE_OK, st);
    TEST_ASSERT_NULL(out);
}

void test_teko_zlib_bad_magic(void) {
    /* zlib header fcheck byte deliberately wrong. */
    static const uint8_t bad[] = {
        0x78, 0xFF,   /* CMF=0x78 (valid), FLG=0xFF (fcheck fails: 0x78FF % 31 != 0) */
        0x63, 0x00    /* tiny DEFLATE payload */
    };
    uint8_t *out = NULL;
    size_t   out_len = 0;
    TekoDeflateStatus st = teko_zlib_decompress(bad, sizeof(bad), &out, &out_len,
                                                 TEKO_DEFLATE_MAX_OUT);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_CORRUPT, st);
    TEST_ASSERT_NULL(out);
}

void test_teko_gzip_bad_magic(void) {
    /* Wrong ID1/ID2. */
    static const uint8_t bad[] = {
        0x1f, 0x8C,   /* ID2 wrong: should be 0x8B */
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
        0x03, 0x00,   /* empty DEFLATE block */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    uint8_t *out = NULL;
    size_t   out_len = 0;
    TekoDeflateStatus st = teko_gzip_decompress(bad, sizeof(bad), &out, &out_len,
                                                 TEKO_DEFLATE_MAX_OUT);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_CORRUPT, st);
    TEST_ASSERT_NULL(out);
}

void test_teko_zlib_truncated(void) {
    /* A valid zlib stream, but truncated after 3 bytes. */
    static const uint8_t trunc[] = { 0x78, 0x9c, 0xf3 }; /* 3 bytes only */
    uint8_t *out = NULL;
    size_t   out_len = 0;
    TekoDeflateStatus st = teko_zlib_decompress(trunc, sizeof(trunc), &out, &out_len,
                                                 TEKO_DEFLATE_MAX_OUT);
    TEST_ASSERT_NOT_EQUAL(TEKO_DEFLATE_OK, st);
    TEST_ASSERT_NULL(out);
}

void test_teko_gzip_truncated(void) {
    /* Only 5 bytes (too short for any valid gzip stream). */
    static const uint8_t trunc[] = { 0x1f, 0x8b, 0x08, 0x00, 0x00 };
    uint8_t *out = NULL;
    size_t   out_len = 0;
    TekoDeflateStatus st = teko_gzip_decompress(trunc, sizeof(trunc), &out, &out_len,
                                                 TEKO_DEFLATE_MAX_OUT);
    TEST_ASSERT_NOT_EQUAL(TEKO_DEFLATE_OK, st);
    TEST_ASSERT_NULL(out);
}

/* ============================================================================
 * 7. BADARG (NULL pointers, zero-size where not allowed)
 * ============================================================================ */

void test_teko_deflate_badarg(void) {
    uint8_t *out = NULL;
    size_t   out_len = 0;

    /* NULL dst pointer */
    TekoDeflateStatus st = teko_deflate_compress((const uint8_t*)"x", 1, NULL, &out_len);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_BADARG, st);

    /* NULL dst_len pointer */
    st = teko_deflate_compress((const uint8_t*)"x", 1, &out, NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_BADARG, st);

    /* NULL src with non-zero src_len */
    st = teko_deflate_compress(NULL, 5, &out, &out_len);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_BADARG, st);

    /* Decompress: NULL src */
    st = teko_deflate_decompress(NULL, 4, &out, &out_len, TEKO_DEFLATE_MAX_OUT);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_BADARG, st);

    /* Decompress: NULL dst */
    static const uint8_t dummy[] = { 0x63, 0x00 };
    st = teko_deflate_decompress(dummy, sizeof(dummy), NULL, &out_len, TEKO_DEFLATE_MAX_OUT);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_BADARG, st);

    /* zlib: NULL dst */
    st = teko_zlib_compress((const uint8_t*)"x", 1, NULL, &out_len);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_BADARG, st);

    /* gzip: NULL dst */
    st = teko_gzip_compress((const uint8_t*)"x", 1, NULL, &out_len);
    TEST_ASSERT_EQUAL_INT(TEKO_DEFLATE_BADARG, st);
}
