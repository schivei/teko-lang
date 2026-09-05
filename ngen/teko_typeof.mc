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
//   1. the node table teko_struct.mc builds (`tk_xt_ty`) -- a node THIS module
//      produced, so its type is known exactly, scalar fields included;
//   2. the scope of the function being walked -- its N_PARAM list and the
//      N_VAR of its body, which is what `pass()` makes readable;
//   3. the callee's declared return type, for an N_CALL, the type the core
//      itself put on a literal or a cast, and -- for a binary or a unary --
//      the core's own rule over the operands.
//
// It does NOT read `tk_local` (teko_struct.mc): that table belongs to the parse,
// and by the time the pass runs it is empty. It builds its own scope over the
// tree, under the same rule -- a mark per block, cut back on the way out -- so a
// name declared in an inner block answers only there. A name this oracle cannot
// decide -- a global, a receiver it never saw -- answers -1, and only THEN does
// the member's own name get to resolve the access (`tk_pend_by_name`).
//
// The consumer here is `.` on a receiver the parser could not type: teko_expr.mc
// DEFERS every one of them rather than guessing from the member's name, because
// a name that only another type declares is not this receiver's member. The
// access is RECORDED and rebuilt in the pass, in the same shapes teko_expr.mc emits
// (field load, field store, direct call, vtable call, itab call). "The same
// shapes" includes the two things a call resolves by: the SIGNATURE the
// argument count picks, whose symbol carries the overload suffix, and the
// arguments the site left out, which come from that same declaration's
// defaults -- a rebuild that skipped either would call another method than the
// one written.
//
// The walk carries a SECOND rewrite, teko_this.mc's: an unqualified name inside
// a method is a member of `this` unless a local or a parameter answers first,
// and the scope this file already maintains is what answers that. A program
// with no deferred `.` and no method at all never enters the pass --
// `tk_typeof_pass` returns the root untouched -- so a tree that is not this
// pass's business is not this pass's to move.

#define TK_MAXSCOPE 256               // names of one function: parameters and locals
#define TK_MAXPEND  128               // member accesses waiting for the pass

// what the deferred access still has to become
#define TK_PLOAD  0                   // p.m
#define TK_PSTORE 1                   // p.m = e
#define TK_PCALL  2                   // p.m(...)

// the argument list of a deferred call is read at parse time, by the same
// function teko_expr.mc uses for a call it can resolve at once
i64 tk_args(uptr pn);

// the teko_expr.mc pieces a rebuilt call needs to be the SAME call the parser
// would have produced: the arguments the site left out, taken from the
// declaration the call resolves against, and the two refusals a resolution that
// did not land on a single signature gets -- one for a pick inside a known type,
// one for a pick by name alone
i64 tk_fill_defaults(i64 args, i64 na, i64 np, i64 nreq, i64 d0);
void tk_pick_refuse(i64 mi, uptr m, i64 line, uptr fl);
void tk_loose_refuse(i64 mi, uptr m, i64 line, uptr fl);

uptr sc_name[TK_MAXSCOPE];            // the names in scope, innermost last
i64  sc_ty[TK_MAXSCOPE];              // the type each was declared with
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
// A STACK, marked at every N_BLOCK the walk enters and cut back when it leaves,
// which is the same shape teko_struct.mc's table of locals has at parse time.
// The newest entry wins, so a declaration inside a block shadows the parameter
// of the same name only while that block is open -- and, once it closes, the
// parameter answers with its own type again instead of being poisoned by a name
// it has nothing to do with.
void tk_ty_scope_add(uptr name, i64 ty) {
    if (tk_nscope == TK_MAXSCOPE) return;        // beyond the table: answers -1
    set_sc_name_at(tk_nscope, name);
    set_sc_ty_at(tk_nscope, ty);
    tk_nscope = tk_nscope + 1;
}

i64 tk_ty_scope_find(uptr name) {
    i64 i = tk_nscope - 1;
    loop {
        if (i < 0) break;
        if (str_eq(sc_name_at(i), name)) return sc_ty_at(i);
        i = i - 1;
    }
    return 0 - 1;
}

// a local the body declares. `nd_val` is the array length, so `Point tbl[4]` --
// four references, not one object -- is skipped, exactly as teko_struct.mc's
// on_stmt hook skips it.
void tk_ty_scope_var(i64 n) {
    if (nd_kind(n) != N_VAR) return;
    if (nd_val(n) != 0) return;
    tk_ty_scope_add(nd_name(n), nd_type(n));
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

// ---- the static type of a node, under the scope currently entered ----
i64 tk_ty_of(i64 n);

// A composite expression answers by the CORE's own rule, the one codegen will
// apply to the very same node (mc/src/gen_resolve.mc, res_binary and res_expr's
// N_UNARY arm): a logical operator and a comparison are `i64`, and every other
// binary keeps the LEFT operand's type -- which is the rule the divide and the
// shift already read to pick their signed form. An operand nothing types makes
// the whole expression untyped: -1 travels outward instead of being guessed at.
i64 tk_ty_binary(i64 n) {
    i64 op = nd_op(n);
    if (op == K_ANDAND || op == K_OROR) return TY_I64;
    if (cmp_cond(op) >= 0) return TY_I64;
    return tk_ty_of(nd_a(n));
}

i64 tk_ty_of(i64 n) {
    if (n == 0) return 0 - 1;
    i64 t = tk_xt_ty(n);
    if (t >= 0) return t;
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
    if (k == N_BINARY) return tk_ty_binary(n);
    if (k == N_UNARY) {
        if (nd_op(n) == K_BANG) return TY_I64;
        return tk_ty_of(nd_a(n));
    }
    return 0 - 1;
}

// a receiver whose type IS known but names no row of teko_struct.mc's type
// table -- a scalar (`i64`, `uptr`, a field's own width, ...) -- declares no
// member at all, so a `.` on it is always wrong. Distinguishing this from
// "the type is not known" (where the member's own name gets to answer,
// tk_pend_by_name) is the whole point of the oracle knowing `tk_ty_of`.
void tk_reject_scalar_member(uptr fl, i64 line, i64 ty, uptr m) {
    err_at2(fl, line, tk_join3("teko: ", type_name(ty), " has no members"), m);
}

// ---- the walk a pass drives ----
// `visit` is called on every node of every function body, in the order the
// source wrote it and under the scope that holds AT that node: a block pushes a
// mark and pops it, and a local joins the scope only after the statement that
// declares it has been walked, so its own initializer still reads the outer
// name. A visitor may rewrite the node it is given in place; the walk reads its
// children afterwards, so what a rewrite produces is walked as well.
void tk_ty_walk_list(i64 n, uptr visit) {
    loop {
        if (n == 0) break;
        callp(visit, n);
        if (nd_kind(n) == N_BLOCK) {
            i64 mark = tk_nscope;
            tk_ty_walk_list(nd_a(n), visit);
            tk_nscope = mark;
        } else {
            tk_ty_walk_list(nd_a(n), visit);
            tk_ty_walk_list(nd_b(n), visit);
            tk_ty_walk_list(nd_c(n), visit);
            tk_ty_walk_list(nd_d(n), visit);
            tk_ty_scope_var(n);
        }
        n = nd_next(n);
    }
}

void tk_ty_pass_walk(i64 root, uptr visit) {
    i64 f = root;
    loop {
        if (f == 0) break;
        if (nd_kind(f) == N_FUNC) {
            tk_nscope = 0;
            tk_this_enter_fn(nd_name(f));        // the type an unqualified name belongs to
            tk_pass_proj = tk_origin_of_file(nd_file(f));   // ...and where its code came from
            tk_ty_scope_params(nd_a(f));
            tk_ty_walk_list(nd_b(f), visit);
        }
        f = nd_next(f);
    }
    tk_nscope = 0;
    tk_pass_proj = 0 - 1;
    tk_this_leave_fn();
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

// `left . m` with the rest of the access still unread: what follows decides the
// FORM (a call, a store, a load) without knowing the type, which is the half of
// the work that has to happen while the parser is here. The placeholder node
// keeps the expression's place in its parent until the pass fills it.
//
// It is a call to a symbol NOTHING declares, and deliberately so. A literal
// would COMPILE: a `.` this pass never got to rewrite -- because the pass was
// not registered, or because one that runs later put the node there -- would be
// a silent `0` in the middle of an expression, and the program would run and
// answer wrong (measured, with the pass unregistered: types_class.tk built and
// exited 12 instead of 42). A name nothing declares cannot end that way: the
// core's own resolver refuses the call where it is written (`call to unknown
// function`, mc/src/gen_resolve.mc res_call), and a declared-but-undefined one
// would stop at the link. It costs nothing when the rewrite does happen -- the
// node is overwritten in place, so the name never reaches codegen.
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
    i64 n = tk_call("tk_unresolved_member", 0);
    tk_pend_add(n, left, m, arg, na, form, line, fl);
    return n;
}

// ---- rebuilding the access, once the receiver's type is known ----
// The three below emit exactly what teko_expr.mc emits for a receiver it could
// type: the load or store of the field's own width, a direct call to the
// mangled Owner_method, the vtable call for a virtual one, and the itab call
// for an interface. `pty`/`ppure` report the result's own TYPE -- a scalar one
// as much as a struct one, so `a.b.c` chains through a deferred link and
// `f(p.side)` has an argument type to be resolved by.
i64 tk_pend_field(i64 pi, i64 fi, uptr pty, uptr ppure) {
    uptr m = pd_name_at(pi);
    if (pd_form_at(pi) == TK_PCALL)
        err_at2(tk_file, tk_line, "teko: the member is a field, not a method", m);
    i64 fty = fd_ty_at(fi);
    i64 addr = tk_bin(K_ADD, pd_recv_at(pi), tk_int(fd_off_at(fi)));
    if (pd_form_at(pi) == TK_PSTORE)
        return tk_os_mark(tk_call2(tk_stn(fty), addr, pd_arg_at(pi)), fty);
    st64(pty, fty);
    st64(ppure, 1);
    return tk_call(tk_ldn(fty), addr);
}

// the call itself, once the declaration is known: the declaration's own symbol
// carries the overload suffix, the arguments the site left out come from that
// declaration's defaults, and a virtual one goes through the object's vtable --
// the shapes teko_expr.mc's tk_emit_call produces for a receiver it could type
i64 tk_pend_emit_call(i64 pi, i64 mi, i64 args, i64 na, uptr pty, uptr ppure) {
    uptr m = pd_name_at(pi);
    args = tk_fill_defaults(args, na, mt_np_at(mi), mt_nreq_at(mi), mt_d0_at(mi));
    st64(pty, mt_ret_at(mi));
    st64(ppure, 0);
    i64 left = pd_recv_at(pi);
    i64 slot = mt_slot_at(mi);
    if (slot < 0) return tk_call(mt_fn_at(mi), list_append(left, args));
    if (!tk_pure(left))
        err_at2(tk_file, tk_line, "teko: a virtual call needs a name or a field on the left", m);
    i64 vt = tk_call("ld64", tk_clone(left));
    i64 fnp = tk_call("ld64", tk_bin(K_ADD, vt, tk_int((TK_VT_FIXED + slot) * 8)));
    return tk_call("callp", list_append(list_append(fnp, left), args));
}

i64 tk_pend_emit_method(i64 pi, i64 mi, uptr pty, uptr ppure) {
    return tk_pend_emit_call(pi, mi, pd_arg_at(pi), pd_na_at(pi), pty, ppure);
}

// `p.X` / `p.X = e` on a receiver only the oracle could type: the accessor the
// property resolves to, and the very call teko_expr.mc's tk_prop_use emits
i64 tk_pend_prop(i64 pi, i64 si, uptr pty, uptr ppure) {
    uptr m = pd_name_at(pi);
    if (pd_form_at(pi) == TK_PCALL)
        err_at2(tk_file, tk_line, "teko: the member is a property; it is not called", m);
    i64 wantset = 0;
    i64 args = 0;
    i64 na = 0;
    if (pd_form_at(pi) == TK_PSTORE) {
        wantset = 1;
        args = pd_arg_at(pi);
        na = 1;
    }
    i64 mi = tk_prop_accessor_of(si, m, wantset, tk_line, tk_file);
    if (mt_static_at(mi)) tk_reject_static_member(si, m, tk_line, tk_file);
    return tk_pend_emit_call(pi, mi, args, na, pty, ppure);
}

// The arguments the site wrote decide WHICH signature of the name is called,
// exactly as they do at parse time (teko_expr.mc's tk_call_method): the pick
// answers the declaration, inside the receiver's own type and its bases.
i64 tk_pend_method(i64 pi, i64 si, uptr pty, uptr ppure) {
    uptr m = pd_name_at(pi);
    if (pd_form_at(pi) != TK_PCALL)
        err_at2(tk_file, tk_line, "teko: the member is a method; call it with ()", m);
    i64 mi = tk_method_pick(si, m, pd_na_at(pi));
    if (mi < 0) tk_pick_refuse(mi, m, tk_line, tk_file);
    tk_check_member(mt_cls_at(mi), mt_vis_at(mi), m, tk_line, tk_file);
    if (mt_static_at(mi)) tk_reject_static_member(si, m, tk_line, tk_file);
    return tk_pend_emit_method(pi, mi, pty, ppure);
}

// `s.X` / `s.X = e` on an interface-typed receiver the oracle answered for
i64 tk_pend_iface_prop(i64 pi, i64 si, uptr pty, uptr ppure) {
    uptr m = pd_name_at(pi);
    if (pd_form_at(pi) == TK_PCALL)
        err_at2(tk_file, tk_line, "teko: the member is a property; it is not called", m);
    i64 wantset = 0;
    i64 args = 0;
    if (pd_form_at(pi) == TK_PSTORE) {
        wantset = 1;
        args = pd_arg_at(pi);
    }
    i64 j = tk_ifprop_pick(si, m, wantset, tk_line, tk_file);
    st64(pty, im_ret_at(sr_m0_at(si) + j));
    st64(ppure, 0);
    return tk_itab_emit(pd_recv_at(pi), si, j, args, m, tk_line, tk_file);
}

i64 tk_pend_iface(i64 pi, i64 si, uptr pty, uptr ppure) {
    uptr m = pd_name_at(pi);
    tk_check_type_use(si, tk_line, tk_file);     // an interface's members are public
    if (tk_prop_find(si, m) >= 0) return tk_pend_iface_prop(pi, si, pty, ppure);
    if (tk_ifmeth_find(si, m) < 0)
        err_at2(tk_file, tk_line, tk_join("teko: unknown member of ", sr_name_at(si)), m);
    if (pd_form_at(pi) != TK_PCALL)
        err_at2(tk_file, tk_line, "teko: the member is a method; call it with ()", m);
    i64 na = pd_na_at(pi);
    i64 j = tk_ifmeth_pick(si, m, na);
    if (j < 0) tk_pick_refuse(j, m, tk_line, tk_file);
    i64 k = sr_m0_at(si) + j;
    i64 args = tk_fill_defaults(pd_arg_at(pi), na, im_np_at(k), im_nreq_at(k), im_d0_at(k));
    st64(pty, im_ret_at(k));
    st64(ppure, 0);
    return tk_itab_emit(pd_recv_at(pi), si, j, args, m, tk_line, tk_file);
}

i64 tk_pend_emit(i64 pi, i64 si, uptr pty, uptr ppure) {
    uptr m = pd_name_at(pi);
    if (tk_is_iface(si)) return tk_pend_iface(pi, si, pty, ppure);
    i64 fi = tk_field_find(si, m);
    if (fi >= 0) {
        tk_check_member(tk_field_owner(fi), fd_vis_at(fi), m, tk_line, tk_file);
        if (fd_sym_at(fi)) tk_reject_static_member(si, m, tk_line, tk_file);
        return tk_pend_field(pi, fi, pty, ppure);
    }
    if (tk_prop_find(si, m) >= 0) return tk_pend_prop(pi, si, pty, ppure);
    if (tk_method_named_find(si, m) >= 0) return tk_pend_method(pi, si, pty, ppure);
    err_at2(tk_file, tk_line, tk_join("teko: unknown member of ", sr_name_at(si)), m);
    return 0;
}

// the LAST resort, and only after the oracle has said it does not know the
// receiver's type: a global, or an expression whose type nothing reports. The
// member's name then has to belong to exactly one type. An interface answers
// ahead of a class, because its dispatch is the one that stays correct for every
// conforming class -- the itab is walked at run time, so no vtable slot is
// guessed.
i64 tk_pend_by_name(i64 pi, uptr pty, uptr ppure) {
    uptr m = pd_name_at(pi);
    i64 fi = tk_field_by_name(m);
    if (fi == 0 - 2)
        err_at2(tk_file, tk_line, "teko: the type of the left side of `.` is not known here", m);
    if (fi >= 0) {
        i64 owner = tk_field_owner(fi);
        tk_check_member(owner, fd_vis_at(fi), m, tk_line, tk_file);
        if (fd_sym_at(fi)) tk_reject_static_member(owner, m, tk_line, tk_file);
        return tk_pend_field(pi, fi, pty, ppure);
    }
    i64 pj = tk_prop_by_name(m);
    if (pj == 0 - 2)
        err_at2(tk_file, tk_line, "teko: the type of the left side of `.` is not known here", m);
    if (pj >= 0) {
        i64 owner = pr_cls_at(pj);
        if (tk_is_iface(owner)) return tk_pend_iface_prop(pi, owner, pty, ppure);
        return tk_pend_prop(pi, owner, pty, ppure);
    }
    i64 si = tk_ifmeth_by_name(m);
    if (si == 0 - 2)
        err_at2(tk_file, tk_line, "teko: the type of the left side of `.` is not known here", m);
    if (si >= 0) return tk_pend_iface(pi, si, pty, ppure);
    if (!tk_method_has_name(m)) err_at2(tk_file, tk_line, "teko: unknown member", m);
    if (pd_form_at(pi) != TK_PCALL)
        err_at2(tk_file, tk_line, "teko: the member is a method; call it with ()", m);
    i64 mi = tk_method_by_name(m, pd_na_at(pi));
    if (mi < 0) tk_loose_refuse(mi, m, tk_line, tk_file);
    tk_check_member(mt_cls_at(mi), mt_vis_at(mi), m, tk_line, tk_file);
    if (mt_static_at(mi)) tk_reject_static_member(mt_cls_at(mi), m, tk_line, tk_file);
    return tk_pend_emit_method(pi, mi, pty, ppure);
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
    tk_this_fix(recv);                           // `inner.x`: the receiver is a field of `this`
    tk_line = pd_line_at(pi);
    tk_file = pd_file_at(pi);
    i64 recv_ty = tk_ty_of(recv);
    i64 si = 0 - 1;
    if (recv_ty >= 0) si = tk_struct_by_ty(recv_ty);
    i64 rty = 0 - 1;
    i64 pure = 0;
    i64 r = 0;
    if (si >= 0)            r = tk_pend_emit(pi, si, &rty, &pure);
    else if (recv_ty >= 0)  tk_reject_scalar_member(tk_file, tk_line, recv_ty, pd_name_at(pi));
    else                    r = tk_pend_by_name(pi, &rty, &pure);
    i64 n = pd_node_at(pi);
    i64 keep = nd_next(n);
    node_assign(n, r);
    set_nd_next(n, keep);
    // the node that ends up in the TREE is the placeholder, not what was built
    // for it, so a store into a slot of counted type is re-marked under the
    // identity the reclaim pass will walk (teko_rc.mc)
    if (tk_os_has(r)) tk_os_add(n);
    if (rty >= 0) tk_xt_put(n, tk_struct_by_ty(rty), rty, pure);
}

// One walk, two rewrites: the deferred `.` this file owns, and the unqualified
// member teko_this.mc owns. They share the walk because they share what it
// carries -- the scope that says whether a name is a local, and the position in
// the tree where a node may be replaced by what it stood for.
void tk_pend_visit(i64 n) {
    i64 pi = tk_pend_at(n);
    if (pi >= 0) {
        tk_pend_do(pi);
        return;
    }
    tk_this_fix(n);
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
    if (tk_npend == 0 && tk_nmethod == 0) return root;
    tk_ty_pass_walk(root, &tk_pend_visit);
    tk_pend_check();
    return root;
}
