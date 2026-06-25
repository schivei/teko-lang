// src/core.h — the prelude made concrete in C. Bottom of the module DAG (B.8).
#ifndef TK_CORE_H
#define TK_CORE_H

#include <stdbool.h>
#include <stdlib.h>    // malloc, realloc, free, abort, size_t
#include <string.h>    // memcpy — for tk_alloc_copy
#include <stdint.h>    // uint32_t — diagnostic source positions (C1.3)

// ── The allocation SEAM (S0 — TEKO_EVOLUTION_DESIGN §5; the swap point for S1 arenas) ──
// One choke point for every dynamic allocation in the seed. Today these are thin
// wrappers over malloc/realloc/free; when arenas (S1) land, the bodies become
// region_alloc with NO call-site change — the swap is mechanical, exactly here.
// OOM PANICS (abort — M.1 fail-loud, never a NULL handed back; matches the existing
// `if (p == NULL) abort();` discipline so callers may drop their own NULL checks).
static inline void *tk_alloc(size_t size) {
    void *p = malloc(size == 0 ? 1 : size);   // 0-byte alloc → 1 (a real, freeable pointer)
    if (p == NULL) { abort(); }
    return p;
}
static inline void *tk_realloc0(void *ptr, size_t size) {
    void *p = realloc(ptr, size == 0 ? 1 : size);
    if (p == NULL) { abort(); }
    return p;
}
static inline void tk_free0(void *ptr) { free(ptr); }
static inline void *tk_alloc_copy(const void *src, size_t size) {
    void *p = tk_alloc(size);
    if (size != 0) { memcpy(p, src, size); }
    return p;
}

// severity — a diagnostic is fatal (error) or advisory (warning). Default 0 =
// TK_SEV_ERROR, so every existing tk_error stays an error with no change (C1.3).
typedef enum { TK_SEV_ERROR, TK_SEV_WARNING } tk_severity;

// error — the built-in error-as-value (B.1). In the bootstrap the message is a
// static ASCII literal (a const char*), keeping core.h independent of teko::text
// (DAG one-directional); the Teko-level message is a `str`.
//
// (C1.3 / diagnostics axis) The bootstrap error doubles as the COMPILER DIAGNOSTIC
// carrier, so it also holds OPTIONAL adornments — all default-zero/NULL, so every
// existing construction site is unchanged (M.5 additive). Strings stay const char*
// (off the teko::text DAG, exactly like `message`):
//   file/line/col   — source position of the offending construct (NULL/0 = unknown);
//                     populated once expr nodes carry positions (C1-POS), printed by
//                     the driver's renderer (C1.8). (Eixo E1/E2.)
//   expected/actual — rendered type names for a mismatch ("expected X, found Y"); NULL
//                     when not a type mismatch.
//   severity        — error (default) | warning (the warnings channel, Phase 5).
typedef struct {
    const char *message;
    const char *file;        // source file (NULL = unknown)
    uint32_t    line, col;   // 1-based source position (0 = unknown)
    const char *expected;    // rendered expected type (NULL = n/a)
    const char *actual;      // rendered actual type   (NULL = n/a)
    tk_severity severity;    // TK_SEV_ERROR (default) | TK_SEV_WARNING
} tk_error;

static inline tk_error tk_error_make(const char *message) {
    return (tk_error){ .message = message };   // other fields zero/NULL — unknown/error (C1.3)
}

// Composable "with" adornments — return a copy with the field(s) set, so callers chain:
//   tk_error_at(tk_error_make("argument type mismatch"), file, line, col)
// ADDITIVE: existing constructors/sites need no change (C1.3). Real population at error
// sites lands with expr positions (C1-POS) + the renderer (C1.8).
static inline tk_error tk_error_at(tk_error e, const char *file, uint32_t line, uint32_t col) {
    e.file = file; e.line = line; e.col = col; return e;
}
static inline tk_error tk_error_types(tk_error e, const char *expected, const char *actual) {
    e.expected = expected; e.actual = actual; return e;
}
static inline tk_error tk_warning_make(const char *message) {
    return (tk_error){ .message = message, .severity = TK_SEV_WARNING };   // warnings channel (Phase 5)
}

// TK_RESULT(T, Name) — the C form of `T | error` (no generics in the seed — M.5).
// Read `.as.value` only when `ok`, `.as.error` only when `!ok` (the match).
#define TK_RESULT(T, Name)                     \
    typedef struct {                           \
        bool ok;                               \
        union { T value; tk_error error; } as; \
    } Name

// TK_LIST(T, Name) — teko::list realized concretely. `push` CONSUMES and RETURNS
// (xs = Name##_push(xs, item)); allocation failure PANICS (abort — M.5).
#define TK_LIST(T, Name)                                            \
    typedef struct { T *ptr; size_t len; size_t cap; } Name;        \
    static inline Name Name##_empty(void) {                         \
        return (Name){ .ptr = NULL, .len = 0, .cap = 0 };           \
    }                                                               \
    static inline Name Name##_push(Name xs, T item) {               \
        if (xs.len == xs.cap) {                                     \
            size_t ncap = (xs.cap == 0) ? 8 : (xs.cap * 2);         \
            xs.ptr = (T *)tk_realloc0(xs.ptr, ncap * sizeof(T));    \
            xs.cap = ncap;                                          \
        }                                                           \
        xs.ptr[xs.len] = item;                                      \
        xs.len = xs.len + 1;                                        \
        return xs;                                                  \
    }                                                               \
    static inline void Name##_free(Name xs) { tk_free0(xs.ptr); }

#endif // TK_CORE_H
