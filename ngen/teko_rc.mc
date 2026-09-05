// teko_rc.mc -- the reclaim (D218): reference counting per SCOPE, injected over
// the tree of every function body. It is what turns the fixed 4 MiB arena of
// ngen/lib/rt.mc into memory a program gets back: a local of class type is
// released at the `}` that closes its block, at every jump that leaves it, and
// at the `return` that leaves the function, and the release the count reaches
// runs the destructor, releases the object's own counted fields and hands the
// block to its size class.
//
// What the pass writes, and nothing else:
//
//   C x = e;            ->  C x = rt_own(e);        (e borrowed; `new` moves in)
//   x = e;              ->  rt_store(&x, e);        (rt_store_own when e is owned)
//   p.f = e;            ->  rt_store(p + F, e);     (the parser marked the store)
//   { ... }             ->  { ... rc_dec(x); }      (reverse declaration order)
//   break N; continue;  ->  { rc_dec(x); break N; } (every scope inside the loop)
//   return e;           ->  { T $t = e; rc_inc($t); rc_dec(x); return $t; }
//   f();                ->  rt_drop(f());           (a reference nobody took)
//
// WHY A PASS AND NOT THE PARSER. The scope half of this could be written where
// the block is parsed, as `mc/examples/lang` writes it (`lang_stmt.mc`
// lg_decs_from) -- but the OWNERSHIP half cannot. Whether `e` already carries a
// reference of its own is a question about `e`'s static TYPE, and in the ngen a
// member access on a receiver the parser cannot type is DEFERRED by design
// (teko_expr.mc's tk_defer_member): at parse time such a node is a placeholder
// with no type at all. Guessing there is a silent leak (an increment too many)
// or a silent use-after-free (one too few), and the alert the mc session raised
// is that scope must have ONE owner and never two. So both halves live here,
// after teko_typeof.mc has resolved every deferred access and every node has
// its type -- the pass being the single source, which is what that alert asked
// for. The parser's own table of locals is untouched and still does what it
// always did, resolve `.`.
//
// Two more things the lx gets for free and this pass has to earn. `loop` is a
// CORE keyword, and `word_add` refuses to let a module own one
// (mc/src/hooks.mc), so there is no `syntax_stmt("loop")` to push a mark from
// and `on_jump`'s `depth` counts BLOCKS, not loops -- here the N_LOOP node is
// in the tree, so `break N` reads the mark of the N-th loop out. And a
// destructor may reach a field the release is about to drop, so the destructors
// run before any field is released (teko_class.mc's tk_release_fn).
//
// A program that declares no class and no interface is not this pass's
// business: the root comes back untouched, so a `struct`-only source keeps the
// tree it had, byte for byte.

#define TK_MAXLOOP 32                 // loops open at one point of one function

i64 rc_lp[TK_MAXLOOP];                // the scope top at the head of each open loop
i64 tk_nlp = 0;
i64 tk_rc_floor = 0;                  // where the body's own locals start: past the parameters
i64 tk_rc_ret = 0 - 1;                // the declared return type of the function being walked

i64 rc_lp_at(i64 i)          { return ld64(rc_lp + i * 8); }
void set_rc_lp_at(i64 i, i64 v) { st64(rc_lp + i * 8, v); }

void tk_rc_stmt(i64 n);
void tk_rc_list(i64 n);

// nodes this pass builds report the position of the statement they replace
void tk_rc_at(i64 n) {
    tk_line = nd_line(n);
    tk_file = nd_file(n);
}

// 1 when the value already carries a reference of its own: `new C(...)`, or a
// call to anything declared to return a class or an interface -- every such
// function hands out a reference of the caller's own, which is the protocol the
// return lowering below keeps. A name, a field load and an element load are
// BORROWED: the object belongs to whoever the load read it from.
i64 tk_rc_own(i64 e) {
    if (e == 0) return 0;
    i64 x = tk_xt_at(e);
    if (x >= 0) {
        if (xt_pure_at(x)) return 0;
        return tk_is_counted(xt_ty_at(x));
    }
    if (nd_kind(e) != N_CALL) return 0;
    i64 d = decl_find(nd_name(e));
    if (d < 0) return 0;
    return tk_is_counted(decl_ret(d));
}

// where `name` sits in the scope stack, or -1: below the floor it is a
// parameter, which is borrowed and never released
i64 tk_rc_index(uptr name) {
    i64 i = tk_nscope - 1;
    loop {
        if (i < 0) break;
        if (str_eq(sc_name_at(i), name)) return i;
        i = i - 1;
    }
    return 0 - 1;
}

// the rc_dec of every counted local from `base` to the top of the scope, in
// REVERSE declaration order: an object built from one declared before it is
// released first, so the reference it holds is still valid while it runs
i64 tk_rc_releases(i64 base) {
    i64 head = 0;
    i64 i = tk_nscope - 1;
    loop {
        if (i < base) break;
        if (tk_is_counted(sc_ty_at(i)))
            head = list_append(head, tk_stmt(tk_call("rc_dec", tk_id(sc_name_at(i)))));
        i = i - 1;
    }
    return head;
}

// ---- the statements the pass rewrites ----

// `C x = e;`: an owning slot takes a reference. A value that already carries
// one moves in; a borrowed one is incremented, so the local and whoever it was
// read from each hold their own.
void tk_rc_var(i64 n) {
    if (nd_val(n) != 0) return;                  // `C tbl[4]`: references, not one object
    if (!tk_is_counted(nd_type(n))) return;
    i64 e = nd_a(n);
    if (e == 0) return;
    if (tk_rc_own(e)) return;
    tk_rc_at(n);
    set_nd_a(n, tk_call("rt_own", e));
}

// `x = e;` on a local of class type: one call keeps both counts straight, and
// the old object of the slot is released the moment it stops being reachable.
// A PARAMETER is borrowed -- the caller's reference was never counted on entry,
// so releasing it here would free an object the caller still holds.
void tk_rc_assign(i64 n) {
    i64 li = tk_rc_index(nd_name(n));
    if (li < 0) return;
    if (!tk_is_counted(sc_ty_at(li))) return;
    tk_rc_at(n);
    if (li < tk_rc_floor)
        err_at2(tk_file, tk_line, "teko: a parameter of class type is borrowed; it is not reassigned",
                nd_name(n));
    uptr fn = "rt_store";
    if (tk_rc_own(nd_a(n))) fn = "rt_store_own";
    i64 call = tk_call2(fn, tk_addr(nd_name(n)), nd_a(n));
    set_nd_kind(n, N_EXPRSTMT);
    set_nd_name(n, 0);
    set_nd_a(n, call);
}

// `return e;` becomes `{ T $t = e; [rc_inc($t);] releases...; return $t; }`.
// The temporary is what makes the releases safe: the value is computed before
// the locals go away, and a borrowed one is incremented first, so the caller
// receives a reference of its own whatever the expression was.
void tk_rc_return(i64 n) {
    i64 e = nd_a(n);
    i64 needinc = 0;
    if (e != 0 && tk_is_counted(tk_rc_ret)) needinc = !tk_rc_own(e);
    i64 rel = tk_rc_releases(tk_rc_floor);
    if (rel == 0 && !needinc) return;
    tk_rc_at(n);
    i64 st = 0;
    i64 rv = 0;
    if (e != 0) {
        uptr t = gensym_new();
        st = tk_var(tk_rc_ret, t, e);
        if (needinc) st = list_append(st, tk_stmt(tk_call("rc_inc", tk_id(t))));
        rv = tk_id(t);
    }
    st = list_append(st, rel);
    st = list_append(st, tk_ret(rv));
    set_nd_kind(n, N_BLOCK);
    set_nd_type(n, 0);
    set_nd_val(n, 0);
    set_nd_name(n, 0);
    set_nd_a(n, st);
}

// `break N` / `continue` leave every scope opened inside the loop they jump out
// of, and the block's own releases at the `}` are exactly what they skip. The
// mark of the target loop is the scope top at its head, which is why the loop
// stack is kept while the tree is walked.
void tk_rc_jump(i64 n) {
    if (tk_nlp == 0) return;
    i64 lvl = 1;
    if (nd_kind(n) == N_BREAK) lvl = nd_val(n);
    if (lvl < 1) lvl = 1;
    i64 idx = tk_nlp - lvl;
    if (idx < 0) return;
    i64 rel = tk_rc_releases(rc_lp_at(idx));
    if (rel == 0) return;
    tk_rc_at(n);
    i64 j = node_new(nd_kind(n), tk_line, tk_file);
    set_nd_val(j, nd_val(n));
    set_nd_kind(n, N_BLOCK);
    set_nd_val(n, 0);
    set_nd_a(n, list_append(rel, j));
}

// `f();` on its own line, where `f` hands out a reference of the caller's own:
// nobody took it, so it is released where it was produced instead of living for
// the rest of the run
void tk_rc_exprstmt(i64 n) {
    i64 e = nd_a(n);
    if (!tk_rc_own(e)) return;
    tk_rc_at(n);
    set_nd_a(n, tk_call("rt_drop", e));
}

// a store into a slot of counted type, recorded where the parser built it: the
// pass is what can tell an owned value from a borrowed one, so it is the pass
// that picks between the two stores
void tk_rc_store(i64 n) {
    if (!tk_os_has(n)) return;
    i64 v = nd_next(nd_a(n));                    // stW(address, value): the second argument
    if (tk_rc_own(v)) set_nd_name(n, "rt_store_own");
    else              set_nd_name(n, "rt_store");
}

// ---- the walk ----
// An expression is walked for the marked stores alone; a statement is walked
// for everything else. A statement the pass rewrote is NOT walked again: the
// block it became holds the very node that was just lowered.

void tk_rc_expr(i64 n) {
    loop {
        if (n == 0) break;
        tk_rc_store(n);
        tk_rc_expr(nd_a(n));
        tk_rc_expr(nd_b(n));
        tk_rc_expr(nd_c(n));
        tk_rc_expr(nd_d(n));
        n = nd_next(n);
    }
}

// `{ ... }`: the locals it declares die at its `}`, in reverse order, and the
// releases are appended to the list the block already holds
void tk_rc_block(i64 n) {
    i64 mark = tk_nscope;
    tk_rc_list(nd_a(n));
    tk_rc_at(n);
    i64 rel = tk_rc_releases(mark);
    tk_nscope = mark;
    if (rel == 0) return;
    set_nd_a(n, list_append(nd_a(n), rel));
}

void tk_rc_loop(i64 n) {
    if (tk_nlp == TK_MAXLOOP) err_at(tk_file, tk_line, "teko: loops nested too deep");
    set_rc_lp_at(tk_nlp, tk_nscope);
    tk_nlp = tk_nlp + 1;
    tk_rc_stmt(nd_a(n));
    tk_nlp = tk_nlp - 1;
}

void tk_rc_if(i64 n) {
    tk_rc_expr(nd_a(n));
    tk_rc_stmt(nd_b(n));
    tk_rc_stmt(nd_c(n));
}

void tk_rc_stmt(i64 n) {
    if (n == 0) return;
    i64 k = nd_kind(n);
    if (k == N_BLOCK)    { tk_rc_block(n);    return; }
    if (k == N_LOOP)     { tk_rc_loop(n);     return; }
    if (k == N_IF)       { tk_rc_if(n);       return; }
    if (k == N_RETURN)   { tk_rc_expr(nd_a(n)); tk_rc_return(n); return; }
    if (k == N_BREAK)    { tk_rc_jump(n);     return; }
    if (k == N_CONTINUE) { tk_rc_jump(n);     return; }
    if (k == N_VAR) {
        tk_rc_expr(nd_a(n));
        tk_rc_var(n);
        tk_ty_scope_var(n);                      // in scope only AFTER its initializer
        return;
    }
    if (k == N_ASSIGN)   { tk_rc_expr(nd_a(n)); tk_rc_assign(n); return; }
    if (k == N_EXPRSTMT) { tk_rc_expr(nd_a(n)); tk_rc_exprstmt(n); return; }
    tk_rc_expr(n);
}

void tk_rc_list(i64 n) {
    loop {
        if (n == 0) break;
        tk_rc_stmt(n);
        n = nd_next(n);
    }
}

// the parameters of the function being walked: borrowed, every one of them, so
// they are in scope for what reads them and below the floor the releases start
// at
void tk_rc_fn(i64 f) {
    tk_nscope = 0;
    tk_nlp = 0;
    tk_ty_scope_params(nd_a(f));
    tk_rc_floor = tk_nscope;
    tk_rc_ret = nd_type(f);
    tk_rc_stmt(nd_b(f));
    tk_nscope = 0;
}

// 1 when the program declares something the reclaim has anything to do with
i64 tk_rc_needed() {
    i64 i = 0;
    loop {
        if (i >= tk_nstruct) break;
        if (tk_is_class(i)) return 1;
        if (tk_is_iface(i)) return 1;
        i = i + 1;
    }
    return 0;
}

i64 tk_rc_pass(i64 root) {
    if (!tk_rc_needed()) return root;
    i64 f = root;
    loop {
        if (f == 0) break;
        if (nd_kind(f) == N_FUNC) tk_rc_fn(f);
        f = nd_next(f);
    }
    tk_rc_ret = 0 - 1;
    return root;
}
