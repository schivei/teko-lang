// teko_iface.mc -- `interface Name { signatures }`, the third construct of
// entrega 3 (D214), taught the way `mc/examples/api/oop.mc` teaches it: the
// declaration itself produces NO code at all, only a table, and a class names
// the interfaces it conforms to in the list after `:` (teko_class.mc).
//
//   interface Shape {                 (no declaration is generated)
//       i64 area();                   type_new("Shape", 8, 8, TK_INT)
//       i64 scaled(i64 k);
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
//
// Three shapes beyond the bare signature (D223, dono 2026-09-04):
//
//   i64 area() { return side() * side(); }   a DEFAULT body (C# 8): compiled as
//                                            `shape_area(uptr this)`, and the
//                                            itab of a class that does not
//                                            redeclare it points at that symbol
//   i64 Label { get; set; }                  a property: the two accessors, as
//                                            signatures (teko_prop.mc)
//   static abstract i64 unit();              C# 11: the implementing TYPE
//                                            provides it, `Square.unit()`
//                                            resolves at compile time, and it
//                                            takes no slot in any method table
//
// `this` inside a default body is the IMPLEMENTING object seen as this
// interface: the members it reaches are the interface's own, and each of them
// dispatches through the itab, so a class that redeclares one is the one that
// answers -- to the site AND to the default that called it.

#define TK_MAXIFMETH 128              // interface methods, summed across all interfaces
#define TK_MAXIMPL   64               // (class, interface) pairs, summed across all classes
#define TK_MAXCONF   8                // interfaces named in ONE class's `:` list

uptr im_name[TK_MAXIFMETH];           // the signatures of every interface, in declaration order
uptr im_sig[TK_MAXIFMETH];            // its parameter types: what a conforming class has to match
i64  im_np[TK_MAXIFMETH];             // parameters, not counting the receiver
i64  im_nreq[TK_MAXIFMETH];           // of those, the ones with no default: the smallest call
i64  im_d0[TK_MAXIFMETH];             // where its defaults start in the default table
i64  im_ret[TK_MAXIFMETH];
i64  im_static[TK_MAXIFMETH];         // 1 for a `static abstract` member: no receiver, no itab slot
i64  im_prop[TK_MAXIFMETH];           // 1 for a property's `get`, 2 for its `set`, 0 otherwise
uptr im_def[TK_MAXIFMETH];            // the symbol of its DEFAULT body, or 0
i64  tk_nifmeth = 0;

i64  ci_if[TK_MAXIMPL];               // the interface row of one implementation
i64  ci_cls[TK_MAXIMPL];              // ...and the class that implements it
i64  tk_nimpl = 0;

i64  conf_if[TK_MAXCONF];             // scratch: the `:` list of the class being read
i64  tk_nconf = 0;

// ---- table accessors (no raw ld64/st64 outside this section) ----
uptr im_name_at(i64 i) { return ld64(im_name + i * 8); }
uptr im_sig_at(i64 i)  { return ld64(im_sig + i * 8); }
i64  im_np_at(i64 i)   { return ld64(im_np + i * 8); }
i64  im_nreq_at(i64 i) { return ld64(im_nreq + i * 8); }
i64  im_d0_at(i64 i)   { return ld64(im_d0 + i * 8); }
i64  im_ret_at(i64 i)  { return ld64(im_ret + i * 8); }
i64  im_static_at(i64 i) { return ld64(im_static + i * 8); }
i64  im_prop_at(i64 i) { return ld64(im_prop + i * 8); }
uptr im_def_at(i64 i)  { return ld64(im_def + i * 8); }
i64  ci_if_at(i64 i)   { return ld64(ci_if + i * 8); }
i64  ci_cls_at(i64 i)  { return ld64(ci_cls + i * 8); }
i64  conf_if_at(i64 i) { return ld64(conf_if + i * 8); }

void set_im_name_at(i64 i, uptr v) { st64(im_name + i * 8, v); }
void set_im_sig_at(i64 i, uptr v)  { st64(im_sig + i * 8, v); }
void set_im_np_at(i64 i, i64 v)    { st64(im_np + i * 8, v); }
void set_im_nreq_at(i64 i, i64 v)  { st64(im_nreq + i * 8, v); }
void set_im_d0_at(i64 i, i64 v)    { st64(im_d0 + i * 8, v); }
void set_im_ret_at(i64 i, i64 v)   { st64(im_ret + i * 8, v); }
void set_im_static_at(i64 i, i64 v) { st64(im_static + i * 8, v); }
void set_im_prop_at(i64 i, i64 v)  { st64(im_prop + i * 8, v); }
void set_im_def_at(i64 i, uptr v)  { st64(im_def + i * 8, v); }
void set_ci_if_at(i64 i, i64 v)    { st64(ci_if + i * 8, v); }
void set_ci_cls_at(i64 i, i64 v)   { st64(ci_cls + i * 8, v); }

// where the class's k-th implemented interface sits in the list, under the same
// ownership rule the fields and the slots follow (teko_class.mc's
// tk_slot_index): what one class listed is not a contiguous run of the table
i64 tk_impl_index(i64 ci, i64 k) {
    i64 n = 0;
    i64 i = 0;
    loop {
        if (i >= tk_nimpl) break;
        if (ci_cls_at(i) == ci) {
            if (n == k) return i;
            n = n + 1;
        }
        i = i + 1;
    }
    return 0 - 1;
}
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

// the position of `name` at `sig` inside `si`, or -1: two signatures of one name
// are two entries of the table, each with its own slot
i64 tk_ifmeth_sig_find(i64 si, uptr name, uptr sig) {
    i64 i = 0;
    loop {
        if (i >= sr_mn_at(si)) break;
        if (str_eq(im_name_at(sr_m0_at(si) + i), name)) {
            if (str_eq(im_sig_at(sr_m0_at(si) + i), sig)) return i;
        }
        i = i + 1;
    }
    return 0 - 1;
}

// the position of the signature of `name` that takes `na` arguments, under the
// same rule the classes use: -1 the name is not declared, -2 it is and no
// signature takes that many arguments, -3 two do
i64 tk_ifmeth_pick(i64 si, uptr name, i64 na) {
    i64 found = 0 - 1;
    i64 named = 0;
    i64 i = 0;
    loop {
        if (i >= sr_mn_at(si)) break;
        i64 k = sr_m0_at(si) + i;
        if (str_eq(im_name_at(k), name)) {
            named = 1;
            if (na >= im_nreq_at(k) && na <= im_np_at(k)) {
                if (found >= 0) return 0 - 3;
                found = i;
            }
        }
        i = i + 1;
    }
    if (found >= 0) return found;
    if (named) return 0 - 2;
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

void tk_ifmeth_add(uptr name, uptr sig, i64 np, i64 nreq, i64 d0, i64 ret, i64 stat, uptr def) {
    if (tk_nifmeth == TK_MAXIFMETH) err_at(tk_file, tk_line, "teko: too many interface methods");
    set_im_name_at(tk_nifmeth, name);
    set_im_sig_at(tk_nifmeth, sig);
    set_im_np_at(tk_nifmeth, np);
    set_im_nreq_at(tk_nifmeth, nreq);
    set_im_d0_at(tk_nifmeth, d0);
    set_im_ret_at(tk_nifmeth, ret);
    set_im_static_at(tk_nifmeth, stat);
    set_im_def_at(tk_nifmeth, def);
    tk_nifmeth = tk_nifmeth + 1;
}

// how many members of `si` reach the method table a conforming class publishes:
// a `static abstract` one has no receiver to dispatch on, so it takes no slot
// there at all and the ones after it do not shift
i64 tk_ifinst(i64 si) {
    i64 n = 0;
    i64 i = 0;
    loop {
        if (i >= sr_mn_at(si)) break;
        if (!im_static_at(sr_m0_at(si) + i)) n = n + 1;
        i = i + 1;
    }
    return n;
}

// the slot of the member at position `j`, or -1 when it is static
i64 tk_ifslot(i64 si, i64 j) {
    if (im_static_at(sr_m0_at(si) + j)) return 0 - 1;
    i64 slot = 0;
    i64 i = 0;
    loop {
        if (i >= j) break;
        if (!im_static_at(sr_m0_at(si) + i)) slot = slot + 1;
        i = i + 1;
    }
    return slot;
}

// the indirect call an interface member is: the object's word 0 is its vtable,
// the vtable's word 0 is the class's interface table, and `tk_itab` walks that
// table for this interface's id (ngen/lib/rt.mc). The receiver is read twice --
// once for the table, once as the receiver -- so it is only accepted where
// re-evaluating it is free.
i64 tk_itab_emit(i64 left, i64 si, i64 j, i64 args, uptr m, i64 line, uptr fl) {
    if (tk_ifslot(si, j) < 0)
        err_at2(fl, line, "teko: a static interface member is reached through the type", m);
    if (!tk_pure(left))
        err_at2(fl, line, "teko: an interface call needs a name or a field on the left", m);
    tk_line = line;
    tk_file = fl;
    i64 vt = tk_call("ld64", tk_clone(left));
    i64 mt = tk_call2("tk_itab", vt, tk_int(si));
    i64 fnp = tk_call("ld64", tk_bin(K_ADD, mt, tk_int(tk_ifslot(si, j) * 8)));
    return tk_call("callp", list_append(list_append(fnp, left), args));
}

// the interface whose slice of the signature table holds entry `k`
i64 tk_iface_of_im(i64 k) {
    i64 i = 0;
    loop {
        if (i >= tk_nstruct) break;
        if (tk_is_iface(i)) {
            if (k >= sr_m0_at(i) && k < sr_m0_at(i) + sr_mn_at(i)) return i;
        }
        i = i + 1;
    }
    return 0 - 1;
}

// the interface whose DEFAULT body the function `fn` is, or -1: the pass reads
// it to know that `this` inside that body is the implementing object seen as
// this interface (teko_this.mc)
i64 tk_iface_of_def(uptr fn) {
    i64 i = 0;
    loop {
        if (i >= tk_nifmeth) break;
        if (im_def_at(i)) {
            if (str_eq(im_def_at(i), fn)) return tk_iface_of_im(i);
        }
        i = i + 1;
    }
    return 0 - 1;
}

// the accessor of an interface property a use site resolves to
i64 tk_ifprop_pick(i64 si, uptr m, i64 wantset, i64 line, uptr fl) {
    if (wantset) {
        i64 js = tk_ifmeth_pick(si, tk_set_name(m), 1);
        if (js >= 0) return js;
        err_at2(fl, line, "teko: the property has no `set`", m);
    }
    i64 jg = tk_ifmeth_pick(si, tk_get_name(m), 0);
    if (jg >= 0) return jg;
    err_at2(fl, line, "teko: the property has no `get`", m);
    return 0;
}

// 1 when `ci` already lists interface `fi`
i64 tk_impl_has(i64 ci, i64 fi) {
    i64 i = 0;
    loop {
        if (i >= tk_nimpl) break;
        if (ci_cls_at(i) == ci && ci_if_at(i) == fi) return 1;
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
    set_ci_cls_at(tk_nimpl, ci);
    tk_nimpl = tk_nimpl + 1;
    set_sr_ni_at(ci, sr_ni_at(ci) + 1);
}

// the base's interfaces are the derived class's too: they are copied rather
// than followed, because the derived class publishes its OWN method tables and
// an `override` has to reach the interface as well
void tk_impls_inherit(i64 ci, i64 base) {
    set_sr_ni_at(ci, 0);
    if (base < 0) return;
    i64 n = sr_ni_at(base);
    i64 i = 0;
    loop {
        if (i >= n) break;
        tk_impl_add(ci, ci_if_at(tk_impl_index(base, i)));
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

// the modifiers an interface member may carry: `public` says out loud what an
// interface member already is, `abstract` says out loud what a member with no
// body already is, and `static abstract` (C# 11) is the member the implementing
// TYPE provides -- resolved by that type at compile time, so it has no receiver
// and reaches no method table.
void tk_iface_mods(uptr pstat, uptr pabst) {
    i64 stat = 0;
    i64 abst = 0;
    loop {
        if (tk_word("public")) { p_next(); continue; }
        if (tk_word("private") || tk_word("protected"))
            err_at2(p_file(), p_line(), "teko: an interface member is public", p_name());
        if (tk_word("static")) {
            if (stat) err_at(p_file(), p_line(), "teko: the member is already static");
            stat = 1;
            p_next();
            continue;
        }
        if (tk_word("abstract")) {
            if (abst) err_at(p_file(), p_line(), "teko: the member is already abstract");
            abst = 1;
            p_next();
            continue;
        }
        break;
    }
    if (stat && !abst) err_at(p_file(), p_line(), "teko: a static interface member is `static abstract`");
    st64(pstat, stat);
    st64(pabst, abst);
}

// the symbol an interface's DEFAULT body is compiled under, by the rule
// teko_class.mc gives a class's methods: the first member of a name keeps the
// plain `iface_m`, and an overload after it carries its own signature
uptr tk_ifdef_symbol(i64 si, uptr m, uptr sig) {
    uptr fn = tk_fname(sr_name_at(si), m);
    if (tk_ifmeth_find(si, m) < 0) return fn;
    if (cstrlen(sig) == 0) return tk_join(fn, "__void");
    return tk_join(fn, sig);
}

// one accessor of an interface property: a signature, and never a body -- the
// class that conforms is where an accessor of this property is written
i64 tk_iface_accessor(i64 si, uptr m, i64 rty, i64 stat) {
    i64 wantset = 0;
    if (tk_kw("set")) wantset = 1;
    else if (!tk_kw("get")) err_at2(p_file(), p_line(), "teko: a property declares `get`, `set` or both", m);
    p_next();
    uptr acc = tk_get_name(m);
    i64 aty = rty;
    if (wantset) {
        acc = tk_set_name(m);
        aty = TY_VOID;
    }
    i64 params = tk_prop_params(rty, wantset, stat);
    uptr sig = tk_sig_of(params, !stat);
    if (tk_ifmeth_sig_find(si, acc, sig) >= 0)
        err_at2(tk_file, tk_line, "teko: the property already declares this accessor", m);
    if (!p_accept(K_SEMI))
        err_at2(p_file(), p_line(), "teko: an interface property declares accessors, not bodies", m);
    tk_ifmeth_add(acc, sig, wantset, wantset, tk_ndflt, aty, stat, 0);
    set_im_prop_at(tk_nifmeth - 1, wantset + 1);
    set_sr_mn_at(si, tk_nifmeth - sr_m0_at(si));
    return 1;
}

// `T Name { get; set; }` in an interface: the two signatures a conforming
// class's own property answers with
void tk_iface_prop(i64 si, uptr m, i64 rty, i64 stat) {
    if (rty == TY_VOID) err_at2(tk_file, tk_line, "teko: property of type void", m);
    if (tk_prop_own(si, m) >= 0) err_at2(tk_file, tk_line, "teko: duplicate property", m);
    p_expect(K_LBRACE, "expected { in the property body");
    i64 n = 0;
    loop {
        if (p_id() == K_RBRACE) break;
        if (p_id() == T_EOF) err_at2(tk_file, tk_line, "teko: unterminated property", m);
        n = n + tk_iface_accessor(si, m, rty, stat);
    }
    p_next();                                    // }
    p_accept(K_SEMI);
    if (n == 0) err_at2(tk_file, tk_line, "teko: a property declares `get`, `set` or both", m);
    tk_prop_add(si, m, rty, 0 - 1);
}

// one member of the body: `T m(...);`, a property, or a method with a DEFAULT
// BODY (C# 8), which the classes that do not redeclare it answer with. An
// interface declares no field, and says so rather than failing inside the
// parameter list.
void tk_iface_member(i64 si) {
    i64 stat = 0;
    i64 abst = 0;
    tk_iface_mods(&stat, &abst);
    i64 rty = p_type();
    if (tk_kw("operator"))
        err_at(p_file(), p_line(), "teko: an operator is declared by a class or a struct, not by an interface");
    uptr m = p_ident();
    if (p_id() == K_LBRACE) {
        tk_iface_prop(si, m, rty, stat);
        return;
    }
    if (p_id() != K_LPAR)
        err_at2(tk_file, tk_line, "teko: an interface declares methods and properties, not fields", m);
    i64 np = 0;
    i64 nreq = 0;
    i64 d0 = tk_ndflt;
    i64 extra = 1;                               // the vtable word a dispatch spends
    if (stat) extra = 0;
    i64 params = tk_params(&np, &nreq, extra, !stat);
    uptr sig = tk_sig_of(params, !stat);
    if (tk_ifmeth_sig_find(si, m, sig) >= 0) err_at2(tk_file, tk_line, "teko: duplicate interface method", m);
    uptr def = 0;
    if (p_id() == K_LBRACE) def = tk_ifdef_symbol(si, m, sig);
    else p_expect(K_SEMI, "expected ; after the interface method");
    if (def && abst) err_at2(tk_file, tk_line, "teko: an abstract interface member has no body", m);
    tk_ifmeth_add(m, sig, np, nreq, d0, rty, stat, def);
    set_sr_mn_at(si, tk_nifmeth - sr_m0_at(si));
    if (def) tk_member_body(si, rty, def, params, 0);
}

// ---- interface Name { T method(...); ... } ----
void tk_interface() {
    tk_line = p_line();
    tk_file = p_file();
    i64 head_line = tk_line;                     // position of the `interface` word
    uptr head_file = tk_file;
    p_next();                                    // the `interface` word
    i64 vis = tk_take_decl_vis();                // the `public`/`internal` before the word
    i64 proj = tk_take_decl_proj();
    uptr name = tk_newname("interface");
    i64 ty = tk_type_word(name);                 // a name of its own type parses
    i64 si = tk_type_add(name, ty, 0 - 1, TK_KIFACE, vis, proj);
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
