// src/checker/resolve.h — type resolution + the user-type registry.
#ifndef TK_CHECK_RESOLVE_H
#define TK_CHECK_RESOLVE_H

#include "type.h"
#include "scope.h"
#include "../parser/ast.h"   // tk_type_expr, tk_type_decl, tk_path, … (the parser's AST)

typedef struct { tk_str name; tk_type_decl decl; } tk_type_reg;
TK_LIST(tk_type_reg, tk_type_table);

TK_RESULT(tk_type_decl, tk_decl_result);   // TypeDecl | error

tk_decl_result tk_type_table_find(tk_type_table table, tk_str name);
tk_type_result tk_resolve_type(tk_type_expr te, tk_type_table table);
tk_type_result resolve_named(tk_path path, tk_type_table table);   // shared with match.c (C7)

#endif // TK_CHECK_RESOLVE_H
