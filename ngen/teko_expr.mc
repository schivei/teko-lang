// teko_expr.mc -- expression-position words teko adds beyond the core's own
// Pratt grammar (arithmetic, comparisons, calls, casts: all inherited
// unchanged, D213). Entrega 3 turns the two the struct and class systems need
// into real handlers over teko_struct.mc / teko_class.mc's tables:
//
//   new Point                syntax_expr        -> point_new()
//   p.x   p.x = e            syntax_infix(".")  -> ldW(p + POINT_X) / stW(...)
//   p.area()                 syntax_infix(".")  -> a direct call, or callp
//                                                  through the object's vtable
//
// Member assignment works because `=` is deliberately not in the core's infix
// table: the Pratt loop has already stopped by the time the handler runs, so it
// reads the `=` itself and the core sees a plain expression statement
// (mc/lib/user_syntax_demo.mc's `p ~> len = 3`, the same mechanism).
//
// The receiver's type comes from tk_struct_of_expr, whose answer is exact: a
// node this module built, or a local whose N_VAR the on_stmt hook saw, under
// the block scope teko_stmt.mc's `{` handler maintains. When it has no answer --
// a parameter, the one thing the core never reports to a module mid-body -- the
// access is DEFERRED to teko_typeof.mc's pass rather than resolved by the
// member's name: a name that only ANOTHER type declares is not this receiver's
// member, and calling it would be a call to a foreign type's method.

// new Name  /  new Name()  -- the allocation is the generated constructor's, so
// nothing here knows the type's size; `new` only names it.
i64 tk_new() {
    i64 line = p_line();
    uptr fl = p_file();
    p_next();                                    // the `new` word
    uptr name = p_name();                        // a type word is reserved: not T_IDENT
    i64 si = tk_struct_find(name);
    if (si < 0 && tk_trait_find(name) >= 0)
        err_at2(fl, line, "teko: a trait is not a type; `new` needs a struct or a class", name);
    if (si < 0) err_at2(fl, line, "teko: unknown struct or class after `new`", name);
    if (tk_is_iface(si)) err_at2(fl, line, "teko: an interface has no object to allocate", name);
    p_next();
    if (p_accept(K_LPAR)) p_expect(K_RPAR, "expected ) after the type name");
    tk_line = line;
    tk_file = fl;
    i64 n = tk_call(tk_ctor_name(name), 0);
    tk_xt_add(n, si, 0);                         // it allocates: never re-evaluated
    return n;
}

// the argument list of a call, `(` NOT yet consumed
i64 tk_args(uptr pn) {
    p_expect(K_LPAR, "expected ( in the method call");
    i64 head = 0;
    i64 n = 0;
    loop {
        if (p_id() == K_RPAR) break;
        head = list_append(head, parse_expr(0));
        n = n + 1;
        if (!p_accept(K_COMMA)) break;
    }
    p_expect(K_RPAR, "expected ) after the arguments");
    st64(pn, n);
    return head;
}

// the arguments a call left out, taken from the callee's own defaults. Each
// site gets a CLONE: a node lives in exactly one sibling list, so handing the
// same default to two calls would rewire it out of the first.
i64 tk_fill_defaults(i64 args, i64 na, i64 np, i64 nreq, i64 d0) {
    i64 k = na;
    loop {
        if (k >= np) break;
        args = list_append(args, tk_clone(df_node_at(d0 + k - nreq)));
        k = k + 1;
    }
    return args;
}

// `p.f` / `p.f = e`: the load or the store of the field's own width
i64 tk_field_use(i64 left, i64 fi, i64 line, uptr fl) {
    i64 fty = fd_ty_at(fi);
    i64 addr = tk_bin(K_ADD, left, tk_int(fd_off_at(fi)));
    if (p_accept(K_ASSIGN)) {
        i64 v = parse_expr(0);
        tk_line = line;
        tk_file = fl;
        return tk_call2(tk_stn(fty), addr, v);
    }
    i64 r = tk_call(tk_ldn(fty), addr);
    i64 fs = tk_struct_by_ty(fty);
    if (fs >= 0) tk_xt_add(r, fs, 1);            // a field of struct type: `a.b.c`
    return r;
}

// what a pick INSIDE a known type says when it did not land on one signature.
// The receiver's type is not in question here -- the type is known and declares
// the name -- so -2 means what tk_method_pick/tk_ifmeth_pick define it to mean:
// no signature of that name takes this many arguments. -3 is two that do, which
// only the argument types could tell apart.
void tk_pick_refuse(i64 mi, uptr m, i64 line, uptr fl) {
    if (mi == 0 - 3)
        err_at2(fl, line, "teko: ambiguous overload; two signatures take this many arguments", m);
    if (mi == 0 - 2)
        err_at(fl, line, tk_join("teko: wrong number of arguments for ", m));
    err_at2(fl, line, "teko: unknown member", m);
}

// the same question asked of the NAME alone, where the codes mean something
// else: tk_method_by_name answers -2 when two unrelated classes declare it (the
// receiver's type is what would decide, and nothing knows it), -3 when one class
// has two signatures taking this many arguments, and -1 when the name exists but
// no signature takes them.
void tk_loose_refuse(i64 mi, uptr m, i64 line, uptr fl) {
    if (mi == 0 - 3)
        err_at2(fl, line, "teko: ambiguous overload; two signatures take this many arguments", m);
    if (mi == 0 - 2)
        err_at2(fl, line, "teko: the type of the left side of `.` is not known here", m);
    err_at(fl, line, tk_join("teko: wrong number of arguments for ", m));
}

// `p.m(...)` once the method is known: a plain one is a direct call to the
// mangled Owner_method, a virtual one goes through the object's vtable. The
// virtual form reads the receiver twice, so it is only accepted where
// re-evaluating it is free.
i64 tk_emit_call(i64 left, i64 mi, i64 args, i64 na, i64 line, uptr fl) {
    tk_line = line;
    tk_file = fl;
    args = tk_fill_defaults(args, na, mt_np_at(mi), mt_nreq_at(mi), mt_d0_at(mi));
    i64 slot = mt_slot_at(mi);
    i64 r = 0;
    if (slot < 0) {
        r = tk_call(mt_fn_at(mi), list_append(left, args));
    } else {
        if (!tk_pure(left))
            err_at2(fl, line, "teko: a virtual call needs a name or a field on the left", mt_name_at(mi));
        i64 vt = tk_call("ld64", tk_clone(left));
        i64 fnp = tk_call("ld64", tk_bin(K_ADD, vt, tk_int((TK_VT_FIXED + slot) * 8)));
        r = tk_call("callp", list_append(list_append(fnp, left), args));
    }
    i64 rs = tk_struct_by_ty(mt_ret_at(mi));
    if (rs >= 0) tk_xt_add(r, rs, 0);
    return r;
}

// `p.m(...)` on a receiver whose class IS known: the arguments come first,
// because how many there are is what tells one overload from the other
i64 tk_call_method(i64 left, i64 si, uptr m, i64 line, uptr fl) {
    i64 na = 0;
    i64 args = tk_args(&na);
    i64 mi = tk_method_pick(si, m, na);
    if (mi < 0) tk_pick_refuse(mi, m, line, fl);
    return tk_emit_call(left, mi, args, na, line, fl);
}

// `s.m(...)` where `s` is of INTERFACE type: the class is only known at run
// time, so the method table comes from the object's own itab (`tk_itab`,
// ngen/lib/rt.mc) and the call is indirect. The receiver is read twice -- once
// for the table, once as `self` -- so, as with a virtual call, it is only
// accepted where re-evaluating it is free.
i64 tk_iface_call(i64 left, i64 si, uptr m, i64 line, uptr fl) {
    if (tk_ifmeth_find(si, m) < 0)
        err_at2(fl, line, tk_join("teko: unknown member of ", sr_name_at(si)), m);
    i64 na = 0;
    i64 args = tk_args(&na);
    i64 j = tk_ifmeth_pick(si, m, na);
    if (j < 0) tk_pick_refuse(j, m, line, fl);
    i64 k = sr_m0_at(si) + j;
    tk_line = line;
    tk_file = fl;
    args = tk_fill_defaults(args, na, im_np_at(k), im_nreq_at(k), im_d0_at(k));
    if (!tk_pure(left))
        err_at2(fl, line, "teko: an interface call needs a name or a field on the left", m);
    i64 vt = tk_call("ld64", tk_clone(left));
    i64 mt = tk_call2("tk_itab", vt, tk_int(si));
    i64 fnp = tk_call("ld64", tk_bin(K_ADD, mt, tk_int(j * 8)));
    i64 r = tk_call("callp", list_append(list_append(fnp, left), args));
    i64 rs = tk_struct_by_ty(im_ret_at(k));
    if (rs >= 0) tk_xt_add(r, rs, 0);
    return r;
}

// the member of a receiver whose type IS known: a field, then a method
i64 tk_member_of(i64 left, i64 si, uptr m, i64 line, uptr fl) {
    if (tk_is_iface(si)) return tk_iface_call(left, si, m, line, fl);
    i64 fi = tk_field_find(si, m);
    if (fi >= 0) return tk_field_use(left, fi, line, fl);
    if (tk_method_named_find(si, m) >= 0) return tk_call_method(left, si, m, line, fl);
    err_at2(fl, line, tk_join("teko: unknown member of ", sr_name_at(si)), m);
    return 0;
}

// A receiver the parser can type is resolved AGAINST THAT TYPE, and only that
// type: its own members, its bases', its traits' (flattened into it) and its
// interfaces'. A name the type does not declare is an error, not a search of the
// other types that happen to declare it.
//
// A receiver the parser cannot type is not guessed either: it is DEFERRED to
// teko_typeof.mc's pass, where the unit exists and a parameter's declared type
// can be read. Resolving a member by its name alone survives only there, and
// only after the oracle has answered that it does not know the type -- a global,
// or an expression whose type nothing reports.
i64 tk_dot(i64 left) {
    i64 line = p_line();
    uptr fl = p_file();
    tk_line = line;
    tk_file = fl;
    uptr m = p_ident();                          // the member name, on the right
    i64 si = tk_struct_of_expr(left);
    if (si >= 0) return tk_member_of(left, si, m, line, fl);
    return tk_defer_member(left, m, line, fl);
}
