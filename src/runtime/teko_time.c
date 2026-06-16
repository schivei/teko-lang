#include "teko_time.h"
#include <stdlib.h>

// Phase 14 (wall-clock / timezone surface) — portable civil-time formatter (source of truth). Pure
// integer math: no libc time/tz calls, so it is deterministic (KAT-able) AND runs unchanged in the
// freestanding wasm32 reactor. The platform-specific bits (current epoch, DST-correct local offset)
// are supplied by teko_rt.c (native OS calls / WASM host imports) and passed into teko_time_format.

// Civil date <-> days since 1970-01-01 (Howard Hinnant, "chrono-compatible Low-Level Date
// Algorithms"). Valid across the full proleptic Gregorian range; handles negative days.
static void civil_from_days(long long z, long* y, unsigned* m, unsigned* d) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);                    // [0, 146096]
    unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365; // [0, 399]
    long yy = (long)((long long)yoe + era * 400);
    unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);              // [0, 365]
    unsigned mp = (5*doy + 2)/153;                                 // [0, 11]
    *d = doy - (153*mp+2)/5 + 1;                                   // [1, 31]
    *m = mp < 10 ? mp + 3 : mp - 9;                                // [1, 12]
    *y = yy + (*m <= 2);
}

long long teko_time_days_from_civil(long y, unsigned m, unsigned d) {
    y -= (m <= 2);
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);                      // [0, 399]
    unsigned doy = (153*(m > 2 ? m - 3 : m + 9) + 2)/5 + d - 1;    // [0, 365]
    unsigned doe = yoe*365 + yoe/4 - yoe/100 + doy;                // [0, 146096]
    return era * 146097 + (long long)doe - 719468;
}

static char* put2(char* p, int v) { p[0] = '0' + (v/10)%10; p[1] = '0' + v%10; return p + 2; }

char* teko_time_format(long long epoch_s, long offset_s) {
    long long local = epoch_s + (long long)offset_s;
    long long days = local / 86400, rem = local % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }
    int hh = (int)(rem / 3600), mm = (int)((rem % 3600) / 60), ss = (int)(rem % 60);
    long y; unsigned mo, d;
    civil_from_days(days, &y, &mo, &d);

    char* out = (char*)malloc(48);
    if (!out) return NULL;
    char* p = out;
    if (y < 0) { *p++ = '-'; y = -y; }
    // Year, >= 4 digits, no leading-zero truncation.
    char yb[16]; int yn = 0; long ya = y;
    if (ya == 0) yb[yn++] = '0';
    while (ya > 0) { yb[yn++] = (char)('0' + (int)(ya % 10)); ya /= 10; }
    while (yn < 4) yb[yn++] = '0';
    for (int i = yn - 1; i >= 0; i--) *p++ = yb[i];
    *p++ = '-'; p = put2(p, (int)mo);
    *p++ = '-'; p = put2(p, (int)d);
    *p++ = 'T'; p = put2(p, hh);
    *p++ = ':'; p = put2(p, mm);
    *p++ = ':'; p = put2(p, ss);
    if (offset_s == 0) {
        *p++ = 'Z';
    } else {
        long ao = offset_s < 0 ? -offset_s : offset_s;
        *p++ = offset_s < 0 ? '-' : '+';
        p = put2(p, (int)(ao / 3600));
        *p++ = ':';
        p = put2(p, (int)((ao % 3600) / 60));
    }
    *p = '\0';
    return out;
}

char* teko_time_epoch_str(long long epoch_s) {
    char tmp[24]; int n = 0;
    int neg = epoch_s < 0;
    unsigned long long v = neg ? (unsigned long long)(-(epoch_s + 1)) + 1ULL : (unsigned long long)epoch_s;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    char* out = (char*)malloc((size_t)n + (neg ? 1 : 0) + 1);
    if (!out) return NULL;
    char* p = out;
    if (neg) *p++ = '-';
    for (int i = n - 1; i >= 0; i--) *p++ = tmp[i];
    *p = '\0';
    return out;
}

long long teko_time_parse(const char* s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') { s++; }
    long long v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}
