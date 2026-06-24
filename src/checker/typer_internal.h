// src/checker/typer_internal.h   (namespace 'teko::checker')
// Cross-TU glue between expr.c and typer.c (same-namespace visibility, realized
// as C headers). NOT a public surface — only the two checker typing TUs include it.
#ifndef TK_CHECK_TYPER_INTERNAL_H
#define TK_CHECK_TYPER_INTERNAL_H

#include "tast.h"
#include "resolve.h"   // tk_type_table

// a typed block: thread the env, collect typed statements. Defined in typer.c
// (statement side); expr.c's type_if/type_match/type_arm call back into it for
// branch/arm bodies.
tk_typed_block_result tk_type_block(tk_statement *stmts, size_t n, tk_env env, tk_type_table table);

// the value-type a typed block yields (last stmt's, if an ExprStmt; else Unit).
// Defined in expr.c; typer.c's if/match STATEMENT forms reuse it.
tk_type tk_tblock_type(tk_tstatement *stmts, size_t n);

// one typed match arm (pattern extends env; `when` guard bool; body typed).
// Defined in expr.c (helper of the match VALUE form); typer.c's match STATEMENT
// form reuses it. Shared from match.c too: tk_exhaustive / tk_check_pattern.
tk_tarm_result tk_type_arm(tk_arm a, tk_type subject, tk_env env, tk_type_table table);
bool           tk_exhaustive(tk_arm *arms, size_t n, tk_type subject);

#endif // TK_CHECK_TYPER_INTERNAL_H
