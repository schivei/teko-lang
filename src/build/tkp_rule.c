// src/build/tkp_rule.c   (namespace 'teko::build')
//
// The C SEED impl of tkp_rule.tks — the `.tkp` main-file rule (R-main-d). The build
// driver supplies `artifact` (from the manifest) and `has_main` (from the file set);
// that wiring is deferred (M.4).
#include "tkp_rule.h"

tk_artifact_result tk_check_main_file_rule(tk_artifact artifact, bool has_main) {
    if (artifact == TK_ARTIFACT_BINARY && !has_main)
        return (tk_artifact_result){ .ok = false,
            .as.error = tk_error_make("a binary project requires a main.tks") };
    if (artifact != TK_ARTIFACT_BINARY && has_main)
        return (tk_artifact_result){ .ok = false,
            .as.error = tk_error_make("a library project (static/shared/package) may not have a main.tks") };
    return (tk_artifact_result){ .ok = true, .as.value = artifact };
}
