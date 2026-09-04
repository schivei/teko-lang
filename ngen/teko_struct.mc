// teko_struct.mc -- `struct Name { type field; ... }`, the first construct of
// entrega 3 (D214: primitivas -> TIPOS -> superfície). Taught from outside
// `mc`'s `src/`, with the same three registrations the mc guide names for a
// declaration that a `#rule` cannot reach (docs/guide/30-teaching.md § step 3):
// `syntax("struct")` for the declaration, `type_new` for the word, `top_add` /
// `def_add` for what it generates. The layout machinery is
// `mc/examples/api/oop.mc`'s, minus the vtable: a struct has no interface, so
// its fields start at offset 0.
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
// A value of struct type is a POINTER to the allocation, eight bytes wide, the
// same reference shape `oop.mc` gives a class -- `type_new` rather than
// `type_alias(TY_UPTR)` because the id has to stay DISTINCT: `p.x` needs the
// static struct of `p`, and the only place that survives the core's own parse
// of `Point p = new Point;` is the type id on the N_VAR node (teko_expr.mc
// reads it back through `tk_struct_of_expr`). Width 8, `TK_INT`: a pointer is
// an integer the core's own operators already fit, so no derived machine and no
// intrinsic is needed for it.
//
// The runtime belongs to the PROGRAM, not to this file: `Name_new` calls
// `rt_alloc(n)` from `ngen/lib/rt.mc`, which hands out `n` zeroed bytes, so a
// field nobody assigned reads as 0 and a struct-typed field reads as a null
// pointer.

#define TK_MAXSTRUCT 32               // structs declared in one source
#define TK_MAXFIELD  256              // fields, summed across all of them
#define TK_MAXLOCAL  256              // struct-typed locals seen so far
#define TK_MAXXT     256              // expressions whose struct this file knows

uptr sr_name[TK_MAXSTRUCT];
i64  sr_ty[TK_MAXSTRUCT];             // the id type_new returned for the name
i64  sr_first[TK_MAXSTRUCT];          // slice [first, first+count) of the field table
i64  sr_count[TK_MAXSTRUCT];
i64  sr_size[TK_MAXSTRUCT];
i64  tk_nstruct = 0;

uptr fd_name[TK_MAXFIELD];
i64  fd_off[TK_MAXFIELD];
i64  fd_ty[TK_MAXFIELD];
i64  tk_nfield = 0;

uptr lv_name[TK_MAXLOCAL];            // a local the CORE declared, seen via on_stmt
i64  lv_str[TK_MAXLOCAL];
i64  tk_nlocal = 0;

i64  xt_node[TK_MAXXT];               // a node this module built, and its struct
i64  xt_str[TK_MAXXT];
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
uptr fd_name_at(i64 i)  { return ld64(fd_name + i * 8); }
i64  fd_off_at(i64 i)   { return ld64(fd_off + i * 8); }
i64  fd_ty_at(i64 i)    { return ld64(fd_ty + i * 8); }
uptr lv_name_at(i64 i)  { return ld64(lv_name + i * 8); }
i64  lv_str_at(i64 i)   { return ld64(lv_str + i * 8); }
i64  xt_node_at(i64 i)  { return ld64(xt_node + i * 8); }
i64  xt_str_at(i64 i)   { return ld64(xt_str + i * 8); }

void set_sr_name_at(i64 i, uptr v)  { st64(sr_name + i * 8, v); }
void set_sr_ty_at(i64 i, i64 v)     { st64(sr_ty + i * 8, v); }
void set_sr_first_at(i64 i, i64 v)  { st64(sr_first + i * 8, v); }
void set_sr_count_at(i64 i, i64 v)  { st64(sr_count + i * 8, v); }
void set_sr_size_at(i64 i, i64 v)   { st64(sr_size + i * 8, v); }
void set_fd_name_at(i64 i, uptr v)  { st64(fd_name + i * 8, v); }
void set_fd_off_at(i64 i, i64 v)    { st64(fd_off + i * 8, v); }
void set_fd_ty_at(i64 i, i64 v)     { st64(fd_ty + i * 8, v); }
void set_lv_name_at(i64 i, uptr v)  { st64(lv_name + i * 8, v); }
void set_lv_str_at(i64 i, i64 v)    { st64(lv_str + i * 8, v); }
void set_xt_node_at(i64 i, i64 v)   { st64(xt_node + i * 8, v); }
void set_xt_str_at(i64 i, i64 v)    { st64(xt_str + i * 8, v); }

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

// Point         ->  point_new       (the generated constructor)
uptr tk_ctor_name(uptr name) { return tk_join(tk_case(name, 0), "_new"); }
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

// index into the field table of `name` in struct `si`, or -1
i64 tk_field_find(i64 si, uptr name) {
    i64 first = sr_first_at(si);
    i64 i = 0;
    loop {
        if (i >= sr_count_at(si)) break;
        if (str_eq(fd_name_at(first + i), name)) return first + i;
        i = i + 1;
    }
    return 0 - 1;
}

// 1 when the struct being read, whose fields start at `first`, already declared
// `name` -- checked before def_add so the duplicate is reported as a field
// rather than as the core's own "duplicate #define"
i64 tk_field_dup(i64 first, uptr name) {
    i64 i = first;
    loop {
        if (i >= tk_nfield) break;
        if (str_eq(fd_name_at(i), name)) return 1;
        i = i + 1;
    }
    return 0;
}

// the field called `name` when the left side's struct is NOT known statically --
// a parameter, or a global, neither of which the core reports to a module. The
// answer is the field itself when exactly ONE struct declares that name, which
// is the same "resolve the name in the module's own table" `mc`'s own teaching
// demo does for `p ~> len` (lib/user_syntax_demo.mc). -1 = no struct declares
// it, -2 = more than one does and the left side has to be a known struct.
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

// the newest declaration of `name` wins, so a second function reusing the name
// for another struct is not shadowed by the first one's entry
i64 tk_local_find(uptr name) {
    i64 i = tk_nlocal - 1;
    loop {
        if (i < 0) break;
        if (str_eq(lv_name_at(i), name)) return lv_str_at(i);
        i = i - 1;
    }
    return 0 - 1;
}

i64 tk_xt_find(i64 n) {
    i64 i = tk_nxt - 1;
    loop {
        if (i < 0) break;
        if (xt_node_at(i) == n) return xt_str_at(i);
        i = i - 1;
    }
    return 0 - 1;
}

// ---- table appends ----
void tk_local_add(uptr name, i64 si) {
    if (tk_nlocal == TK_MAXLOCAL) err_at(tk_file, tk_line, "teko: too many struct-typed locals");
    set_lv_name_at(tk_nlocal, name);
    set_lv_str_at(tk_nlocal, si);
    tk_nlocal = tk_nlocal + 1;
}

void tk_xt_add(i64 n, i64 si) {
    if (tk_nxt == TK_MAXXT) err_at(tk_file, tk_line, "teko: too many struct-typed expressions");
    set_xt_node_at(tk_nxt, n);
    set_xt_str_at(tk_nxt, si);
    tk_nxt = tk_nxt + 1;
}

void tk_field_add(uptr name, i64 off, i64 ty) {
    if (tk_nfield == TK_MAXFIELD) err_at(tk_file, tk_line, "teko: too many struct fields");
    set_fd_name_at(tk_nfield, name);
    set_fd_off_at(tk_nfield, off);
    set_fd_ty_at(tk_nfield, ty);
    tk_nfield = tk_nfield + 1;
}

void tk_struct_add(uptr name, i64 ty, i64 first, i64 count, i64 size) {
    if (tk_nstruct == TK_MAXSTRUCT) err_at(tk_file, tk_line, "teko: too many structs");
    set_sr_name_at(tk_nstruct, name);
    set_sr_ty_at(tk_nstruct, ty);
    set_sr_first_at(tk_nstruct, first);
    set_sr_count_at(tk_nstruct, count);
    set_sr_size_at(tk_nstruct, size);
    tk_nstruct = tk_nstruct + 1;
}

// ---- the static struct of an already parsed expression ----
// Three sources, in the order that keeps the precise answer ahead of the
// heuristic one: a node this file built (a field of struct type, so `a.b.c`
// chains), a local the core declared (its N_VAR carried the struct's own type
// id, which is what type_new buys over type_alias), and a call, whose declared
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
// is observable here as the N_VAR node, and its type id is the struct. Nothing
// is rewritten -- the node that arrives is the node that leaves. `nd_val` is
// the array length, so `Point tbl[4];` (four pointers, not a struct) is skipped.
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
// Name Name_new() { uptr p = rt_alloc(NAME_SIZE); return p; }
// The declared return type is the struct's own id, which is what makes
// `Point p = new Point;` and a call in `.` position resolve to the struct.
i64 tk_ctor(uptr name, i64 ty, i64 size) {
    i64 stmts = tk_var(TY_UPTR, "p", tk_call("rt_alloc", tk_int(size)));
    stmts = list_append(stmts, tk_ret(tk_id("p")));
    return tk_func(ty, tk_ctor_name(name), 0, tk_blk(stmts));
}

// new struct name: an identifier that is still free. `type_new` reserves the
// word, so a second `struct Point` (or `struct str`) arrives here as a keyword
// rather than as T_IDENT -- without this guard the error would come out as an
// unexplained "name expected".
uptr tk_newname() {
    if (p_id() == T_IDENT) return p_ident();
    if (alias_find(p_id()) >= 0)
        err_at2(p_file(), p_line(), "teko: the name is already a type", p_name());
    err_at2(p_file(), p_line(), "teko: name of struct expected", p_name());
    return 0;
}

// ---- struct Name { type field; ... } ----
void tk_struct() {
    tk_line = p_line();
    tk_file = p_file();
    i64 head_line = tk_line;                     // position of the `struct` word
    uptr head_file = tk_file;
    p_next();                                    // the `struct` word
    uptr name = tk_newname();
    i64 ty = type_new(name, 8, 8, TK_INT);       // a field of its own type parses
    p_expect(K_LBRACE, "expected { in the struct body");
    i64 first = tk_nfield;
    i64 off = 0;
    loop {
        if (p_id() == K_RBRACE) break;
        if (p_id() == T_EOF) err_at(head_file, head_line, "unterminated struct");
        tk_line = p_line();                      // errors and nodes for this field
        tk_file = p_file();
        i64 fty = p_type();
        uptr m = p_ident();
        p_expect(K_SEMI, "expected ; after the struct field");
        if (fty == TY_VOID) err_at2(tk_file, tk_line, "teko: field of type void", m);
        if (tk_field_dup(first, m)) err_at2(tk_file, tk_line, "teko: duplicate struct field", m);
        i64 w = type_width(fty);
        off = (off + w - 1) & ~(w - 1);          // the field's natural alignment
        def_add(tk_cname(name, m), off, tk_line, tk_file);
        tk_field_add(m, off, fty);
        off = off + w;
    }
    p_next();                                    // }
    p_accept(K_SEMI);                            // a C programmer's trailing ;
    tk_line = head_line;                         // back to the struct's own level
    tk_file = head_file;
    off = (off + 7) & ~7;                        // the whole object aligned to 8
    if (off == 0) off = 8;                       // struct with no fields: it still exists
    def_add(tk_join(tk_case(name, 1), "_SIZE"), off, tk_line, tk_file);
    tk_struct_add(name, ty, first, tk_nfield - first, off);
    top_add(tk_ctor(name, ty, off));
}
