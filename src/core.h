// src/core.h — the prelude made concrete in C. Bottom of the module DAG (B.8).
#ifndef TK_CORE_H
#define TK_CORE_H

#include <stdbool.h>
#include <stdlib.h>    // realloc, free, abort, size_t — for TK_LIST

// error — the built-in error-as-value (B.1). In the bootstrap the message is a
// static ASCII literal (a const char*), keeping core.h independent of teko::text
// (DAG one-directional); the Teko-level message is a `str`.
typedef struct {
    const char *message;
} tk_error;

static inline tk_error tk_error_make(const char *message) {
    return (tk_error){ .message = message };
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
            T *np = realloc(xs.ptr, ncap * sizeof(T));              \
            if (np == NULL) { abort(); }                            \
            xs.ptr = np;                                            \
            xs.cap = ncap;                                          \
        }                                                           \
        xs.ptr[xs.len] = item;                                      \
        xs.len = xs.len + 1;                                        \
        return xs;                                                  \
    }                                                               \
    static inline void Name##_free(Name xs) { free(xs.ptr); }

#endif // TK_CORE_H
