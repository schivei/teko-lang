// src/checker/typer.h — the typed producers (the evolved check_*).
#ifndef TK_CHECK_TYPER_H
#define TK_CHECK_TYPER_H

#include "tast.h"
#include "resolve.h"   // tk_type_table
#include "expr.h"      // tk_typer_expr + tk_cast_ok (expression-typing surface, paired with expr.c)

tk_typed_stmt_result tk_type_statement(tk_statement s, tk_env env, tk_type_table table);
tk_tfunction_result  tk_type_function(tk_function f, tk_env env, tk_type_table table);
tk_titem_result      tk_type_item(tk_item item, tk_env env, tk_type_table table);
tk_tprogram_result   tk_type_program(tk_program program);

#endif // TK_CHECK_TYPER_H
