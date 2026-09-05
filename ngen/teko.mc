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
// Everything else in docs/design/port-teko-mc.md §3 (types/classes,
// generics, error-union, `service`/DI, concurrency, the
// rest of the stdlib) is a later entrega and is not stubbed here: it does
// not yet have a reserved word to stop on, so it simply is not part of the
// language this compiler accepts.

#include "teko_type.mc"
#include "teko_float.mc"
#include "teko_struct.mc"
#include "teko_iface.mc"
#include "teko_trait.mc"
#include "teko_generic.mc"
#include "teko_class.mc"
#include "teko_prop.mc"
#include "teko_this.mc"
#include "teko_access.mc"
#include "teko_typeof.mc"
#include "teko_stmt.mc"
#include "teko_expr.mc"
#include "teko_params.mc"
#include "teko_over.mc"
#include "teko_ops.mc"
#include "teko_rc.mc"

void user_init() {
    tk_types_init();
    tk_float_init();
    tk_access_init();

    syntax("public",    &tk_public);
    syntax("internal",  &tk_internal);
    syntax("abstract",  &tk_abstract);
    syntax("partial",   &tk_partial);
    syntax("class",     &tk_class);
    syntax("interface", &tk_interface);
    syntax("trait",     &tk_trait);
    syntax("type",      &tk_stop_type);
    syntax("namespace", &tk_stop_namespace);
    syntax("import",    &tk_stop_import);
    syntax("using",     &tk_stop_using);

    syntax_stmt("{",     &tk_block);
    syntax_stmt("var",   &tk_stop_var);
    syntax_stmt("const", &tk_stop_const);
    syntax_stmt("match", &tk_stop_match);
    syntax_stmt("when",  &tk_stop_when);

    syntax("struct", &tk_struct);

    syntax_expr("new", &tk_new);
    syntax_expr("this", &tk_this);
    syntax_infix(".", 12, &tk_dot);
    syntax_infix("[", 12, &tk_bracket);

    on_stmt(&tk_on_stmt);

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

    pass(&tk_params_pass);
    pass(&tk_typeof_pass);

    // BEHIND the oracle, because it asks the oracle what the two operands of a
    // `+` are, and AHEAD of the overload mangling, because the call it puts in
    // the tree already names the method's own symbol -- the one teko_class.mc
    // gave the declaration -- and has nothing left for a mangling pass to pick.
    pass(&tk_ops_pass);

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
