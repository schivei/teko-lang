// teko_iface.mc -- `interface Name { signatures }`, the third construct of
// entrega 3 (D214), taught the way `mc/examples/api/oop.mc` teaches it: the
// declaration itself produces NO code at all, only a table, and a class names
// the interfaces it conforms to in the list after `:` (teko_class.mc).
//
//   interface Shape {                 (no declaration is generated)
//       i64 area(self);               type_new("Shape", 8, 8, TK_INT)
//       i64 scaled(self, i64 k);
//   }
//
//   class Rect : Shape, Named {       u8 rect_shape_mt[16]   the two methods, in order
//       ...                           u8 rect_named_mt[8]
//   }                                 u8 rect_itab[40]       { 2, (Shape, mt), (Named, mt) }
//                                     rect_vt word 0 = rect_itab
//
// Dispatch is `mc/examples/lang`'s, minus the reference counting: the object's
// word 0 is its vtable, the vtable's word 0 is the class's interface table, and
// `tk_itab` (ngen/lib/rt.mc) walks that table for the interface's own id and
// answers with the method table to index --
//
//   callp(ld64(tk_itab(ld64(obj), SHAPE) + j * 8), obj, ...)
//
// so a class implements an interface at ANY vtable slot, virtual or not, and
// two classes that share no base still answer the same interface. The id is the
// row of the type table teko_struct.mc keeps, which is a compile-time constant
// because `mc` compiles one unit at a time (docs/reference/hooks.md § 4).
//
// An interface is NOT a class: it declares no field, allocates nothing, and
// `new Shape` is refused (teko_expr.mc). The conformance check -- every method
// implemented, at the interface's own arity and return type -- is
// teko_class.mc's, at the point where it fills the tables.

#define TK_MAXIFMETH 128              // interface methods, summed across all interfaces
#define TK_MAXIMPL   64               // (class, interface) pairs, summed across all classes
#define TK_MAXCONF   8                // interfaces named in ONE class's `:` list

uptr im_name[TK_MAXIFMETH];           // the signatures of every interface, in declaration order
i64  im_np[TK_MAXIFMETH];             // parameters, not counting `self`
i64  im_nreq[TK_MAXIFMETH];           // of those, the ones with no default: the smallest call
i64  im_d0[TK_MAXIFMETH];             // where its defaults start in the default table
i64  im_ret[TK_MAXIFMETH];
i64  tk_nifmeth = 0;

i64  ci_if[TK_MAXIMPL];               // the interface row of one implementation
i64  tk_nimpl = 0;

i64  conf_if[TK_MAXCONF];             // scratch: the `:` list of the class being read
i64  tk_nconf = 0;

// ---- table accessors (no raw ld64/st64 outside this section) ----
uptr im_name_at(i64 i) { return ld64(im_name + i * 8); }
i64  im_np_at(i64 i)   { return ld64(im_np + i * 8); }
i64  im_nreq_at(i64 i) { return ld64(im_nreq + i * 8); }
i64  im_d0_at(i64 i)   { return ld64(im_d0 + i * 8); }
i64  im_ret_at(i64 i)  { return ld64(im_ret + i * 8); }
i64  ci_if_at(i64 i)   { return ld64(ci_if + i * 8); }
i64  conf_if_at(i64 i) { return ld64(conf_if + i * 8); }

void set_im_name_at(i64 i, uptr v) { st64(im_name + i * 8, v); }
void set_im_np_at(i64 i, i64 v)    { st64(im_np + i * 8, v); }
void set_im_nreq_at(i64 i, i64 v)  { st64(im_nreq + i * 8, v); }
void set_im_d0_at(i64 i, i64 v)    { st64(im_d0 + i * 8, v); }
void set_im_ret_at(i64 i, i64 v)   { st64(im_ret + i * 8, v); }
void set_ci_if_at(i64 i, i64 v)    { st64(ci_if + i * 8, v); }
void set_conf_if_at(i64 i, i64 v)  { st64(conf_if + i * 8, v); }

// the position of `name` INSIDE interface `si`, which is also its slot in the
// method table a conforming class publishes, or -1
i64 tk_ifmeth_find(i64 si, uptr name) {
    i64 i = 0;
    loop {
        if (i >= sr_mn_at(si)) break;
        if (str_eq(im_name_at(sr_m0_at(si) + i), name)) return i;
        i = i + 1;
    }
    return 0 - 1;
}

// the interface declaring `name` when the receiver's type is not known
// statically -- a parameter, which the core never reports to a module. Answering
// with the INTERFACE rather than with some class's method is what keeps that
// call honest: the itab lookup is dynamic, so it runs the right implementation
// for whatever class arrives, and panics instead of reading a foreign vtable
// slot. -1 = no interface declares it, -2 = more than one does.
i64 tk_ifmeth_by_name(uptr name) {
    i64 found = 0 - 1;
    i64 i = 0;
    loop {
        if (i >= tk_nstruct) break;
        if (tk_is_iface(i)) {
            if (tk_ifmeth_find(i, name) >= 0) {
                if (found >= 0) return 0 - 2;
                found = i;
            }
        }
        i = i + 1;
    }
    return found;
}

void tk_ifmeth_add(uptr name, i64 np, i64 nreq, i64 d0, i64 ret) {
    if (tk_nifmeth == TK_MAXIFMETH) err_at(tk_file, tk_line, "teko: too many interface methods");
    set_im_name_at(tk_nifmeth, name);
    set_im_np_at(tk_nifmeth, np);
    set_im_nreq_at(tk_nifmeth, nreq);
    set_im_d0_at(tk_nifmeth, d0);
    set_im_ret_at(tk_nifmeth, ret);
    tk_nifmeth = tk_nifmeth + 1;
}

// 1 when `ci` already lists interface `fi`
i64 tk_impl_has(i64 ci, i64 fi) {
    i64 i = 0;
    loop {
        if (i >= sr_ni_at(ci)) break;
        if (ci_if_at(sr_i0_at(ci) + i) == fi) return 1;
        i = i + 1;
    }
    return 0;
}

// appends `fi` to the class's own slice, which is the last one in the list
// because one class is read at a time
void tk_impl_add(i64 ci, i64 fi) {
    if (tk_impl_has(ci, fi)) return;
    if (tk_nimpl == TK_MAXIMPL) err_at(tk_file, tk_line, "teko: too many implemented interfaces");
    set_ci_if_at(tk_nimpl, fi);
    tk_nimpl = tk_nimpl + 1;
    set_sr_ni_at(ci, tk_nimpl - sr_i0_at(ci));
}

// the base's interfaces are the derived class's too: they are copied rather
// than followed, because the derived class publishes its OWN method tables and
// an `override` has to reach the interface as well
void tk_impls_inherit(i64 ci, i64 base) {
    set_sr_i0_at(ci, tk_nimpl);
    set_sr_ni_at(ci, 0);
    if (base < 0) return;
    i64 i = 0;
    loop {
        if (i >= sr_ni_at(base)) break;
        tk_impl_add(ci, ci_if_at(sr_i0_at(base) + i));
        i = i + 1;
    }
}

// the interfaces named in the `:` list of the class being read
void tk_conf_add(i64 fi, i64 line, uptr fl, uptr nm) {
    if (tk_nconf == TK_MAXCONF) err_at2(fl, line, "teko: too many interfaces in one class", nm);
    set_conf_if_at(tk_nconf, fi);
    tk_nconf = tk_nconf + 1;
}

// Rect + Shape  ->  rect_shape_mt    (this class's table of that interface)
uptr tk_mt_name(uptr cls, uptr iface) {
    return tk_join3(tk_case(cls, 0), "_", tk_join(tk_case(iface, 0), "_mt"));
}

// Rect  ->  rect_itab                (the { count, (id, mt)* } the vtable points at)
uptr tk_itab_name(uptr cls) { return tk_join(tk_case(cls, 0), "_itab"); }

// one signature of the body: `T m(self, ...);` and nothing else -- an interface
// declares no field, and says so rather than failing inside the parameter list
void tk_iface_member(i64 si) {
    i64 rty = p_type();
    uptr m = p_ident();
    if (p_id() != K_LPAR) err_at2(tk_file, tk_line, "teko: an interface declares methods, not fields", m);
    if (tk_ifmeth_find(si, m) >= 0) err_at2(tk_file, tk_line, "teko: duplicate interface method", m);
    i64 np = 0;
    i64 nreq = 0;
    i64 d0 = tk_ndflt;
    tk_params(&np, &nreq, 1);                    // the list itself is the class's business
    p_expect(K_SEMI, "expected ; after the interface method");
    tk_ifmeth_add(m, np, nreq, d0, rty);
    set_sr_mn_at(si, tk_nifmeth - sr_m0_at(si));
}

// ---- interface Name { T method(self, ...); ... } ----
void tk_interface() {
    tk_line = p_line();
    tk_file = p_file();
    i64 head_line = tk_line;                     // position of the `interface` word
    uptr head_file = tk_file;
    p_next();                                    // the `interface` word
    uptr name = tk_newname("interface");
    i64 ty = type_new(name, 8, 8, TK_INT);       // a name of its own type parses
    i64 si = tk_type_add(name, ty, 0 - 1, TK_KIFACE);
    set_sr_m0_at(si, tk_nifmeth);
    p_expect(K_LBRACE, "expected { in the interface body");
    loop {
        if (p_id() == K_RBRACE) break;
        if (p_id() == T_EOF) err_at(head_file, head_line, "unterminated interface");
        tk_line = p_line();                      // errors for this signature, not the interface
        tk_file = p_file();
        tk_iface_member(si);
    }
    p_next();                                    // }
    p_accept(K_SEMI);
    tk_line = head_line;                         // back to the interface's own level
    tk_file = head_file;
    if (sr_mn_at(si) == 0) err_at2(tk_file, tk_line, "teko: interface with no methods", name);
}
