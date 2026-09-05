// teko_class.mc -- `class Name [: Base] [, Interface...] { fields and methods }`,
// the second construct of entrega 3 (D214), and the honest-stops for the
// top-level words teko still owes. A class is a struct (teko_struct.mc's
// tables) plus three things: word 0 of the object is the vtable, fields are
// laid out BASE-FIRST (so a derived object is a valid base object), and methods
// take a receiver the source does not write (D219, teko_this.mc; the layout
// machinery is `mc/examples/lang` § Layout and `mc/examples/api/oop.mc`).
//
//   class Shape {                     u8 shape_vt[16] (word 0 = itab, then the slots)
//       i64 side;                     #define SHAPE_SIDE 8   (object word 0 = vtable)
//       virtual i64 area() {          i64 shape_area(uptr this)  -> vtable slot 0
//           return side;              void shape_vt_init()
//       }                             Shape shape_new()
//   }                                 type_new("Shape", 8, 8, TK_INT)
//
//   class Square : Shape {            #define SQUARE_SIZE 16 (Shape's fields kept)
//       override i64 area() {         i64 square_area(uptr this) -> the SAME slot 0
//           return side * this.side;
//       }
//   }
//
// `virtual` and `override` are CONTEXTUAL: this module does not reserve them
// (lang's `lg_kw`), so they stay usable as ordinary names everywhere else. A
// plain method is called directly, a virtual one through
// `callp(ld64(ld64(obj) + (1 + slot) * 8), obj, ...)` -- teko_expr.mc, which is
// also where `p.field` and `p.method(...)` are resolved.
//
// The names after `:` are one base class at most, first, and then the
// interfaces the class conforms to: the tables that answer them, and the
// conformance check itself, are here (teko_iface.mc holds the declaration).
//
// One limit, the same one `mc/examples/lang` has: a QUALIFIED name resolves in
// declaration order, so `this.m()` reaches the methods declared above it in the
// same body. An unqualified `m()` is decided in the pass (teko_this.mc), where
// the whole body has been read, and reaches them all.
//
// A name may be OVERLOADED: methods are keyed by (name, parameter types), the
// first one of a name keeps the plain `class_method` symbol and the ones after
// it carry their signature, and a virtual overload is a slot of its own -- so an
// `override` fills the inherited slot of ITS signature. A call resolves by how
// many arguments it passes; two signatures taking the same number are refused at
// the call site, because the argument types are what would tell them apart.
//
// A parameter may carry a DEFAULT (`i64 area(i64 k = 2)`): the constant is
// folded once, at the declaration, and every call that omits the argument gets a
// clone of it. It is a property of the declaration the call site resolves
// against, not of the vtable -- a `virtual`/`override` pair keeps one slot, and
// each of the two declarations answers with its own default.
//
// What is NOT here, and stops with a message rather than a surprise: `type`,
// `namespace`, `import` and `using` (docs/design/port-teko-mc.md §3).

// teko_ops.mc is included after this file, because its pass reads the method
// table built here; these two are what a member declaration has to ask it, the
// name `operator+` resolves to and the shape such a declaration has to have
uptr tk_op_name(uptr pisop);
void tk_op_decl_check(i64 np, i64 nreq, i64 fty);

// teko_this.mc is included after this file for the same reason -- it reads the
// method table too -- and these four are the receiver's side of a member
// declaration: the name the prepended parameter carries, the refusal of the old
// explicit one, and the body a `this`/`base` inside it belongs to
uptr tk_this_name();
void tk_reject_self(uptr name);
i64 tk_this_enter_body(i64 ci);
void tk_this_leave_body(i64 keep);

#define TK_MAXMETHOD 128              // methods, summed across all classes
#define TK_MAXVSLOT  128              // virtual slots, summed across all classes
#define TK_MAXDFLT   64               // default arguments, summed across all signatures
#define TK_VT_FIXED  1                // vtable word 0 is the interface table; slots follow

uptr mt_name[TK_MAXMETHOD];
i64  mt_cls[TK_MAXMETHOD];
uptr mt_sig[TK_MAXMETHOD];            // its parameter types, `__i64__Point`: the overload key
uptr mt_fn[TK_MAXMETHOD];             // the mangled name: class_method, class_method__i64...
i64  mt_np[TK_MAXMETHOD];             // parameters, not counting the receiver
i64  mt_nreq[TK_MAXMETHOD];           // of those, the ones with no default: the smallest call
i64  mt_d0[TK_MAXMETHOD];             // where its defaults start in the default table
i64  mt_ret[TK_MAXMETHOD];
i64  mt_slot[TK_MAXMETHOD];           // its vtable slot, or -1 when not virtual
i64  tk_nmethod = 0;

i64  df_node[TK_MAXDFLT];             // the folded constant a missing argument becomes
i64  tk_ndflt = 0;

uptr vs_m[TK_MAXVSLOT];               // the method name a slot answers to
uptr vs_sig[TK_MAXVSLOT];             // ...at that signature: an overload is a slot of its own
uptr vs_fn[TK_MAXVSLOT];              // the function currently filling it
i64  tk_nvslot = 0;

uptr mt_name_at(i64 i) { return ld64(mt_name + i * 8); }
i64  mt_cls_at(i64 i)  { return ld64(mt_cls + i * 8); }
uptr mt_sig_at(i64 i)  { return ld64(mt_sig + i * 8); }
uptr mt_fn_at(i64 i)   { return ld64(mt_fn + i * 8); }
i64  mt_np_at(i64 i)   { return ld64(mt_np + i * 8); }
i64  mt_nreq_at(i64 i) { return ld64(mt_nreq + i * 8); }
i64  mt_d0_at(i64 i)   { return ld64(mt_d0 + i * 8); }
i64  mt_ret_at(i64 i)  { return ld64(mt_ret + i * 8); }
i64  mt_slot_at(i64 i) { return ld64(mt_slot + i * 8); }
uptr vs_m_at(i64 i)    { return ld64(vs_m + i * 8); }
uptr vs_sig_at(i64 i)  { return ld64(vs_sig + i * 8); }
uptr vs_fn_at(i64 i)   { return ld64(vs_fn + i * 8); }
i64  df_node_at(i64 i) { return ld64(df_node + i * 8); }

void set_mt_name_at(i64 i, uptr v) { st64(mt_name + i * 8, v); }
void set_mt_cls_at(i64 i, i64 v)   { st64(mt_cls + i * 8, v); }
void set_mt_sig_at(i64 i, uptr v)  { st64(mt_sig + i * 8, v); }
void set_mt_fn_at(i64 i, uptr v)   { st64(mt_fn + i * 8, v); }
void set_mt_np_at(i64 i, i64 v)    { st64(mt_np + i * 8, v); }
void set_mt_nreq_at(i64 i, i64 v)  { st64(mt_nreq + i * 8, v); }
void set_mt_d0_at(i64 i, i64 v)    { st64(mt_d0 + i * 8, v); }
void set_mt_ret_at(i64 i, i64 v)   { st64(mt_ret + i * 8, v); }
void set_mt_slot_at(i64 i, i64 v)  { st64(mt_slot + i * 8, v); }
void set_vs_m_at(i64 i, uptr v)    { st64(vs_m + i * 8, v); }
void set_vs_sig_at(i64 i, uptr v)  { st64(vs_sig + i * 8, v); }
void set_vs_fn_at(i64 i, uptr v)   { st64(vs_fn + i * 8, v); }
void set_df_node_at(i64 i, i64 v)  { st64(df_node + i * 8, v); }

// the signature key of a parameter list: `__i64__Point` for `(i64 a, Point p)`,
// and empty for a method that takes no parameter at all. It is what tells two
// overloads apart -- one name, one class, different parameter types -- and the
// suffix that keeps their two symbols apart.
uptr tk_sig_of(i64 params) {
    uptr s = "";
    i64 p = nd_next(params);                     // past the receiver, which every method has
    loop {
        if (p == 0) break;
        s = tk_join3(s, "__", type_name(nd_type(p)));
        p = nd_next(p);
    }
    return s;
}

// the first method called `name` in `ci` or in one of its bases, whatever its
// signature, or -1: the question "does this type have such a member at all",
// which comes before the arguments are even read
i64 tk_method_named_find(i64 ci, uptr name) {
    loop {
        if (ci < 0) break;
        i64 i = 0;
        loop {
            if (i >= tk_nmethod) break;
            if (mt_cls_at(i) == ci) {
                if (str_eq(mt_name_at(i), name)) return i;
            }
            i = i + 1;
        }
        ci = sr_base_at(ci);
    }
    return 0 - 1;
}

// the method `name` with signature `sig` in `ci` or in one of its bases, or -1
i64 tk_method_sig_find(i64 ci, uptr name, uptr sig) {
    loop {
        if (ci < 0) break;
        i64 i = 0;
        loop {
            if (i >= tk_nmethod) break;
            if (mt_cls_at(i) == ci && str_eq(mt_name_at(i), name)) {
                if (str_eq(mt_sig_at(i), sig)) return i;
            }
            i = i + 1;
        }
        ci = sr_base_at(ci);
    }
    return 0 - 1;
}

// the method `name` at signature `sig` declared by `ci` ITSELF, or -1: a
// redeclaration of the same signature in the same body is a duplicate, one at
// another signature is an OVERLOAD, and one that collides with a base's is the
// business of tk_slot_take (an inherited virtual needs `override`)
i64 tk_method_own(i64 ci, uptr name, uptr sig) {
    i64 i = 0;
    loop {
        if (i >= tk_nmethod) break;
        if (mt_cls_at(i) == ci && str_eq(mt_name_at(i), name)) {
            if (str_eq(mt_sig_at(i), sig)) return i;
        }
        i = i + 1;
    }
    return 0 - 1;
}

// 1 when `ci` already registered a method under `name`, at any signature: the
// next one to arrive is an overload, and takes the signature suffix on its
// symbol so that the FIRST one keeps the plain `class_method`
i64 tk_method_overloads(i64 ci, uptr name) {
    i64 i = 0;
    loop {
        if (i >= tk_nmethod) break;
        if (mt_cls_at(i) == ci) {
            if (str_eq(mt_name_at(i), name)) return 1;
        }
        i = i + 1;
    }
    return 0;
}

// 1 when a call passing `na` arguments fits the method: the parameters with no
// default are the floor, all of them the ceiling
i64 tk_method_fits(i64 i, i64 na) {
    if (na < mt_nreq_at(i)) return 0;
    if (na > mt_np_at(i)) return 0;
    return 1;
}

// the method of `ci` (or of a base) called `name` that takes `na` arguments, ONE
// level of the chain at a time -- so a class's own declaration hides the base's
// overloads of that name, as it does in C#. -1: nobody declares the name; -2:
// the name is there and no signature takes that many arguments; -3: two do, and
// only the argument types could tell them apart -- which is what a module cannot
// ask the core about mid-body (`decl_*` answers after the declaration closes).
i64 tk_method_pick(i64 ci, uptr name, i64 na) {
    i64 named = 0;
    loop {
        if (ci < 0) break;
        i64 found = 0 - 1;
        i64 i = 0;
        loop {
            if (i >= tk_nmethod) break;
            if (mt_cls_at(i) == ci && str_eq(mt_name_at(i), name)) {
                named = 1;
                if (tk_method_fits(i, na)) {
                    if (found >= 0) return 0 - 3;
                    found = i;
                }
            }
            i = i + 1;
        }
        if (found >= 0) return found;
        ci = sr_base_at(ci);
    }
    if (named) return 0 - 2;
    return 0 - 1;
}

// 1 when `a` is `b` or one of its bases
i64 tk_is_ancestor(i64 a, i64 b) {
    loop {
        if (b < 0) break;
        if (a == b) return 1;
        b = sr_base_at(b);
    }
    return 0;
}

// 1 when some class declares a method called `name`
i64 tk_method_has_name(uptr name) {
    i64 i = 0;
    loop {
        if (i >= tk_nmethod) break;
        if (str_eq(mt_name_at(i), name)) return 1;
        i = i + 1;
    }
    return 0;
}

// the method called `name` taking `na` arguments when the receiver's class is
// not known statically, under the same rule tk_field_by_name uses -- except that
// an `override` is not a second method: candidates inside ONE inheritance chain
// answer with the BASE's declaration, whose slot is the one the whole chain
// shares. -1 = no signature takes that many arguments, -2 = two unrelated
// classes answer, -3 = two signatures of ONE class do.
i64 tk_method_by_name(uptr name, i64 na) {
    i64 found = 0 - 1;
    i64 i = 0;
    loop {
        if (i >= tk_nmethod) break;
        if (str_eq(mt_name_at(i), name) && tk_method_fits(i, na)) {
            if (found < 0) found = i;
            else if (mt_cls_at(i) == mt_cls_at(found)) return 0 - 3;
            else if (tk_is_ancestor(mt_cls_at(i), mt_cls_at(found))) found = i;
            else if (!tk_is_ancestor(mt_cls_at(found), mt_cls_at(i))) return 0 - 2;
        }
        i = i + 1;
    }
    return found;
}

i64 tk_method_add(uptr name, i64 ci, uptr sig, uptr fn, i64 np, i64 nreq, i64 d0, i64 ret, i64 slot) {
    if (tk_nmethod == TK_MAXMETHOD) err_at(tk_file, tk_line, "teko: too many methods");
    set_mt_name_at(tk_nmethod, name);
    set_mt_cls_at(tk_nmethod, ci);
    set_mt_sig_at(tk_nmethod, sig);
    set_mt_fn_at(tk_nmethod, fn);
    set_mt_np_at(tk_nmethod, np);
    set_mt_nreq_at(tk_nmethod, nreq);
    set_mt_d0_at(tk_nmethod, d0);
    set_mt_ret_at(tk_nmethod, ret);
    set_mt_slot_at(tk_nmethod, slot);
    tk_nmethod = tk_nmethod + 1;
    return tk_nmethod - 1;
}

// 1 when the current token is the identifier `w` -- a contextual keyword this
// module deliberately does NOT reserve
i64 tk_kw(uptr w) {
    if (p_id() != T_IDENT) return 0;
    return str_eq(p_name(), w);
}

// the base's slots, copied so the derived class's table is a PREFIX of its
// base's: a slot number means the same thing in both, which is what makes a
// virtual call correct through a base-typed name
void tk_slots_inherit(i64 ci, i64 base) {
    i64 v0 = tk_nvslot;
    set_sr_v0_at(ci, v0);
    set_sr_nv_at(ci, 0);
    if (base < 0) return;
    i64 n = sr_nv_at(base);
    i64 i = 0;
    loop {
        if (i >= n) break;
        if (tk_nvslot == TK_MAXVSLOT) err_at(tk_file, tk_line, "teko: too many virtual slots");
        set_vs_m_at(tk_nvslot, vs_m_at(sr_v0_at(base) + i));
        set_vs_sig_at(tk_nvslot, vs_sig_at(sr_v0_at(base) + i));
        set_vs_fn_at(tk_nvslot, vs_fn_at(sr_v0_at(base) + i));
        tk_nvslot = tk_nvslot + 1;
        i = i + 1;
    }
    set_sr_nv_at(ci, n);
}

// the slot of the class's own slice that answers to `name` at `sig`, or -1: an
// overload is a slot of its own, so a derived class that adds `area(i64)`
// beside the inherited `area()` extends the table instead of colliding with it
i64 tk_slot_find(i64 ci, uptr name, uptr sig) {
    i64 i = 0;
    loop {
        if (i >= sr_nv_at(ci)) break;
        if (str_eq(vs_m_at(sr_v0_at(ci) + i), name)) {
            if (str_eq(vs_sig_at(sr_v0_at(ci) + i), sig)) return i;
        }
        i = i + 1;
    }
    return 0 - 1;
}

// `override` fills an inherited slot, `virtual` takes a new one, and a plain
// method that collides with an inherited slot is refused rather than silently
// hiding it. Returns the method's slot, or -1 when it is not virtual.
i64 tk_slot_take(i64 ci, i64 kind, uptr m, uptr sig, uptr fn) {
    i64 slot = tk_slot_find(ci, m, sig);
    if (kind == 2) {
        if (slot < 0) err_at2(tk_file, tk_line, "teko: override of a method the base does not declare", m);
        set_vs_fn_at(sr_v0_at(ci) + slot, fn);
        return slot;
    }
    if (kind == 1) {
        if (slot >= 0) err_at2(tk_file, tk_line, "teko: virtual redeclares an inherited slot; use override", m);
        if (tk_nvslot == TK_MAXVSLOT) err_at(tk_file, tk_line, "teko: too many virtual slots");
        set_vs_m_at(tk_nvslot, m);
        set_vs_sig_at(tk_nvslot, sig);
        set_vs_fn_at(tk_nvslot, fn);
        tk_nvslot = tk_nvslot + 1;
        set_sr_nv_at(ci, sr_nv_at(ci) + 1);
        return sr_nv_at(ci) - 1;
    }
    if (slot >= 0) err_at2(tk_file, tk_line, "teko: method hides an inherited virtual; use override", m);
    return 0 - 1;
}

// the `= constant` a parameter may carry. The expression is folded HERE, once,
// and every call site that omits the argument gets a CLONE of the node (a node
// sits in one sibling list only, so a shared default would be rewired by the
// second call that used it). `mark` is where this signature's defaults start:
// past it there is a default, so a bare parameter after one is refused rather
// than leaving a hole no call could fill.
void tk_param_default(i64 mark) {
    if (!p_accept(K_ASSIGN)) {
        if (tk_ndflt > mark)
            err_at(p_file(), p_line(), "teko: a parameter without a default cannot follow one with a default");
        return;
    }
    i64 line = p_line();
    uptr fl = p_file();
    i64 e = fold(parse_expr(0));
    if (nd_kind(e) != N_INT) err_at(fl, line, "teko: a default argument must be a constant");
    if (tk_ndflt == TK_MAXDFLT) err_at(fl, line, "teko: too many default arguments");
    set_df_node_at(tk_ndflt, e);
    tk_ndflt = tk_ndflt + 1;
}

// `()`, `(type name, ...)` or `(type name = constant, ...)`: the receiver is
// not written (D219), so the list the source spells is the parameters alone and
// `this` is the slot prepended here. parse_params cannot be used, because a
// default is not in the core's parameter grammar at all -- and it is reachable
// here precisely because the body of a type is parsed by this module. `extra` is
// the argument slot dispatch spends besides the parameters: 1 for a virtual
// method, whose call is `callp(slot, this, ...)`, 0 for a direct one. The
// defaults go to the shared table starting at the caller's own `tk_ndflt`, and
// `pnreq` answers with the smallest number of arguments a call may pass.
i64 tk_params(uptr pnp, uptr pnreq, i64 extra) {
    i64 mark = tk_ndflt;
    p_expect(K_LPAR, "expected ( in the method parameter list");
    i64 head = param_new(TY_UPTR, tk_this_name());
    i64 np = 0;
    loop {
        if (p_id() == K_RPAR) break;
        tk_reject_self(p_name());                // `(self, ...)`: the old form
        i64 ty = tk_gen_ty();
        if (ty == TY_VOID) err_at(p_file(), p_line(), "teko: parameter of type void");
        uptr pn = p_ident();
        tk_reject_self(pn);
        head = list_append(head, param_new(ty, pn));
        np = np + 1;
        tk_param_default(mark);
        if (np + 1 + extra > MAXPARAMS)
            err_at(p_file(), p_line(), "teko: method with too many parameters (the receiver counts, and so does the vtable pointer of a virtual call)");
        if (!p_accept(K_COMMA)) break;
    }
    p_expect(K_RPAR, "expected ) in the method parameter list");
    st64(pnp, np);
    st64(pnreq, np - (tk_ndflt - mark));
    return head;
}

// st64(name_vt + (TK_VT_FIXED + k) * 8, &fn_k) for every virtual slot
i64 tk_vt_slots(i64 ci, uptr vt) {
    i64 stmts = 0;
    i64 i = 0;
    loop {
        if (i >= sr_nv_at(ci)) break;
        i64 dst = tk_bin(K_ADD, tk_id(vt), tk_int((TK_VT_FIXED + i) * 8));
        stmts = list_append(stmts, tk_stmt(tk_call2("st64", dst, tk_addr(vs_fn_at(sr_v0_at(ci) + i)))));
        i = i + 1;
    }
    return stmts;
}

// the function of `ci` answering the interface method `k`: the class's method of
// the same NAME and the same PARAMETER TYPES, so a class that overloads the name
// still publishes the one the interface asked for. This is where "interface
// method not implemented" comes from, and the three signature checks with it.
uptr tk_conform(i64 ci, i64 k, uptr iname) {
    uptr m = im_name_at(k);
    i64 mi = tk_method_sig_find(ci, m, im_sig_at(k));
    if (mi >= 0) {
        if (mt_ret_at(mi) != im_ret_at(k))
            err_at2(tk_file, tk_line, "teko: method with a return type different from the interface", m);
        return mt_fn_at(mi);
    }
    mi = tk_method_named_find(ci, m);            // the name is there: say what differs
    if (mi < 0)
        err_at2(tk_file, tk_line, tk_join3("teko: method of `", iname, "` not implemented"), m);
    if (mt_np_at(mi) != im_np_at(k))
        err_at2(tk_file, tk_line, "teko: method with an arity different from the interface", m);
    if (mt_ret_at(mi) != im_ret_at(k))
        err_at2(tk_file, tk_line, "teko: method with a return type different from the interface", m);
    err_at2(tk_file, tk_line, "teko: method with parameter types different from the interface", m);
    return 0;
}

// u8 class_iface_mt[n * 8], and the stores that fill it: the class's own
// implementation of each method, in the interface's declaration order, so the
// slot a dispatch indexes means the same thing for every conforming class
i64 tk_mt_fill(i64 ci, uptr cls, i64 fi) {
    uptr iname = sr_name_at(fi);
    uptr mt = tk_mt_name(cls, iname);
    i64 n = sr_mn_at(fi);
    top_add(tk_glb(TY_U8, mt, n * 8));
    i64 stmts = 0;
    i64 j = 0;
    loop {
        if (j >= n) break;
        i64 dst = tk_bin(K_ADD, tk_id(mt), tk_int(j * 8));
        stmts = list_append(stmts, tk_stmt(tk_call2("st64", dst, tk_addr(tk_conform(ci, sr_m0_at(fi) + j, iname)))));
        j = j + 1;
    }
    return stmts;
}

// u8 class_itab[8 + ni * 16] = { ni, (interface id, its method table)* }, the
// table `tk_itab` walks at run time (ngen/lib/rt.mc)
i64 tk_itab_fill(i64 ci, uptr cls, uptr itab) {
    i64 ni = sr_ni_at(ci);
    i64 stmts = tk_stmt(tk_call2("st64", tk_id(itab), tk_int(ni)));
    i64 i = 0;
    loop {
        if (i >= ni) break;
        i64 fi = ci_if_at(sr_i0_at(ci) + i);
        stmts = list_append(stmts, tk_mt_fill(ci, cls, fi));
        i64 idst = tk_bin(K_ADD, tk_id(itab), tk_int(8 + i * 16));
        stmts = list_append(stmts, tk_stmt(tk_call2("st64", idst, tk_int(fi))));
        i64 mdst = tk_bin(K_ADD, tk_id(itab), tk_int(16 + i * 16));
        stmts = list_append(stmts, tk_stmt(tk_call2("st64", mdst, tk_id(tk_mt_name(cls, sr_name_at(fi))))));
        i = i + 1;
    }
    return stmts;
}

// void Name_vt_init() { the vtable, the interface tables and the itab }
// Called by the constructor, as `oop.mc` does: the stores are idempotent, so no
// program-start hook is needed for a table that never changes.
i64 tk_vt_init(i64 ci, uptr cls, uptr vt) {
    i64 stmts = tk_vt_slots(ci, vt);
    if (sr_ni_at(ci) > 0) {
        uptr itab = tk_itab_name(cls);
        top_add(tk_glb(TY_U8, itab, 8 + sr_ni_at(ci) * 16));
        stmts = list_append(stmts, tk_stmt(tk_call2("st64", tk_id(vt), tk_id(itab))));
        stmts = list_append(stmts, tk_itab_fill(ci, cls, itab));
    }
    return tk_func(TY_VOID, tk_join(vt, "_init"), 0, tk_blk(stmts));
}

// a member the class DECLARES loses to nothing; one COPIED from a trait loses
// to the class's own (PHP's precedence, teko_trait.mc), and two traits bringing
// the same name is a conflict rather than a last-one-wins. Every one of the
// three questions is asked of the SIGNATURE, not of the name alone: a trait
// method overloading a name the class also declares is a member of its own.
i64 tk_member_gate(i64 ci, uptr m, uptr sig, i64 ti, i64 kind) {
    i64 own = tk_method_own(ci, m, sig);
    if (ti < 0) {
        if (own >= 0) err_at2(tk_file, tk_line, "teko: duplicate method", m);
        return kind;
    }
    if (own >= 0 && own < tk_own_methods) return 0 - 1;        // the class declares it: not copied
    if (own >= 0) err_at2(tk_file, tk_line, "teko: two traits bring the same member", m);
    if (kind == 0 && tk_slot_find(ci, m, sig) >= 0) return 2;  // the trait wins over the base
    return kind;
}

// the symbol of a method: the FIRST one of a name in the class keeps the plain
// `class_method`, and every overload after it carries its signature, so the two
// reach the linker as two functions and the programs that never overload emit
// exactly the symbols they emitted before. The one that takes no parameter has
// an EMPTY signature, and would collide with the plain symbol when it is not the
// first to arrive -- `void` names it, being the one type a parameter cannot have.
uptr tk_method_symbol(i64 ci, uptr name, uptr m, uptr sig) {
    if (!tk_method_overloads(ci, m)) return tk_fname(name, m);
    if (cstrlen(sig) == 0) return tk_join(tk_fname(name, m), "__void");
    return tk_join(tk_fname(name, m), sig);
}

// one member of a body: a `use` of a trait, a field, or a method whose body the
// CORE parses (parse_function) with the parameter list already built here. `ti`
// is the trait the member is being COPIED from, or -1 when the class declares it
// itself -- the one difference between the two bodies, which is what PHP's
// flattening needs and why both run the same machine.
i64 tk_member(i64 ci, uptr name, i64 off, i64 ti) {
    if (tk_kw("use")) {
        if (!tk_is_class(ci)) err_at(tk_file, tk_line, "teko: only a class uses a trait");
        return tk_use(ci, name, off);
    }
    if (ti >= 0) tk_trait_gate();
    i64 kind = 0;
    if (tk_kw("virtual")) { kind = 1; p_next(); }
    else if (tk_kw("override")) { kind = 2; p_next(); }
    if (kind && !tk_is_class(ci))
        err_at(tk_file, tk_line, "teko: a struct has no vtable; `virtual`/`override` needs a class");
    i64 fty = tk_gen_ty();
    i64 isop = 0;
    uptr m = tk_op_name(&isop);                  // `operator+` names the method `op_add`
    if (p_id() != K_LPAR) {
        if (isop) err_at(tk_file, tk_line, "teko: an operator is a method; it takes a parameter list");
        if (kind) err_at2(tk_file, tk_line, "teko: virtual/override on a field", m);
        i64 nel = tk_field_dim();                // `T items[N]`: 0 when it is a scalar
        p_expect(K_SEMI, "expected ; after the field");
        if (ti >= 0 && tk_field_find(ci, m) >= 0)
            err_at2(tk_file, tk_line,
                    tk_join3("teko: field of trait `", tr_name_at(ti), "` collides with a field of the class"), m);
        return tk_field_place(ci, name, m, fty, nel, off);
    }
    if (str_eq(m, "new")) err_at2(tk_file, tk_line, "teko: method name reserved by the class", m);
    i64 extra = 0;
    if (kind) extra = 1;
    i64 np = 0;
    i64 nreq = 0;
    i64 d0 = tk_ndflt;
    i64 params = tk_params(&np, &nreq, extra);   // the signature decides every gate below
    if (isop) tk_op_decl_check(np, nreq, fty);
    uptr sig = tk_sig_of(params);
    kind = tk_member_gate(ci, m, sig, ti, kind);
    if (kind < 0) {
        tk_skip_body();                          // the class's own wins: its body is not parsed
        return off;
    }
    if (kind && np + 2 > MAXPARAMS)              // a trait method promoted to a slot pays the vtable word
        err_at(tk_file, tk_line, "teko: method with too many parameters (the receiver counts, and so does the vtable pointer of a virtual call)");
    uptr fn = tk_method_symbol(ci, name, m, sig);
    tk_method_add(m, ci, sig, fn, np, nreq, d0, fty, tk_slot_take(ci, kind, m, sig, fn));
    i64 mark = tk_nlocal;                        // the receiver belongs to this body alone
    tk_local_add(tk_this_name(), ci);            // `this.field` inside the body
    i64 keep = tk_this_enter_body(ci);           // ...and `this`/`base` themselves
    i64 line = p_line();
    uptr fl = p_file();
    i64 f = parse_function(fty, fn, params);
    tk_this_leave_body(keep);
    tk_nlocal = mark;
    set_nd_line(f, line);                        // the declaration starts at the {
    set_nd_file(f, fl);
    top_add(f);
    return off;
}

// one name of the `:` list, whose word is reserved by then (type_new), so the
// raw lexeme is read and looked up in the type table
i64 tk_conf_name(i64 base) {
    uptr nm = p_name();
    i64 line = p_line();
    uptr fl = p_file();
    p_next();
    i64 si = tk_struct_find(nm);
    if (si < 0 && tk_trait_find(nm) >= 0)
        err_at2(fl, line, "teko: a trait is not a base class nor an interface; use `use`", nm);
    if (si < 0) err_at2(fl, line, "teko: unknown base class or interface", nm);
    if (tk_is_iface(si)) {
        tk_conf_add(si, line, fl, nm);
        return base;
    }
    if (!tk_is_class(si)) err_at2(fl, line, "teko: a base has to be a class, not a struct", nm);
    if (base >= 0) err_at2(fl, line, "teko: a class has one base class", nm);
    if (tk_nconf > 0) err_at2(fl, line, "teko: the base class comes before the interfaces", nm);
    return si;
}

// `: Base`, `: Iface`, `: Base, IfaceA, IfaceB` -- one base class at most, and
// it comes first; everything else in the list is an interface. Returns the base
// class's row, or -1, and leaves the interfaces in conf_if/tk_nconf.
i64 tk_class_conf() {
    i64 base = 0 - 1;
    tk_nconf = 0;
    loop {
        base = tk_conf_name(base);
        if (!p_accept(K_COMMA)) break;
    }
    return base;
}

// ---- class Name [: Base] { fields and methods } ----
void tk_class() {
    tk_line = p_line();
    tk_file = p_file();
    i64 head_line = tk_line;                     // position of the `class` word
    uptr head_file = tk_file;
    p_next();                                    // the `class` word
    uptr name = tk_newname("class");
    if (p_id() == K_LT) {                        // class Name<T, const N: i64>
        tk_gen_record(name, TK_KCLASS);          // recorded, not declared
        return;
    }
    i64 base = 0 - 1;
    tk_nconf = 0;
    if (p_accept(K_COLON)) base = tk_class_conf();
    i64 ty = type_new(name, 8, 8, TK_INT);
    i64 ci = tk_type_add(name, ty, base, TK_KCLASS);
    tk_slots_inherit(ci, base);
    tk_impls_inherit(ci, base);                  // the base's interfaces are the derived class's
    i64 c = 0;
    loop {
        if (c >= tk_nconf) break;
        tk_impl_add(ci, conf_if_at(c));
        c = c + 1;
    }
    i64 off = 8;                                 // word 0 is the vtable
    if (base >= 0) off = sr_size_at(base);       // the base's fields keep their offsets
    tk_use_reset();
    p_expect(K_LBRACE, "expected { in the class body");
    loop {
        if (p_id() == K_RBRACE) break;
        if (p_id() == T_EOF) err_at(head_file, head_line, "unterminated class");
        tk_line = p_line();                      // errors and nodes for this member
        tk_file = p_file();
        off = tk_member(ci, name, off, 0 - 1);
    }
    tk_own_methods = tk_nmethod;                 // what a trait's copy has to lose to
    if (tk_ntu > 0) off = tk_flatten(ci, name, 0, off);   // the push spends the `}`
    else            p_next();                    // }
    p_accept(K_SEMI);
    tk_line = head_line;                         // back to the class's own level
    tk_file = head_file;
    i64 size = tk_size_of(off);
    set_sr_size_at(ci, size);
    def_add(tk_join(tk_case(name, 1), "_SIZE"), size, tk_line, tk_file);
    uptr vt = tk_join(tk_case(name, 0), "_vt");
    top_add(tk_glb(TY_U8, vt, (TK_VT_FIXED + sr_nv_at(ci)) * 8));
    top_add(tk_vt_init(ci, name, vt));
    i64 install = tk_stmt(tk_call(tk_join(vt, "_init"), 0));
    install = list_append(install, tk_stmt(tk_call2("st64", tk_id("p"), tk_id(vt))));
    top_add(tk_ctor(name, ty, size, install));
}

void tk_stop_type()      { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: type not taught yet"); }
void tk_stop_namespace() { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: namespace not taught yet"); }
void tk_stop_import()    { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: import not taught yet"); }
void tk_stop_using()     { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: using not taught yet"); }
