// teko.mc -- teko taught to `mc` from outside `src/`, in `ngen/` (not
// `mc/examples/`, per the dono's 2026-09-04 ruling): the first entrega, a
// vertical slice proving the pipeline end to end. Not compiled as a program:
// `mc build` links this INSIDE a compiler (`#include <mc/core>` plus this
// file, docs/build.md § [compiler]) that then compiles a `.tk` source.
//
// D213 (dono 2026-09-04): reuse the mc core's own syntax; teach only the
// DELTA. `examples/lang` does not reimplement expressions/statements/types
// either -- it adds classes/generics/interfaces on top of the base. So the
// core's own spelling (`i64 name(params) { ... return e; }`, `if (c) {}`,
// the whole Pratt grammar) IS the teko-over-mc spelling here; nothing in
// this file re-teaches it.
//
// What this entrega actually adds:
//   bool                                                    type_alias  (M12)
//   class interface namespace import using type              syntax      (M12, honest-stop)
//   var const match when                                     syntax_stmt (M12, honest-stop)
//   new                                                       syntax_expr (M21, honest-stop)
//
// Everything else in docs/design/port-teko-mc.md §3 (generics, operator
// overload, error-union, `service`/DI, concurrency, the stdlib) is a later
// entrega and is not stubbed here: it does not yet have a reserved word to
// stop on, so it simply is not part of the language this compiler accepts.

#include "teko_type.mc"
#include "teko_class.mc"
#include "teko_stmt.mc"
#include "teko_expr.mc"

void user_init() {
    tk_types_init();

    syntax("class",     &tk_stop_class);
    syntax("type",      &tk_stop_type);
    syntax("interface", &tk_stop_interface);
    syntax("namespace", &tk_stop_namespace);
    syntax("import",    &tk_stop_import);
    syntax("using",     &tk_stop_using);

    syntax_stmt("var",   &tk_stop_var);
    syntax_stmt("const", &tk_stop_const);
    syntax_stmt("match", &tk_stop_match);
    syntax_stmt("when",  &tk_stop_when);

    syntax_expr("new", &tk_stop_new);
}
