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
// parsed"). It rewrites three things:
//
//   i64 total(params xs)  ->  i64 total(uptr xs, i64 xs_len)
//   xs[i]                 ->  tk_va_at(xs, xs_len, i)
//   total(a, b, c)        ->  total(tk_va_put(tk_va_put(tk_va_put(
//                                 tk_va_new(3), 0, a), 1, b), 2, c), 3)
//
// so nothing with the `params` type ever reaches the lowering, and the taught
// surface costs the core nothing: the callee reads an ordinary pointer and an
// ordinary count.
//
// THE LIST IS ALLOCATED AT THE CALL SITE, on the arena (`tk_va_new` ->
// `rt_alloc`, ngen/lib/rt.mc -- the very path `new` takes), and the pass writes
// one word per index into it. The mc's pointers are opaque `uptr` and the only
// way to take an address is `&name` over a direct name (docs/core-language.md
// § Operators, and § "&arr[i]" in the C-differences list: `&` does not accept
// an indexing expression), so a caller-frame list would need a local array
// declared per site and inserted as a STATEMENT before the expression that
// carries the call -- which a pass over expressions cannot place in general (a
// call sits inside a condition, an argument, a return). The arena is what a
// site can allocate from inside the expression it already is.
//
// Nothing is shared between two sites, so nothing has to be refused for
// reentrancy: a variadic call nested inside another one, and one inside the
// body of a variadic function, both compile and each gets its own block.
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
    i64 x = tk_ax_find(left);                    // an ARRAY FIELD, whose length is known
    if (x >= 0) return tk_array_index(left, x);  // here: teko_struct.mc lowers it now
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

// the count the compiler names after the list: `xs` -> `xs_len`
uptr tk_va_len_name(uptr name) {
    return p_cat(name, "_len", 0, 4);
}

// `params` may be declared once, last, and with room left for the count
void tk_va_check_decl(i64 d) {
    i64 k = tk_va_pos(d);
    if (k < 0) return;
    if (k != decl_nparams(d) - 1)
        err_at2(nd_file(d), nd_line(d), "teko: `params` must be the last parameter, and there is only one", nd_name(d));
    if (k + 2 > MAXPARAMS)
        err_at2(nd_file(d), nd_line(d), "teko: too many parameters before `params` (the list costs two of the twelve)", nd_name(d));
}

// `params` names a parameter form, not a type: a local, a global, a field or a
// cast that spells it is refused where it is written
void tk_va_check_stray(i64 n) {
    if (nd_type(n) != tk_ty_params) return;
    if (nd_kind(n) == N_PARAM) return;
    err_at(nd_file(n), nd_line(n), "teko: `params` declares a parameter list, nothing else");
}

// xs[i] -> tk_va_at(xs, xs_len, i), in place, keeping the sibling link
void tk_va_lower_index(i64 n) {
    i64 base = nd_a(n);
    if (nd_kind(base) != N_IDENT)
        err_at(nd_file(n), nd_line(n), "teko: `[` indexes a `params` list only");
    if (!tk_va_is_list(nd_name(base)))
        err_at2(nd_file(n), nd_line(n), "teko: `[` indexes a `params` list only", nd_name(base));
    tk_line = nd_line(n);
    tk_file = nd_file(n);
    i64 keep = nd_next(n);
    i64 args = list_append(base, tk_id(tk_va_len_name(nd_name(base))));
    i64 c = tk_call("tk_va_at", list_append(args, nd_b(n)));
    node_assign(n, c);
    set_nd_next(n, keep);
}

// the tail of a call's argument list, packed one word per index into a block
// the site allocates: tk_va_new(v), then a tk_va_put per argument, each handing
// the block to the next. Every argument is cut off the sibling list before it
// becomes an argument of its own put -- a node lives in ONE list.
i64 tk_va_pack(i64 tail, i64 v) {
    if (v == 0) return tk_int(0);
    i64 pack = tk_call("tk_va_new", tk_int(v));
    i64 t = tail;
    i64 i = 0;
    loop {
        if (t == 0) break;
        i64 nxt = nd_next(t);
        set_nd_next(t, 0);
        pack = tk_call("tk_va_put", list_append(list_append(pack, tk_int(i)), t));
        i = i + 1;
        t = nxt;
    }
    return pack;
}

// total(a, b, c) -> total(<packed block>, 3). The fixed arguments stay where
// they are; the tail is cut off the list and becomes the block's contents.
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
    args = list_append(args, tk_va_pack(tail, v));
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
    set_nd_next(p, param_new(TY_I64, tk_va_len_name(nd_name(p))));
}

// The pass, in four sweeps over the node array: the checks read the tree as the
// source wrote it, then the three rewrites run outward-in -- indexes, call
// sites, declarations -- because both of the first two ask a declaration what
// its parameters are, and the third is what takes that answer away. `nnodes` is
// snapshotted, so the nodes a rewrite creates are not swept again; a nested
// variadic call is an ORIGINAL node and is rewritten in place, whichever of the
// two the sweep reaches first.
i64 tk_params_pass(i64 root) {
    i64 last = nnodes;
    i64 n = 1;
    loop {
        if (n >= last) break;
        tk_va_check_stray(n);
        tk_va_check_decl(n);
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
