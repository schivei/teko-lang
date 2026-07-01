// src/win32_int128_builtins.c — software implementations of __int128 arithmetic
// builtins for Windows / MSVC-ABI targets.
//
// Clang on Windows (MSVC ABI) emits calls to these compiler-rt symbols when
// __int128 division, modulo, or float↔int128 conversions are used, but the
// compiler-rt builtins library is not shipped with standard Windows LLVM
// distributions and is not auto-linked.  We provide correct C implementations
// compiled as part of the teko build so no external library is required.
//
// Naming convention (LLVM compiler-rt):
//   ti  = "tetra int" = 128-bit integer
//   df  = "double float" = f64 (double)
//   u   = unsigned
//
// Only compiled on _WIN32.  POSIX builds get these from the system's
// libclang_rt.builtins automatically.

#ifdef _WIN32

#include <stdint.h>

typedef unsigned __int128  u128;
typedef          __int128  i128;

// ── unsigned 128-bit division ────────────────────────────────────────────────
// Returns the position (0-based) of the most-significant set bit, or -1 for 0.
static int u128_msb(u128 x) {
    for (int b = 127; b >= 0; b--)
        if ((x >> b) & (u128)1) return b;
    return -1;
}

// Long division: returns quotient, writes remainder to *rem (may be NULL).
// Precondition: d != 0 (caller must check).
static u128 udiv128(u128 n, u128 d, u128 *rem) {
    if (n < d)  { if (rem) *rem = n; return 0; }
    int d_msb    = u128_msb(d);
    int max_shift = 127 - d_msb;   // d << max_shift has MSB at bit 127 — no overflow
    u128 q = 0;
    for (int s = max_shift; s >= 0; s--) {
        u128 ds = d << s;
        if (n >= ds) { n -= ds; q |= (u128)1 << s; }
    }
    if (rem) *rem = n;
    return q;
}

u128 __udivti3(u128 a, u128 b) { return udiv128(a, b, (u128*)0); }
u128 __umodti3(u128 a, u128 b) { u128 r; udiv128(a, b, &r); return r; }

// ── signed 128-bit division ──────────────────────────────────────────────────
i128 __divti3(i128 a, i128 b) {
    int neg = (a < 0) != (b < 0);
    u128 ua = (a < 0) ? (u128)(-a) : (u128)a;
    u128 ub = (b < 0) ? (u128)(-b) : (u128)b;
    u128 q  = udiv128(ua, ub, (u128*)0);
    return neg ? -(i128)q : (i128)q;
}
i128 __modti3(i128 a, i128 b) {
    u128 ua = (a < 0) ? (u128)(-a) : (u128)a;
    u128 ub = (b < 0) ? (u128)(-b) : (u128)b;
    u128 r;
    udiv128(ua, ub, &r);
    return (a < 0) ? -(i128)r : (i128)r;
}

// ── float ↔ i128 conversions ─────────────────────────────────────────────────
// 2^64 is exactly representable as a double.
#define TK_TWO64  18446744073709551616.0

double __floatuntidf(u128 a) {
    uint64_t hi = (uint64_t)(a >> 64);
    uint64_t lo = (uint64_t)a;
    return (double)hi * TK_TWO64 + (double)lo;
}
double __floattidf(i128 a) {
    if (a < 0) return -__floatuntidf((u128)(-a));
    return __floatuntidf((u128)a);
}

u128 __fixunsdfti(double a) {
    if (a < 0.0 || a >= TK_TWO64 * TK_TWO64) return 0;
    uint64_t hi = (uint64_t)(a / TK_TWO64);
    uint64_t lo = (uint64_t)(a - (double)hi * TK_TWO64);
    return ((u128)hi << 64) | (u128)lo;
}
i128 __fixdfti(double a) {
    if (a >= 0.0) return  (i128)__fixunsdfti(a);
    return              -(i128)__fixunsdfti(-a);
}

#endif  // _WIN32
