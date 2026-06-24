// runtime/teko_rt.h — libteko_rt: runtime for GENERATED Teko programs (M.1 fail-loud).
// Distinct from the compiler's own src/core.h; self-contained, libc-only.
#ifndef TEKO_RT_H
#define TEKO_RT_H

#include <stdint.h>   // uint8_t
#include <stddef.h>   // size_t

// byte — one octet (mirrors src/text/text.h's tk_byte; same rep).
typedef uint8_t tk_byte;

// str — a VIEW into UTF-8 bytes; len is in BYTES, NOT NUL-terminated.
// Identical shape to src/text/text.h's tk_str.
typedef struct {
    const tk_byte *ptr;   // the bytes
    size_t         len;   // length in BYTES
} tk_str;

// tk_print — write exactly s.len bytes from s.ptr to stdout; no newline, no NUL.
void tk_print(tk_str s);
// tk_println — tk_print(s) then a single '\n' (0x0A).
void tk_println(tk_str s);

// tk_panic — fail loud (M.1): "teko: panic: <msg>\n" to stderr, then non-zero exit.
_Noreturn void tk_panic(const char *msg);
_Noreturn void tk_panic_div0(void);       // "division by zero"
_Noreturn void tk_panic_oob(void);        // "index out of bounds"
_Noreturn void tk_panic_cast(void);       // "impossible conversion" (the `x to T` guard — B.36 / M.1)
_Noreturn void tk_panic_overflow(void);   // "integer overflow"

#endif // TEKO_RT_H
