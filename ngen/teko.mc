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
//   interface Name { T method(...); ... }                    syntax      (M12)
//   trait Name { fields, methods }  +  use A, B;             syntax      (M12)
//   new Name                                                  syntax_expr (M21)
//   p.field, p.field = e, p.method(...)                       syntax_infix (M21)
//   the N_VAR of a local of struct/class/interface type       on_stmt     (M21.5)
//   { ... } -- every block, so the locals table has a scope   syntax_stmt (M21.5)
//
// What entrega 4's C8 adds -- generics with constants, by record and replay
// (teko_generic.mc), which is what makes a bound a compile-time constant:
//   class/struct Name<T, const N: i64> { ... }                p_skip_balanced
//   Name<Circle, 4> x = new Name<Circle, 4>;                  p_push_source + p_subst_*
//   T items[N]  /  x.items[i]                                 an inline array field
//
// and C7c makes `params` one of them -- a declaration generic in its COUNT,
// instantiated per call site so the bound is a constant there too:
//   i64 total(params xs) { ... xs_len ... xs[i] ... }          teko_params.mc
//   total(1, 2)  ->  total__2(<two words>)                     one body per count
// What entrega 5's C5b adds -- operator overloading as C# writes it, a STATIC
// member naming both operands and resolved over both of them (teko_ops.mc):
//   public static Vec operator+(Vec a, Vec b)                  a contextual word
//   public static Vec operator+(i64 k, Vec v)                  the reversed form
//   public static Vec operator-(Vec a)                         the unary form
//   public static i64 operator==  /  operator!=                declared in pairs
//   a + b  /  (a + b) == c  /  2 + v  /  -a                    pass() over
//                                                              N_BINARY, N_UNARY
// What entrega 4's C6 adds -- a default parameter value in a free
// (top-level) function, unblocked by mc 0.10.3's `syntax_param`
// (teko_default.mc):
//   i64 add(i64 a, i64 b = 10) { return a + b; }              syntax_param
//   add(1)                                                     pass() ->
//                                                              add(1, 10)
//   add(1) against an overload of `add` that needs no default  teko_over.mc's
//                                                              own fourth round
//
// What entrega 5's member crumb adds (D220, teko_access.mc) -- C#'s modifiers,
// with C#'s defaults (a type is `internal`, a member is `private`):
//   public/internal before class/struct/interface/trait        syntax      (M12)
//   public/private/protected/static on a member                read by tk_member
//   Name.field, Name.field = e, Name.m(...)                    syntax_expr (M21)
//   the statement either of those two opens                    syntax_stmt (M21.5)
//
// What entrega 5's property crumb adds (D223, teko_prop.mc) -- C#'s properties
// and the interface C# 8 and C# 11 grew, both read by the machinery above:
//   T Name { get; set; }  /  { get => e; set => s; }  /  { get { } }
//   `value` inside a `set`, visibility per accessor, virtual/override/static
//   p.Name, p.Name = e, Name (inside the type), Type.Name for a static one
//   interface: T Name { get; set; }, a method WITH a body, `static abstract`
//
// What entrega 5's `abstract` crumb adds (D224) -- C#'s own abstract class:
//   abstract class Shape { public abstract i64 area(); }      syntax (M12)
//   an abstract member is a vtable slot with nothing in it, which the first
//   concrete class of the chain has to fill with an `override`; `new Shape` is
//   refused, and the class emits no vtable and no constructor at all
//   partial class Shape { ... }   the same class, declared in more than one
//   partial class Shape { ... }   place; a part after the first adds members to
//                                 the row the first one opened, and the type
//                                 closes at its first use or at the end of the
//                                 unit -- a part written after that is refused
//
// What entrega 5's reclaim crumb adds (D218) -- the arena gives memory back, and
// C#'s two members that say when:
//   the object grows a reference count (word 1) and the vtable a release (word 0)
//   public Name(params) { } / : base(args)                    a constructor
//   ~Name() { }                                               a destructor
//   new Name(args)                                            picks by signature
//   the reference counting itself, injected over every body   pass (teko_rc.mc)
//
// What entrega 5's `while`/`do`/`for` crumb adds (D218/D221/D226,
// teko_loop.mc) -- C#'s three loops, lowered at parse time to the core's own
// `loop`/`if`/`break N` (kept, D221):
//   while (c) stmt                                              syntax_stmt
//   do stmt while (c);                                          syntax_stmt
//   for (init; cond; step) stmt                                 syntax_stmt
//   x++;  x--;  x += e;  x -= e;   (also a `for` loop's step)   word_add +
//                                                               a pushed #rule
//
// What entrega 5's M45 crumb adds -- the unary `+` (teko_ops.mc's own
// `tk_unary_plus`, the mc 0.13.0 release the `syntax_expr`/`parse_expr`
// route came from) and the two boolean literals (teko_type.mc):
//   +v  (a type declares `operator+(T a)`)                     syntax_expr,
//   +i  (over a core type: the identity, `+i == i`)             pass()
//   true  false                                                 syntax_expr
//
// What entrega 5's N1 crumb adds -- `namespace`/`using` and qualified type
// names (teko_ns.mc; a free function inside a namespace stays plain, N2's):
//   namespace A.B { class Circle { ... } }                      syntax
//   namespace A.B;                          (file-scoped, C# 10)  syntax
//   using geo;                                                  syntax
//   Circle c = new Circle();  (the short name, inside or with a `using`)
//   geo.Circle c = new geo.Circle();  geo.Circle.made           the segment
//
// What entrega 5's N2 crumb adds -- a free function declared inside a
// namespace, mangled the same way a type already is (teko_ns.mc's own
// `tk_ns_pass`):
//   namespace geo { i64 area(i64 r) { ... } }                    geo__area
//   geo.area(x)                              (qualified)         syntax_expr/
//                                                                 syntax_stmt
//   area(x)     (bare, from inside geo or through a `using geo;`) pass()
//
// What entrega 5's N3 crumb adds -- `import`, sugar over the core's own
// `lex_include` plus an implicit `using` (teko_ns.mc's own `tk_import`):
//   import parts.geo;             (once-only, "parts/geo.tk")      syntax
//   Circle c = new Circle();  parts.geo.twice(x)  &twice           the
//                              bare/qualified forms `using` already gives
//
// What entrega 5's `const` crumb adds -- a folded constant, sugar over the
// core's own `#define` (D218, teko_const.mc):
//   const i64 N = 10;                  top level, or `geo__N` in a namespace  syntax
//   N  /  geo.N  (qualified)  /  N  (bare, from inside geo or a `using`)
//   var x[N];                          array size: the core's own `#define` folding
//   Box<Circle, N>                     a generic's own `const` bound (teko_generic.mc)
//   public const i64 MAX = 4;          a class/struct member, no slot
//   Nome.MAX  /  MAX (bare, inside)    the same two forms a static member takes
//
// What entrega 5's ternary crumb adds -- C#'s `c ? a : b`, associative to the
// right, hoisted at a pass() to the `if`/local the core already lowers to
// (D228, teko_ternary.mc):
//   c ? a : b                          an initializer, a return, an argument   syntax_infix
//   c1 ? a : c2 ? b : d                 right-associative, lazy either way
//   while (c ? 1 : 0) { ... }           the hoist lands inside the loop's own body
//
// What entrega 5's `switch` crumb adds -- both of C#'s own spellings
// (D222/D228, teko_switch.mc): the statement rebaixed at parse time to a
// one-lap `loop`/`if` chain (teko_loop.mc's own shape), and the expression as
// sugar over the ternary's own placeholder chain (D228, no machinery of its
// own):
//   switch (x) { case 1: ... break; case 2: case 3: ... break; default: ... }  syntax_stmt
//   case N when cond:                   a guarded label; `default` moves last
//   break; / break N;                    the switch counts as ONE loop level
//   x switch { 1 => a, 2 or 3 => b, _ when c => d, _ => e }                    syntax_infix
//
// Everything else in docs/design/port-teko-mc.md §3 (types/classes,
// generics, error-union, `service`/DI, concurrency, the
// rest of the stdlib) is a later entrega and is not stubbed here: it does
// not yet have a reserved word to stop on, so it simply is not part of the
// language this compiler accepts.

#include "teko_type.mc"
#include "teko_float.mc"
#include "teko_struct.mc"
#include "teko_const.mc"
#include "teko_ns.mc"
#include "teko_iface.mc"
#include "teko_trait.mc"
#include "teko_generic.mc"
#include "teko_class.mc"
#include "teko_prop.mc"
#include "teko_this.mc"
#include "teko_access.mc"
#include "teko_typeof.mc"
#include "teko_ternary.mc"
#include "teko_stmt.mc"
#include "teko_expr.mc"
#include "teko_params.mc"
#include "teko_default.mc"
#include "teko_over.mc"
#include "teko_ops.mc"
#include "teko_rc.mc"
#include "teko_loop.mc"
#include "teko_switch.mc"

void user_init() {
    tk_types_init();
    tk_float_init();
    tk_access_init();
    tk_loop_init();
    tk_ns_init();

    syntax("public",    &tk_public);
    syntax("internal",  &tk_internal);
    syntax("abstract",  &tk_abstract);
    syntax("partial",   &tk_partial);
    syntax("class",     &tk_class);
    syntax("interface", &tk_interface);
    syntax("trait",     &tk_trait);
    syntax("type",      &tk_stop_type);
    syntax("namespace", &tk_namespace);
    syntax("import",    &tk_import);
    syntax("using",     &tk_using);
    syntax("const",     &tk_const_top);

    syntax_stmt("{",     &tk_block);
    syntax_stmt("var",   &tk_stop_var);
    syntax_stmt("const", &tk_stop_const);
    syntax_stmt("match", &tk_stop_match);
    syntax_stmt("when",  &tk_stop_when);
    syntax_stmt("while",  &tk_while);
    syntax_stmt("do",     &tk_do);
    syntax_stmt("for",    &tk_for);
    syntax_stmt("switch", &tk_switch_stmt);

    syntax("struct", &tk_struct);

    syntax_expr("new", &tk_new);
    syntax_expr("this", &tk_this);
    syntax_expr("+", &tk_unary_plus);
    syntax_expr("true", &tk_true);
    syntax_expr("false", &tk_false);
    syntax_infix(".", 12, &tk_dot);
    syntax_infix("[", 12, &tk_bracket);
    syntax_infix("?", TK_TERN_PREC, &tk_tern_infix);
    syntax_infix("switch", TK_TERN_PREC, &tk_switch_infix);

    on_stmt(&tk_on_stmt);

    // C6: a default parameter value in a free function's own list. The only
    // `syntax_param` registration in this compiler, so registration order
    // among handlers does not come up -- see teko_default.mc's own header.
    syntax_param(&tk_default_param);

    // FIRST among the passes, and a later one must not slip in front: the
    // `params` pass INSTANTIATES `i64 total(params xs)` once per argument count
    // -- `total(a, b, c)` becomes `total__3(ptr)`, with the count a constant
    // inside the body -- and the declaration that was generic leaves the unit.
    // Whatever comes next -- the overload mangling of C4 above all -- then sees
    // ordinary functions with ordinary parameters, and never has to know what a
    // `params` list is. Running after it would mean mangling a declaration the
    // `params` pass is about to take away.
    // AHEAD of every other pass: a partial class no use closed is closed here,
    // and what that emits -- a constructor, a vtable initializer -- is an
    // ordinary declaration the passes below have to see like any other.
    pass(&tk_partial_pass);

    // entrega 5, crumb N2: every namespaced free function/prototype is
    // mangled and every bare call inside a namespace resolved before ANY
    // pass that censuses by name -- `params`, the oracle, overload
    // mangling and defaults all have to see the FINAL symbol.
    pass(&tk_ns_pass);

    pass(&tk_params_pass);
    pass(&tk_typeof_pass);

    // entrega 5, ternary crumb (D228): BEHIND the oracle, because `tk_ty_of`
    // -- what this pass leans on to type an arm -- only answers a `.` on a
    // receiver the parser could not type once teko_typeof.mc's own pass has
    // rewritten the deferred placeholder into the load/call it stands for;
    // ahead of every pass below, so none of them has to know a ternary was
    // ever written (teko_ternary.mc's own header spells the whole ordering
    // out, arm by arm).
    pass(&tk_ternary_pass);

    // BEHIND the oracle, because it asks the oracle what the two operands of a
    // `+` are, and AHEAD of the overload mangling, because the call it puts in
    // the tree already names the method's own symbol -- the one teko_class.mc
    // gave the declaration -- and has nothing left for a mangling pass to pick.
    pass(&tk_ops_pass);

    // C6's own default-fill, for a name declared exactly once: no type to
    // compare, so it needs neither the oracle above nor the overload mangling
    // below, and it must run BEFORE that mangling -- a name declared MORE
    // than once is left untouched here on purpose (teko_default.mc's own
    // header explains why) and is what the fourth round tk_over_pass adds
    // just below resolves instead, defaults included.
    pass(&tk_default_pass);

    // BEHIND both of the above: the overload mangling reads ordinary
    // parameter lists (a `params` list is gone by now, replaced by its
    // instances, whose `__k` suffix is a number and cannot collide with the
    // type-named suffix an overload takes) and ordinary calls (the oracle's
    // pass has already rebuilt every deferred `.`), and it renames
    // declarations, which is the one rewrite the two before it would not
    // survive.
    pass(&tk_over_pass);

    // LAST of all. Ownership is a question about a value's static TYPE: the
    // oracle has to have rewritten every deferred `.` first, the operators have
    // to have become the calls they are, and the overloads have to carry their
    // own symbols -- two `pick`s spelled the same are two declarations of one
    // name until the mangling, and only one of them may answer with an object.
    pass(&tk_rc_pass);
}
