// teko_expr.mc -- expression-position words teko adds beyond the core's own
// Pratt grammar (arithmetic, comparisons, calls, casts: all inherited
// unchanged, D213). Entrega 3 turns the two the struct system needs into real
// handlers over teko_struct.mc's tables:
//
//   new Point        syntax_expr        -> point_new()
//   p.x   p.x = e    syntax_infix(".")  -> ldW(p + POINT_X) / stW(p + POINT_X, e)
//
// Member assignment works because `=` is deliberately not in the core's infix
// table: the Pratt loop has already stopped by the time the handler runs, so it
// reads the `=` itself and the core sees a plain expression statement
// (mc/lib/user_syntax_demo.mc's `p ~> len = 3`, the same mechanism).

// new Name  /  new Name()  -- the allocation is the generated constructor's, so
// nothing here knows the struct's size; `new` only names the struct.
i64 tk_new() {
    i64 line = p_line();
    uptr fl = p_file();
    p_next();                                    // the `new` word
    uptr name = p_name();                        // a struct word is reserved: not T_IDENT
    i64 si = tk_struct_find(name);
    if (si < 0) err_at2(fl, line, "teko: unknown struct after `new`", name);
    p_next();
    if (p_accept(K_LPAR)) p_expect(K_RPAR, "expected ) after the struct name");
    tk_line = line;
    tk_file = fl;
    i64 n = tk_call(tk_ctor_name(name), 0);
    tk_xt_add(n, si);
    return n;
}

// the field `p` names on the right of the dot, with the struct of `p` resolved
// statically when the module knows it and by the field's own name when it does
// not (tk_field_by_name, teko_struct.mc)
i64 tk_dot_field(i64 left, uptr m, i64 line, uptr fl) {
    i64 si = tk_struct_of_expr(left);
    if (si >= 0) {
        i64 fi = tk_field_find(si, m);
        if (fi < 0) err_at2(fl, line, tk_join("teko: unknown field of struct ", sr_name_at(si)), m);
        return fi;
    }
    i64 any = tk_field_by_name(m);
    if (any == 0 - 2)
        err_at2(fl, line, "teko: the struct of the left side of `.` is not known here", m);
    if (any < 0) err_at2(fl, line, "teko: unknown struct field", m);
    return any;
}

i64 tk_dot(i64 left) {
    i64 line = p_line();
    uptr fl = p_file();
    tk_line = line;
    tk_file = fl;
    uptr m = p_ident();                          // the field name, on the right
    i64 fi = tk_dot_field(left, m, line, fl);
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
    if (fs >= 0) tk_xt_add(r, fs);               // a field of struct type: `a.b.c`
    return r;
}
