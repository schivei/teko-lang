// teko.mc -- teko taught to `mc` from outside `src/`, in `ngen/` (not
// `mc/examples/`, per the dono's 2026-09-04 ruling): entrega 2 teaches the
// primitives on top of entrega 1's vertical slice. Not compiled as a
// program: `mc build` links this INSIDE a compiler (`#include <mc/core>`
// plus this file, docs/build.md § [compiler]) that then compiles a `.tk`
// source.
//
// D213 (dono 2026-09-04): reuse the mc core's own syntax; teach only the
// DELTA. `examples/lang` does not reimplement expressions/statements/types
// either -- it adds classes/generics/interfaces on top of the base. So the
// core's own spelling (`i64 name(params) { ... return e; }`, `if (c) {}`,
// the whole Pratt grammar) IS the teko-over-mc spelling here; nothing in
// this file re-teaches it.
//
// What entrega 1 added:
//   bool                                                    type_alias  (M12)
//   class interface namespace import using type              syntax      (M12, honest-stop)
//   var const match when                                     syntax_stmt (M12, honest-stop)
//   new                                                       syntax_expr (M21, honest-stop)
//
// What entrega 2 (this one) adds -- docs/design/port-teko-mc.md §3, all
// `type_alias`/library wiring, no new syntax:
//   char isize usize byte ptr str                            type_alias  (M12)
//   f32 f64 (+ ldf64/ldf32/stf64/stf32/sqrt_f64/fabs/fmin/fmax) <float> (M24)
//
// What entrega 3 (tipos, D214) adds, one construct per commit -- `struct` (no
// vtable, fields from offset 0), `class` (vtable at word 0, base-first fields,
// `virtual`/`override`) and `interface` (no object at all: a table, and itab
// dispatch through the class's vtable):
//   struct Name { type field; ... }                          syntax      (M12)
//   class Name [: Base][, Iface...] { fields, methods }      syntax      (M12)
//   interface Name { T method(self, ...); ... }              syntax      (M12)
//   trait Name { fields, methods }  +  use A, B;             syntax      (M12)
//   new Name                                                  syntax_expr (M21)
//   p.field, p.field = e, p.method(...)                       syntax_infix (M21)
//   the N_VAR of a local of struct/class/interface type       on_stmt     (M21.5)
//
// Everything else in docs/design/port-teko-mc.md §3 (types/classes,
// generics, operator overload, error-union, `service`/DI, concurrency, the
// rest of the stdlib) is a later entrega and is not stubbed here: it does
// not yet have a reserved word to stop on, so it simply is not part of the
// language this compiler accepts.

#include "teko_type.mc"
#include "teko_float.mc"
#include "teko_struct.mc"
#include "teko_iface.mc"
#include "teko_trait.mc"
#include "teko_class.mc"
#include "teko_typeof.mc"
#include "teko_stmt.mc"
#include "teko_expr.mc"

void user_init() {
    tk_types_init();
    tk_float_init();

    syntax("class",     &tk_class);
    syntax("interface", &tk_interface);
    syntax("trait",     &tk_trait);
    syntax("type",      &tk_stop_type);
    syntax("namespace", &tk_stop_namespace);
    syntax("import",    &tk_stop_import);
    syntax("using",     &tk_stop_using);

    syntax_stmt("var",   &tk_stop_var);
    syntax_stmt("const", &tk_stop_const);
    syntax_stmt("match", &tk_stop_match);
    syntax_stmt("when",  &tk_stop_when);

    syntax("struct", &tk_struct);

    syntax_expr("new", &tk_new);
    syntax_infix(".", 12, &tk_dot);

    on_stmt(&tk_on_stmt);
    pass(&tk_typeof_pass);
}
