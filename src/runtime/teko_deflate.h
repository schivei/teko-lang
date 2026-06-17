#ifndef TEKO_DEFLATE_H
#define TEKO_DEFLATE_H

/*
 * Phase 19 (DEFLATE-CORE) — small, portable, freestanding-safe DEFLATE/gzip/zlib codec.
 *
 * This is the SINGLE SOURCE OF TRUTH for teko's optional `native` backend for
 * `compress.deflate`, `compress.gzip`, and `compress.zlib` surfaces (§0.3 backend hierarchy),
 * AND the implementation compiled into the wasm32 runtime reactor (no FFI on WASM — the same
 * pattern as the Phase-13 crypto runtime and every other phase's C source of truth).
 *
 * KAT-tested in the Unity suite (tests/runtime/test_deflate.c). Linked via teko_rt_deflate_*
 * wrappers into native binaries (when added to the runtime); imported from the reactor in WASM.
 *
 * Algorithms:
 *   - CRC32 (ITU-T V.42 / ISO 3309) — used by gzip
 *   - Adler-32 — used by zlib (RFC 1950)
 *   - DEFLATE inflate  (RFC 1951 §3) — fixed + dynamic Huffman, full LZ77 back-reference decode
 *   - DEFLATE deflate  (RFC 1951 §3) — fixed Huffman, greedy LZ77 with a 32 KB hash-chain window
 *   - zlib wrapper     (RFC 1950) — CMF/FLG header + Adler-32 trailer
 *   - gzip wrapper     (RFC 1952) — 10-byte header + CRC32/ISIZE trailer; FNAME/FEXTRA/FCOMMENT
 *                                   fields are skipped on inflate (read-past-end guarded)
 *
 * OP_CALL_RUNTIME id range 120–139 is reserved for the deflate-core surface
 * (compress.deflate/gzip/zlib lowering — frontend wiring is deferred to a later wave):
 *   120 = compress.deflate.compress   (raw DEFLATE)
 *   121 = compress.deflate.decompress (raw DEFLATE)
 *   122 = compress.zlib.compress      (zlib wrapper)
 *   123 = compress.zlib.decompress    (zlib wrapper)
 *   124 = compress.gzip.compress      (gzip wrapper)
 *   125 = compress.gzip.decompress    (gzip wrapper)
 *   126–139 = reserved (extended compress surface, future)
 * No emission/frontend wiring exists yet; these ids are commented only.
 *
 * SECURITY (attacker-controlled inflate input):
 *   - Every inflate path enforces a caller-supplied output byte cap; if the decompressed
 *     output would exceed the cap, inflate returns TEKO_DEFLATE_BOMB (decompression-bomb
 *     guard — fail loud, never OOM).
 *   - The 32 KB sliding window is a fixed-size stack/heap buffer; every back-reference
 *     distance and length is bounds-checked before the copy loop.
 *   - All length/distance arithmetic uses size_t with explicit overflow checks before
 *     pointer arithmetic; no narrowing without a guard.
 *   - Invalid Huffman codes, bad block headers, truncated input, and format errors all
 *     produce TEKO_DEFLATE_CORRUPT — not undefined behavior.
 *   - calloc is used for heap allocations; NULL returns are checked and produce
 *     TEKO_DEFLATE_OOM.
 *
 * Freestanding / MSVC portability:
 *   - No libc beyond <stdint.h> / <stddef.h> / <string.h> / <stdlib.h>
 *     (the wasm32 reactor's libc_shim.c provides malloc/calloc/free/memset/memcpy/memmove).
 *   - No computed-goto, no C23 auto/nullptr, no __attribute__((packed)).
 *   - Signed/unsigned arithmetic uses explicit casts with range guards.
 */

#include <stddef.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * Return codes
 * -------------------------------------------------------------------------- */
typedef enum {
    TEKO_DEFLATE_OK      =  0, /* success                                      */
    TEKO_DEFLATE_OOM     = -1, /* malloc/calloc returned NULL                  */
    TEKO_DEFLATE_CORRUPT = -2, /* invalid input: bad magic, Huffman, distance  */
    TEKO_DEFLATE_BOMB    = -3, /* decompression-bomb: output would exceed cap  */
    TEKO_DEFLATE_BADARG  = -4  /* NULL pointer or zero-size where not allowed  */
} TekoDeflateStatus;

/* --------------------------------------------------------------------------
 * Decompression-bomb output cap
 *
 * Default maximum decompressed bytes per call (256 MiB).  Callers that need a
 * different limit should call the _ex (extended) variants (reserved for future
 * surface wiring); for now the cap is a compile-time constant the caller can
 * override by defining TEKO_DEFLATE_MAX_OUT before including this header.
 * -------------------------------------------------------------------------- */
#ifndef TEKO_DEFLATE_MAX_OUT
#  define TEKO_DEFLATE_MAX_OUT ((size_t)256 * 1024 * 1024)
#endif

/* --------------------------------------------------------------------------
 * CRC32 / Adler-32 checksums
 * -------------------------------------------------------------------------- */

/* CRC32 (ITU-T V.42 / ISO 3309 / gzip).
 * Accepts any buf (including NULL when len==0); init with crc=0 for a fresh
 * digest; chain by passing the previous return value as crc. */
uint32_t teko_crc32(uint32_t crc, const uint8_t *buf, size_t len);

/* Adler-32 (RFC 1950 §9 / zlib).
 * Accepts any buf (including NULL when len==0); init with adler=1 for a fresh
 * digest; chain by passing the previous return value as adler. */
uint32_t teko_adler32(uint32_t adler, const uint8_t *buf, size_t len);

/* --------------------------------------------------------------------------
 * Raw DEFLATE (RFC 1951) — no wrapper header or trailer
 * -------------------------------------------------------------------------- */

/*
 * Compress `src[0..src_len)` using DEFLATE (fixed-Huffman blocks, greedy LZ77).
 * Allocates `*dst` with calloc; caller must free(*dst) on OK.
 * Returns TEKO_DEFLATE_OK and writes the output length to *dst_len on success.
 * Returns TEKO_DEFLATE_OOM, TEKO_DEFLATE_CORRUPT, or TEKO_DEFLATE_BADARG on
 * failure; *dst is NULL and *dst_len is 0.
 */
TekoDeflateStatus teko_deflate_compress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len);

/*
 * Decompress raw DEFLATE stream `src[0..src_len)`.
 * Allocates `*dst` with calloc; caller must free(*dst) on OK.
 * Returns TEKO_DEFLATE_OK and writes the output length to *dst_len on success.
 * Returns TEKO_DEFLATE_BOMB if decompressed output would exceed max_out bytes
 * (decompression-bomb guard; pass TEKO_DEFLATE_MAX_OUT for the default cap).
 * Returns TEKO_DEFLATE_CORRUPT on malformed input (bad Huffman tree, invalid
 * back-reference distance/length, truncated stream, etc.).
 * Returns TEKO_DEFLATE_OOM, or TEKO_DEFLATE_BADARG on NULL/zero args.
 */
TekoDeflateStatus teko_deflate_decompress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len,
    size_t max_out);

/* --------------------------------------------------------------------------
 * zlib (RFC 1950) — CMF/FLG header + Adler-32 trailer
 * -------------------------------------------------------------------------- */

/*
 * Compress `src[0..src_len)` as a zlib stream (CM=8, CINFO=7, FLEVEL=2).
 * Allocates `*dst`; caller must free(*dst) on OK.
 */
TekoDeflateStatus teko_zlib_compress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len);

/*
 * Decompress a zlib stream `src[0..src_len)`.
 * Verifies the CMF/FLG header check (fcheck) and the trailing Adler-32.
 * Returns TEKO_DEFLATE_CORRUPT if either check fails.
 * max_out is the decompression-bomb cap (pass TEKO_DEFLATE_MAX_OUT).
 */
TekoDeflateStatus teko_zlib_decompress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len,
    size_t max_out);

/* --------------------------------------------------------------------------
 * gzip (RFC 1952) — 10-byte header + CRC32/ISIZE trailer
 * -------------------------------------------------------------------------- */

/*
 * Compress `src[0..src_len)` as a gzip stream.
 * Writes a minimal header (ID1/ID2/CM/FLG=0/MTIME=0/XFL=0/OS=255).
 * Allocates `*dst`; caller must free(*dst) on OK.
 */
TekoDeflateStatus teko_gzip_compress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len);

/*
 * Decompress a gzip stream `src[0..src_len)`.
 * Verifies ID1/ID2/CM magic; skips FNAME/FEXTRA/FCOMMENT optional fields with
 * bounds guards; verifies the trailing CRC32 and ISIZE (lower 32 bits).
 * Returns TEKO_DEFLATE_CORRUPT on any header/trailer mismatch.
 * max_out is the decompression-bomb cap (pass TEKO_DEFLATE_MAX_OUT).
 */
TekoDeflateStatus teko_gzip_decompress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len,
    size_t max_out);

#endif /* TEKO_DEFLATE_H */
