// teko_struct.mc -- the type table teko's `struct` and `class` share, and the
// `struct` declaration itself: entrega 3's first two constructs (D214:
// primitivas -> TIPOS -> superfície), taught from outside `mc`'s `src/` with
// the registrations the mc guide names for a declaration a `#rule` cannot reach
// (docs/guide/30-teaching.md § step 3). The layout machinery is
// `mc/examples/api/oop.mc`'s; `struct` is the case with no vtable, so its
// fields start at offset 0 (`class` is teko_class.mc, and starts at 8).
//
// Form (D213: C-like, the mc core's own spelling -- teko-classic's
// `type X = struct {}` is NOT inherited by this port):
//
//   struct Point {                    #define POINT_X 0
//       i64 x;                        #define POINT_FLAG 8
//       u8  flag;                     #define POINT_SIZE 16
//   }                                 Point point_new()  -> rt_alloc(POINT_SIZE)
//                                     type_new("Point", 8, 8, TK_INT)
//
// A value of struct or class type is a POINTER to the allocation, eight bytes
// wide, the same reference shape `oop.mc` gives a class -- `type_new` rather
// than `type_alias(TY_UPTR)` because the id has to stay DISTINCT: `p.x` needs
// the static type of `p`, and the only place that survives the core's own parse
// of `Point p = new Point;` is the type id on the N_VAR node, which `tk_on_stmt`
// reads back. Width 8, `TK_INT`: a pointer is an integer the core's own
// operators already fit, so no derived machine and no intrinsic is needed.
//
// The runtime belongs to the PROGRAM, not to this file: `Name_new` calls
// `rt_alloc(n)` from `ngen/lib/rt.mc`, which hands out `n` zeroed bytes, so a
// field nobody assigned reads as 0 and a reference field reads as null.
//
// A struct body is read by the very machine a class body is read by
// (`tk_member`, teko_class.mc), so a struct declares METHODS -- with `self`,
// with default arguments and with overloads -- under one difference: no vtable,
// hence no `virtual`/`override` and no trait, both refused by name.

#define TK_MAXSTRUCT 32               // structs, classes and interfaces declared in one source
#define TK_MAXFIELD  256              // fields, summed across all of them
#define TK_MAXLOCAL  256              // locals of struct/class type seen so far
#define TK_MAXXT     256              // expressions whose type this module knows
#define TK_MAXAX     128              // array-field addresses this module handed out

// teko_generic.mc is included after this file, because a generic body is
// re-parsed by the very machine below; these two are what the declaration of a
// type has to ask it before there is a type at all
i64 tk_gen_find(uptr name);
void tk_gen_record(uptr name, i64 kind);

// what a row of the type table declares: the three share the table because a
// value of any of them is one 8-byte reference, and `.` has to tell them apart
#define TK_KSTRUCT 0
#define TK_KCLASS  1
#define TK_KIFACE  2

uptr sr_name[TK_MAXSTRUCT];
i64  sr_ty[TK_MAXSTRUCT];             // the id type_new returned for the name
i64  sr_first[TK_MAXSTRUCT];          // slice [first, first+count) of the field table
i64  sr_count[TK_MAXSTRUCT];
i64  sr_size[TK_MAXSTRUCT];
i64  sr_base[TK_MAXSTRUCT];           // the base class, or -1
i64  sr_form[TK_MAXSTRUCT];           // TK_KSTRUCT, TK_KCLASS or TK_KIFACE
i64  sr_v0[TK_MAXSTRUCT];             // slice [v0, v0+nv) of the virtual-slot table
i64  sr_nv[TK_MAXSTRUCT];
i64  sr_m0[TK_MAXSTRUCT];             // an interface's slice [m0, m0+mn) of the signature table
i64  sr_mn[TK_MAXSTRUCT];
i64  sr_i0[TK_MAXSTRUCT];             // a class's slice [i0, i0+ni) of the implemented-interface list
i64  sr_ni[TK_MAXSTRUCT];
i64  tk_nstruct = 0;

uptr fd_name[TK_MAXFIELD];
i64  fd_off[TK_MAXFIELD];
i64  fd_ty[TK_MAXFIELD];
i64  fd_nel[TK_MAXFIELD];             // elements of an INLINE array field, 0 for a scalar
i64  tk_nfield = 0;

i64  ax_node[TK_MAXAX];               // the address node of an array field, and its shape
i64  ax_ty[TK_MAXAX];
i64  ax_nel[TK_MAXAX];
uptr ax_name[TK_MAXAX];
i64  tk_nax = 0;

uptr lv_name[TK_MAXLOCAL];            // a local the CORE declared, seen via on_stmt
i64  lv_str[TK_MAXLOCAL];
i64  tk_nlocal = 0;

i64  xt_node[TK_MAXXT];               // a node this module built, and its type
i64  xt_str[TK_MAXXT];                // the row of the type table, or -1 for a scalar
i64  xt_ty[TK_MAXXT];                 // the type id itself, scalar ones included
i64  xt_pure[TK_MAXXT];               // 1 when re-evaluating the node is free of effects
i64  tk_nxt = 0;

// position used on generated nodes and on declaration-level errors
i64  tk_line = 0;
uptr tk_file = 0;

// ---- table accessors (no raw ld64/st64 outside this section) ----
uptr sr_name_at(i64 i)  { return ld64(sr_name + i * 8); }
i64  sr_ty_at(i64 i)    { return ld64(sr_ty + i * 8); }
i64  sr_first_at(i64 i) { return ld64(sr_first + i * 8); }
i64  sr_count_at(i64 i) { return ld64(sr_count + i * 8); }
i64  sr_size_at(i64 i)  { return ld64(sr_size + i * 8); }
i64  sr_base_at(i64 i)  { return ld64(sr_base + i * 8); }
i64  sr_kind_at(i64 i)  { return ld64(sr_form + i * 8); }
i64  sr_v0_at(i64 i)    { return ld64(sr_v0 + i * 8); }
i64  sr_nv_at(i64 i)    { return ld64(sr_nv + i * 8); }
i64  sr_m0_at(i64 i)    { return ld64(sr_m0 + i * 8); }
i64  sr_mn_at(i64 i)    { return ld64(sr_mn + i * 8); }
i64  sr_i0_at(i64 i)    { return ld64(sr_i0 + i * 8); }
i64  sr_ni_at(i64 i)    { return ld64(sr_ni + i * 8); }
uptr fd_name_at(i64 i)  { return ld64(fd_name + i * 8); }
i64  fd_off_at(i64 i)   { return ld64(fd_off + i * 8); }
i64  fd_ty_at(i64 i)    { return ld64(fd_ty + i * 8); }
i64  fd_nel_at(i64 i)   { return ld64(fd_nel + i * 8); }
i64  ax_node_at(i64 i)  { return ld64(ax_node + i * 8); }
i64  ax_ty_at(i64 i)    { return ld64(ax_ty + i * 8); }
i64  ax_nel_at(i64 i)   { return ld64(ax_nel + i * 8); }
uptr ax_name_at(i64 i)  { return ld64(ax_name + i * 8); }
uptr lv_name_at(i64 i)  { return ld64(lv_name + i * 8); }
i64  lv_str_at(i64 i)   { return ld64(lv_str + i * 8); }
i64  xt_node_at(i64 i)  { return ld64(xt_node + i * 8); }
i64  xt_str_at(i64 i)   { return ld64(xt_str + i * 8); }
i64  xt_ty_at(i64 i)    { return ld64(xt_ty + i * 8); }
i64  xt_pure_at(i64 i)  { return ld64(xt_pure + i * 8); }

void set_sr_name_at(i64 i, uptr v)  { st64(sr_name + i * 8, v); }
void set_sr_ty_at(i64 i, i64 v)     { st64(sr_ty + i * 8, v); }
void set_sr_first_at(i64 i, i64 v)  { st64(sr_first + i * 8, v); }
void set_sr_count_at(i64 i, i64 v)  { st64(sr_count + i * 8, v); }
void set_sr_size_at(i64 i, i64 v)   { st64(sr_size + i * 8, v); }
void set_sr_base_at(i64 i, i64 v)   { st64(sr_base + i * 8, v); }
void set_sr_kind_at(i64 i, i64 v)   { st64(sr_form + i * 8, v); }
void set_sr_v0_at(i64 i, i64 v)     { st64(sr_v0 + i * 8, v); }
void set_sr_nv_at(i64 i, i64 v)     { st64(sr_nv + i * 8, v); }
void set_sr_m0_at(i64 i, i64 v)     { st64(sr_m0 + i * 8, v); }
void set_sr_mn_at(i64 i, i64 v)     { st64(sr_mn + i * 8, v); }
void set_sr_i0_at(i64 i, i64 v)     { st64(sr_i0 + i * 8, v); }
void set_sr_ni_at(i64 i, i64 v)     { st64(sr_ni + i * 8, v); }

i64 tk_is_class(i64 si) { return sr_kind_at(si) == TK_KCLASS; }
i64 tk_is_iface(i64 si) { return sr_kind_at(si) == TK_KIFACE; }
void set_fd_name_at(i64 i, uptr v)  { st64(fd_name + i * 8, v); }
void set_fd_off_at(i64 i, i64 v)    { st64(fd_off + i * 8, v); }
void set_fd_ty_at(i64 i, i64 v)     { st64(fd_ty + i * 8, v); }
void set_fd_nel_at(i64 i, i64 v)    { st64(fd_nel + i * 8, v); }
void set_ax_node_at(i64 i, i64 v)   { st64(ax_node + i * 8, v); }
void set_ax_ty_at(i64 i, i64 v)     { st64(ax_ty + i * 8, v); }
void set_ax_nel_at(i64 i, i64 v)    { st64(ax_nel + i * 8, v); }
void set_ax_name_at(i64 i, uptr v)  { st64(ax_name + i * 8, v); }
void set_lv_name_at(i64 i, uptr v)  { st64(lv_name + i * 8, v); }
void set_lv_str_at(i64 i, i64 v)    { st64(lv_str + i * 8, v); }
void set_xt_node_at(i64 i, i64 v)   { st64(xt_node + i * 8, v); }
void set_xt_str_at(i64 i, i64 v)    { st64(xt_str + i * 8, v); }
void set_xt_ty_at(i64 i, i64 v)     { st64(xt_ty + i * 8, v); }
void set_xt_pure_at(i64 i, i64 v)   { st64(xt_pure + i * 8, v); }

// ---- derived names ----
i64 tk_lower_ch(i64 c) { if (c >= 'A' && c <= 'Z') return c + 32; return c; }
i64 tk_upper_ch(i64 c) { if (c >= 'a' && c <= 'z') return c - 32; return c; }

// copy of `s` with the letters' case forced: up = 1 uppercase, 0 lowercase
uptr tk_case(uptr s, i64 up) {
    i64 n = cstrlen(s);
    uptr d = xalloc(n + 1);
    i64 i = 0;
    loop {
        if (i >= n) break;
        i64 c = ld8(s + i);
        if (up) st8(d + i, tk_upper_ch(c));
        else    st8(d + i, tk_lower_ch(c));
        i = i + 1;
    }
    st8(d + n, 0);
    return d;
}

uptr tk_join(uptr a, uptr b) {
    i64 la = cstrlen(a);
    i64 lb = cstrlen(b);
    uptr d = xalloc(la + lb + 1);
    mem_copy(d, a, la);
    mem_copy(d + la, b, lb);
    st8(d + la + lb, 0);
    return d;
}

uptr tk_join3(uptr a, uptr b, uptr c) { return tk_join(tk_join(a, b), c); }

// an integer as the decimal text a name and a message are built from: the
// const argument of a generic travels as a LEXEME, so `Box__Circle__4` and the
// substitution that produced it come from one and the same spelling
uptr tk_num(i64 v) {
    u8 tmp[24];
    i64 neg = 0;
    if (v < 0) { neg = 1; v = 0 - v; }
    i64 i = 24;
    loop {
        i = i - 1;
        st8(tmp + i, '0' + v % 10);
        v = v / 10;
        if (v == 0) break;
    }
    if (neg) { i = i - 1; st8(tmp + i, '-'); }
    return xstrdup(tmp + i, 24 - i);
}

// Point         ->  point_new       (the generated constructor)
uptr tk_ctor_name(uptr name) { return tk_join(tk_case(name, 0), "_new"); }
// Point + area  ->  point_area      (a method, with an implicit `self`)
uptr tk_fname(uptr name, uptr m) { return tk_join3(tk_case(name, 0), "_", m); }
// Point + x     ->  POINT_X         (the #define of the field's offset)
uptr tk_cname(uptr name, uptr m) { return tk_join3(tk_case(name, 1), "_", tk_case(m, 1)); }

// ---- node constructors: only node_new/set_nd_* from src/ast.mc ----
i64 tk_nd(i64 kind) { return node_new(kind, tk_line, tk_file); }

i64 tk_int(i64 v) {
    i64 n = tk_nd(N_INT);
    set_nd_val(n, v);
    set_nd_type(n, TY_I64);
    return n;
}

i64 tk_id(uptr name) {
    i64 n = tk_nd(N_IDENT);
    set_nd_name(n, name);
    set_nd_type(n, TY_I64);
    return n;
}

// &name of a function: M10's uptr
i64 tk_addr(uptr name) {
    i64 n = tk_nd(N_ADDR);
    set_nd_name(n, name);
    set_nd_type(n, TY_UPTR);
    return n;
}

i64 tk_bin(i64 op, i64 a, i64 b) {
    i64 n = tk_nd(N_BINARY);
    set_nd_op(n, op);
    set_nd_a(n, a);
    set_nd_b(n, b);
    return n;
}

i64 tk_call(uptr name, i64 args) {
    i64 n = tk_nd(N_CALL);
    set_nd_name(n, name);
    set_nd_a(n, args);
    set_nd_type(n, TY_I64);
    return n;
}

i64 tk_call2(uptr name, i64 a, i64 b) { return tk_call(name, list_append(a, b)); }

i64 tk_ret(i64 e) {
    i64 n = tk_nd(N_RETURN);
    set_nd_a(n, e);
    return n;
}

i64 tk_stmt(i64 e) {
    i64 n = tk_nd(N_EXPRSTMT);
    set_nd_a(n, e);
    return n;
}

i64 tk_blk(i64 stmts) {
    i64 n = tk_nd(N_BLOCK);
    set_nd_a(n, stmts);
    return n;
}

i64 tk_var(i64 ty, uptr name, i64 init) {
    i64 n = tk_nd(N_VAR);
    set_nd_name(n, name);
    set_nd_type(n, ty);
    set_nd_a(n, init);
    return n;
}

i64 tk_func(i64 ty, uptr name, i64 params, i64 body) {
    i64 f = tk_nd(N_FUNC);
    set_nd_name(f, name);
    set_nd_type(f, ty);
    set_nd_a(f, params);
    set_nd_b(f, body);
    return f;
}

i64 tk_str(uptr s) {
    i64 n = tk_nd(N_STR);
    set_nd_name(n, s);
    set_nd_val(n, cstrlen(s));
    set_nd_type(n, TY_UPTR);
    return n;
}

i64 tk_if(i64 cond, i64 then) {
    i64 n = tk_nd(N_IF);
    set_nd_a(n, cond);
    set_nd_b(n, then);
    return n;
}

// a global with no initializer: it goes to __bss, already zeroed
i64 tk_glb(i64 ty, uptr name, i64 nel) {
    i64 n = tk_nd(N_GLOBAL);
    set_nd_name(n, name);
    set_nd_type(n, ty);
    set_nd_val(n, nel);
    return n;
}

// ---- copying an expression the parser handed over ----
// A virtual call reads the receiver twice (its vtable, and `self`), and a node
// may sit in only ONE sibling list -- list_append would rewire the copy inside
// the vtable load. tk_clone is the way out, and tk_pure below is what keeps it
// honest: only a receiver whose re-evaluation costs nothing is ever cloned.
i64 tk_clone(i64 n);

i64 tk_clone_list(i64 n) {
    if (n == 0) return 0;
    i64 c = tk_clone(n);
    set_nd_next(c, tk_clone_list(nd_next(n)));
    return c;
}

i64 tk_clone(i64 n) {
    if (n == 0) return 0;
    i64 c = node_new(nd_kind(n), nd_line(n), nd_file(n));
    set_nd_op(c, nd_op(n));
    set_nd_type(c, nd_type(n));
    set_nd_val(c, nd_val(n));
    set_nd_name(c, nd_name(n));
    set_nd_a(c, tk_clone_list(nd_a(n)));
    set_nd_b(c, tk_clone_list(nd_b(n)));
    set_nd_c(c, tk_clone_list(nd_c(n)));
    set_nd_d(c, tk_clone_list(nd_d(n)));
    return c;
}

// memory-access intrinsic matching the field type's width
uptr tk_ldn(i64 ty) {
    i64 w = type_width(ty);
    if (w == 1) return "ld8";
    if (w == 2) return "ld16";
    if (w == 4) return "ld32";
    return "ld64";
}

uptr tk_stn(i64 ty) {
    i64 w = type_width(ty);
    if (w == 1) return "st8";
    if (w == 2) return "st16";
    if (w == 4) return "st32";
    return "st64";
}

// ---- lookups, linear and in declaration order (mc docs/determinism.md rule 1) ----
i64 tk_struct_find(uptr name) {
    i64 i = 0;
    loop {
        if (i >= tk_nstruct) break;
        if (str_eq(sr_name_at(i), name)) return i;
        i = i + 1;
    }
    return 0 - 1;
}

i64 tk_struct_by_ty(i64 ty) {
    i64 i = 0;
    loop {
        if (i >= tk_nstruct) break;
        if (sr_ty_at(i) == ty) return i;
        i = i + 1;
    }
    return 0 - 1;
}

// index into the field table of `name` in `si` or in one of its bases -- the
// base's fields keep their offsets in the derived object (base-first layout),
// so the entry found up the chain is valid as it stands
i64 tk_field_find(i64 si, uptr name) {
    loop {
        if (si < 0) break;
        i64 first = sr_first_at(si);
        i64 i = 0;
        loop {
            if (i >= sr_count_at(si)) break;
            if (str_eq(fd_name_at(first + i), name)) return first + i;
            i = i + 1;
        }
        si = sr_base_at(si);
    }
    return 0 - 1;
}

// the field called `name` when the left side's type is NOT known statically --
// a parameter, or a global, neither of which the core reports to a module. The
// answer is the field itself when exactly ONE type declares that name, which is
// the same "resolve the name in the module's own table" `mc`'s own teaching
// demo does for `p ~> len` (lib/user_syntax_demo.mc). -1 = no type declares it,
// -2 = more than one does and the left side has to be statically known.
i64 tk_field_by_name(uptr name) {
    i64 found = 0 - 1;
    i64 i = 0;
    loop {
        if (i >= tk_nfield) break;
        if (str_eq(fd_name_at(i), name)) {
            if (found >= 0) return 0 - 2;
            found = i;
        }
        i = i + 1;
    }
    return found;
}

// the table is a STACK, marked at every `{` and cut back at the matching `}`
// (teko_stmt.mc's tk_block), so what it holds is exactly the names in scope
// where the parser stands; the newest entry wins, which is what makes an inner
// declaration shadow an outer one for as long as its block is open
i64 tk_local_find(uptr name) {
    i64 i = tk_nlocal - 1;
    loop {
        if (i < 0) break;
        if (str_eq(lv_name_at(i), name)) return lv_str_at(i);
        i = i - 1;
    }
    return 0 - 1;
}

i64 tk_xt_at(i64 n) {
    i64 i = tk_nxt - 1;
    loop {
        if (i < 0) break;
        if (xt_node_at(i) == n) return i;
        i = i - 1;
    }
    return 0 - 1;
}

i64 tk_xt_find(i64 n) {
    i64 x = tk_xt_at(n);
    if (x < 0) return 0 - 1;
    return xt_str_at(x);
}

// the node's own type id, which a SCALAR one has as well: a field of `i64` is
// no row of the type table, and answering -1 for it is what left `self.side`
// untypable to whoever asks (teko_typeof.mc's tk_ty_of, and through it the
// overload resolution)
i64 tk_xt_ty(i64 n) {
    i64 x = tk_xt_at(n);
    if (x < 0) return 0 - 1;
    return xt_ty_at(x);
}

// 1 when the node may be evaluated twice with no observable difference: a name,
// or a field load this module built. A constructor and a method call answer 0,
// so `new C().m()` is refused instead of allocating twice.
i64 tk_pure(i64 n) {
    if (n == 0) return 0;
    if (nd_kind(n) == N_IDENT) return 1;
    i64 x = tk_xt_at(n);
    if (x < 0) return 0;
    return xt_pure_at(x);
}

// ---- table appends ----
void tk_local_add(uptr name, i64 si) {
    if (tk_nlocal == TK_MAXLOCAL) err_at(tk_file, tk_line, "teko: too many locals of struct type");
    set_lv_name_at(tk_nlocal, name);
    set_lv_str_at(tk_nlocal, si);
    tk_nlocal = tk_nlocal + 1;
}

// the node's type as this module knows it: the row of the type table when the
// type has one, and the type id in every case. `si` is -1 for a scalar, which
// is the answer `.` and the oracle both need to tell "not a struct" from
// "nothing known".
void tk_xt_put(i64 n, i64 si, i64 ty, i64 pure) {
    if (tk_nxt == TK_MAXXT) err_at(tk_file, tk_line, "teko: too many expressions whose type is known");
    set_xt_node_at(tk_nxt, n);
    set_xt_str_at(tk_nxt, si);
    set_xt_ty_at(tk_nxt, ty);
    set_xt_pure_at(tk_nxt, pure);
    tk_nxt = tk_nxt + 1;
}

void tk_xt_add(i64 n, i64 si, i64 pure) { tk_xt_put(n, si, sr_ty_at(si), pure); }

// appends the field and extends the owner's slice, so a method parsed further
// down the same body already sees the fields declared above it
void tk_field_add(i64 si, uptr name, i64 off, i64 ty, i64 nel) {
    if (tk_nfield == TK_MAXFIELD) err_at(tk_file, tk_line, "teko: too many fields");
    set_fd_name_at(tk_nfield, name);
    set_fd_off_at(tk_nfield, off);
    set_fd_ty_at(tk_nfield, ty);
    set_fd_nel_at(tk_nfield, nel);
    tk_nfield = tk_nfield + 1;
    set_sr_count_at(si, tk_nfield - sr_first_at(si));
}

// the row is appended BEFORE the body is read, so a field or a method body may
// name the type being declared
i64 tk_type_add(uptr name, i64 ty, i64 base, i64 kind) {
    if (tk_nstruct == TK_MAXSTRUCT) err_at(tk_file, tk_line, "teko: too many type declarations");
    set_sr_name_at(tk_nstruct, name);
    set_sr_ty_at(tk_nstruct, ty);
    set_sr_first_at(tk_nstruct, tk_nfield);
    set_sr_count_at(tk_nstruct, 0);
    set_sr_size_at(tk_nstruct, 0);
    set_sr_base_at(tk_nstruct, base);
    set_sr_kind_at(tk_nstruct, kind);
    set_sr_v0_at(tk_nstruct, 0);
    set_sr_nv_at(tk_nstruct, 0);
    set_sr_m0_at(tk_nstruct, 0);
    set_sr_mn_at(tk_nstruct, 0);
    set_sr_i0_at(tk_nstruct, 0);
    set_sr_ni_at(tk_nstruct, 0);
    tk_nstruct = tk_nstruct + 1;
    return tk_nstruct - 1;
}

// ---- the static type of an already parsed expression ----
// Three sources, in the order that keeps the precise answer ahead of the
// heuristic one: a node this module built (a field of struct type, so `a.b.c`
// chains), a local the core declared (its N_VAR carried the type's own id,
// which is what type_new buys over type_alias), and a call, whose declared
// return type the core publishes through decl_find/decl_ret -- `new Point`
// lowers to `point_new()`, declared to return `Point`.
i64 tk_struct_of_expr(i64 n) {
    if (n == 0) return 0 - 1;
    i64 x = tk_xt_find(n);
    if (x >= 0) return x;
    if (nd_kind(n) == N_IDENT) return tk_local_find(nd_name(n));
    if (nd_kind(n) == N_CALL) {
        i64 d = decl_find(nd_name(n));
        if (d < 0) return 0 - 1;
        return tk_struct_by_ty(decl_ret(d));
    }
    return 0 - 1;
}

// M21.5's statement hook: a local the CORE declared (`Point p = new Point;`)
// is observable here as the N_VAR node, and its type id is the struct or class.
// Nothing is rewritten -- the node that arrives is the node that leaves.
// `nd_val` is the array length, so `Point tbl[4];` (four pointers, not one
// object) is skipped.
i64 tk_on_stmt(i64 n) {
    if (n == 0) return n;
    if (nd_kind(n) != N_VAR) return n;
    if (nd_val(n) != 0) return n;
    i64 si = tk_struct_by_ty(nd_type(n));
    if (si >= 0) {
        tk_line = nd_line(n);
        tk_file = nd_file(n);
        tk_local_add(nd_name(n), si);
    }
    return n;
}

// ---- the constructor ----
// Name Name_new() { uptr p = rt_alloc(NAME_SIZE); <install> return p; }
// The declared return type is the type's own id, which is what makes
// `Point p = new Point;` and a call in `.` position resolve to it. `install` is
// empty for a struct and the vtable store for a class (teko_class.mc).
i64 tk_ctor(uptr name, i64 ty, i64 size, i64 install) {
    i64 stmts = tk_var(TY_UPTR, "p", tk_call("rt_alloc", tk_int(size)));
    if (install) stmts = list_append(stmts, install);
    stmts = list_append(stmts, tk_ret(tk_id("p")));
    return tk_func(ty, tk_ctor_name(name), 0, tk_blk(stmts));
}

// a name being declared: an identifier that is still free. `type_new` reserves
// the word, so a second `struct Point` (or `struct str`) arrives here as a
// keyword rather than as T_IDENT -- without this guard the error would come out
// as an unexplained "name expected".
uptr tk_newname(uptr what) {
    if (tk_gen_find(p_name()) >= 0)
        err_at2(p_file(), p_line(), "teko: the name is already a generic", p_name());
    if (p_id() == T_IDENT) return p_ident();
    if (alias_find(p_id()) >= 0)
        err_at2(p_file(), p_line(), "teko: the name is already a type", p_name());
    err_at2(p_file(), p_line(), tk_join3("teko: name of ", what, " expected"), p_name());
    return 0;
}

// `[4]` after a field name: an INLINE array of that many elements, whose size
// the instantiation of a generic fixes (`T items[N]`, N substituted by
// p_subst_int). 0 when the field is a scalar.
i64 tk_field_dim() {
    if (!p_accept(K_LBRACK)) return 0;
    if (p_id() != T_INT)
        err_at(p_file(), p_line(), "teko: an array field size is an integer literal");
    i64 n = p_val();
    if (n < 1) err_at(p_file(), p_line(), "teko: an array field size is positive");
    p_next();
    p_expect(K_RBRACK, "expected ] after the array field size");
    return n;
}

// places `m`, of type `fty`, at or after `off` and returns the next offset:
// the natural alignment of the field's own width, the `#define` of the offset,
// and the row in the field table. `nel` elements when the field is an array,
// laid out inline, so the object grows with the constant the generic was
// instantiated with.
i64 tk_field_place(i64 si, uptr tname, uptr m, i64 fty, i64 nel, i64 off) {
    if (fty == TY_VOID) err_at2(tk_file, tk_line, "teko: field of type void", m);
    if (tk_field_find(si, m) >= 0) err_at2(tk_file, tk_line, "teko: duplicate field", m);
    i64 w = type_width(fty);
    off = (off + w - 1) & ~(w - 1);
    def_add(tk_cname(tname, m), off, tk_line, tk_file);
    tk_field_add(si, m, off, fty, nel);
    if (nel > 0) return off + w * nel;
    return off + w;
}

// ---- an array field, and the `[` that is the only way to reach one ----
void tk_ax_add(i64 n, i64 ty, i64 nel, uptr name) {
    if (tk_nax == TK_MAXAX) err_at(tk_file, tk_line, "teko: too many array-field accesses");
    set_ax_node_at(tk_nax, n);
    set_ax_ty_at(tk_nax, ty);
    set_ax_nel_at(tk_nax, nel);
    set_ax_name_at(tk_nax, name);
    tk_nax = tk_nax + 1;
}

i64 tk_ax_find(i64 n) {
    i64 i = tk_nax - 1;
    loop {
        if (i < 0) break;
        if (ax_node_at(i) == n) return i;
        i = i - 1;
    }
    return 0 - 1;
}

// `p.items` is the ADDRESS of the array, tagged with the element type and the
// length so that `[` can bound-check it. The array itself is not a value: there
// is nothing for `p.items` alone, or for `p.items = e`, to be.
i64 tk_array_of(i64 addr, i64 fi, i64 line, uptr fl) {
    if (p_id() == K_ASSIGN)
        err_at2(fl, line, "teko: an array field is assigned one element at a time", fd_name_at(fi));
    if (p_id() != K_LBRACK)
        err_at2(fl, line, "teko: an array field is read one element at a time", fd_name_at(fi));
    tk_ax_add(addr, fd_ty_at(fi), fd_nel_at(fi), fd_name_at(fi));
    return addr;
}

uptr tk_ax_msg(i64 k, uptr what, uptr name, i64 nel) {
    return tk_join3(tk_join3("teko: index ", tk_num(k), what),
                    name, tk_join3("[", tk_num(nel), "]"));
}

// i64 tk_ix(i64 i, i64 n) { if (i < 0) rt_panic(...); if (i >= n) rt_panic(...);
// return i; } -- emitted ONCE, into the program, and only when a program really
// indexes an array field with something other than a literal. A program that
// does not is left with exactly the tree it had, which is what the no-op proof
// of a taught construct means.
i64 tk_ix_emitted = 0;

void tk_ix_emit() {
    if (tk_ix_emitted) return;
    tk_ix_emitted = 1;
    i64 low = tk_if(tk_bin(K_LT, tk_id("i"), tk_int(0)),
                    tk_stmt(tk_call("rt_panic", tk_str("index below zero into an array field"))));
    i64 high = tk_if(tk_bin(K_GE, tk_id("i"), tk_id("n")),
                     tk_stmt(tk_call("rt_panic", tk_str("index past the end of an array field"))));
    i64 body = tk_blk(list_append(list_append(low, high), tk_ret(tk_id("i"))));
    i64 params = list_append(param_new(TY_I64, "i"), param_new(TY_I64, "n"));
    top_add(tk_func(TY_I64, "tk_ix", params, body));
}

// A LITERAL index is decided here, against the constant the instantiation
// fixed -- which is the whole point of a generic taking a `const N`. Anything
// else is checked at run time by tk_ix, so no index ever reads past the object
// in silence.
i64 tk_ax_index(i64 idx, i64 nel, uptr name, i64 line, uptr fl) {
    if (nd_kind(idx) != N_INT) {
        tk_ix_emit();
        return tk_call2("tk_ix", idx, tk_int(nel));
    }
    i64 k = nd_val(idx);
    if (k < 0) err_at(fl, line, tk_ax_msg(k, " is before the start of ", name, nel));
    if (k >= nel) err_at(fl, line, tk_ax_msg(k, " is past the end of ", name, nel));
    return idx;
}

// `p.items[i]` and `p.items[i] = e`: the load or the store of the element's own
// width. `=` is read here for the same reason tk_field_use reads it -- it is not
// in the core's infix table, so the Pratt loop has already stopped.
i64 tk_array_index(i64 addr, i64 x) {
    i64 line = p_line();
    uptr fl = p_file();
    i64 ety = ax_ty_at(x);
    i64 idx = parse_expr(0);
    p_expect(K_RBRACK, "expected ] after the index");
    tk_line = line;
    tk_file = fl;
    i64 k = tk_ax_index(idx, ax_nel_at(x), ax_name_at(x), line, fl);
    i64 at = tk_bin(K_ADD, addr, tk_bin(K_MUL, k, tk_int(type_width(ety))));
    if (p_accept(K_ASSIGN)) {
        i64 v = parse_expr(0);
        tk_line = line;
        tk_file = fl;
        return tk_call2(tk_stn(ety), at, v);
    }
    i64 r = tk_call(tk_ldn(ety), at);
    i64 es = tk_struct_by_ty(ety);
    if (es >= 0) tk_xt_add(r, es, 1);
    return r;
}

// the whole object is aligned to 8, and exists even with no field at all
i64 tk_size_of(i64 off) {
    off = (off + 7) & ~7;
    if (off == 0) return 8;
    return off;
}

// ---- struct Name { type field; ... } ----
void tk_struct() {
    tk_line = p_line();
    tk_file = p_file();
    i64 head_line = tk_line;                     // position of the `struct` word
    uptr head_file = tk_file;
    p_next();                                    // the `struct` word
    uptr name = tk_newname("struct");
    if (p_id() == K_LT) {                        // struct Name<T, const N: i64>
        tk_gen_record(name, TK_KSTRUCT);         // recorded, not declared
        return;
    }
    i64 ty = type_new(name, 8, 8, TK_INT);       // a field of its own type parses
    i64 si = tk_type_add(name, ty, 0 - 1, TK_KSTRUCT);   // no base, no vtable: offset 0
    tk_use_reset();
    p_expect(K_LBRACE, "expected { in the struct body");
    i64 off = 0;
    loop {
        if (p_id() == K_RBRACE) break;
        if (p_id() == T_EOF) err_at(head_file, head_line, "unterminated struct");
        tk_line = p_line();                      // errors and nodes for this member
        tk_file = p_file();
        off = tk_member(si, name, off, 0 - 1);   // fields and methods, the class's own machine
    }
    p_next();                                    // }
    p_accept(K_SEMI);                            // a C programmer's trailing ;
    tk_line = head_line;                         // back to the struct's own level
    tk_file = head_file;
    i64 size = tk_size_of(off);
    set_sr_size_at(si, size);
    def_add(tk_join(tk_case(name, 1), "_SIZE"), size, tk_line, tk_file);
    top_add(tk_ctor(name, ty, size, 0));
}
