#include "teko_rt.h"
#include <stdio.h>

// See teko_rt.h. This translation unit is compiled into the static archive
// `libteko_rt.a`, which the native runner links into every produced executable.
// Crypto wrappers (Sub-phase B) are added alongside this print primitive; they call
// the portable C crypto runtime (the single source of truth), which is compiled into
// the same archive so produced binaries are self-contained.

void teko_rt_emit(const char* s) {
    puts(s ? s : "");
}
