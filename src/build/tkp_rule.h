// src/build/tkp_rule.h   (namespace 'teko::build')
//
// The C SEED of tkp_rule.tks: the `.tkp` artifact kind + the main-file rule (R-main-d).
// This is the SINGLE home of tk_artifact (manifest.h includes this header rather than
// re-declaring the enum).
#ifndef TK_BUILD_TKP_RULE_H
#define TK_BUILD_TKP_RULE_H

#include "../core.h"   // TK_RESULT, tk_error
#include <stdbool.h>   // bool

// the `.tkp` artifact kind (tkp_rule.tks `enum { Binary; Static; Shared; Package }`). The
// manifest encodes these as `[artifact] kind = "binary" | "static" | "shared" | "package"`.
// Binary = executable; Static/Shared = native libs (.a / .dylib|.so|.dll); Package = a `.tkl`.
typedef enum {
    TK_ARTIFACT_BINARY,
    TK_ARTIFACT_STATIC,
    TK_ARTIFACT_SHARED,
    TK_ARTIFACT_PACKAGE,
} tk_artifact;

// `Artifact | error` — the result of the main-file rule (pass-through on success).
TK_RESULT(tk_artifact, tk_artifact_result);

// tk_check_main_file_rule — the R-main-d rule: an executable REQUIRES a main.tks; a
// library FORBIDS it. Returns the artifact (pass-through) on success, an error otherwise.
tk_artifact_result tk_check_main_file_rule(tk_artifact artifact, bool has_main);

#endif // TK_BUILD_TKP_RULE_H
