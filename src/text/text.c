// src/text/text.c — UTF-8 validation (the bootstrap's only bytes → str door).
#include "text.h"

// Well-formed UTF-8 per RFC 3629: reject overlong, UTF-16 surrogates
// (U+D800..U+DFFF), and codepoints > U+10FFFF. The first continuation byte
// carries the tightest constraint; the rest are plain 0x80..0xBF. One walk (M.3).
static bool valid_utf8(const tk_byte *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        tk_byte b = s[i];
        if (b <= 0x7F) { i += 1; continue; }        // ASCII — a single byte

        size_t  cont;                                // continuation bytes that follow
        tk_byte lo, hi;                              // valid range for the FIRST of them
        if      (b >= 0xC2 && b <= 0xDF) { cont = 1; lo = 0x80; hi = 0xBF; }
        else if (b == 0xE0)              { cont = 2; lo = 0xA0; hi = 0xBF; } // no overlong
        else if (b >= 0xE1 && b <= 0xEC) { cont = 2; lo = 0x80; hi = 0xBF; }
        else if (b == 0xED)              { cont = 2; lo = 0x80; hi = 0x9F; } // no surrogate
        else if (b >= 0xEE && b <= 0xEF) { cont = 2; lo = 0x80; hi = 0xBF; }
        else if (b == 0xF0)              { cont = 3; lo = 0x90; hi = 0xBF; } // no overlong
        else if (b >= 0xF1 && b <= 0xF3) { cont = 3; lo = 0x80; hi = 0xBF; }
        else if (b == 0xF4)              { cont = 3; lo = 0x80; hi = 0x8F; } // <= U+10FFFF
        else return false;                           // 0x80..0xC1, 0xF5..0xFF: invalid lead

        if (len - i <= cont) return false;           // truncated: not enough bytes
        if (s[i + 1] < lo || s[i + 1] > hi) return false;            // first continuation
        for (size_t k = 2; k <= cont; k += 1) {                      // the rest, plain
            if (s[i + k] < 0x80 || s[i + k] > 0xBF) return false;
        }
        i += cont + 1;
    }
    return true;
}

tk_str_result tk_str_from_utf8(const tk_byte *bytes, size_t len) {
    if (!valid_utf8(bytes, len)) {
        return (tk_str_result){ .ok = false, .as.error = tk_error_make("invalid UTF-8") };
    }
    return (tk_str_result){ .ok = true, .as.value = (tk_str){ bytes, len } };
}
