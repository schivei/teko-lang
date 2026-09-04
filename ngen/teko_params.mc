// teko_params.mc -- `params`, the C# multi-parameter, taught as a type word and
// one AST pass.
//
//   i64 total(params xs) { ... xs_len ... xs[i] ... }
//   total(1, 2, 3)
//
// The spelling is the mc core's own (D213: type first, no `fn`, no `func`).
// C#'s `params i64[] xs` is not reachable and is not attempted: the core has no
// `[` in parameter position and the ngen has no array type, so `params` is
// registered as a primitive of its own (`type_new`, teko_type.mc) and the
// element type is the register-wide word every teko scalar already is.
//
// One `pass()` does everything, because a pass is the only place where the
// whole unit exists at once -- a call site above the declaration it targets
// would see nothing from a parser hook (`decl_find` answers -1 for what has not
// been parsed yet, hooks.md § "Asking about a declaration the core already
// parsed"). It rewrites three things and refuses four:
//
//   i64 total(params xs)    ->  i64 total(uptr xs, i64 xs_len)
//   xs[i]                   ->  tk_va_at(xs, i)
//   total(a, b, c)          ->  total(tk_va3(a, b, c), 3)
//
// so nothing with the `params` type ever reaches the lowering, and the taught
// surface costs the core nothing: the callee reads an ordinary pointer and an
// ordinary count.
//
// THE DECLARED RESTRICTION OF THIS FIRST SLICE: the argument buffer
// (`tk_va_buf`, ngen/lib/rt.mc) is STATIC, so it does NOT reenter. A variadic
// call nested inside another one, or inside the body of a variadic function,
// would overwrite the list its caller is still reading -- and is REFUSED here,
// with its own message, rather than miscompiled. Reentrancy is not invented
// around it; a frame-allocated list is what a later entrega buys.
//
// The teto is the ABI's: MAXPARAMS = 12 (mc/src/arena.mc:58, 1..8 in registers
// and 9..12 on the stack). A variadic list costs two of them, so a declaration
// carries at most 10 fixed parameters, and one call site passes at most 12
// arguments counting the fixed ones.

// xs[i] -- the `[` the core's Pratt grammar does not have. `[` is punctuation
// the lexer already knows, and `ops_init()` does not touch it, so unlike an
// infix over a CORE operator this handler really is reached (a `syntax_infix`
// on `+` is silently dead: parse_unit calls ops_init after user_init, and
// infix_set clears the handler column). The node is an N_INDEX the core defines
// but never builds and never lowers, so an index this pass does not rewrite is
// impossible to miss.
i64 tk_bracket(i64 left) {
    i64 line = p_line();
    uptr fl = p_file();
    i64 idx = parse_expr(0);
    p_expect(K_RBRACK, "expected ] after the index");
    i64 n = node_new(N_INDEX, line, fl);
    set_nd_a(n, left);
    set_nd_b(n, idx);
    set_nd_type(n, TY_I64);
    return n;
}

// position of the `params` parameter of declaration `d`, 0-based, or -1
i64 tk_va_pos(i64 d) {
    if (!decl_valid(d)) return -1;
    i64 i = 0;
    i64 p = nd_a(d);
    loop {
        if (p == 0) break;
        if (nd_type(p) == tk_ty_params) return i;
        i = i + 1;
        p = nd_next(p);
    }
    return -1;
}

// 1 when `name` is the name of some `params` parameter in the unit. The
// declarations have not been rewritten yet when this is asked, so the type on
// the N_PARAM is still the answer.
i64 tk_va_is_list(uptr name) {
    i64 n = 1;
    loop {
        if (n >= nnodes) break;
        if (nd_kind(n) == N_PARAM && nd_type(n) == tk_ty_params && str_eq(nd_name(n), name)) return 1;
        n = n + 1;
    }
    return 0;
}

// 1 when `n` is a call to a function that declares a `params` list
i64 tk_va_is_call(i64 n) {
    if (nd_kind(n) != N_CALL) return 0;
    return tk_va_pos(decl_find(nd_name(n))) >= 0;
}

// 1 when the subtree at `n`, siblings included, contains a variadic call
i64 tk_va_in_subtree(i64 n) {
    if (n == 0) return 0;
    if (tk_va_is_call(n)) return 1;
    if (tk_va_in_subtree(nd_a(n))) return 1;
    if (tk_va_in_subtree(nd_b(n))) return 1;
    if (tk_va_in_subtree(nd_c(n))) return 1;
    if (tk_va_in_subtree(nd_d(n))) return 1;
    return tk_va_in_subtree(nd_next(n));
}

// tk_va3, tk_va12: the packer of that arity, in ngen/lib/rt.mc
uptr tk_va_fn(i64 v) {
    if (v < 10) return p_cat("tk_va", "0123456789", v, 1);
    return p_cat(p_cat("tk_va", "1", 0, 1), "0123456789", v - 10, 1);
}

// `params` may be declared once, last, and with room left for the count
void tk_va_check_decl(i64 d) {
    i64 k = tk_va_pos(d);
    if (k < 0) return;
    if (k != decl_nparams(d) - 1)
        err_at2(nd_file(d), nd_line(d), "teko: `params` must be the last parameter, and there is only one", nd_name(d));
    if (k + 2 > MAXPARAMS)
        err_at2(nd_file(d), nd_line(d), "teko: too many parameters before `params` (the list costs two of the twelve)", nd_name(d));
    if (tk_va_in_subtree(nd_b(d)))
        err_at2(nd_file(d), nd_line(d), "teko: a variadic call may not appear inside the body of a variadic function", nd_name(d));
}

// the static buffer is written before the call and read during it: an argument
// that is itself a variadic call would overwrite the list being built
void tk_va_check_args(i64 n) {
    if (!tk_va_is_call(n)) return;
    if (tk_va_in_subtree(nd_a(n)))
        err_at2(nd_file(n), nd_line(n), "teko: a variadic call may not appear inside another variadic call", nd_name(n));
}

// `params` names a parameter form, not a type: a local, a global, a field or a
// cast that spells it is refused where it is written
void tk_va_check_stray(i64 n) {
    if (nd_type(n) != tk_ty_params) return;
    if (nd_kind(n) == N_PARAM) return;
    err_at(nd_file(n), nd_line(n), "teko: `params` declares a parameter list, nothing else");
}

// xs[i] -> tk_va_at(xs, i), in place, keeping the sibling link
void tk_va_lower_index(i64 n) {
    i64 base = nd_a(n);
    if (nd_kind(base) != N_IDENT)
        err_at(nd_file(n), nd_line(n), "teko: `[` indexes a `params` list only");
    if (!tk_va_is_list(nd_name(base)))
        err_at2(nd_file(n), nd_line(n), "teko: `[` indexes a `params` list only", nd_name(base));
    tk_line = nd_line(n);
    tk_file = nd_file(n);
    i64 keep = nd_next(n);
    i64 c = tk_call2("tk_va_at", base, nd_b(n));
    node_assign(n, c);
    set_nd_next(n, keep);
}

// total(a, b, c) -> total(tk_va3(a, b, c), 3). The fixed arguments stay where
// they are; the tail is cut off the list and becomes the packer's own argument
// list, so nothing is copied and no node is rebuilt.
void tk_va_lower_call(i64 n) {
    i64 d = decl_find(nd_name(n));
    i64 k = tk_va_pos(d);
    if (k < 0) return;
    i64 args = nd_a(n);
    i64 prev = 0;
    i64 tail = args;
    i64 i = 0;
    loop {
        if (i >= k) break;
        if (tail == 0) err_at2(nd_file(n), nd_line(n), "teko: too few arguments", nd_name(n));
        prev = tail;
        tail = nd_next(tail);
        i = i + 1;
    }
    i64 v = 0;
    i64 t = tail;
    loop {
        if (t == 0) break;
        v = v + 1;
        t = nd_next(t);
    }
    if (k + v > MAXPARAMS)
        err_at2(nd_file(n), nd_line(n), "teko: too many arguments for a `params` list (twelve, the fixed ones included)", nd_name(n));
    if (prev) set_nd_next(prev, 0);
    if (!prev) args = 0;
    tk_line = nd_line(n);
    tk_file = nd_file(n);
    i64 pack = tk_int(0);                        // an empty list: len is 0, the pointer is never read
    if (v > 0) pack = tk_call(tk_va_fn(v), tail);
    args = list_append(args, pack);
    set_nd_a(n, list_append(args, tk_int(v)));
}

// i64 total(params xs) -> i64 total(uptr xs, i64 xs_len)
void tk_va_lower_decl(i64 d) {
    if (tk_va_pos(d) < 0) return;
    i64 p = nd_a(d);
    loop {
        if (nd_type(p) == tk_ty_params) break;
        p = nd_next(p);
    }
    set_nd_type(p, TY_UPTR);
    set_nd_next(p, param_new(TY_I64, p_cat(nd_name(p), "_len", 0, 4)));
}

// The pass, in four sweeps over the node array: the checks read the tree as the
// source wrote it, then the three rewrites run outward-in -- indexes, call
// sites, declarations -- because both of the first two ask a declaration what
// its parameters are, and the third is what takes that answer away. `nnodes` is
// snapshotted, so the nodes a rewrite creates are not swept again.
i64 tk_params_pass(i64 root) {
    i64 last = nnodes;
    i64 n = 1;
    loop {
        if (n >= last) break;
        tk_va_check_stray(n);
        tk_va_check_decl(n);
        tk_va_check_args(n);
        n = n + 1;
    }
    n = 1;
    loop {
        if (n >= last) break;
        if (nd_kind(n) == N_INDEX) tk_va_lower_index(n);
        n = n + 1;
    }
    n = 1;
    loop {
        if (n >= last) break;
        if (nd_kind(n) == N_CALL) tk_va_lower_call(n);
        n = n + 1;
    }
    n = 1;
    loop {
        if (n >= last) break;
        if (decl_valid(n)) tk_va_lower_decl(n);
        n = n + 1;
    }
    return root;
}
