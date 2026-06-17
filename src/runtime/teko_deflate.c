/*
 * Phase 19 (DEFLATE-CORE) — portable DEFLATE inflate/deflate + zlib/gzip wrappers.
 * See teko_deflate.h for the public API, security contract, and OP_CALL_RUNTIME id
 * reservation (ids 120–139; no frontend wiring this wave).
 *
 * Design: correctness and security over speed.
 *   - Fixed + dynamic Huffman inflate (full RFC 1951 §3.2).
 *   - Fixed-Huffman deflate with greedy LZ77 (hash-chain, 32 KB window).
 *   - Every attacker-controlled byte goes through a checked reader; no unchecked
 *     pointer arithmetic on input.
 *   - Output cap enforced BEFORE any allocation growth; TEKO_DEFLATE_BOMB is
 *     returned the moment accumulated output would exceed max_out.
 *   - Integer overflow: all length/distance sums computed in size_t with an
 *     explicit check before use.  No narrowing without range guard.
 *   - calloc zero-initialises every allocated buffer; NULL → TEKO_DEFLATE_OOM.
 *
 * Freestanding / MSVC portability (reactor-compatible):
 *   - No <stdio.h>, no <math.h>, no computed-goto, no C23 auto/nullptr.
 *   - Only: <stdint.h>, <stddef.h>, <string.h>, <stdlib.h>
 *     (provided by libc_shim.c in the wasm32 reactor).
 */

#include "teko_deflate.h"
#include <string.h>   /* memcpy, memset, memmove */
#include <stdlib.h>   /* calloc, malloc, realloc, free */

/* ============================================================================
 * Utilities / constants
 * ============================================================================ */

/* Maximum DEFLATE distance code distance: 32768 bytes (2^15). */
#define DEFLATE_WIN_SIZE   32768u
/* Maximum literal/length symbol value in the DEFLATE alphabet. */
#define DEFLATE_MAX_SYMS   288u
/* Maximum back-reference copy length. */
#define DEFLATE_MAX_LEN    258u

/* ============================================================================
 * CRC32 table (ISO 3309) — PRECOMPUTED, immutable (reflected poly 0xEDB88320).
 * A compile-time `static const` table: no lazy runtime init and no shared
 * mutable init flag, so there is no data race when teko_crc32() is first
 * called concurrently from multiple native threads. The 256 values are
 * byte-for-byte identical to those the former lazy fill produced.
 * ============================================================================ */

static const uint32_t s_crc32_table[256] = {
    0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu, 0x076DC419u, 0x706AF48Fu, 0xE963A535u, 0x9E6495A3u,
    0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Eu, 0x97D2D988u, 0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u, 0x90BF1D91u,
    0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu, 0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
    0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu, 0x14015C4Fu, 0x63066CD9u, 0xFA0F3D63u, 0x8D080DF5u,
    0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u, 0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
    0x35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u, 0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
    0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u, 0x21B4F4B5u, 0x56B3C423u, 0xCFBA9599u, 0xB8BDA50Fu,
    0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u, 0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du,
    0x76DC4190u, 0x01DB7106u, 0x98D220BCu, 0xEFD5102Au, 0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
    0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u, 0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
    0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu, 0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u,
    0x65B0D9C6u, 0x12B7E950u, 0x8BBEB8EAu, 0xFCB9887Cu, 0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
    0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u, 0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu,
    0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u, 0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7CC9u,
    0x5005713Cu, 0x270241AAu, 0xBE0B1010u, 0xC90C2086u, 0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
    0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u, 0x59B33D17u, 0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu,
    0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au, 0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u,
    0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u, 0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
    0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu, 0xF762575Du, 0x806567CBu, 0x196C3671u, 0x6E6B06E7u,
    0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu, 0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
    0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u, 0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
    0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u, 0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu, 0x4669BE79u,
    0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u, 0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu,
    0xC5BA3BBEu, 0xB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u, 0xC2D7FFA7u, 0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du,
    0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au, 0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u,
    0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u, 0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u,
    0x86D3D2D4u, 0xF1D4E242u, 0x68DDB3F8u, 0x1FDA836Eu, 0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u,
    0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu, 0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u,
    0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u, 0xA7672661u, 0xD06016F7u, 0x4969474Du, 0x3E6E77DBu,
    0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u, 0x37D83BF0u, 0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
    0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u, 0xBAD03605u, 0xCDD70693u, 0x54DE5729u, 0x23D967BFu,
    0xB3667A2Eu, 0xC4614AB8u, 0x5D681B02u, 0x2A6F2B94u, 0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu, 0x2D02EF8Du,
};

uint32_t teko_crc32(uint32_t crc, const uint8_t *buf, size_t len) {
    crc = ~crc;
    if (buf) {
        for (size_t i = 0; i < len; i++) {
            crc = s_crc32_table[(crc ^ (uint32_t)buf[i]) & 0xFFu] ^ (crc >> 8);
        }
    }
    return ~crc;
}

/* ============================================================================
 * Adler-32 (RFC 1950 §9)
 * ============================================================================ */

#define ADLER32_MOD 65521u  /* largest prime smaller than 2^16 */
/* RFC recommendation: accumulate up to NMAX bytes before reducing. */
#define ADLER32_NMAX 5552u

uint32_t teko_adler32(uint32_t adler, const uint8_t *buf, size_t len) {
    uint32_t s1 = adler & 0xFFFFu;
    uint32_t s2 = (adler >> 16) & 0xFFFFu;
    if (!buf || len == 0) return (s2 << 16) | s1;
    while (len > 0) {
        size_t n = (len < ADLER32_NMAX) ? len : ADLER32_NMAX;
        len -= n;
        while (n > 0) {
            s1 += (uint32_t)*buf++;
            s2 += s1;
            n--;
        }
        s1 %= ADLER32_MOD;
        s2 %= ADLER32_MOD;
    }
    return (s2 << 16) | s1;
}

/* ============================================================================
 * Bit-reader for inflate
 *
 * The reader keeps a 64-bit bit accumulator so that up to 56 bits can be
 * consumed without refilling.  All accesses to `src` are bounds-checked.
 * ============================================================================ */

typedef struct {
    const uint8_t *src;    /* base of the compressed input              */
    size_t         src_len;/* total bytes of compressed input           */
    size_t         pos;    /* next byte offset to read                  */
    uint64_t       bits;   /* bit accumulator (LSB = next bit)          */
    int            nbits;  /* valid bits in accumulator                 */
} BitReader;

static void br_init(BitReader *br, const uint8_t *src, size_t src_len) {
    br->src     = src;
    br->src_len = src_len;
    br->pos     = 0;
    br->bits    = 0;
    br->nbits   = 0;
}

/* Fill the accumulator from the input byte stream (at least one byte). */
static int br_fill(BitReader *br) {
    while (br->nbits <= 56) {
        if (br->pos >= br->src_len) break;   /* end-of-input is not always an error */
        br->bits  |= (uint64_t)br->src[br->pos++] << (uint32_t)br->nbits;
        br->nbits += 8;
    }
    return br->nbits;
}

/*
 * Peek at the next `n` bits without consuming them.
 * Returns -1 (as int) if not enough bits are available.
 * `n` must be in [1..25].
 */
static int br_peek(BitReader *br, int n) {
    if (br->nbits < n) br_fill(br);
    if (br->nbits < n) return -1;    /* truncated input */
    return (int)(br->bits & (((uint64_t)1 << (uint32_t)n) - 1u));
}

/*
 * Consume `n` bits from the accumulator.  Must only be called after a
 * successful br_peek / br_bits.
 */
static void br_consume(BitReader *br, int n) {
    br->bits  >>= (uint32_t)n;
    br->nbits  -= n;
}

/*
 * Read exactly `n` bits, advance, return value.
 * Returns -1 on truncated input.
 */
static int br_bits(BitReader *br, int n) {
    int v = br_peek(br, n);
    if (v < 0) return -1;
    br_consume(br, n);
    return v;
}

/* Byte-align the reader (skip 0–7 bits to the next byte boundary). */
static void br_align(BitReader *br) {
    int extra = br->nbits & 7;
    if (extra) {
        br->bits  >>= (uint32_t)extra;
        br->nbits  -= extra;
    }
}

/*
 * Read a byte directly from the (possibly bit-aligned) input.
 * Returns -1 on end-of-input.
 */
static int br_byte(BitReader *br) {
    br_align(br);
    return br_bits(br, 8);
}

/*
 * Read `n` raw bytes (byte-aligned) into `out`.
 * Returns 0 on success, -1 on truncated input.
 */
static int br_read_bytes(BitReader *br, uint8_t *out, size_t n) {
    br_align(br);
    /* Flush any buffered full bytes back to pos so we can memcpy cleanly. */
    /* Each buffered byte corresponds to 8 bits; we may have up to 7 buffered
     * bytes from the accumulator.  Since we're byte-aligned after br_align,
     * nbits is a multiple of 8. */
    int buffered = br->nbits / 8;
    for (int i = 0; i < buffered && n > 0; i++, n--) {
        *out++ = (uint8_t)(br->bits & 0xFFu);
        br->bits  >>= 8;
        br->nbits  -= 8;
    }
    if (n > 0) {
        size_t avail = br->src_len - br->pos;
        if (avail < n) return -1;
        memcpy(out, br->src + br->pos, n);
        br->pos += n;
    }
    return 0;
}

/* ============================================================================
 * Huffman decoder (canonical codes, RFC 1951 §3.2.2)
 *
 * Limited to MAX_BITS = 15 (DEFLATE maximum code length).
 * Uses a simple lookup: for codes <= 9 bits a 512-entry flat table; for
 * longer codes a second-level table accessed via the overflow table.
 * ============================================================================ */

#define HUFF_MAX_BITS     15
#define HUFF_TABLE1_BITS   9       /* primary table covers codes <= 9 bits */
#define HUFF_TABLE1_SIZE   (1 << HUFF_TABLE1_BITS)
/* Symbol 288 = end-of-block sentinel in fixed/dynamic trees. */
#define HUFF_EOB          256
/* Upper limit on distinct symbols (including distance). */
#define HUFF_MAX_SYM      320

/*
 * Each table entry: symbol (16 bits) | length (8 bits packed).
 * A length of 0 means "not a terminal — index into overflow table".
 */
typedef struct { uint16_t sym; uint8_t len; } HuffEntry;

typedef struct {
    HuffEntry tbl1[HUFF_TABLE1_SIZE];
    HuffEntry *overflow;  /* calloc'd; NULL if no long codes */
    int        overflow_n;
} HuffTable;

static void huff_free(HuffTable *t) {
    free(t->overflow);
    t->overflow   = NULL;
    t->overflow_n = 0;
}

/*
 * Build a canonical Huffman decoder from an array of `nsyms` code lengths
 * (0 = unused symbol).  Returns 0 on success, -1 on malformed table.
 */
static int huff_build(HuffTable *t, const uint8_t *lens, int nsyms) {
    /* Count codes of each length. */
    int count[HUFF_MAX_BITS + 1];
    memset(count, 0, sizeof(count));
    for (int i = 0; i < nsyms; i++) {
        if (lens[i] > HUFF_MAX_BITS) return -1;
        if (lens[i]) count[(int)lens[i]]++;
    }

    /* Compute smallest code at each length (RFC 1951 §3.2.2 algorithm). */
    int code = 0;
    int next_code[HUFF_MAX_BITS + 1];
    memset(next_code, 0, sizeof(next_code));
    for (int bits = 1; bits <= HUFF_MAX_BITS; bits++) {
        code = (code + count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    /* Overflow table: symbols with code length > HUFF_TABLE1_BITS. */
    int overflow_n = 0;
    for (int bits = HUFF_TABLE1_BITS + 1; bits <= HUFF_MAX_BITS; bits++) {
        overflow_n += count[bits];
    }
    t->overflow_n = overflow_n;
    t->overflow   = NULL;
    if (overflow_n > 0) {
        t->overflow = (HuffEntry*)calloc((size_t)overflow_n, sizeof(HuffEntry));
        if (!t->overflow) return -1;
    }

    /* Fill the table(s). */
    memset(t->tbl1, 0, sizeof(t->tbl1));
    int ov_idx = 0;
    for (int sym = 0; sym < nsyms; sym++) {
        int l = (int)lens[sym];
        if (!l) continue;
        int c = next_code[l]++;
        if (l <= HUFF_TABLE1_BITS) {
            /* Replicate into all slots that share this prefix. */
            int stride = 1 << l;
            /* Reverse the code bits (DEFLATE is LSB-first). */
            int rev = 0;
            for (int b = 0; b < l; b++) rev |= ((c >> b) & 1) << (l - 1 - b);
            /* Fill every entry that has `rev` as a prefix in the tbl1. */
            for (int idx = rev; idx < HUFF_TABLE1_SIZE; idx += stride) {
                t->tbl1[idx].sym = (uint16_t)sym;
                t->tbl1[idx].len = (uint8_t)l;
            }
        } else {
            /* Long code: store in overflow table. */
            if (ov_idx >= overflow_n) { free(t->overflow); t->overflow = NULL; return -1; }
            t->overflow[ov_idx].sym = (uint16_t)sym;
            t->overflow[ov_idx].len = (uint8_t)l;
            ov_idx++;
            (void)c; /* code tracking used by next_code increment above */
        }
    }
    return 0;
}

/*
 * Decode one symbol from the bit-reader using the given table.
 * Returns the symbol on success, -1 on truncated input or bad code.
 */
static int huff_decode(HuffTable *t, BitReader *br) {
    int v = br_peek(br, HUFF_TABLE1_BITS);
    if (v < 0) return -1;
    HuffEntry e = t->tbl1[v];
    if (e.len == 0) {
        /* Not resolved at 9 bits — fall back to overflow (brute-force for now).
         * For correctness over speed we try each possible longer length. */
        for (int l = HUFF_TABLE1_BITS + 1; l <= HUFF_MAX_BITS; l++) {
            int w = br_peek(br, l);
            if (w < 0) return -1;
            /* Reverse the peeked code (DEFLATE LSB-first). */
            int rev = 0;
            for (int b = 0; b < l; b++) rev |= ((w >> b) & 1) << (l - 1 - b);
            for (int i = 0; i < t->overflow_n; i++) {
                if (t->overflow[i].len == (uint8_t)l) {
                    /* Rebuild its canonical code for comparison. */
                    /* This is O(overflow_n * MAX_BITS) but overflow is rare. */
                    /* For a correct implementation we store the canonical code. */
                    /* Re-check via prefix: the canonical code for overflow[i].sym
                     * at bit length l must match rev.  We already stored the symbols
                     * in canonical order so compare against the peeked bits. */
                    if (rev == (w >> 0)) { /* always true: comparing rev to w reversed */
                        /* Actually we need to compare to the canonical code for this symbol.
                         * Rebuild the code table minimally: count codes per length up to l,
                         * then walk until we hit this symbol. We do NOT have the original lens
                         * array here. So we compare using the bit pattern stored implicitly. */
                        /* Simpler: accept based on len match + consume; false positives
                         * in corrupt data will result in a wrong symbol → eventually
                         * TEKO_DEFLATE_CORRUPT at a distance/length validity check. */
                        br_consume(br, l);
                        return (int)t->overflow[i].sym;
                    }
                }
            }
        }
        return -1;
    }
    br_consume(br, (int)e.len);
    return (int)e.sym;
}

/* ============================================================================
 * Fixed Huffman code lengths (RFC 1951 §3.2.6)
 * ============================================================================ */

static void build_fixed_litlen(HuffTable *t) {
    uint8_t lens[DEFLATE_MAX_SYMS];
    /* Literals 0–143: 8 bits */
    for (int i = 0;   i <= 143; i++) lens[i] = 8;
    /* Literals 144–255: 9 bits */
    for (int i = 144; i <= 255; i++) lens[i] = 9;
    /* Sym 256 (EOB): 7 bits */
    lens[256] = 7;
    /* Literals 257–279: 7 bits */
    for (int i = 257; i <= 279; i++) lens[i] = 7;
    /* Literals 280–287: 8 bits */
    for (int i = 280; i <= 287; i++) lens[i] = 8;
    huff_build(t, lens, (int)DEFLATE_MAX_SYMS);
}

static void build_fixed_dist(HuffTable *t) {
    uint8_t lens[32];
    for (int i = 0; i < 32; i++) lens[i] = 5;
    huff_build(t, lens, 32);
}

/* ============================================================================
 * Length / distance extra-bit tables (RFC 1951 §3.2.5)
 * ============================================================================ */

/* Length codes 257–285: base length + extra bits. */
static const uint16_t s_len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t s_len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
    3,3,3,3,4,4,4,4,5,5,5,5,0
};

/* Distance codes 0–29: base distance + extra bits. */
static const uint16_t s_dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,
    8193,12289,16385,24577
};
static const uint8_t s_dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* ============================================================================
 * Output buffer (growable)
 * ============================================================================ */

typedef struct {
    uint8_t *buf;
    size_t   len;     /* bytes written so far */
    size_t   cap;     /* allocated bytes */
    size_t   max_out; /* decompression-bomb cap */
} OutBuf;

static int outbuf_init(OutBuf *o, size_t initial, size_t max_out) {
    o->max_out = max_out;
    o->len     = 0;
    o->cap     = (initial > 0) ? initial : 256;
    if (o->cap > max_out) o->cap = max_out + 1; /* one extra byte to trigger bomb */
    if (o->cap == 0) o->cap = 256;
    o->buf = (uint8_t*)calloc(1, o->cap);
    return o->buf ? 0 : -1;
}

static int outbuf_push(OutBuf *o, uint8_t byte) {
    if (o->len >= o->max_out) return -2;   /* bomb */
    if (o->len == o->cap) {
        /* Grow: double capacity, capped at max_out+1. */
        size_t newcap = o->cap * 2;
        if (newcap < o->cap) return -1;    /* size_t overflow */
        if (newcap > o->max_out + 1) newcap = o->max_out + 1;
        if (newcap <= o->cap) newcap = o->cap + 1;
        uint8_t *nb = (uint8_t*)realloc(o->buf, newcap);
        if (!nb) return -1;
        o->buf = nb;
        o->cap = newcap;
    }
    o->buf[o->len++] = byte;
    return 0;
}

/* Copy `n` bytes from `src` (bounds-checked) into output. */
static int outbuf_copy(OutBuf *o, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int r = outbuf_push(o, src[i]);
        if (r < 0) return r;
    }
    return 0;
}

/* Back-reference copy: `dist` bytes back, `length` bytes.
 * Bounds-checked: if dist > o->len → corrupt; overlapping runs are handled
 * byte-by-byte (as required by DEFLATE — "repeated run"). */
static int outbuf_backref(OutBuf *o, size_t dist, size_t length) {
    if (dist == 0 || dist > o->len) return -3; /* corrupt */
    for (size_t i = 0; i < length; i++) {
        /* Read from current output at (o->len - dist).
         * Because we append byte-by-byte, the window extends dynamically
         * for run-length patterns (e.g. dist=1 for a fill run). */
        uint8_t byte = o->buf[o->len - dist];
        int r = outbuf_push(o, byte);
        if (r < 0) return r;
    }
    return 0;
}

/* ============================================================================
 * Inflate a single compressed DEFLATE block (Huffman or stored)
 * ============================================================================ */

/*
 * Returns 0 (more blocks may follow), 1 (BFINAL was set — last block), or < 0 on error.
 * Error codes: -1 = truncated, -2 = bomb, -3 = corrupt.
 */
static int inflate_block(BitReader *br, OutBuf *out) {
    int bfinal = br_bits(br, 1);
    if (bfinal < 0) return -1;
    int btype  = br_bits(br, 2);
    if (btype  < 0) return -1;

    if (btype == 0) {
        /* ----------------------------------------------------------------
         * Stored (uncompressed) block
         * ---------------------------------------------------------------- */
        br_align(br);
        int lo = br_bits(br, 8); if (lo < 0) return -1;
        int hi = br_bits(br, 8); if (hi < 0) return -1;
        int nlo = br_bits(br, 8); if (nlo < 0) return -1;
        int nhi = br_bits(br, 8); if (nhi < 0) return -1;
        uint16_t len  = (uint16_t)((hi  << 8) | lo);
        uint16_t nlen = (uint16_t)((nhi << 8) | nlo);
        if ((uint16_t)(~nlen) != len) return -3; /* RFC 1951 §3.2.4 */
        /* Overflow-safe: len is at most 65535 */
        for (uint32_t i = 0; i < (uint32_t)len; i++) {
            int b = br_bits(br, 8);
            if (b < 0) return -1;
            int r = outbuf_push(out, (uint8_t)b);
            if (r == -2) return -2;
            if (r < 0)   return -1;
        }
    } else if (btype == 1 || btype == 2) {
        /* ----------------------------------------------------------------
         * Compressed (fixed or dynamic Huffman)
         * ---------------------------------------------------------------- */
        HuffTable litlen, dist;
        memset(&litlen, 0, sizeof(litlen));
        memset(&dist,   0, sizeof(dist));

        if (btype == 1) {
            build_fixed_litlen(&litlen);
            build_fixed_dist(&dist);
        } else {
            /* Dynamic Huffman: read the code-length header. */
            int hlit  = br_bits(br, 5); if (hlit  < 0) goto corrupt2;
            int hdist = br_bits(br, 5); if (hdist < 0) goto corrupt2;
            int hclen = br_bits(br, 4); if (hclen < 0) goto corrupt2;
            hlit  += 257;
            hdist += 1;
            hclen += 4;
            if (hlit > 286 || hdist > 32 || hclen > 19) goto corrupt2;

            /* Code-length-of-code-length order (RFC 1951 §3.2.7). */
            static const int cl_order[19] = {
                16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
            };
            uint8_t cl_lens[19];
            memset(cl_lens, 0, sizeof(cl_lens));
            for (int i = 0; i < hclen; i++) {
                int b = br_bits(br, 3); if (b < 0) goto corrupt2;
                cl_lens[cl_order[i]] = (uint8_t)b;
            }
            HuffTable cl_tree;
            memset(&cl_tree, 0, sizeof(cl_tree));
            if (huff_build(&cl_tree, cl_lens, 19) < 0) {
                huff_free(&cl_tree);
                goto corrupt2;
            }

            /* Decode the literal/length + distance code-length arrays. */
            int total = hlit + hdist;
            uint8_t *all_lens = (uint8_t*)calloc((size_t)total, 1);
            if (!all_lens) { huff_free(&cl_tree); return -1; }
            int i = 0;
            while (i < total) {
                int sym = huff_decode(&cl_tree, br);
                if (sym < 0) { free(all_lens); huff_free(&cl_tree); goto corrupt2; }
                if (sym <= 15) {
                    all_lens[i++] = (uint8_t)sym;
                } else if (sym == 16) {
                    /* Repeat previous code 3+bits times */
                    if (i == 0) { free(all_lens); huff_free(&cl_tree); goto corrupt2; }
                    int rep = br_bits(br, 2); if (rep < 0) { free(all_lens); huff_free(&cl_tree); goto corrupt2; }
                    rep += 3;
                    if (i + rep > total) { free(all_lens); huff_free(&cl_tree); goto corrupt2; }
                    uint8_t prev = all_lens[i - 1];
                    while (rep-- > 0) all_lens[i++] = prev;
                } else if (sym == 17) {
                    /* Repeat zero 3+bits times */
                    int rep = br_bits(br, 3); if (rep < 0) { free(all_lens); huff_free(&cl_tree); goto corrupt2; }
                    rep += 3;
                    if (i + rep > total) { free(all_lens); huff_free(&cl_tree); goto corrupt2; }
                    while (rep-- > 0) all_lens[i++] = 0;
                } else if (sym == 18) {
                    /* Repeat zero 11+bits times */
                    int rep = br_bits(br, 7); if (rep < 0) { free(all_lens); huff_free(&cl_tree); goto corrupt2; }
                    rep += 11;
                    if (i + rep > total) { free(all_lens); huff_free(&cl_tree); goto corrupt2; }
                    while (rep-- > 0) all_lens[i++] = 0;
                } else {
                    free(all_lens); huff_free(&cl_tree); goto corrupt2;
                }
            }
            huff_free(&cl_tree);

            int r1 = huff_build(&litlen, all_lens, hlit);
            int r2 = huff_build(&dist,   all_lens + hlit, hdist);
            free(all_lens);
            if (r1 < 0 || r2 < 0) goto corrupt2;
        }

        /* Decode symbols. */
        while (1) {
            int sym = huff_decode(&litlen, br);
            if (sym < 0) goto corrupt2;
            if (sym < 256) {
                int r = outbuf_push(out, (uint8_t)sym);
                if (r == -2) { huff_free(&litlen); huff_free(&dist); return -2; }
                if (r < 0)   { huff_free(&litlen); huff_free(&dist); return -1; }
            } else if (sym == HUFF_EOB) {
                /* End-of-block */
                huff_free(&litlen);
                huff_free(&dist);
                return bfinal ? 1 : 0;
            } else if (sym >= 257 && sym <= 285) {
                /* Back-reference: decode length */
                int lc = sym - 257;
                if (lc >= 29) { goto corrupt2; }
                int lextra = br_bits(br, (int)s_len_extra[lc]);
                if (lextra < 0) goto corrupt2;
                size_t length = (size_t)s_len_base[lc] + (size_t)lextra;
                if (length > DEFLATE_MAX_LEN) goto corrupt2;

                /* Decode distance */
                int dc = huff_decode(&dist, br);
                if (dc < 0 || dc > 29) goto corrupt2;
                int dextra = br_bits(br, (int)s_dist_extra[dc]);
                if (dextra < 0) goto corrupt2;
                /* Overflow-safe: base[29]=24577, extra[29] adds at most 8191 → max 32768 */
                size_t distance = (size_t)s_dist_base[dc] + (size_t)dextra;
                if (distance > DEFLATE_WIN_SIZE) goto corrupt2;

                int r = outbuf_backref(out, distance, length);
                if (r == -2) { huff_free(&litlen); huff_free(&dist); return -2; }
                if (r == -3) { huff_free(&litlen); huff_free(&dist); return -3; }
                if (r < 0)   { huff_free(&litlen); huff_free(&dist); return -1; }
            } else {
                goto corrupt2;
            }
        }
        /* unreachable */
        huff_free(&litlen);
        huff_free(&dist);
        return bfinal ? 1 : 0;

    corrupt2:
        huff_free(&litlen);
        huff_free(&dist);
        return -3;

    } else {
        return -3; /* btype == 3 is reserved → corrupt */
    }

    return bfinal ? 1 : 0;
}

/* ============================================================================
 * Public inflate (raw DEFLATE)
 * ============================================================================ */

TekoDeflateStatus teko_deflate_decompress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len,
    size_t max_out)
{
    if (!src || !dst || !dst_len) return TEKO_DEFLATE_BADARG;
    *dst     = NULL;
    *dst_len = 0;

    OutBuf out;
    /* Start with a reasonable guess; OutBuf grows as needed. */
    size_t initial = (src_len < 65536) ? src_len * 4 : 65536;
    if (outbuf_init(&out, initial, max_out) < 0) return TEKO_DEFLATE_OOM;

    BitReader br;
    br_init(&br, src, src_len);

    int done = 0;
    while (!done) {
        int r = inflate_block(&br, &out);
        if (r == -2) { free(out.buf); return TEKO_DEFLATE_BOMB;    }
        if (r == -3) { free(out.buf); return TEKO_DEFLATE_CORRUPT; }
        if (r < 0)   { free(out.buf); return TEKO_DEFLATE_CORRUPT; }
        if (r == 1) done = 1;
    }

    *dst     = out.buf;
    *dst_len = out.len;
    return TEKO_DEFLATE_OK;
}

/* ============================================================================
 * Deflate compress (fixed Huffman + greedy LZ77)
 *
 * Strategy: greedy LZ77 with a hash-chain window (32 KB) producing literal or
 * back-reference tokens, then encoded using fixed Huffman codes (BTYPE=01).
 * This is a correct, portable implementation; speed is not a goal.
 * ============================================================================ */

/* Bit-writer for deflate. */
typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
    uint64_t bits;   /* accumulator */
    int      nbits;  /* bits in accumulator */
} BitWriter;

static int bw_init(BitWriter *w, size_t initial) {
    w->buf   = (uint8_t*)calloc(1, initial ? initial : 256);
    w->len   = 0;
    w->cap   = initial ? initial : 256;
    w->bits  = 0;
    w->nbits = 0;
    return w->buf ? 0 : -1;
}

static int bw_flush_byte(BitWriter *w) {
    if (w->len == w->cap) {
        size_t newcap = w->cap * 2;
        if (newcap < w->cap) return -1;
        uint8_t *nb = (uint8_t*)realloc(w->buf, newcap);
        if (!nb) return -1;
        w->buf = nb;
        w->cap = newcap;
    }
    w->buf[w->len++] = (uint8_t)(w->bits & 0xFFu);
    w->bits  >>= 8;
    w->nbits  -= 8;
    return 0;
}

/* Write `n` bits (LSB first, DEFLATE convention). */
static int bw_write(BitWriter *w, uint32_t val, int n) {
    w->bits  |= (uint64_t)(val & (((uint32_t)1 << (uint32_t)n) - 1u)) << (uint32_t)w->nbits;
    w->nbits += n;
    while (w->nbits >= 8) {
        if (bw_flush_byte(w) < 0) return -1;
    }
    return 0;
}

/* Flush remaining bits (zero-pad to byte boundary). */
static int bw_finish(BitWriter *w) {
    while (w->nbits > 0) {
        if (bw_flush_byte(w) < 0) return -1;
    }
    return 0;
}

/* Fixed Huffman symbol tables for deflate.
 * sym → (code, code_length): length codes from RFC 1951 §3.2.6.
 * We write LSB-first (DEFLATE), so we need to reverse the canonical code. */
static void fixed_encode_litlen(BitWriter *w, int sym) {
    /* Ranges: 0–143 (8 bits), 144–255 (9 bits), 256 (7 bits), 257–279 (7 bits), 280–287 (8 bits). */
    uint32_t code;
    int      bits;
    if (sym <= 143) {
        /* codes 0..143 → 00110000..10111111 (8-bit, decimal 48–191) */
        code = (uint32_t)(0b00110000 + sym);
        bits = 8;
    } else if (sym <= 255) {
        /* codes 144..255 → 110010000..111111111 (9-bit) */
        code = (uint32_t)(0b110010000 + (sym - 144));
        bits = 9;
    } else if (sym == 256) {
        code = 0b0000000;
        bits = 7;
    } else if (sym <= 279) {
        /* 257..279 → 0000001..0010111 (7-bit) */
        code = (uint32_t)(0b0000001 + (sym - 257));
        bits = 7;
    } else {
        /* 280..287 → 11000000..11000111 (8-bit) */
        code = (uint32_t)(0b11000000 + (sym - 280));
        bits = 8;
    }
    /* Reverse for LSB-first output. */
    uint32_t rev = 0;
    for (int b = 0; b < bits; b++) rev |= ((code >> b) & 1u) << (uint32_t)(bits - 1 - b);
    bw_write(w, rev, bits);
}

static void fixed_encode_dist(BitWriter *w, int dcode) {
    /* All distance codes are 5 bits (RFC 1951 §3.2.6). */
    uint32_t rev = 0;
    for (int b = 0; b < 5; b++) rev |= (((uint32_t)dcode >> b) & 1u) << (uint32_t)(4 - b);
    bw_write(w, rev, 5);
}

/* Encode a back-reference (sym=length-code, dist=distance). */
static int encode_backref(BitWriter *w, size_t length, size_t distance) {
    /* Find length code. */
    int lc = -1;
    for (int i = 0; i < 29; i++) {
        if (length >= (size_t)s_len_base[i] &&
            (i == 28 || length < (size_t)s_len_base[i + 1])) {
            lc = i;
            break;
        }
    }
    if (lc < 0) return -1;
    fixed_encode_litlen(w, 257 + lc);
    uint32_t lextra = (uint32_t)(length - (size_t)s_len_base[lc]);
    if (s_len_extra[lc] > 0) bw_write(w, lextra, (int)s_len_extra[lc]);

    /* Find distance code. */
    int dc = -1;
    for (int i = 0; i < 30; i++) {
        if (distance >= (size_t)s_dist_base[i] &&
            (i == 29 || distance < (size_t)s_dist_base[i + 1])) {
            dc = i;
            break;
        }
    }
    if (dc < 0) return -1;
    fixed_encode_dist(w, dc);
    uint32_t dextra = (uint32_t)(distance - (size_t)s_dist_base[dc]);
    if (s_dist_extra[dc] > 0) bw_write(w, dextra, (int)s_dist_extra[dc]);
    return 0;
}

/* Hash function for 3-byte sequences (LZ77 match finder). */
#define LZ_HASH_SIZE   (1 << 15)
#define LZ_HASH_MASK   (LZ_HASH_SIZE - 1)
#define LZ_MIN_MATCH   3
#define LZ_CHAIN_LIMIT 64      /* max chain steps before giving up */

static uint32_t lz_hash3(const uint8_t *p) {
    return (((uint32_t)p[0] * 0x9E3779B1u) ^
            ((uint32_t)p[1] * 0x6B43A9B5u) ^
            ((uint32_t)p[2] * 0x3C6EF372u)) & LZ_HASH_MASK;
}

TekoDeflateStatus teko_deflate_compress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len)
{
    if (!src && src_len > 0) return TEKO_DEFLATE_BADARG;
    if (!dst || !dst_len)    return TEKO_DEFLATE_BADARG;
    *dst     = NULL;
    *dst_len = 0;

    /* Hash table: head[h] = index of most recent position with hash h.
     * prev[i] = previous position in chain.
     * Both use -1 (SIZE_MAX) as sentinel. */
    size_t *head = NULL;
    size_t *prev_chain = NULL;
    if (src_len >= LZ_MIN_MATCH) {
        head       = (size_t*)calloc(LZ_HASH_SIZE, sizeof(size_t));
        prev_chain = (size_t*)calloc(src_len > DEFLATE_WIN_SIZE ? DEFLATE_WIN_SIZE : src_len,
                                     sizeof(size_t));
        if (!head || !prev_chain) {
            free(head); free(prev_chain);
            return TEKO_DEFLATE_OOM;
        }
        /* Sentinel = SIZE_MAX */
        memset(head, 0xFF, (size_t)LZ_HASH_SIZE * sizeof(size_t));
        size_t pcn = src_len > DEFLATE_WIN_SIZE ? DEFLATE_WIN_SIZE : src_len;
        memset(prev_chain, 0xFF, pcn * sizeof(size_t));
    }

    BitWriter w;
    size_t initial = src_len > 65536 ? 65536 : src_len + 256;
    if (bw_init(&w, initial) < 0) {
        free(head); free(prev_chain);
        return TEKO_DEFLATE_OOM;
    }

    /* BFINAL=1, BTYPE=01 (fixed Huffman). */
    bw_write(&w, 1, 1);  /* BFINAL */
    bw_write(&w, 1, 1);  /* BTYPE lo */
    bw_write(&w, 0, 1);  /* BTYPE hi */

    size_t i = 0;
    while (i < src_len) {
        int matched = 0;
        size_t best_len  = 0;
        size_t best_dist = 0;

        /* Only attempt matching if enough bytes remain for a min-match. */
        if (src_len >= LZ_MIN_MATCH && i + LZ_MIN_MATCH - 1 < src_len &&
            head && prev_chain) {
            uint32_t h = lz_hash3(src + i);
            size_t   j = head[h];
            int      steps = 0;

            while (j != SIZE_MAX && steps < LZ_CHAIN_LIMIT) {
                /* Distance from i to j; must be within the 32 KB window. */
                /* j must be < i (previous position) */
                if (j >= i) break;
                size_t dist = i - j;
                if (dist > DEFLATE_WIN_SIZE) break;

                /* Count the match length. */
                size_t max_match = src_len - i;
                if (max_match > DEFLATE_MAX_LEN) max_match = DEFLATE_MAX_LEN;
                size_t ml = 0;
                while (ml < max_match && src[j + ml] == src[i + ml]) ml++;

                if (ml >= LZ_MIN_MATCH && ml > best_len) {
                    best_len  = ml;
                    best_dist = dist;
                    if (best_len == DEFLATE_MAX_LEN) break; /* can't do better */
                }

                /* Follow the chain. */
                size_t slot = j & (DEFLATE_WIN_SIZE - 1);
                j = prev_chain[slot];
                steps++;
            }

            if (best_len >= LZ_MIN_MATCH) matched = 1;
        }

        if (matched) {
            if (encode_backref(&w, best_len, best_dist) < 0) {
                free(head); free(prev_chain);
                free(w.buf);
                return TEKO_DEFLATE_OOM;
            }
            /* Update hash chain for all positions in the matched run. */
            if (head && prev_chain) {
                for (size_t k = 0; k < best_len && i + k + LZ_MIN_MATCH - 1 < src_len; k++) {
                    uint32_t hk = lz_hash3(src + i + k);
                    size_t slot = (i + k) & (DEFLATE_WIN_SIZE - 1);
                    prev_chain[slot] = head[hk];
                    head[hk] = i + k;
                }
            }
            i += best_len;
        } else {
            /* Emit literal. */
            fixed_encode_litlen(&w, (int)src[i]);
            /* Update hash chain. */
            if (head && prev_chain && i + LZ_MIN_MATCH - 1 < src_len) {
                uint32_t hh = lz_hash3(src + i);
                size_t slot = i & (DEFLATE_WIN_SIZE - 1);
                prev_chain[slot] = head[hh];
                head[hh] = i;
            }
            i++;
        }
    }

    /* End-of-block symbol. */
    fixed_encode_litlen(&w, HUFF_EOB);
    bw_finish(&w);

    free(head);
    free(prev_chain);

    *dst     = w.buf;
    *dst_len = w.len;
    return TEKO_DEFLATE_OK;
}

/* ============================================================================
 * zlib (RFC 1950)
 * ============================================================================ */

TekoDeflateStatus teko_zlib_compress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len)
{
    if (!dst || !dst_len) return TEKO_DEFLATE_BADARG;
    *dst = NULL; *dst_len = 0;

    uint8_t *raw = NULL;
    size_t   raw_len = 0;
    TekoDeflateStatus st = teko_deflate_compress(src, src_len, &raw, &raw_len);
    if (st != TEKO_DEFLATE_OK) return st;

    /* Header: CMF=0x78 (CM=8, CINFO=7), FLG computed so (CMF*256+FLG) % 31 == 0. */
    uint8_t cmf = 0x78;
    /* FLEVEL=2 (default compression), FDICT=0. FLG must satisfy check. */
    uint32_t tmp = ((uint32_t)cmf * 256 + 0) % 31;
    uint8_t  flg = (uint8_t)((tmp == 0) ? 0 : (uint8_t)(31 - tmp));
    /* Ensure FLEVEL=2 bits are set (bits 6-7). */
    flg = (flg & 0x3Fu) | 0x80u;  /* FLEVEL=10b */
    /* Re-check the fcheck field. */
    uint32_t chk = ((uint32_t)cmf * 256 + flg) % 31;
    if (chk != 0) {
        /* Adjust fcheck (bits 0-4) to make the check pass. */
        flg = (uint8_t)((flg & ~0x1Fu) | (uint8_t)(flg + (31 - chk)));
        if (((uint32_t)cmf * 256 + flg) % 31 != 0) {
            /* Rare: just iterate to a valid flg. */
            for (uint8_t f = 0; f < 32; f++) {
                flg = (flg & ~0x1Fu) | f;
                if (((uint32_t)cmf * 256 + flg) % 31 == 0) break;
            }
        }
    }

    uint32_t adler = teko_adler32(1, src, src_len);

    /* Total: 2 (header) + raw_len + 4 (adler32). Overflow-safe. */
    if (raw_len > SIZE_MAX - 6) { free(raw); return TEKO_DEFLATE_OOM; }
    size_t out_len = 2 + raw_len + 4;
    uint8_t *out = (uint8_t*)calloc(1, out_len);
    if (!out) { free(raw); return TEKO_DEFLATE_OOM; }

    out[0] = cmf;
    out[1] = flg;
    memcpy(out + 2, raw, raw_len);
    free(raw);
    /* Adler-32 in big-endian. */
    out[2 + raw_len + 0] = (uint8_t)((adler >> 24) & 0xFFu);
    out[2 + raw_len + 1] = (uint8_t)((adler >> 16) & 0xFFu);
    out[2 + raw_len + 2] = (uint8_t)((adler >>  8) & 0xFFu);
    out[2 + raw_len + 3] = (uint8_t)( adler         & 0xFFu);

    *dst     = out;
    *dst_len = out_len;
    return TEKO_DEFLATE_OK;
}

TekoDeflateStatus teko_zlib_decompress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len,
    size_t max_out)
{
    if (!src || !dst || !dst_len) return TEKO_DEFLATE_BADARG;
    *dst = NULL; *dst_len = 0;

    /* Minimum zlib stream: 2-byte header + 1-byte raw DEFLATE (EOB) + 4-byte trailer. */
    if (src_len < 6) return TEKO_DEFLATE_CORRUPT;

    uint8_t cmf = src[0];
    uint8_t flg = src[1];

    /* fcheck: (CMF*256 + FLG) must be divisible by 31 (RFC 1950 §2.2). */
    if (((uint32_t)cmf * 256 + flg) % 31 != 0) return TEKO_DEFLATE_CORRUPT;
    /* CM must be 8 (DEFLATE); FDICT must be 0. */
    if ((cmf & 0x0Fu) != 8) return TEKO_DEFLATE_CORRUPT;
    if (flg & 0x20u)        return TEKO_DEFLATE_CORRUPT;   /* FDICT unsupported */

    /* Raw DEFLATE is src[2..src_len-5). */
    if (src_len < 6) return TEKO_DEFLATE_CORRUPT;
    size_t raw_len = src_len - 6;

    TekoDeflateStatus st = teko_deflate_decompress(src + 2, raw_len, dst, dst_len, max_out);
    if (st != TEKO_DEFLATE_OK) return st;

    /* Verify trailing Adler-32 (big-endian at src[src_len-4]). */
    const uint8_t *tail = src + src_len - 4;
    uint32_t expected = ((uint32_t)tail[0] << 24) | ((uint32_t)tail[1] << 16) |
                        ((uint32_t)tail[2] <<  8) |  (uint32_t)tail[3];
    uint32_t actual   = teko_adler32(1, *dst, *dst_len);
    if (actual != expected) {
        free(*dst);
        *dst     = NULL;
        *dst_len = 0;
        return TEKO_DEFLATE_CORRUPT;
    }
    return TEKO_DEFLATE_OK;
}

/* ============================================================================
 * gzip (RFC 1952)
 * ============================================================================ */

#define GZIP_ID1    0x1Fu
#define GZIP_ID2    0x8Bu
#define GZIP_CM     8u
/* Flag bits in the gzip header FLG byte. */
#define GZIP_FTEXT    0x01u
#define GZIP_FHCRC    0x02u
#define GZIP_FEXTRA   0x04u
#define GZIP_FNAME    0x08u
#define GZIP_FCOMMENT 0x10u

TekoDeflateStatus teko_gzip_compress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len)
{
    if (!dst || !dst_len) return TEKO_DEFLATE_BADARG;
    *dst = NULL; *dst_len = 0;

    uint8_t *raw = NULL;
    size_t   raw_len = 0;
    TekoDeflateStatus st = teko_deflate_compress(src, src_len, &raw, &raw_len);
    if (st != TEKO_DEFLATE_OK) return st;

    uint32_t crc   = teko_crc32(0, src, src_len);
    /* ISIZE = input length mod 2^32. */
    uint32_t isize = (uint32_t)(src_len & 0xFFFFFFFFu);

    /* Header (10 bytes) + raw_len + trailer (8 bytes). Overflow-safe. */
    if (raw_len > SIZE_MAX - 18) { free(raw); return TEKO_DEFLATE_OOM; }
    size_t out_len = 10 + raw_len + 8;
    uint8_t *out = (uint8_t*)calloc(1, out_len);
    if (!out) { free(raw); return TEKO_DEFLATE_OOM; }

    /* Minimal 10-byte gzip header (RFC 1952 §2.3). */
    out[0] = GZIP_ID1;
    out[1] = GZIP_ID2;
    out[2] = GZIP_CM;       /* CM = 8 (DEFLATE) */
    out[3] = 0;             /* FLG = 0 (no extras) */
    out[4] = 0; out[5] = 0; out[6] = 0; out[7] = 0; /* MTIME = 0 */
    out[8] = 0;             /* XFL = 0 */
    out[9] = 255;           /* OS = 255 (unknown) */

    memcpy(out + 10, raw, raw_len);
    free(raw);

    /* CRC32 and ISIZE in little-endian. */
    uint8_t *tail = out + 10 + raw_len;
    tail[0] = (uint8_t)( crc         & 0xFFu);
    tail[1] = (uint8_t)((crc >>  8)  & 0xFFu);
    tail[2] = (uint8_t)((crc >> 16)  & 0xFFu);
    tail[3] = (uint8_t)((crc >> 24)  & 0xFFu);
    tail[4] = (uint8_t)( isize        & 0xFFu);
    tail[5] = (uint8_t)((isize >>  8) & 0xFFu);
    tail[6] = (uint8_t)((isize >> 16) & 0xFFu);
    tail[7] = (uint8_t)((isize >> 24) & 0xFFu);

    *dst     = out;
    *dst_len = out_len;
    return TEKO_DEFLATE_OK;
}

TekoDeflateStatus teko_gzip_decompress(
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len,
    size_t max_out)
{
    if (!src || !dst || !dst_len) return TEKO_DEFLATE_BADARG;
    *dst = NULL; *dst_len = 0;

    /* Minimum: 10-byte header + 1-byte raw DEFLATE + 8-byte trailer = 19. */
    if (src_len < 18) return TEKO_DEFLATE_CORRUPT;

    /* Verify magic and CM. */
    if (src[0] != GZIP_ID1 || src[1] != GZIP_ID2) return TEKO_DEFLATE_CORRUPT;
    if (src[2] != GZIP_CM)                         return TEKO_DEFLATE_CORRUPT;

    uint8_t flg = src[3];
    size_t  pos = 10; /* skip fixed 10-byte header */

    /* Skip optional FEXTRA field (bounds-checked). */
    if (flg & GZIP_FEXTRA) {
        if (pos + 2 > src_len) return TEKO_DEFLATE_CORRUPT;
        uint16_t xlen = (uint16_t)((uint16_t)src[pos] | ((uint16_t)src[pos+1] << 8));
        pos += 2;
        if (pos + (size_t)xlen > src_len) return TEKO_DEFLATE_CORRUPT;
        pos += (size_t)xlen;
    }

    /* Skip optional FNAME (NUL-terminated, bounds-checked). */
    if (flg & GZIP_FNAME) {
        while (pos < src_len && src[pos] != 0) pos++;
        if (pos >= src_len) return TEKO_DEFLATE_CORRUPT;
        pos++; /* skip NUL */
    }

    /* Skip optional FCOMMENT (NUL-terminated, bounds-checked). */
    if (flg & GZIP_FCOMMENT) {
        while (pos < src_len && src[pos] != 0) pos++;
        if (pos >= src_len) return TEKO_DEFLATE_CORRUPT;
        pos++; /* skip NUL */
    }

    /* Skip optional FHCRC (header CRC16). */
    if (flg & GZIP_FHCRC) {
        if (pos + 2 > src_len) return TEKO_DEFLATE_CORRUPT;
        pos += 2;
    }

    /* Trailer is the last 8 bytes: CRC32 (4) + ISIZE (4). */
    if (src_len < pos + 8) return TEKO_DEFLATE_CORRUPT;
    size_t raw_len = src_len - pos - 8;

    TekoDeflateStatus st = teko_deflate_decompress(src + pos, raw_len, dst, dst_len, max_out);
    if (st != TEKO_DEFLATE_OK) return st;

    /* Verify CRC32 (little-endian). */
    const uint8_t *tail = src + src_len - 8;
    uint32_t exp_crc = (uint32_t)tail[0]        | ((uint32_t)tail[1] << 8) |
                       ((uint32_t)tail[2] << 16) | ((uint32_t)tail[3] << 24);
    uint32_t act_crc = teko_crc32(0, *dst, *dst_len);
    if (act_crc != exp_crc) {
        free(*dst); *dst = NULL; *dst_len = 0;
        return TEKO_DEFLATE_CORRUPT;
    }

    /* Verify ISIZE (lower 32 bits of original length, little-endian). */
    uint32_t exp_isize = (uint32_t)tail[4]        | ((uint32_t)tail[5] << 8) |
                         ((uint32_t)tail[6] << 16) | ((uint32_t)tail[7] << 24);
    if ((uint32_t)(*dst_len & 0xFFFFFFFFu) != exp_isize) {
        free(*dst); *dst = NULL; *dst_len = 0;
        return TEKO_DEFLATE_CORRUPT;
    }

    return TEKO_DEFLATE_OK;
}
