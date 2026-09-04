// teko_typeof.mc -- the static-type oracle (entrega 4, crumb C3), and the one
// thing it already pays for: a member access whose receiver is a PARAMETER.
//
// The core answers what a declaration declares -- `decl_find`, `decl_ret`,
// `decl_param_type` (mc docs/reference/hooks.md § "Asking about a declaration
// the core already parsed") -- but only for what it has already parsed, and a
// function is appended to the unit AFTER its own body. So inside `i64 report(
// Shape s) { return s.area(); }` nothing can be asked about `s` while the body
// is being read; the answer exists one phase later, in a `pass()`, where the
// whole unit is there. That is the entire reason this file registers a pass.
//
// Three sources answer, in this order:
//
//   1. the node table teko_struct.mc builds (`tk_xt_find`) -- a node THIS
//      module produced, so its type is known exactly;
//   2. the scope of the function being walked -- its N_PARAM list and the
//      N_VAR of its body, which is what `pass()` makes readable;
//   3. the callee's declared return type, for an N_CALL, and the type the core
//      itself put on a literal or a cast.
//
// It does NOT read `tk_local` (teko_struct.mc): that table is global and has no
// scope, so a name declared in one function answers in another. A name this
// oracle cannot decide -- two declarations of different type under one name,
// a global, a receiver it never saw -- answers -1, and the consumer reports it.
// Conservative, never a guess.
//
// The consumer here is `.` on a receiver the parser could not type: teko_expr.mc
// resolves the member by NAME when the receiver is opaque, and that is exact
// until two unrelated types declare the same member. Where it used to stop with
// "the type of the left side of `.` is not known here", the access is now
// RECORDED and rebuilt in the pass, in the same shapes teko_expr.mc emits
// (field load, field store, direct call, vtable call, itab call). A program in
// which nothing is deferred never enters the pass at all -- `tk_typeof_pass`
// returns the root untouched -- so a tree that is not this pass's business is
// not this pass's to move.

#define TK_MAXSCOPE 256               // names of one function: parameters and locals
#define TK_MAXPEND  128               // member accesses waiting for the pass

// what the deferred access still has to become
#define TK_PLOAD  0                   // p.m
#define TK_PSTORE 1                   // p.m = e
#define TK_PCALL  2                   // p.m(...)

// the argument list of a deferred call is read at parse time, by the same
// function teko_expr.mc uses for a call it can resolve at once
i64 tk_args(uptr pn);

uptr sc_name[TK_MAXSCOPE];            // one function's names, rebuilt per N_FUNC
i64  sc_ty[TK_MAXSCOPE];              // its declared type, or -1 when two disagree
i64  tk_nscope = 0;

i64  pd_node[TK_MAXPEND];             // the placeholder the pass rewrites in place
i64  pd_recv[TK_MAXPEND];             // the receiver, parsed and typed later
uptr pd_name[TK_MAXPEND];             // the member on the right of the `.`
i64  pd_arg[TK_MAXPEND];              // the argument list, or the assigned value
i64  pd_na[TK_MAXPEND];
i64  pd_form[TK_MAXPEND];
i64  pd_line[TK_MAXPEND];
uptr pd_file[TK_MAXPEND];
i64  pd_done[TK_MAXPEND];
i64  tk_npend = 0;

// ---- table accessors (no raw ld64/st64 outside this section) ----
uptr sc_name_at(i64 i) { return ld64(sc_name + i * 8); }
i64  sc_ty_at(i64 i)   { return ld64(sc_ty + i * 8); }
i64  pd_node_at(i64 i) { return ld64(pd_node + i * 8); }
i64  pd_recv_at(i64 i) { return ld64(pd_recv + i * 8); }
uptr pd_name_at(i64 i) { return ld64(pd_name + i * 8); }
i64  pd_arg_at(i64 i)  { return ld64(pd_arg + i * 8); }
i64  pd_na_at(i64 i)   { return ld64(pd_na + i * 8); }
i64  pd_form_at(i64 i) { return ld64(pd_form + i * 8); }
i64  pd_line_at(i64 i) { return ld64(pd_line + i * 8); }
uptr pd_file_at(i64 i) { return ld64(pd_file + i * 8); }
i64  pd_done_at(i64 i) { return ld64(pd_done + i * 8); }

void set_sc_name_at(i64 i, uptr v) { st64(sc_name + i * 8, v); }
void set_sc_ty_at(i64 i, i64 v)    { st64(sc_ty + i * 8, v); }
void set_pd_node_at(i64 i, i64 v)  { st64(pd_node + i * 8, v); }
void set_pd_recv_at(i64 i, i64 v)  { st64(pd_recv + i * 8, v); }
void set_pd_name_at(i64 i, uptr v) { st64(pd_name + i * 8, v); }
void set_pd_arg_at(i64 i, i64 v)   { st64(pd_arg + i * 8, v); }
void set_pd_na_at(i64 i, i64 v)    { st64(pd_na + i * 8, v); }
void set_pd_form_at(i64 i, i64 v)  { st64(pd_form + i * 8, v); }
void set_pd_line_at(i64 i, i64 v)  { st64(pd_line + i * 8, v); }
void set_pd_file_at(i64 i, uptr v) { st64(pd_file + i * 8, v); }
void set_pd_done_at(i64 i, i64 v)  { st64(pd_done + i * 8, v); }

// ---- the scope of one function ----
// A name that arrives twice under two different types is POISONED rather than
// resolved to the last one seen: an inner block may shadow a parameter, and the
// walk is flat, so the honest answer for that name is "not known here".
void tk_ty_scope_add(uptr name, i64 ty) {
    i64 i = 0;
    loop {
        if (i >= tk_nscope) break;
        if (str_eq(sc_name_at(i), name)) {
            if (sc_ty_at(i) != ty) set_sc_ty_at(i, 0 - 1);
            return;
        }
        i = i + 1;
    }
    if (tk_nscope == TK_MAXSCOPE) return;        // beyond the table: answers -1
    set_sc_name_at(tk_nscope, name);
    set_sc_ty_at(tk_nscope, ty);
    tk_nscope = tk_nscope + 1;
}

i64 tk_ty_scope_find(uptr name) {
    i64 i = 0;
    loop {
        if (i >= tk_nscope) break;
        if (str_eq(sc_name_at(i), name)) return sc_ty_at(i);
        i = i + 1;
    }
    return 0 - 1;
}

// every N_VAR of the body, at any depth. `nd_val` is the array length, so
// `Point tbl[4]` -- four references, not one object -- is skipped, exactly as
// teko_struct.mc's on_stmt hook skips it.
void tk_ty_scope_vars(i64 n) {
    loop {
        if (n == 0) break;
        if (nd_kind(n) == N_VAR) {
            if (nd_val(n) == 0) tk_ty_scope_add(nd_name(n), nd_type(n));
        }
        tk_ty_scope_vars(nd_a(n));
        tk_ty_scope_vars(nd_b(n));
        tk_ty_scope_vars(nd_c(n));
        tk_ty_scope_vars(nd_d(n));
        n = nd_next(n);
    }
}

// the N_PARAM list carries the declared type as the parser read it -- the id
// `type_new` returned, not a collapsed TY_*, which is what makes a parameter of
// class type answerable at all
void tk_ty_scope_params(i64 p) {
    loop {
        if (p == 0) break;
        tk_ty_scope_add(nd_name(p), nd_type(p));
        p = nd_next(p);
    }
}

// the scope `fn`'s body is read under; returns how many names it holds
i64 tk_ty_scope_enter(i64 fn) {
    tk_nscope = 0;
    tk_ty_scope_params(nd_a(fn));
    tk_ty_scope_vars(nd_b(fn));
    return tk_nscope;
}

// ---- the static type of a node, under the scope currently entered ----
i64 tk_ty_of(i64 n) {
    if (n == 0) return 0 - 1;
    i64 x = tk_xt_find(n);
    if (x >= 0) return sr_ty_at(x);
    i64 k = nd_kind(n);
    if (k == N_IDENT) return tk_ty_scope_find(nd_name(n));
    if (k == N_CALL) {
        i64 d = decl_find(nd_name(n));
        if (d < 0) return 0 - 1;
        return decl_ret(d);
    }
    if (k == N_INT) return nd_type(n);
    if (k == N_STR) return nd_type(n);
    if (k == N_CAST) return nd_type(n);
    return 0 - 1;
}

// the row of teko_struct.mc's type table the node's type names, or -1
i64 tk_ty_struct_of(i64 n) {
    i64 x = tk_xt_find(n);
    if (x >= 0) return x;
    i64 t = tk_ty_of(n);
    if (t < 0) return 0 - 1;
    return tk_struct_by_ty(t);
}

// ---- the walk a pass drives ----
// `visit` is called on every node of every function body, with that function's
// scope already entered. A visitor may rewrite the node it is given in place;
// the walk reads its children afterwards, so what a rewrite produces is walked
// as well.
void tk_ty_walk_list(i64 n, uptr visit) {
    loop {
        if (n == 0) break;
        callp(visit, n);
        tk_ty_walk_list(nd_a(n), visit);
        tk_ty_walk_list(nd_b(n), visit);
        tk_ty_walk_list(nd_c(n), visit);
        tk_ty_walk_list(nd_d(n), visit);
        n = nd_next(n);
    }
}

void tk_ty_pass_walk(i64 root, uptr visit) {
    i64 f = root;
    loop {
        if (f == 0) break;
        if (nd_kind(f) == N_FUNC) {
            tk_ty_scope_enter(f);
            tk_ty_walk_list(nd_b(f), visit);
        }
        f = nd_next(f);
    }
    tk_nscope = 0;
}

// ---- the deferred member access ----
i64 tk_pend_at(i64 n) {
    i64 i = tk_npend - 1;
    loop {
        if (i < 0) break;
        if (pd_node_at(i) == n) return i;
        i = i - 1;
    }
    return 0 - 1;
}

void tk_pend_add(i64 n, i64 recv, uptr m, i64 arg, i64 na, i64 form, i64 line, uptr fl) {
    if (tk_npend == TK_MAXPEND) err_at(fl, line, "teko: too many member accesses on a value of unknown type");
    set_pd_node_at(tk_npend, n);
    set_pd_recv_at(tk_npend, recv);
    set_pd_name_at(tk_npend, m);
    set_pd_arg_at(tk_npend, arg);
    set_pd_na_at(tk_npend, na);
    set_pd_form_at(tk_npend, form);
    set_pd_line_at(tk_npend, line);
    set_pd_file_at(tk_npend, fl);
    set_pd_done_at(tk_npend, 0);
    tk_npend = tk_npend + 1;
}

// 1 when the member's NAME cannot decide the type -- the exact case
// teko_expr.mc's tk_member_by_name stops on, in the same order it tries them
i64 tk_member_ambiguous(uptr m) {
    i64 fi = tk_field_by_name(m);
    if (fi == 0 - 2) return 1;
    if (fi >= 0) return 0;
    i64 si = tk_ifmeth_by_name(m);
    if (si == 0 - 2) return 1;
    if (si >= 0) return 0;
    return tk_method_by_name(m) == 0 - 2;
}

// `left . m` with the rest of the access still unread: what follows decides the
// FORM (a call, a store, a load) without knowing the type, which is the half of
// the work that has to happen while the parser is here. The placeholder node
// keeps the expression's place in its parent until the pass fills it.
i64 tk_defer_member(i64 left, uptr m, i64 line, uptr fl) {
    i64 form = TK_PLOAD;
    i64 arg = 0;
    i64 na = 0;
    if (p_id() == K_LPAR) {
        form = TK_PCALL;
        arg = tk_args(&na);
    } else if (p_accept(K_ASSIGN)) {
        form = TK_PSTORE;
        arg = parse_expr(0);
    }
    tk_line = line;
    tk_file = fl;
    i64 n = tk_int(0);
    tk_pend_add(n, left, m, arg, na, form, line, fl);
    return n;
}

// ---- rebuilding the access, once the receiver's type is known ----
// The three below emit exactly what teko_expr.mc emits for a receiver it could
// type: the load or store of the field's own width, a direct call to the
// mangled Owner_method, the vtable call for a virtual one, and the itab call
// for an interface. `prs`/`ppure` report the result's own type, so `a.b.c`
// chains through a deferred link as well.
i64 tk_pend_field(i64 pi, i64 fi, uptr prs, uptr ppure) {
    uptr m = pd_name_at(pi);
    if (pd_form_at(pi) == TK_PCALL)
        err_at2(tk_file, tk_line, "teko: the member is a field, not a method", m);
    i64 fty = fd_ty_at(fi);
    i64 addr = tk_bin(K_ADD, pd_recv_at(pi), tk_int(fd_off_at(fi)));
    if (pd_form_at(pi) == TK_PSTORE) return tk_call2(tk_stn(fty), addr, pd_arg_at(pi));
    st64(prs, tk_struct_by_ty(fty));
    st64(ppure, 1);
    return tk_call(tk_ldn(fty), addr);
}

i64 tk_pend_method(i64 pi, i64 mi, uptr prs, uptr ppure) {
    uptr m = pd_name_at(pi);
    if (pd_form_at(pi) != TK_PCALL)
        err_at2(tk_file, tk_line, "teko: the member is a method; call it with ()", m);
    if (pd_na_at(pi) != mt_np_at(mi))
        err_at2(tk_file, tk_line, "teko: wrong number of arguments", m);
    st64(prs, tk_struct_by_ty(mt_ret_at(mi)));
    st64(ppure, 0);
    i64 left = pd_recv_at(pi);
    i64 slot = mt_slot_at(mi);
    if (slot < 0) return tk_call(mt_fn_at(mi), list_append(left, pd_arg_at(pi)));
    if (!tk_pure(left))
        err_at2(tk_file, tk_line, "teko: a virtual call needs a name or a field on the left", m);
    i64 vt = tk_call("ld64", tk_clone(left));
    i64 fnp = tk_call("ld64", tk_bin(K_ADD, vt, tk_int((TK_VT_FIXED + slot) * 8)));
    return tk_call("callp", list_append(list_append(fnp, left), pd_arg_at(pi)));
}

i64 tk_pend_iface(i64 pi, i64 si, uptr prs, uptr ppure) {
    uptr m = pd_name_at(pi);
    i64 j = tk_ifmeth_find(si, m);
    if (j < 0) err_at2(tk_file, tk_line, tk_join("teko: unknown member of ", sr_name_at(si)), m);
    if (pd_form_at(pi) != TK_PCALL)
        err_at2(tk_file, tk_line, "teko: the member is a method; call it with ()", m);
    i64 k = sr_m0_at(si) + j;
    if (pd_na_at(pi) != im_np_at(k))
        err_at2(tk_file, tk_line, "teko: wrong number of arguments", m);
    i64 left = pd_recv_at(pi);
    if (!tk_pure(left))
        err_at2(tk_file, tk_line, "teko: an interface call needs a name or a field on the left", m);
    st64(prs, tk_struct_by_ty(im_ret_at(k)));
    st64(ppure, 0);
    i64 vt = tk_call("ld64", tk_clone(left));
    i64 mt = tk_call2("tk_itab", vt, tk_int(si));
    i64 fnp = tk_call("ld64", tk_bin(K_ADD, mt, tk_int(j * 8)));
    return tk_call("callp", list_append(list_append(fnp, left), pd_arg_at(pi)));
}

i64 tk_pend_emit(i64 pi, i64 si, uptr prs, uptr ppure) {
    uptr m = pd_name_at(pi);
    if (tk_is_iface(si)) return tk_pend_iface(pi, si, prs, ppure);
    i64 fi = tk_field_find(si, m);
    if (fi >= 0) return tk_pend_field(pi, fi, prs, ppure);
    i64 mi = tk_method_find(si, m);
    if (mi >= 0) return tk_pend_method(pi, mi, prs, ppure);
    err_at2(tk_file, tk_line, tk_join("teko: unknown member of ", sr_name_at(si)), m);
    return 0;
}

// The rewrite is IN PLACE, so the node index stays what the parent points at
// and the sibling list is put back untouched -- `mc` docs/reference/hooks.md
// § pass(). A receiver that is itself deferred is resolved first, which is what
// makes `p.inner.x` on a parameter work: the inner link registers its own type
// on the very node the outer one reads.
void tk_pend_do(i64 pi) {
    if (pd_done_at(pi)) return;
    set_pd_done_at(pi, 1);
    i64 recv = pd_recv_at(pi);
    i64 rp = tk_pend_at(recv);
    if (rp >= 0) tk_pend_do(rp);
    tk_line = pd_line_at(pi);
    tk_file = pd_file_at(pi);
    i64 si = tk_ty_struct_of(recv);
    if (si < 0)
        err_at2(tk_file, tk_line, "teko: the type of the left side of `.` is not known here", pd_name_at(pi));
    i64 rs = 0 - 1;
    i64 pure = 0;
    i64 r = tk_pend_emit(pi, si, &rs, &pure);
    i64 n = pd_node_at(pi);
    i64 keep = nd_next(n);
    node_assign(n, r);
    set_nd_next(n, keep);
    if (rs >= 0) tk_xt_add(n, rs, pure);
}

void tk_pend_visit(i64 n) {
    i64 pi = tk_pend_at(n);
    if (pi >= 0) tk_pend_do(pi);
}

// a deferred access the walk never reached -- a `.` outside any function body --
// is reported with the position it was written at, not left in the tree
void tk_pend_check() {
    i64 i = 0;
    loop {
        if (i >= tk_npend) break;
        if (!pd_done_at(i))
            err_at2(pd_file_at(i), pd_line_at(i),
                    "teko: the type of the left side of `.` is not known here", pd_name_at(i));
        i = i + 1;
    }
}

i64 tk_typeof_pass(i64 root) {
    if (tk_npend == 0) return root;
    tk_ty_pass_walk(root, &tk_pend_visit);
    tk_pend_check();
    return root;
}
