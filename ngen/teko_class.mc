// teko_class.mc -- `class Name [: Base] [, Interface...] { fields and methods }`,
// the second construct of entrega 3 (D214), and the honest-stops for the
// top-level words teko still owes. A class is a struct (teko_struct.mc's
// tables) plus three things: word 0 of the object is the vtable, fields are
// laid out BASE-FIRST (so a derived object is a valid base object), and methods
// take an implicit `self` (`mc/examples/lang` § Layout, and
// `mc/examples/api/oop.mc` for the generated declarations).
//
//   class Shape {                     u8 shape_vt[16] (word 0 = itab, then the slots)
//       i64 side;                     #define SHAPE_SIDE 8   (object word 0 = vtable)
//       virtual i64 area(self) {      i64 shape_area(uptr self)  -> vtable slot 0
//           return self.side;         void shape_vt_init()
//       }                             Shape shape_new()
//   }                                 type_new("Shape", 8, 8, TK_INT)
//
//   class Square : Shape {            #define SQUARE_SIZE 16 (Shape's fields kept)
//       override i64 area(self) {     i64 square_area(uptr self) -> the SAME slot 0
//           return self.side * self.side;
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
// One limit, the same one `mc/examples/lang` has: names resolve in declaration
// order, so a method may call the methods declared ABOVE it in the same body.
//
// What is NOT here, and stops with a message rather than a surprise: `type`,
// `namespace`, `import` and `using` (docs/design/port-teko-mc.md §3).

#define TK_MAXMETHOD 128              // methods, summed across all classes
#define TK_MAXVSLOT  128              // virtual slots, summed across all classes
#define TK_VT_FIXED  1                // vtable word 0 is the interface table; slots follow

uptr mt_name[TK_MAXMETHOD];
i64  mt_cls[TK_MAXMETHOD];
uptr mt_fn[TK_MAXMETHOD];             // the mangled name: class_method
i64  mt_np[TK_MAXMETHOD];             // parameters, not counting `self`
i64  mt_ret[TK_MAXMETHOD];
i64  mt_slot[TK_MAXMETHOD];           // its vtable slot, or -1 when not virtual
i64  tk_nmethod = 0;

uptr vs_m[TK_MAXVSLOT];               // the method name a slot answers to
uptr vs_fn[TK_MAXVSLOT];              // the function currently filling it
i64  tk_nvslot = 0;

uptr mt_name_at(i64 i) { return ld64(mt_name + i * 8); }
i64  mt_cls_at(i64 i)  { return ld64(mt_cls + i * 8); }
uptr mt_fn_at(i64 i)   { return ld64(mt_fn + i * 8); }
i64  mt_np_at(i64 i)   { return ld64(mt_np + i * 8); }
i64  mt_ret_at(i64 i)  { return ld64(mt_ret + i * 8); }
i64  mt_slot_at(i64 i) { return ld64(mt_slot + i * 8); }
uptr vs_m_at(i64 i)    { return ld64(vs_m + i * 8); }
uptr vs_fn_at(i64 i)   { return ld64(vs_fn + i * 8); }

void set_mt_name_at(i64 i, uptr v) { st64(mt_name + i * 8, v); }
void set_mt_cls_at(i64 i, i64 v)   { st64(mt_cls + i * 8, v); }
void set_mt_fn_at(i64 i, uptr v)   { st64(mt_fn + i * 8, v); }
void set_mt_np_at(i64 i, i64 v)    { st64(mt_np + i * 8, v); }
void set_mt_ret_at(i64 i, i64 v)   { st64(mt_ret + i * 8, v); }
void set_mt_slot_at(i64 i, i64 v)  { st64(mt_slot + i * 8, v); }
void set_vs_m_at(i64 i, uptr v)    { st64(vs_m + i * 8, v); }
void set_vs_fn_at(i64 i, uptr v)   { st64(vs_fn + i * 8, v); }

// the method `name` of `ci` or of one of its bases, or -1
i64 tk_method_find(i64 ci, uptr name) {
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

// the method `name` declared by `ci` ITSELF, or -1: a redeclaration in the same
// body is a duplicate, while one that collides with a base's is the business of
// tk_slot_take (an inherited virtual needs `override`, a plain one is hidden)
i64 tk_method_own(i64 ci, uptr name) {
    i64 i = 0;
    loop {
        if (i >= tk_nmethod) break;
        if (mt_cls_at(i) == ci) {
            if (str_eq(mt_name_at(i), name)) return i;
        }
        i = i + 1;
    }
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

// the method called `name` when the receiver's class is not known statically,
// under the same rule tk_field_by_name uses -- except that an `override` is not
// a second method: candidates inside ONE inheritance chain answer with the
// BASE's declaration, whose slot is the one the whole chain shares. -1 = nobody
// declares it, -2 = two unrelated classes do.
i64 tk_method_by_name(uptr name) {
    i64 found = 0 - 1;
    i64 i = 0;
    loop {
        if (i >= tk_nmethod) break;
        if (str_eq(mt_name_at(i), name)) {
            if (found < 0) found = i;
            else if (tk_is_ancestor(mt_cls_at(i), mt_cls_at(found))) found = i;
            else if (!tk_is_ancestor(mt_cls_at(found), mt_cls_at(i))) return 0 - 2;
        }
        i = i + 1;
    }
    return found;
}

i64 tk_method_add(uptr name, i64 ci, uptr fn, i64 np, i64 ret, i64 slot) {
    if (tk_nmethod == TK_MAXMETHOD) err_at(tk_file, tk_line, "teko: too many methods");
    set_mt_name_at(tk_nmethod, name);
    set_mt_cls_at(tk_nmethod, ci);
    set_mt_fn_at(tk_nmethod, fn);
    set_mt_np_at(tk_nmethod, np);
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
        set_vs_fn_at(tk_nvslot, vs_fn_at(sr_v0_at(base) + i));
        tk_nvslot = tk_nvslot + 1;
        i = i + 1;
    }
    set_sr_nv_at(ci, n);
}

// the slot of the class's own slice that answers to `name`, or -1
i64 tk_slot_find(i64 ci, uptr name) {
    i64 i = 0;
    loop {
        if (i >= sr_nv_at(ci)) break;
        if (str_eq(vs_m_at(sr_v0_at(ci) + i), name)) return i;
        i = i + 1;
    }
    return 0 - 1;
}

// `override` fills an inherited slot, `virtual` takes a new one, and a plain
// method that collides with an inherited slot is refused rather than silently
// hiding it. Returns the method's slot, or -1 when it is not virtual.
i64 tk_slot_take(i64 ci, i64 kind, uptr m, uptr fn) {
    i64 slot = tk_slot_find(ci, m);
    if (kind == 2) {
        if (slot < 0) err_at2(tk_file, tk_line, "teko: override of a method the base does not declare", m);
        set_vs_fn_at(sr_v0_at(ci) + slot, fn);
        return slot;
    }
    if (kind == 1) {
        if (slot >= 0) err_at2(tk_file, tk_line, "teko: virtual redeclares an inherited slot; use override", m);
        if (tk_nvslot == TK_MAXVSLOT) err_at(tk_file, tk_line, "teko: too many virtual slots");
        set_vs_m_at(tk_nvslot, m);
        set_vs_fn_at(tk_nvslot, fn);
        tk_nvslot = tk_nvslot + 1;
        set_sr_nv_at(ci, sr_nv_at(ci) + 1);
        return sr_nv_at(ci) - 1;
    }
    if (slot >= 0) err_at2(tk_file, tk_line, "teko: method hides an inherited virtual; use override", m);
    return 0 - 1;
}

// `(self)` or `(self, type name, ...)`: parse_params cannot be used, because
// `self` comes with no type and that is exactly the sugar being taught. `extra`
// is the argument slot dispatch spends besides the parameters: 1 for a virtual
// method, whose call is `callp(slot, self, ...)`, 0 for a direct one.
i64 tk_params(uptr pnp, i64 extra) {
    p_expect(K_LPAR, "expected ( in the method parameter list");
    if (p_id() != T_IDENT || !str_eq(p_name(), "self"))
        err_at(p_file(), p_line(), "teko: the first parameter of a method is `self`");
    p_next();
    i64 head = param_new(TY_UPTR, "self");
    i64 np = 0;
    loop {
        if (!p_accept(K_COMMA)) break;
        i64 ty = p_type();
        if (ty == TY_VOID) err_at(p_file(), p_line(), "teko: parameter of type void");
        head = list_append(head, param_new(ty, p_ident()));
        np = np + 1;
        if (np + 1 + extra > MAXPARAMS)
            err_at(p_file(), p_line(), "teko: method with too many parameters (`self` counts, and so does the vtable pointer of a virtual call)");
    }
    p_expect(K_RPAR, "expected ) in the method parameter list");
    st64(pnp, np);
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

// the function of `ci` answering the interface method `k` -- this is where
// "interface method not implemented" comes from, and the two signature checks
// with it: a class conforms only if every method is there, at the interface's
// own arity and return type
uptr tk_conform(i64 ci, i64 k, uptr iname) {
    uptr m = im_name_at(k);
    i64 mi = tk_method_find(ci, m);
    if (mi < 0)
        err_at2(tk_file, tk_line, tk_join3("teko: method of `", iname, "` not implemented"), m);
    if (mt_np_at(mi) != im_np_at(k))
        err_at2(tk_file, tk_line, "teko: method with an arity different from the interface", m);
    if (mt_ret_at(mi) != im_ret_at(k))
        err_at2(tk_file, tk_line, "teko: method with a return type different from the interface", m);
    return mt_fn_at(mi);
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

// one member of the body: a field, or a method whose body the CORE parses
// (parse_function), with the parameter list already built here
i64 tk_class_member(i64 ci, uptr name, i64 off) {
    i64 kind = 0;
    if (tk_kw("virtual")) { kind = 1; p_next(); }
    else if (tk_kw("override")) { kind = 2; p_next(); }
    i64 fty = p_type();
    uptr m = p_ident();
    if (p_id() != K_LPAR) {
        if (kind) err_at2(tk_file, tk_line, "teko: virtual/override on a field", m);
        p_expect(K_SEMI, "expected ; after the class field");
        return tk_field_place(ci, name, m, fty, off);
    }
    if (str_eq(m, "new")) err_at2(tk_file, tk_line, "teko: method name reserved by the class", m);
    i64 extra = 0;
    if (kind) extra = 1;
    i64 np = 0;
    i64 params = tk_params(&np, extra);
    uptr fn = tk_fname(name, m);
    if (tk_method_own(ci, m) >= 0) err_at2(tk_file, tk_line, "teko: duplicate method", m);
    tk_method_add(m, ci, fn, np, fty, tk_slot_take(ci, kind, m, fn));
    tk_local_add("self", ci);                    // `self.field` inside the body
    i64 line = p_line();
    uptr fl = p_file();
    i64 f = parse_function(fty, fn, params);
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
    p_expect(K_LBRACE, "expected { in the class body");
    loop {
        if (p_id() == K_RBRACE) break;
        if (p_id() == T_EOF) err_at(head_file, head_line, "unterminated class");
        tk_line = p_line();                      // errors and nodes for this member
        tk_file = p_file();
        off = tk_class_member(ci, name, off);
    }
    p_next();                                    // }
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
