// teko_deleg.mc -- K1 (D221/§41): a NAMED delegate (C# 1.0), not a generic
// `Func<>`/`Action<>` -- one `type_new` per declaration, the signature kept in
// a small table of its own beside teko_struct.mc's type row.
//
//   public delegate i64 Op(i64 a, i64 b);
//   i64 add(i64 a, i64 b) { return a + b; }
//   Op f = add;                 // contextual: a local of delegate type
//   Op g = new Op(add);         // explicit, valid anywhere `new` is
//   f(3, 4)                     // rebaixed in a pass() to a typed `callp`
//
// A delegate value IS an object, with the same layout a class carries: word 0
// the vtable (release only, `TK_VT_FIXED` words, no itab), word 1 the count,
// word 2 the CODE pointer. `tk_is_counted` (teko_struct.mc) already answers
// for a `TK_KDELEG` row, so the reclaim (teko_rc.mc) needs zero new line: a
// local, a field, a parameter of delegate type is scoped and released exactly
// as a class reference is.
//
// ABI: every function reached through a delegate takes the OBJECT as an
// invisible first parameter (an `__env`, unused by a plain function -- K4's
// captures read it), so `callp(code, obj, args...)` is the ONE calling
// convention every delegate value shares, captures or none. A free function
// with no capture gets a THUNK that forwards to it, memoized per (delegate,
// function) pair: `Op__thunk_add(uptr __env, i64 a, i64 b) { return add(a,
// b); }`. Its own vtable, release and allocator are generated alongside it
// (`Op__vt_add`, `Op__release_add`, `Op__new_add`) -- the layout is per
// LAMBDA, not per delegate TYPE, so K4's captures cost no rewrite here.
//
// `f(3, 4)` on a local/parameter of delegate type is core grammar (an
// identifier followed by `(`), parsed with no hook at all -- `tk_deleg_pass`
// rewrites every such `N_CALL` into the typed `callp` above, reusing
// teko_typeof.mc's own scope walk (`tk_ty_pass_walk`); what it does not
// rewrite dies at `call to unknown function`, never in silence. A field of
// delegate type (`h.cb(2, 3)`) is resolved where it is written instead,
// teko_expr.mc's `tk_field_use`.

#define TK_DGMAX (MAXPARAMS - 2)      // callp: code + obj + params, <= MAXPARAMS
#define TK_DGOBJSIZE 24                // vtable + count + code, no capture (K1)
#define TK_MAXDGI 64                   // (delegate, function) pairs one unit wraps

i64 dg_ret[TK_MAXSTRUCT];             // the declared return type
i64 dg_np[TK_MAXSTRUCT];              // how many parameters
i64 dg_pty[TK_MAXSTRUCT * TK_DGMAX];  // the parameter types, flattened

i64  dg_ret_at(i64 i)         { return ld64(dg_ret + i * 8); }
i64  dg_np_at(i64 i)          { return ld64(dg_np + i * 8); }
i64  dg_pty_at(i64 si, i64 j) { return ld64(dg_pty + (si * TK_DGMAX + j) * 8); }

void set_dg_ret_at(i64 i, i64 v)         { st64(dg_ret + i * 8, v); }
void set_dg_np_at(i64 i, i64 v)          { st64(dg_np + i * 8, v); }
void set_dg_pty_at(i64 si, i64 j, i64 v) { st64(dg_pty + (si * TK_DGMAX + j) * 8, v); }

uptr dgi_row[TK_MAXDGI];              // the delegate row a wrap targets
uptr dgi_fn[TK_MAXDGI];               // the function it wraps
uptr dgi_alloc[TK_MAXDGI];            // the allocator symbol already built for it
i64  tk_ndgi = 0;

i64  dgi_row_at(i64 i)   { return ld64(dgi_row + i * 8); }
uptr dgi_fn_at(i64 i)    { return ld64(dgi_fn + i * 8); }
uptr dgi_alloc_at(i64 i) { return ld64(dgi_alloc + i * 8); }

void set_dgi_row_at(i64 i, i64 v)   { st64(dgi_row + i * 8, v); }
void set_dgi_fn_at(i64 i, uptr v)   { st64(dgi_fn + i * 8, v); }
void set_dgi_alloc_at(i64 i, uptr v) { st64(dgi_alloc + i * 8, v); }

// `Op(i64, i64)`, for a mismatch message
uptr tk_deleg_sig_str(i64 si) {
    uptr s = tk_join(sr_name_at(si), "(");
    i64 np = dg_np_at(si);
    i64 i = 0;
    loop {
        if (i >= np) break;
        if (i > 0) s = tk_join(s, ", ");
        s = tk_join(s, type_name(dg_pty_at(si, i)));
        i = i + 1;
    }
    return tk_join(s, ")");
}

// the row of a delegate row-index `ty` names, or -1 for anything else --
// teko_struct.mc's own type table, filtered to `TK_KDELEG`
i64 tk_deleg_row(i64 ty) {
    i64 si = tk_struct_by_ty(ty);
    if (si < 0) return 0 - 1;
    if (!tk_is_deleg(si)) return 0 - 1;
    return si;
}

// the delegate row named `name`, or -1 for a plain type/no type at all: the
// early branch `tk_new()` takes before its own struct/class lookup
i64 tk_deleg_find(uptr name) {
    i64 si = tk_struct_find(name);
    if (si < 0) return 0 - 1;
    if (!tk_is_deleg(si)) return 0 - 1;
    return si;
}

// `d`'s signature has to match `si`'s exactly: same arity, same return, same
// parameter types in order -- `add does not match the delegate Op(i64, i64)`
void tk_deleg_check_sig(i64 si, uptr fn, i64 d) {
    i64 np = dg_np_at(si);
    i64 ok = decl_nparams(d) == np && decl_ret(d) == dg_ret_at(si);
    i64 i = 0;
    loop {
        if (!ok) break;
        if (i >= np) break;
        if (decl_param_type(d, i) != dg_pty_at(si, i)) ok = 0;
        i = i + 1;
    }
    if (ok) return;
    err_at(tk_file, tk_line,
           tk_join(tk_join(tk_join("teko: ", fn), " does not match the delegate "),
                   tk_deleg_sig_str(si)));
}

// `i64 Op__thunk_add(uptr env, i64 a, i64 b) { return add(a, b); }` -- the
// ABI every delegate value shares, so a plain function needs a forwarder
i64 tk_deleg_thunk_fn(i64 si, uptr thunkname, uptr fn) {
    i64 params = param_new(TY_UPTR, "env");
    i64 args = 0;
    i64 np = dg_np_at(si);
    i64 i = 0;
    loop {
        if (i >= np) break;
        uptr pn = tk_join("a", tk_num(i));
        params = list_append(params, param_new(dg_pty_at(si, i), pn));
        args = list_append(args, tk_id(pn));
        i = i + 1;
    }
    i64 ret = dg_ret_at(si);
    i64 call = tk_call(fn, args);
    i64 body;
    if (ret == TY_VOID) body = tk_blk(tk_stmt(call));
    else                body = tk_blk(tk_ret(call));
    return tk_func(ret, thunkname, params, body);
}

// `void Op__release_add(uptr this) { rt_free(this, 24); }` -- no capture in
// K1, so nothing but the block itself to hand back
i64 tk_deleg_release_fn(uptr relname) {
    i64 st = tk_stmt(tk_call2("rt_free", tk_id(tk_this_name()), tk_int(TK_DGOBJSIZE)));
    return tk_func(TY_VOID, relname, param_new(TY_UPTR, tk_this_name()), tk_blk(st));
}

// `Op Op__new_add() { ... }` -- the vtable's word 0 is filled on every call,
// idempotently, the same shape `tk_new_install` (teko_class.mc) fills a
// class's; the count starts at 1, the reference `new`/the coercion hands out
i64 tk_deleg_alloc_fn(i64 si, uptr allocname, uptr vtname, uptr relname, uptr thunkname) {
    i64 st = tk_var(TY_UPTR, "p", tk_call("rt_alloc", tk_int(TK_DGOBJSIZE)));
    st = list_append(st, tk_stmt(tk_call2("st64", tk_id(vtname), tk_addr(relname))));
    st = list_append(st, tk_stmt(tk_call2("st64", tk_id("p"), tk_id(vtname))));
    i64 cnt = tk_bin(K_ADD, tk_id("p"), tk_int(8));
    st = list_append(st, tk_stmt(tk_call2("st64", cnt, tk_int(1))));
    i64 code = tk_bin(K_ADD, tk_id("p"), tk_int(16));
    st = list_append(st, tk_stmt(tk_call2("st64", code, tk_addr(thunkname))));
    i64 r = tk_id("p");
    tk_xt_add(r, si, 0);                          // already owned: the count above, not rc_inc
    st = list_append(st, tk_ret(r));
    return tk_func(sr_ty_at(si), allocname, 0, tk_blk(st));
}

// the allocator that wraps `fn` as a value of delegate row `si`, generated and
// memoized the first time the pair is asked for -- `Op__new_add`
uptr tk_deleg_thunk(i64 si, uptr fn) {
    i64 i = 0;
    loop {
        if (i >= tk_ndgi) break;
        if (dgi_row_at(i) == si && str_eq(dgi_fn_at(i), fn)) return dgi_alloc_at(i);
        i = i + 1;
    }
    i64 d = decl_find(fn);
    if (d < 0 || !decl_valid(d)) err_at2(tk_file, tk_line, "teko: unknown function", fn);
    tk_deleg_check_sig(si, fn, d);
    uptr base = sr_name_at(si);
    uptr allocname = tk_join(tk_join(base, "__new_"), fn);
    uptr thunkname = tk_join(tk_join(base, "__thunk_"), fn);
    uptr vtname = tk_join(tk_join(base, "__vt_"), fn);
    uptr relname = tk_join(tk_join(base, "__release_"), fn);
    top_add(tk_deleg_thunk_fn(si, thunkname, fn));
    top_add(tk_glb(TY_U8, vtname, TK_VT_FIXED * 8));
    top_add(tk_deleg_release_fn(relname));
    top_add(tk_deleg_alloc_fn(si, allocname, vtname, relname, thunkname));
    if (tk_ndgi == TK_MAXDGI) err_at(tk_file, tk_line, "teko: too many delegate targets");
    set_dgi_row_at(tk_ndgi, si);
    set_dgi_fn_at(tk_ndgi, fn);
    set_dgi_alloc_at(tk_ndgi, allocname);
    tk_ndgi = tk_ndgi + 1;
    return allocname;
}

// `delegate i64 Op(i64 a, i64 b);` -- the parameter list's own types, read by
// the core's `parse_params()` (the exact grammar a delegate's own list needs)
// and kept; the names themselves are not
void tk_deleg_set_sig(i64 si, i64 ret, i64 params, i64 line, uptr fl) {
    set_dg_ret_at(si, ret);
    i64 n = 0;
    i64 p = params;
    loop {
        if (p == 0) break;
        if (n == TK_DGMAX)
            err_at(fl, line, "teko: method with too many parameters (the receiver counts, and so does the vtable pointer of a virtual call)");
        set_dg_pty_at(si, n, nd_type(p));
        n = n + 1;
        p = nd_next(p);
    }
    set_dg_np_at(si, n);
}

// `[public|internal] delegate T Name(params);` -- a top-level declaration,
// C#'s own default visibility (teko_access.mc's `tk_take_decl_vis`)
void tk_delegate() {
    i64 line = p_line();
    uptr fl = p_file();
    p_next();                                     // the `delegate` word
    i64 vis = tk_take_decl_vis();
    i64 proj = tk_take_decl_proj();
    i64 ret = p_type();
    uptr name = tk_newname("delegate");
    name = tk_ns_qualify(name);
    i64 ty = tk_type_word(name);
    i64 si = tk_type_add(name, ty, 0 - 1, TK_KDELEG, vis, proj);
    p_set_decl_name(name);
    i64 params = parse_params();
    p_expect(K_SEMI, "expected ; after the delegate declaration");
    tk_deleg_set_sig(si, ret, params, line, fl);
}

// `new Op(add)` / `Op f = add;`: the object `tk_deleg_thunk` allocates,
// re-typed to the delegate row `si`
i64 tk_deleg_wrap(i64 si, uptr fname, i64 line, uptr fl) {
    tk_line = line;
    tk_file = fl;
    i64 n = tk_call(tk_deleg_thunk(si, fname), 0);
    tk_xt_put(n, si, sr_ty_at(si), 0);
    return n;
}

// `new Op(add)`: valid anywhere `new` is, including a function-livre argument
// (D221 decision 19's explicit form)
i64 tk_new_deleg(i64 si, i64 line, uptr fl) {
    tk_check_type_use(si, line, fl);
    p_expect(K_LPAR, "expected ( after the delegate name");
    i64 e = parse_expr(0);
    p_expect(K_RPAR, "expected ) after the delegate target");
    if (nd_kind(e) != N_IDENT)
        err_at(fl, line, "teko: `new Op(...)` takes the name of a function");
    return tk_deleg_wrap(si, nd_name(e), line, fl);
}

// `code`/`obj`/`args` -> the typed `callp` expression, shared by the bare-call
// rewrite (`tk_deleg_call`) and a field call (teko_expr.mc's `tk_field_use`).
// `obj` is read TWICE -- once for the code pointer, once as the call's own
// object argument -- so it has to be pure; a name and a field load both are.
i64 tk_deleg_build(i64 si, i64 obj, i64 args, i64 na, i64 line, uptr fl) {
    tk_line = line;
    tk_file = fl;
    if (na != dg_np_at(si))
        err_at(fl, line, tk_join("teko: wrong number of arguments for ", sr_name_at(si)));
    i64 code = tk_call("tk_deleg_code", tk_clone(obj));
    i64 call = tk_call("callp", list_append(list_append(code, obj), args));
    i64 ret = dg_ret_at(si);
    i64 r = call;
    if (ret != TY_VOID && type_width(ret) < 8) r = tk_cast(ret, call);
    tk_xt_put(r, tk_struct_by_ty(ret), ret, 0);
    return r;
}

// how many arguments an already-parsed sibling list holds
i64 tk_deleg_argc(i64 args) {
    i64 n = 0;
    i64 a = args;
    loop {
        if (a == 0) break;
        n = n + 1;
        a = nd_next(a);
    }
    return n;
}

// `f(3, 4)` where `f` is a local/parameter of delegate row `di`: rewritten IN
// PLACE into the typed `callp` above (hooks.md § pass())
i64 tk_deleg_call(i64 n, i64 di) {
    i64 line = nd_line(n);
    uptr fl = nd_file(n);
    i64 args = nd_a(n);
    i64 obj = tk_id(nd_name(n));
    i64 r = tk_deleg_build(di, obj, args, tk_deleg_argc(args), line, fl);
    i64 keep = nd_next(n);
    node_assign(n, r);
    set_nd_next(n, keep);
    return n;
}

i64 tk_deleg_cur_ns = 0;              // the namespace of the function this pass walks

// `gadd`, named bare inside `namespace geo { ... }`, is not this pass's own
// call/const rewrite (teko_ns.mc's `tk_ns_pass` ran first and only touches an
// `N_CALL`/`N_ADDR`/a const `N_IDENT`) -- a plain function reference is left
// exactly as the source wrote it, so the wrap has to try the DECLARED symbol
// first and only then the same namespace/`using` search a call already gets
uptr tk_deleg_resolve_fn(uptr fname, i64 line, uptr fl) {
    if (decl_find(fname) >= 0) return fname;
    i64 d = tk_ns_call_try_prefixes(fname, tk_deleg_cur_ns);
    if (d < 0) d = tk_ns_call_try_usings(fname, fl, line);
    if (d < 0) return fname;
    return nd_name(d);
}

// `Op f = add;`: a local/field initializer that names a FUNCTION (not a local
// or parameter -- one of those shadows the function and is left as an
// ordinary value copy) is coerced into the same wrap `new Op(add)` builds
void tk_deleg_var(i64 n) {
    if (nd_val(n) != 0) return;                   // `Op tbl[4]`: references
    i64 si = tk_deleg_row(nd_type(n));
    if (si < 0) return;
    i64 e = nd_a(n);
    if (e == 0 || nd_kind(e) != N_IDENT) return;
    if (tk_ty_scope_find(nd_name(e)) >= 0) return;
    uptr fname = tk_deleg_resolve_fn(nd_name(e), nd_line(n), nd_file(n));
    set_nd_a(n, tk_deleg_wrap(si, fname, nd_line(n), nd_file(n)));
}

void tk_deleg_visit(i64 n) {
    i64 k = nd_kind(n);
    if (k == N_VAR) { tk_deleg_var(n); return; }
    if (k != N_CALL) return;
    i64 ty = tk_ty_scope_find(nd_name(n));
    if (ty < 0) return;
    i64 di = tk_deleg_row(ty);
    if (di < 0) return;
    tk_deleg_call(n, di);
}

// the same shape teko_typeof.mc's own `tk_ty_walk_list` walks a function's
// body in -- a block scoped, marked and cut back, a local joining scope only
// after its own initializer is walked -- but a walk OF ITS OWN rather than a
// reuse of that one: `tk_deleg_var`'s namespace lookup needs to know which
// function it is inside (`tk_deleg_cur_ns`), which is set per top-level
// N_FUNC below, and the shared walk has nowhere to carry that
void tk_deleg_walk(i64 n) {
    loop {
        if (n == 0) break;
        tk_deleg_visit(n);
        if (nd_kind(n) == N_BLOCK) {
            i64 mark = tk_nscope;
            tk_deleg_walk(nd_a(n));
            tk_nscope = mark;
        } else {
            tk_deleg_walk(nd_a(n));
            tk_deleg_walk(nd_b(n));
            tk_deleg_walk(nd_c(n));
            tk_deleg_walk(nd_d(n));
            tk_ty_scope_var(n);
        }
        n = nd_next(n);
    }
}

// 1 when the program declares at least one delegate: a program that declares
// none is untouched, byte for byte
i64 tk_any_deleg() {
    i64 i = 0;
    loop {
        if (i >= tk_nstruct) break;
        if (tk_is_deleg(i)) return 1;
        i = i + 1;
    }
    return 0;
}

i64 tk_deleg_pass(i64 root) {
    if (!tk_any_deleg()) return root;
    i64 f = root;
    loop {
        if (f == 0) break;
        if (nd_kind(f) == N_FUNC) {
            tk_nscope = 0;
            tk_deleg_cur_ns = tk_ns_of_name(nd_name(f));
            tk_ty_scope_params(nd_a(f));
            tk_deleg_walk(nd_b(f));
            tk_nscope = 0;
        }
        f = nd_next(f);
    }
    tk_deleg_cur_ns = 0;
    return root;
}
