// teko_ns.mc -- `namespace A.B { ... }` / `namespace A.B;` (file-scoped, C# 10)
// and `using A.B;` (entrega 5, crumb N1; plano-ngen-entrega4.md §31). `import`
// (N3) and the mangling of a free function declared inside a namespace (N2)
// stay out: a free function written inside a namespace here is left PLAIN,
// registered under its bare name exactly as it always was.
//
// The precedent read whole is `mini_compiler/examples/lang/lang_class.mc:568-666`
// (`lg_namespace`, `lg_using`, `lg_ns_path`, `lg_ns_register`) and
// `lang_util.mc:33` (`lg_qualify`); the form here is this project's own, not a
// copy, because the ngen already owns type identity through `type_new` and the
// module's own tables (`teko_struct.mc`), which `examples/lang` does not.
//
// ---- the real name, and why the short one is not a `type_alias` ----
//
// `namespace geo { class Circle { ... } }` registers the type under the FULL
// name `geo__Circle` (`type_new`, the very call a plain `class` already makes,
// unchanged) -- so `.`, `new`, a vtable, an interface cost nothing extra, and
// two namespaces may each declare their own `Circle` without collision (§31
// (c): the type table and the declaration list are the two places a generated
// name could collide, and both already refuse a duplicate row).
//
// The SHORT name (`Circle`) is never `type_alias`: `alias_find` answers the
// LAST registration in silence (§31 (a).5), which would make a second `Circle`
// in a second namespace corrupt the first one's identity. Instead, `Circle`
// becomes a plain WORD this module owns in three positions (`syntax` for a
// bare return type at the top level, `syntax_stmt`/`syntax_expr` -- the very
// two teko_access.mc's `tk_type_word` already registers for a plain type, and
// this module points a namespaced short name at the SAME two functions) and
// the identity is decided FRESH at every site by the search order C# uses:
// the current namespace outward, prefix by prefix, and then the `using`s of
// the file the reference is written in. Nothing is memoized across sites.
//
// ---- where each position is read ----
//
//   Circle f(Circle c) { ... }     the top level: the ONE position the core
//                                  reads a type word with no hook at all
//                                  (`tk_ns_top`, registered by `syntax`)
//   Circle c = new Circle();       a local -- `tk_type_stmt` (teko_access.mc),
//                                  which already asked `tk_struct_find`; it
//                                  now falls back to the search order below
//                                  and reports plainly when nothing answers
//   Circle.made                    a static access -- `tk_type_expr`, same
//   i64 g(Circle c) { ... }        a member's field/parameter/return type --
//                                  `tk_gen_ty` (teko_generic.mc), which reads
//                                  a namespaced short word before falling to
//                                  the core's own `p_type()`
//   i64 g(Circle c = ...) { }      a free function's own parameter list --
//                                  `tk_default_param` (teko_default.mc), same
//                                  branch, ahead of `type_of_token`
//
// A QUALIFIED reference (`geo.Circle`, `geo.Circle.made`, `new geo.Circle()`)
// is a different grammar position: the first SEGMENT of the namespace path
// (`geo`) is what this module reserves and reads with `p_name()`+`p_next()`,
// never `p_ident()` -- the segment is a word by then, and `p_ident()` demands
// a plain identifier (§31 (a).3). `tk_ns_walk` below is the one piece of
// machinery both `new geo.Circle()` (`teko_expr.mc`'s `tk_new`) and a bare
// segment's own statement/expression handlers walk: it grows the accumulated
// name across `.`-joined segments for as long as what has been read is a
// namespace this module knows, and stops the instant the accumulated name IS
// a declared type -- `tk_static_member` (teko_access.mc) takes it from there,
// so `geo.Circle.made` and `geo.Circle.tally()` cost this module nothing extra
// (§31 (a).2).
//
// ---- declaration never searches ----
//
// A type BEING declared qualifies with the CURRENT namespace and looks up the
// EXACT result (`tk_ns_qualify`, called once per declaring construct, right
// after the name is read) -- never the search order above. `teko_class.mc`'s
// own reopen check for `partial` is the one site that had to change beyond
// that single call: it compared the bare name it peeked at against the type
// table directly, which would have let the search order's `using` half
// (irrelevant to a declaration) merge two unrelated namespaces' same-named
// classes in silence. It now qualifies the peeked name with `tk_ns_qualified_name`
// (the read-only half of `tk_ns_qualify`, with no registration side effect)
// before the lookup, so two `partial class Foo`s in two different namespaces
// never touch each other's row.
//
// ---- what stays out (§31 (e), registered here rather than hidden) ----
//
//   - a namespace's own generic (`namespace geo { class Box<T> { ... } }`)
//     keeps today's plain, unqualified registration: `tk_gen_find`/
//     `tk_gen_declstmt` compare by the bare name a `syntax_stmt` word carries,
//     and reworking that to the search order above is D31.14's own qualified-
//     generic debt, not this crumb's -- a same-named generic declared in two
//     namespaces collides with today's existing, clear "duplicate generic".
//   - a namespaced type at the top level only opens a FUNCTION: `tk_ns_top`
//     refuses a global of that type with a clear message rather than half
//     rebuilding `parse_global`, which this module has no hook onto.
//   - `main`/`extern`/a global inside a namespace BLOCK are refused where the
//     block's own loop reads them; the same three inside a FILE-SCOPED
//     namespace are not caught here, because nothing calls this module back
//     once a file-scoped namespace has only set its state and returned --
//     catching that needs a pass, and N1 adds none (N2's `tk_ns_pass` is
//     where it belongs).

#define TK_MAXNS    16                 // namespaces declared in one source
#define TK_MAXNSSEG 16                 // namespace segment words reserved
#define TK_MAXSHORT 64                 // namespaced short type words reserved
#define TK_MAXUSING 32                 // `using` directives, across every file
#define TK_MAXNSF   16                 // files that declared a file-scoped namespace

uptr nsq_name[TK_MAXNS];               // the full "__"-joined namespace names known
i64  tk_nns = 0;

uptr nss_word[TK_MAXNSSEG];            // first-segment words already reserved
i64  tk_nnsseg = 0;

uptr nsh_word[TK_MAXSHORT];            // short type words already reserved
i64  tk_nnsh = 0;

uptr ug_ns[TK_MAXUSING];               // the namespace a `using` names
uptr ug_file[TK_MAXUSING];             // ...and the file that wrote it (D31.6: per file)
i64  tk_nusing = 0;

uptr nsf_file[TK_MAXNSF];              // a file with a file-scoped `namespace X;`
uptr nsf_ns[TK_MAXNSF];                // ...and the namespace it named
i64  tk_nnsf = 0;

uptr tk_cur_ns = 0;                    // the BLOCK-form namespace currently open, or 0
i64  tk_ns_dot = 0;                    // the `.` token, interned once at startup

// teko_access.mc is included after this file: the short word and the segment
// word both dispatch through its existing machinery
i64 tk_type_stmt();
i64 tk_type_expr();
i64 tk_static_member(i64 si, i64 line, uptr fl);

void tk_ns_init() { tk_ns_dot = word_add("."); }

// ---- table accessors (no raw ld64/st64 outside this section) ----
uptr nsq_name_at(i64 i) { return ld64(nsq_name + i * 8); }
void set_nsq_name_at(i64 i, uptr v) { st64(nsq_name + i * 8, v); }
uptr nss_word_at(i64 i) { return ld64(nss_word + i * 8); }
void set_nss_word_at(i64 i, uptr v) { st64(nss_word + i * 8, v); }
uptr nsh_word_at(i64 i) { return ld64(nsh_word + i * 8); }
void set_nsh_word_at(i64 i, uptr v) { st64(nsh_word + i * 8, v); }
uptr ug_ns_at(i64 i)    { return ld64(ug_ns + i * 8); }
void set_ug_ns_at(i64 i, uptr v)    { st64(ug_ns + i * 8, v); }
uptr ug_file_at(i64 i)  { return ld64(ug_file + i * 8); }
void set_ug_file_at(i64 i, uptr v)  { st64(ug_file + i * 8, v); }
uptr nsf_file_at(i64 i) { return ld64(nsf_file + i * 8); }
void set_nsf_file_at(i64 i, uptr v) { st64(nsf_file + i * 8, v); }
uptr nsf_ns_at(i64 i)   { return ld64(nsf_ns + i * 8); }
void set_nsf_ns_at(i64 i, uptr v)   { st64(nsf_ns + i * 8, v); }

// ---- the namespaces known, the segment words and the short type words ----
i64 tk_ns_find(uptr cheio) {
    i64 i = 0;
    loop {
        if (i >= tk_nns) break;
        if (str_eq(nsq_name_at(i), cheio)) return i;
        i = i + 1;
    }
    return 0 - 1;
}

// reabertura = merge: a namespace named twice adds nothing the first time did
// not already add (§31 (a).1), so a second `namespace geo { ... }` is silent
void tk_ns_add(uptr cheio) {
    if (tk_ns_find(cheio) >= 0) return;
    if (tk_nns == TK_MAXNS) err_at(tk_file, tk_line, "teko: too many namespaces");
    set_nsq_name_at(tk_nns, cheio);
    tk_nns = tk_nns + 1;
}

i64 tk_ns_seg_known(uptr seg0) {
    i64 i = 0;
    loop {
        if (i >= tk_nnsseg) break;
        if (str_eq(nss_word_at(i), seg0)) return 1;
        i = i + 1;
    }
    return 0;
}

// the first segment of a namespace path, reserved ONCE across however many
// times that namespace is opened: `syntax_stmt`/`syntax_expr` are what let
// `geo.Circle c = ...;` and `geo.Circle.made` start on the word at all
void tk_ns_seg_register(uptr seg0) {
    if (tk_ns_seg_known(seg0)) return;
    if (tk_nnsseg == TK_MAXNSSEG) err_at(tk_file, tk_line, "teko: too many namespace segments");
    set_nss_word_at(tk_nnsseg, seg0);
    tk_nnsseg = tk_nnsseg + 1;
    syntax_stmt(seg0, &tk_ns_seg_stmt);
    syntax_expr(seg0, &tk_ns_seg_expr);
}

i64 tk_ns_short_known(uptr curto) {
    i64 i = 0;
    loop {
        if (i >= tk_nnsh) break;
        if (str_eq(nsh_word_at(i), curto)) return 1;
        i = i + 1;
    }
    return 0;
}

// the short name of a namespaced type, reserved ONCE regardless of how many
// namespaces go on to declare a type spelled the same way -- the identity is
// never this registration's business, only the search order at the site is
void tk_ns_register(uptr curto) {
    if (tk_ns_short_known(curto)) return;
    if (tk_nnsh == TK_MAXSHORT) err_at(tk_file, tk_line, "teko: too many namespaced type names");
    set_nsh_word_at(tk_nnsh, curto);
    tk_nnsh = tk_nnsh + 1;
    syntax(curto, &tk_ns_top);
    syntax_stmt(curto, &tk_type_stmt);
    syntax_expr(curto, &tk_type_expr);
}

void tk_ns_using_add(uptr fl, i64 line, uptr full) {
    if (tk_nusing == TK_MAXUSING) err_at(fl, line, "teko: too many using directives");
    set_ug_file_at(tk_nusing, fl);
    set_ug_ns_at(tk_nusing, full);
    tk_nusing = tk_nusing + 1;
}

uptr tk_ns_file_get(uptr fl) {
    i64 i = 0;
    loop {
        if (i >= tk_nnsf) break;
        if (str_eq(nsf_file_at(i), fl)) return nsf_ns_at(i);
        i = i + 1;
    }
    return 0;
}

void tk_ns_file_set(uptr fl, i64 line, uptr full) {
    if (tk_ns_file_get(fl) != 0)
        err_at(fl, line, "teko: the file already has a file-scoped namespace");
    if (tk_nnsf == TK_MAXNSF) err_at(fl, line, "teko: too many file-scoped namespaces");
    set_nsf_file_at(tk_nnsf, fl);
    set_nsf_ns_at(tk_nnsf, full);
    tk_nnsf = tk_nnsf + 1;
}

// the namespace the parser stands in RIGHT NOW: an open block wins outright,
// and otherwise the answer is whatever file-scoped namespace the CURRENT
// file declared, which is naturally 0 again once an `#include` returns to a
// file that declared none -- no stack to unwind for that half at all
uptr tk_ns_current() { if (tk_cur_ns != 0) return tk_cur_ns; return tk_ns_file_get(p_file()); }

// `nome`, qualified with the current namespace for a DECLARATION's own
// exact-match lookup (`teko_class.mc`'s partial reopen check) -- no
// registration, so a mere peek at what might be a fresh declaration never
// reserves a word by accident
uptr tk_ns_qualified_name(uptr nome) {
    uptr cur = tk_ns_current();
    if (cur == 0) return nome;
    return tk_join3(cur, "__", nome);
}

// the one call every type-declaring construct makes on its own name: outside
// a namespace it is the identity (so a program with none is untouched byte
// for byte), and inside one it reserves the short word and returns the full
uptr tk_ns_qualify(uptr nome) {
    uptr cur = tk_ns_current();
    if (cur == 0) return nome;
    tk_ns_register(nome);
    return tk_join3(cur, "__", nome);
}

// the index of the last "__" strictly before `len` in `s`, or -1 when there is
// none left -- what lets the search order below try each shorter prefix of
// the current namespace in turn
i64 tk_ns_last_sep(uptr s, i64 len) {
    i64 i = len - 1;
    loop {
        if (i < 1) break;
        if (ld8(s + i) == '_' && ld8(s + i - 1) == '_') return i - 1;
        i = i - 1;
    }
    return 0 - 1;
}

// outward from the current namespace, prefix by prefix (`A.B` tries
// `A__B__curto`, then `A__curto`), never the bare `curto` alone -- the
// caller already tried that exact form before falling back here, and trying
// it again would recurse into this very function forever
i64 tk_ns_try_prefixes(uptr curto) {
    uptr cur = tk_ns_current();
    if (cur == 0) return 0 - 1;
    i64 len = cstrlen(cur);
    loop {
        uptr pre = xstrdup(cur, len);
        i64 si = tk_struct_find_exact(tk_join3(pre, "__", curto));
        if (si >= 0) return si;
        i64 cut = tk_ns_last_sep(cur, len);
        if (cut < 0) break;
        len = cut;
    }
    return 0 - 1;
}

uptr tk_ns_ambig_msg(uptr curto, uptr a, uptr b) {
    return tk_join(tk_join3("teko: ambiguous name ", curto, " ("), tk_join3(a, ", ", tk_join(b, ")")));
}

// the short name `curto`'s identity from wherever the parser stands: the
// namespace chain outward, and only then the `using`s of the FILE the
// reference is written in (D31.6) -- a declaration in the current namespace
// always wins over a `using`, because it is tried first and returned at once.
// Two `using`s of the same level naming two different answers is refused,
// never guessed.
i64 tk_ns_resolve(uptr curto) {
    i64 si = tk_ns_try_prefixes(curto);
    if (si >= 0) return si;
    uptr fl = p_file();
    i64 found = 0 - 1;
    uptr found_ns = 0;
    i64 i = 0;
    loop {
        if (i >= tk_nusing) break;
        if (str_eq(ug_file_at(i), fl)) {
            i64 cand = tk_struct_find_exact(tk_join3(ug_ns_at(i), "__", curto));
            if (cand >= 0) {
                if (found >= 0 && cand != found)
                    err_at(fl, p_line(), tk_ns_ambig_msg(curto, found_ns, ug_ns_at(i)));
                found = cand;
                found_ns = ug_ns_at(i);
            }
        }
        i = i + 1;
    }
    return found;
}

// the namespaced short word branch every "read a type here" position takes
// BEFORE its own fallback (`teko_default.mc`'s free-function parameter,
// `teko_generic.mc`'s field/parameter/return type): -1 when the current
// token names no namespaced type, so the caller's own answer is unchanged
i64 tk_ns_param_ty() {
    if (!tk_ns_short_known(p_name())) return 0 - 1;
    i64 si = tk_ns_resolve(p_name());
    if (si < 0) return 0 - 1;
    return sr_ty_at(si);
}

// grows `acc` across `.`-joined segments for as long as the result names a
// namespace this module knows, and stops the instant it names a declared
// type -- `new geo.Circle()` (teko_expr.mc) and a bare segment's own
// handlers below share this one walk. `acc` is already read and consumed by
// the caller; the loop only ever consumes what comes AFTER it.
uptr tk_ns_walk(uptr acc) {
    loop {
        if (tk_struct_find_exact(acc) >= 0) break;
        if (p_id() != tk_ns_dot) break;
        p_next();                                    // the `.`
        uptr nxt = p_name();
        uptr probe = tk_join3(acc, "__", nxt);
        if (tk_struct_find_exact(probe) >= 0) { p_next(); acc = probe; break; }
        if (tk_ns_find(probe) < 0) break;
        p_next();
        acc = probe;
    }
    return acc;
}

// a dotted path, read from the CURRENT token (not yet consumed): the first
// segment is stored at `pmem` for the caller to reserve as a word, and the
// return is every segment joined by "__" -- `namespace`'s own path and
// `using`'s share this one reader (§31 (a).3: `p_name()`+`p_next()`, never
// `p_ident()`, because the first segment is a reserved word from its second
// use onward)
uptr tk_ns_read_path(uptr pmem) {
    uptr seg0 = p_name();
    p_next();
    st64(pmem, seg0);
    uptr full = seg0;
    loop {
        if (p_id() != tk_ns_dot) break;
        p_next();
        uptr seg = p_name();
        p_next();
        full = tk_join3(full, "__", seg);
    }
    return full;
}

// `Circle f(Circle c) { ... }` / `Circle f(Circle c);` at the top level: the
// one type-word position the core reads with no hook at all (`parse_top`
// only ever asks `type_of_token`, which a namespaced short name is
// deliberately not in -- D31.5). A global of the type is refused rather than
// half-rebuilding `parse_global`, which this module has no hook onto.
i64 tk_ns_proto(i64 ty, uptr name, i64 params) {
    i64 n = tk_nd(N_PROTO);
    set_nd_name(n, name);
    set_nd_type(n, ty);
    set_nd_a(n, params);
    return n;
}

void tk_ns_top() {
    i64 line = p_line();
    uptr fl = p_file();
    i64 si = tk_ns_resolve(p_name());
    if (si < 0) err_at2(fl, line, "teko: unresolved name", p_name());
    i64 ty = sr_ty_at(si);
    p_next();                                    // the type word
    uptr name = p_ident();
    p_set_decl_name(name);
    if (p_id() != K_LPAR)
        err_at2(fl, line, "teko: a namespaced type at top level declares a function", name);
    i64 params = parse_params();
    tk_line = line;
    tk_file = fl;
    if (p_accept(K_SEMI)) { top_add(tk_ns_proto(ty, name, params)); return; }
    top_add(parse_function(ty, name, params));
}

// the rest of a local declaration once its type is already known AND already
// consumed -- unlike the core's own `parse_var`, which expects to be sitting
// ON the type word and consumes it itself. A qualified type name
// (`geo.Circle`) and a generic instantiation (`Box<Circle, 4>`) are both
// read as more than one token before the type is known, so neither can hand
// the core's `parse_var` what it expects; this is the one shared tail both
// take (teko_generic.mc's `tk_gen_declstmt`).
i64 tk_var_after_type(i64 ty, i64 line, uptr fl) {
    uptr vn = p_ident();
    if (p_id() == K_LBRACK)
        err_at2(fl, line, "teko: an array of this type is not taught yet", vn);
    i64 init = 0;
    if (p_accept(K_ASSIGN)) init = parse_expr(0);
    p_expect(K_SEMI, "expected ; after declaration");
    tk_line = line;
    tk_file = fl;
    return tk_var(ty, vn, init);
}

// `geo.Circle c = ...;` / `geo.Circle.made = e;`, the statement forms a bare
// namespace segment opens: a var-decl when the CURRENT token is not itself a
// `.` (the whole qualified type name is already read and consumed by
// `tk_ns_walk`, unlike `tk_type_stmt`'s single unconsumed word, so this asks
// "is the parser standing ON a `.` right now", never `tk_dot_follows`, which
// answers a different question -- does one follow the token still to come),
// a static access otherwise
i64 tk_ns_seg_stmt() {
    i64 line = p_line();
    uptr fl = p_file();
    uptr seg0 = p_name();
    p_next();
    uptr acc = tk_ns_walk(seg0);
    i64 si = tk_struct_find_exact(acc);
    if (si < 0) err_at2(fl, line, "teko: unresolved qualified name", acc);
    if (p_id() != tk_ns_dot) return tk_var_after_type(sr_ty_at(si), line, fl);
    i64 e = tk_static_member(si, line, fl);
    p_expect(K_SEMI, "expected ; after the static member");
    tk_line = line;
    tk_file = fl;
    return tk_stmt(e);
}

// `geo.Circle.made` / `geo.Circle.tally()` in expression position
i64 tk_ns_seg_expr() {
    i64 line = p_line();
    uptr fl = p_file();
    uptr seg0 = p_name();
    p_next();
    uptr acc = tk_ns_walk(seg0);
    i64 si = tk_struct_find_exact(acc);
    if (si < 0) err_at2(fl, line, "teko: unresolved qualified name", acc);
    return tk_static_member(si, line, fl);
}

// a top-level declaration or a global inside a namespace BLOCK: `main` and a
// global are refused here (§31 (a).8), where this module owns the loop that
// reads them; `extern` is caught by the loop itself, before `parse_top` ever
// sees it (the core reads `extern` outside `parse_top`, at `parse_unit`'s
// own level)
void tk_ns_reject_topkind(i64 n) {
    if (n == 0) return;
    if (nd_kind(n) == N_GLOBAL)
        err_at(nd_file(n), nd_line(n), "teko: a global is declared outside every namespace");
    if (nd_kind(n) == N_PROTO && str_eq(nd_name(n), "main"))
        err_at(nd_file(n), nd_line(n), "teko: main is declared outside every namespace");
    if (nd_kind(n) == N_FUNC && str_eq(nd_name(n), "main"))
        err_at(nd_file(n), nd_line(n), "teko: main is declared outside every namespace");
}

// `namespace A.B { ... }` / `namespace A.B;` (D218, D226: C# 10's file-scoped
// form too). Nesting is not taught (§31 (a).1: `A.B` already gives the same
// tree with one linear handler); a `namespace` read while another is already
// open is refused rather than silently prefixing one onto the other.
void tk_namespace() {
    i64 line = p_line();
    uptr fl = p_file();
    p_next();                                    // the `namespace` word
    if (tk_ns_current() != 0) err_at(fl, line, "teko: a nested namespace is not taught");
    uptr seg0mem = xalloc(8);
    uptr full = tk_ns_read_path(seg0mem);
    uptr seg0 = ld64(seg0mem);
    if (p_accept(K_SEMI)) {
        tk_ns_file_set(fl, line, full);
        tk_ns_add(full);
        tk_ns_seg_register(seg0);
        return;
    }
    if (p_id() != K_LBRACE) err_at(fl, line, "teko: expected { or ; after the namespace path");
    p_next();                                    // {
    tk_ns_add(full);
    tk_ns_seg_register(seg0);
    uptr save = tk_cur_ns;
    tk_cur_ns = full;
    loop {
        if (p_id() == K_RBRACE) break;
        if (p_id() == T_EOF) err_at(fl, line, "teko: unterminated namespace");
        if (p_id() == T_DIR) { do_directive(); continue; }
        if (p_id() == K_EXTERN) {
            i64 en = parse_extern();
            err_at(nd_file(en), nd_line(en), "teko: an extern is declared outside every namespace");
        }
        i64 n = parse_top();
        tk_ns_reject_topkind(n);
        top_add(n);
    }
    p_next();                                    // }
    p_accept(K_SEMI);
    tk_cur_ns = save;
}

// `using A.B;` -- adds a search entry for the FILE it is written in (D31.6);
// it never reserves a word of its own, since the namespace it names already
// reserved its first segment the moment it was declared
void tk_using() {
    i64 line = p_line();
    uptr fl = p_file();
    p_next();                                    // the `using` word
    uptr seg0mem = xalloc(8);
    uptr full = tk_ns_read_path(seg0mem);
    p_expect(K_SEMI, "expected ; after using");
    tk_ns_using_add(fl, line, full);
}
