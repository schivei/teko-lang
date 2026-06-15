#include "teko_rt.h"
#include "teko_crypto_sha2.h"
#include "teko_crypto_sha512.h"
#include "teko_crypto_sha3.h"
#include "teko_crypto_blake3.h"
#include "teko_crypto_blake2b.h"
#include "teko_crypto_hmac.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// See teko_rt.h. This translation unit is compiled into the static archive
// `libteko_rt.a`, which the native runner links into every produced executable.
// Crypto wrappers (Sub-phase B) are added alongside this print primitive; they call
// the portable C crypto runtime (the single source of truth), which is compiled into
// the same archive so produced binaries are self-contained.

void teko_rt_emit(const char* s) {
    puts(s ? s : "");
}

// Decode a hex string into a fresh byte buffer (caller frees via free). Sets *out_len.
// Returns NULL on odd length or a non-hex digit. An empty string decodes to a 0-length
// buffer (a 1-byte allocation, so the result is never NULL for valid input).
static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static uint8_t* teko_rt_from_hex(const char* hex, size_t* out_len) {
    if (!hex) return NULL;
    size_t hl = strlen(hex);
    if (hl % 2 != 0) return NULL;
    size_t n = hl / 2;
    uint8_t* out = (uint8_t*)malloc(n ? n : 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        int hi = hexval((unsigned char)hex[2 * i]);
        int lo = hexval((unsigned char)hex[2 * i + 1]);
        if (hi < 0 || lo < 0) { free(out); return NULL; }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    if (out_len) *out_len = n;
    return out;
}

// Lowercase-hex-encode n bytes into a fresh NUL-terminated string (caller-owned).
static char* teko_rt_to_hex(const uint8_t* b, size_t n) {
    static const char hexd[] = "0123456789abcdef";
    char* out = (char*)malloc(n * 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = hexd[(b[i] >> 4) & 0xF];
        out[2 * i + 1] = hexd[b[i] & 0xF];
    }
    out[2 * n] = '\0';
    return out;
}

char* teko_rt_sha256_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_SHA256_DIGEST_LEN];
    teko_sha256(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_SHA256_DIGEST_LEN);
}

char* teko_rt_sha384_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_SHA384_DIGEST_LEN];
    teko_sha384(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_SHA384_DIGEST_LEN);
}

char* teko_rt_sha512_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_SHA512_DIGEST_LEN];
    teko_sha512(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_SHA512_DIGEST_LEN);
}

char* teko_rt_sha3_256_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_SHA3_256_DIGEST_LEN];
    teko_sha3_256(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_SHA3_256_DIGEST_LEN);
}

char* teko_rt_sha3_512_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_SHA3_512_DIGEST_LEN];
    teko_sha3_512(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_SHA3_512_DIGEST_LEN);
}

char* teko_rt_blake3_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_BLAKE3_OUT_LEN];
    teko_blake3(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_BLAKE3_OUT_LEN);
}

char* teko_rt_blake2b_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[64];
    if (teko_blake2b(p, len, digest, 64u) != 0) return NULL;
    return teko_rt_to_hex(digest, 64u);
}

char* teko_rt_hmac_sha256(const char* key_hex, const char* msg) {
    size_t klen = 0;
    uint8_t* key = teko_rt_from_hex(key_hex, &klen);
    if (!key) return NULL;
    uint8_t mac[TEKO_SHA256_DIGEST_LEN];
    teko_hmac_sha256(key, klen, (const uint8_t*)(msg ? msg : ""), msg ? strlen(msg) : 0, mac);
    free(key);
    return teko_rt_to_hex(mac, TEKO_SHA256_DIGEST_LEN);
}

char* teko_rt_hmac_sha384(const char* key_hex, const char* msg) {
    size_t klen = 0;
    uint8_t* key = teko_rt_from_hex(key_hex, &klen);
    if (!key) return NULL;
    uint8_t mac[TEKO_SHA384_DIGEST_LEN];
    teko_hmac_sha384(key, klen, (const uint8_t*)(msg ? msg : ""), msg ? strlen(msg) : 0, mac);
    free(key);
    return teko_rt_to_hex(mac, TEKO_SHA384_DIGEST_LEN);
}

char* teko_rt_hmac_sha512(const char* key_hex, const char* msg) {
    size_t klen = 0;
    uint8_t* key = teko_rt_from_hex(key_hex, &klen);
    if (!key) return NULL;
    uint8_t mac[TEKO_SHA512_DIGEST_LEN];
    teko_hmac_sha512(key, klen, (const uint8_t*)(msg ? msg : ""), msg ? strlen(msg) : 0, mac);
    free(key);
    return teko_rt_to_hex(mac, TEKO_SHA512_DIGEST_LEN);
}
