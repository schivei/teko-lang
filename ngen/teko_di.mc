// teko_di.mc -- DI1 (D229, docs/design/plano-ngen-entrega4.md §58): the three
// service-lifetime markers a class names in its own `:` list, resolved in
// COMPTIME rather than by a runtime container, and `inject T` for a service
// that takes no dependency of its own.
//
//   interface IClock { i64 tick(); }
//   class Clock : IClock, IServiceSingleton { ... }   the marker, not a base
//   IClock a = inject IClock;                          the interface key
//   Clock b = inject Clock;                             the class's own key,
//                                                        the SAME instance
//
// A marker is a NAME, read by `tk_di_marker` where the `:` list already reads
// one (`tk_conf_name`, teko_class.mc; `tk_iface_base_name`, teko_iface.mc) --
// before any lookup in the type table, so a program with none of the three
// words anywhere never allocates a row, a global or a symbol for this file at
// all (§58 (h) risk 1): the loop below over an empty site table is the same
// no-op proof `tk_params_pass` gives for a unit with no `params`.
//
// Registration happens where the `:` list closes (`tk_conf_apply`, decision
// 2): the keys a service answers to are the interfaces its OWN class row
// already publishes (`ci_if_at`/`tk_nimpl`, §50 I1's flattened set) plus the
// class itself (decision 3). `inject T` always defers -- `tk_unresolved_new`'s
// own idiom -- to `tk_di_pass`, run once the whole unit is parsed and every
// service is registered, whatever order the source named them in (§50).
//
// What this crumb does NOT do (decision 19, §58 (g), later crumbs): the
// dependency graph a constructor's own parameters would walk (DI2), `scope {
// }` and the Scoped/Transient lifetimes it distinguishes from the root's
// (DI3), and namespaced or generic keys (DI4). A Scoped or Transient marker
// is still recognized and registered here, so a later crumb only teaches
// their OWN resolution -- `tk_di_resolve` below is where that grows.

#define TK_SVC_SINGLETON 0
#define TK_SVC_SCOPED    1
#define TK_SVC_TRANSIENT 2

#define TK_MAXSVC    32                // classes marked with a service lifetime
#define TK_MAXDISITE 32                // `inject` sites, awaiting tk_di_pass

i64  sv_cls[TK_MAXSVC];                // the class row carrying the marker
i64  sv_life[TK_MAXSVC];               // TK_SVC_SINGLETON/SCOPED/TRANSIENT
i64  sv_slot[TK_MAXSVC];               // 1 once the root slot/getter are emitted
uptr sv_getter[TK_MAXSVC];             // the getter's own symbol, once emitted
i64  tk_nsv = 0;

i64  ds_node[TK_MAXDISITE];            // the placeholder `inject` left in the tree
i64  ds_key[TK_MAXDISITE];             // the type row named at the site
i64  ds_line[TK_MAXDISITE];
uptr ds_file[TK_MAXDISITE];
i64  tk_nds = 0;

// scratch: the lifetime a class's `:` list is naming right now, consumed by
// `tk_di_conf_apply` the moment that class's row exists -- the same lifetime
// `tk_nconf`/`conf_if` (teko_iface.mc) already keeps for the same list
i64  tk_conf_life = 0 - 1;
i64  tk_conf_life_line = 0;
uptr tk_conf_life_file = 0;

i64  sv_cls_at(i64 i)    { return ld64(sv_cls + i * 8); }
i64  sv_life_at(i64 i)   { return ld64(sv_life + i * 8); }
i64  sv_slot_at(i64 i)   { return ld64(sv_slot + i * 8); }
uptr sv_getter_at(i64 i) { return ld64(sv_getter + i * 8); }

void set_sv_cls_at(i64 i, i64 v)    { st64(sv_cls + i * 8, v); }
void set_sv_life_at(i64 i, i64 v)   { st64(sv_life + i * 8, v); }
void set_sv_slot_at(i64 i, i64 v)   { st64(sv_slot + i * 8, v); }
void set_sv_getter_at(i64 i, uptr v) { st64(sv_getter + i * 8, v); }

i64  ds_node_at(i64 i)  { return ld64(ds_node + i * 8); }
i64  ds_key_at(i64 i)   { return ld64(ds_key + i * 8); }
i64  ds_line_at(i64 i)  { return ld64(ds_line + i * 8); }
uptr ds_file_at(i64 i)  { return ld64(ds_file + i * 8); }

void set_ds_node_at(i64 i, i64 v)  { st64(ds_node + i * 8, v); }
void set_ds_key_at(i64 i, i64 v)   { st64(ds_key + i * 8, v); }
void set_ds_line_at(i64 i, i64 v)  { st64(ds_line + i * 8, v); }
void set_ds_file_at(i64 i, uptr v) { st64(ds_file + i * 8, v); }

// the lifetime `nm` names, or -1 when it names none of the three: a plain
// string compare, never a `syntax()` word (decision 1) -- the type table
// never sees a row, a global or a symbol for a program with none of them
i64 tk_di_marker(uptr nm) {
    if (str_eq(nm, "IServiceSingleton")) return TK_SVC_SINGLETON;
    if (str_eq(nm, "IServiceScoped"))    return TK_SVC_SCOPED;
    if (str_eq(nm, "IServiceTransient")) return TK_SVC_TRANSIENT;
    return 0 - 1;
}

// the `:` list just read a marker (`tk_conf_name`): one lifetime per class,
// checked here rather than at `tk_di_conf_apply` because two markers in the
// SAME list are read before that class's row even exists
void tk_di_conf_mark(i64 life, i64 line, uptr fl) {
    if (tk_conf_life >= 0) err_at(fl, line, "teko: a class names two service lifetimes");
    tk_conf_life = life;
    tk_conf_life_line = line;
    tk_conf_life_file = fl;
}

i64 tk_di_sv_find(i64 ci) {
    i64 i = 0;
    loop {
        if (i >= tk_nsv) break;
        if (sv_cls_at(i) == ci) return i;
        i = i + 1;
    }
    return 0 - 1;
}

void tk_di_sv_add(i64 ci, i64 life, i64 line, uptr fl) {
    if (tk_nsv == TK_MAXSVC) err_at(fl, line, "teko: too many services");
    set_sv_cls_at(tk_nsv, ci);
    set_sv_life_at(tk_nsv, life);
    set_sv_slot_at(tk_nsv, 0);
    set_sv_getter_at(tk_nsv, 0);
    tk_nsv = tk_nsv + 1;
}

// consumes the scratch `tk_conf_name` filled, once `ci`'s row exists
// (`tk_conf_apply`, teko_class.mc): a no-op for the far more common case of a
// class naming no lifetime at all, which is what keeps a DI-free program's
// tree untouched (decision 1, §58 (h) risk 1)
void tk_di_conf_apply(i64 ci) {
    if (tk_conf_life < 0) return;
    i64 life = tk_conf_life;
    i64 line = tk_conf_life_line;
    uptr fl = tk_conf_life_file;
    tk_conf_life = 0 - 1;
    if (sr_abst_at(ci)) err_at2(fl, line, "teko: an abstract class is not a service", sr_name_at(ci));
    if (tk_di_sv_find(ci) >= 0) err_at(fl, line, "teko: a class names two service lifetimes");
    tk_di_sv_add(ci, life, line, fl);
}

void tk_di_defer(i64 n, i64 want, i64 line, uptr fl) {
    if (tk_nds == TK_MAXDISITE) err_at(fl, line, "teko: too many `inject` sites");
    set_ds_node_at(tk_nds, n);
    set_ds_key_at(tk_nds, want);
    set_ds_line_at(tk_nds, line);
    set_ds_file_at(tk_nds, fl);
    tk_nds = tk_nds + 1;
}

// `inject T` -- read exactly like `new` (decision 5), but never resolved here:
// the type only has to exist, forward-declared row accepted (`tk_struct_find_fwd`,
// teko_struct.mc's own identity-only lookup), and the real pick waits for
// `tk_di_pass`, when every service in the unit is registered regardless of
// where the source declared it (§50).
i64 tk_inject() {
    i64 line = p_line();
    uptr fl = p_file();
    p_next();                                     // the `inject` word
    uptr name = p_name();
    p_next();
    name = tk_ns_walk(name);
    i64 si = tk_struct_find_fwd(name);
    if (si < 0) err_at2(fl, line, "teko: unknown type after `inject`", name);
    if (!tk_is_class(si) && !tk_is_iface(si))
        err_at2(fl, line, "teko: this type is not a service", sr_name_at(si));
    tk_line = line;
    tk_file = fl;
    i64 n = tk_call("tk_unresolved_inject", 0);
    tk_di_defer(n, si, line, fl);
    tk_xt_add(n, si, 0);                          // owned until the pass knows the lifetime
    return n;
}

// 1 when the service at `sv` answers to the key `want`: its own class row, or
// one of the interfaces `ci_if_at`/`tk_nimpl` (teko_iface.mc) already flattens
// into that class's conformance set (§50 I1) -- decision 3's key set
i64 tk_di_sv_matches(i64 sv, i64 want) {
    i64 ci = sv_cls_at(sv);
    if (ci == want) return 1;
    i64 i = 0;
    loop {
        if (i >= tk_nimpl) break;
        if (ci_cls_at(i) == ci && ci_if_at(i) == want) return 1;
        i = i + 1;
    }
    return 0;
}

// the one service answering `want`, or a refusal naming none, one or both of
// the two candidates a duplicate key leaves ambiguous (decision 4: checked
// where `inject` asks, never at registration, because the source may name
// services in any order at all)
i64 tk_di_find_impl(i64 want, i64 line, uptr fl) {
    i64 found = 0 - 1;
    i64 second = 0 - 1;
    i64 i = 0;
    loop {
        if (i >= tk_nsv) break;
        if (tk_di_sv_matches(i, want)) {
            if (found >= 0) second = i;
            else found = i;
        }
        i = i + 1;
    }
    if (found < 0) err_at2(fl, line, "teko: no service implements this type", sr_name_at(want));
    if (second >= 0)
        err_at(fl, line, tk_join3("teko: two services implement this interface: ",
                                   sr_name_at(sv_cls_at(found)),
                                   tk_join(", ", sr_name_at(sv_cls_at(second)))));
    return found;
}

// a fresh `<cls>_new(...)` of the service's own class: the constructor that
// takes no argument, or every argument defaulted -- `new Name` with none of
// its own reaches the same zeroed object (decision 6, decision 7, the reclaim
// crumb's own `tk_new_pick`); a class whose ONLY constructors need an
// argument this crumb cannot supply is refused rather than silently zeroed.
i64 tk_di_new_call(i64 sv, i64 line, uptr fl) {
    i64 ci = sv_cls_at(sv);
    uptr name = sr_name_at(ci);
    if (tk_ctor_named(ci) && tk_ctor_pick(ci, 0) < 0)
        err_at2(fl, line, "teko: no constructor of this service takes only services", name);
    tk_close_open(ci);
    i64 args = 0;
    tk_line = line;
    tk_file = fl;
    uptr fn = tk_new_pick(ci, name, &args, 0, line, fl);
    return tk_call(fn, args);
}

// `uptr <cls>_di_get() { uptr p = ld64(slot); if (p == 0) { p = <cls>_new();
// st64(slot, p); } return p; }` -- emitted once per service, the moment the
// first `inject` needs it (decision 12/15's own memoized getter); `p` is
// `uptr`, so the RC pass, run last, never touches the slot itself.
uptr tk_di_getter_sym(i64 sv, i64 line, uptr fl) {
    if (sv_slot_at(sv)) return sv_getter_at(sv);
    uptr cname = sr_name_at(sv_cls_at(sv));
    uptr slotsym = tk_join(tk_case(cname, 0), "_di_slot");
    uptr gettersym = tk_join(tk_case(cname, 0), "_di_get");
    top_add(tk_glb(TY_U8, slotsym, 8));
    i64 pvar = tk_var(TY_UPTR, "p", tk_call("ld64", tk_id(slotsym)));
    i64 assignp = tk_nd(N_ASSIGN);
    set_nd_name(assignp, "p");
    set_nd_a(assignp, tk_di_new_call(sv, line, fl));
    i64 storeslot = tk_stmt(tk_call2("st64", tk_id(slotsym), tk_id("p")));
    i64 thenblk = tk_blk(list_append(assignp, storeslot));
    i64 ifstmt = tk_if(tk_bin(K_EQ, tk_id("p"), tk_int(0)), thenblk);
    i64 body = tk_blk(list_append(list_append(pvar, ifstmt), tk_ret(tk_id("p"))));
    top_add(tk_func(TY_UPTR, gettersym, 0, body));
    set_sv_slot_at(sv, 1);
    set_sv_getter_at(sv, gettersym);
    return gettersym;
}

// the call an `inject` site becomes: a Transient builds fresh every time
// (decision 12; DI3 gives it its own scope-of-birth), a Singleton or a
// Scoped resolved with no `scope { }` open answers through the root's own
// memoized getter (decision 10 -- the root is a scope too)
i64 tk_di_resolve(i64 sv, i64 line, uptr fl) {
    if (sv_life_at(sv) == TK_SVC_TRANSIENT) return tk_di_new_call(sv, line, fl);
    return tk_call(tk_di_getter_sym(sv, line, fl), 0);
}

// every deferred `inject`, once the whole unit is parsed and every service in
// it is registered (§50): the placeholder is rewritten in place, the node
// index and its parse-time type untouched (`tk_fwd_resolve_all_new`'s own
// idiom), and only now does `pure` rise to 1 for a borrowed answer -- a
// Singleton or a Scoped's own root instance (decision 16).
i64 tk_di_pass(i64 root) {
    i64 i = 0;
    loop {
        if (i >= tk_nds) break;
        i64 line = ds_line_at(i);
        uptr fl = ds_file_at(i);
        i64 sv = tk_di_find_impl(ds_key_at(i), line, fl);
        i64 r = tk_di_resolve(sv, line, fl);
        i64 n = ds_node_at(i);
        i64 keep = nd_next(n);
        node_assign(n, r);
        set_nd_next(n, keep);
        if (sv_life_at(sv) != TK_SVC_TRANSIENT) {
            i64 x = tk_xt_at(n);
            if (x >= 0) set_xt_pure_at(x, 1);
        }
        i = i + 1;
    }
    return root;
}
