// src/checker/check.h — the shared checker result type (Unit | error).
// (The legacy tk_check_function/item/program driver was retired; the typed
//  layer's tk_type_program is the entry point. revalidate.c still uses this type.)
#ifndef TK_CHECK_H
#define TK_CHECK_H

#include "collect.h"   // pulls resolve.h → tk_error, bool

// Unit | error — `ok` on success; `error` is valid iff `!ok`.
typedef struct { bool ok; tk_error error; } tk_check_result;

#endif // TK_CHECK_H
