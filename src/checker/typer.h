// src/checker/typer.h — the typed producers (the evolved check_*).
#ifndef TK_CHECK_TYPER_H
#define TK_CHECK_TYPER_H

#include "tast.h"
#include "resolve.h"   // tk_type_table

tk_texpr_result      tk_type_expr(tk_expr e, tk_env env, tk_type_table table);
tk_typed_stmt_result tk_type_statement(tk_statement s, tk_env env, tk_type_table table);
tk_tfunction_result  tk_type_function(tk_function f, tk_env env, tk_type_table table);
tk_titem_result      tk_type_item(tk_item item, tk_env env, tk_type_table table);
tk_tprogram_result   tk_type_program(tk_program program);

// cast legality (C2) — exposed so the counter-validation (revalidate.c) re-derives it.
bool tk_cast_ok(tk_type from, tk_type to);

#endif // TK_CHECK_TYPER_H
