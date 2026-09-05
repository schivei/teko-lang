// teko_switch.mc -- `switch` in both of C#'s own spellings (D222/D228,
// entrega 5), the pieces teko_loop.mc's own `while`/`do`/`for` and
// teko_ternary.mc's own `?:` already gave the machinery for.
//
// THE STATEMENT (`switch (x) { case 1: ... break; default: ... break; }`) is
// rebaixed at PARSE TIME (`syntax_stmt("switch")`, teko_loop.mc's own shape)
// to a one-lap `loop` holding a sequence of `if`s, one per group of case
// labels that shares a body, plus the `default` group (wherever it was
// written, moved to the end -- C#'s own rule) and a trailing unconditional
// `break;` that closes the loop even when nothing matched. `x` is read once,
// into a local (`i64 $t = x;`, the loop's own first statement); every label
// then compares `$t`, never `x` again.
//
// `break`/`break N` inside a case body are left EXACTLY as the source wrote
// them: the switch's own loop is the ONE level the source sees, so a bare
// `break;` already means "leave the switch" without any rewrite, and a
// `break N` that reaches further out is picked up correctly by whatever
// OUTER `do`/`for` rewrite (`tk_loop_rewrite_stmt`) walks this switch's own
// loop as just another nested `N_LOOP` it discovers -- the same composition
// `docs/design/plano-ngen-entrega4.md` §29 already relies on for nested
// loops. `continue`, though, has no level in this core (`language.md` § 4:
// "continue restarts the innermost loop", no `continue N`) -- a bare
// `continue;` written directly in a case body would restart the SWITCH's own
// one-lap loop, not the enclosing real loop the source meant, and there is
// no core mechanism to redirect it. Refused with a clear message rather than
// worked around with a flag (the crumb's own instruction); a `continue`
// inside a loop the case body writes ITSELF is untouched, exactly as `break`
// composes.
//
// THE EXPRESSION (`x switch { 1 => a, 2 or 3 => b, _ when c => d, _ => e }`)
// builds NO machinery of its own (D228): the handler reads the arms and
// constructs the very placeholder `teko_ternary.mc`'s own `?:` builds --
// `tk_ternary(x==1, a, tk_ternary(x==2||x==3, b, tk_ternary(c, d, e)))` --
// which `tk_ternary_pass` then hoists exactly as if the programmer had
// written the chain by hand (lazy arms included). `x`, if not a bare name,
// is read once too, but an EXPRESSION position has no statement list to
// insert a preceding local into -- so the local is embedded as a real `N_VAR`
// node placed where the FIRST comparison needs its value, and
// `teko_ternary.mc`'s own scan (`tk_tern_hoist_var`) hoists it into the
// SAME preamble the surrounding ternary chain already hoists its own
// condition into, unconditionally, before any arm is tested.

#define TK_SWITCH_MAXVAL 128
#define TK_SWITCH_MAXARM 64

// a `case`/switch-expression-arm label: folded exactly as `teko_const.mc`
// folds a `const`'s own value (a `#define`'s bare name is already an `N_INT`
// by the time `parse_expr` returns it, so a module const reads for free; a
// namespaced one is not -- known gap, registered, not closed here).
i64 tk_switch_label_val(uptr fl, i64 line) {
    i64 e = fold(parse_expr(0));
    if (nd_kind(e) != N_INT)
        err_at(fl, line, "teko: a case label must be a constant expression");
    return e;
}

i64 tk_switch_seen(uptr tbl, i64 n, i64 v) {
    i64 i = 0;
    loop {
        if (i >= n) break;
        if (ld64(tbl + i * 8) == v) return 1;
        i = i + 1;
    }
    return 0;
}

// the last node of a statement list that is never empty at the call site
i64 tk_switch_last(i64 head) {
    i64 n = head;
    loop {
        if (nd_next(n) == 0) return n;
        n = nd_next(n);
    }
}

// "control cannot fall out of a case" -- a non-empty case/default body has to
// end in one of the four jumps C# itself requires here
void tk_switch_check_end(i64 body) {
    i64 last = tk_switch_last(body);
    i64 k = nd_kind(last);
    if (k == N_BREAK || k == N_RETURN || k == N_CONTINUE) return;
    err_at(nd_file(last), nd_line(last), "teko: control cannot fall out of a case; end it with break");
}

// a `continue` sitting directly in a case body -- not inside a loop the body
// itself opens -- has nowhere to go (the header above explains why); one
// opened by the body composes normally and is left alone, `tk_loop_rewrite_
// stmt`'s own shape.
void tk_switch_no_continue_stmt(i64 n, i64 depth);

void tk_switch_no_continue_list(i64 n, i64 depth) {
    loop {
        if (n == 0) break;
        tk_switch_no_continue_stmt(n, depth);
        n = nd_next(n);
    }
}

void tk_switch_no_continue_stmt(i64 n, i64 depth) {
    if (n == 0) return;
    i64 k = nd_kind(n);
    if (k == N_BLOCK) { tk_switch_no_continue_list(nd_a(n), depth); return; }
    if (k == N_LOOP) { tk_switch_no_continue_stmt(nd_a(n), depth + 1); return; }
    if (k == N_IF) {
        tk_switch_no_continue_stmt(nd_b(n), depth);
        tk_switch_no_continue_stmt(nd_c(n), depth);
        return;
    }
    if (k == N_CONTINUE && depth == 0)
        err_at(nd_file(n), nd_line(n), "teko: a case cannot continue past its own switch; there is no continue N");
}

// `switch (x) { ... }` -- one `i64 $t = x;` ahead of a sequence of `if`s, one
// per group of labels sharing a body, plus `default` (wherever it was
// written) moved to the very end and a trailing `break;` that closes the loop
// even when nothing matched.
i64 tk_switch_stmt() {
    i64 line = p_line();
    uptr fl = p_file();
    p_next();                                     // `switch`
    p_expect(K_LPAR, "expected ( after switch");
    i64 x = parse_expr(0);
    p_expect(K_RPAR, "expected ) after switch value");
    p_expect(K_LBRACE, "expected { after switch value");

    uptr t = gensym_new();
    tk_line = line;
    tk_file = fl;
    i64 pre = tk_var(TY_I64, t, x);

    i64 seen[TK_SWITCH_MAXVAL];
    i64 nseen = 0;

    i64 casesHead = 0;
    i64 defaultBody = 0;
    i64 defaultSeen = 0;

    i64 pendingCond = 0;
    i64 pendingHasDefault = 0;

    loop {
        if (p_id() == K_RBRACE) break;

        loop {
            if (tk_kw("case")) {
                p_next();
                i64 lline = p_line();
                uptr lfl = p_file();
                i64 val = tk_switch_label_val(lfl, lline);
                i64 v = nd_val(val);
                i64 cond = tk_bin(K_EQ, tk_id(t), val);
                i64 guarded = 0;
                if (tk_word("when")) {
                    p_next();
                    cond = tk_bin(K_ANDAND, cond, parse_expr(0));
                    guarded = 1;
                }
                p_expect(K_COLON, "expected ':' after a case label");
                if (!guarded) {
                    if (tk_switch_seen(seen, nseen, v))
                        err_at(lfl, lline, tk_join("teko: duplicate case label: ", tk_num(v)));
                    if (nseen == TK_SWITCH_MAXVAL) err_at(lfl, lline, "teko: too many case labels");
                    st64(seen + nseen * 8, v);
                    nseen = nseen + 1;
                }
                if (pendingCond == 0) pendingCond = cond;
                else pendingCond = tk_bin(K_OROR, pendingCond, cond);
                continue;
            }
            if (tk_kw("default")) {
                p_next();
                p_expect(K_COLON, "expected ':' after default");
                if (pendingHasDefault) err_at(fl, line, "teko: duplicate default label");
                pendingHasDefault = 1;
                continue;
            }
            break;
        }

        i64 body = 0;
        loop {
            if (tk_kw("case") || tk_kw("default") || p_id() == K_RBRACE) break;
            if (p_id() == T_EOF) err_at(fl, line, "unterminated switch");
            body = list_append(body, parse_stmt());
        }
        if (body == 0) continue;                  // empty labels fall through to the next group

        tk_switch_no_continue_list(body, 0);
        tk_switch_check_end(body);

        if (pendingHasDefault) {
            if (defaultSeen) err_at(fl, line, "teko: duplicate default label");
            defaultSeen = 1;
            if (pendingCond != 0) {
                casesHead = list_append(casesHead, tk_if(pendingCond, tk_blk(body)));
                defaultBody = tk_clone_list(body);
            } else {
                defaultBody = body;
            }
        } else {
            casesHead = list_append(casesHead, tk_if(pendingCond, tk_blk(body)));
        }
        pendingCond = 0;
        pendingHasDefault = 0;
    }
    p_next();                                     // }
    if (pendingCond != 0 || pendingHasDefault)
        err_at(fl, line, "teko: control cannot fall out of a case; end it with break");

    i64 outerBody = list_append(0, pre);
    outerBody = list_append(outerBody, casesHead);
    if (defaultBody != 0) outerBody = list_append(outerBody, tk_blk(defaultBody));
    outerBody = list_append(outerBody, tk_break_lvl(1));
    tk_line = line;
    tk_file = fl;
    return tk_loop_of(tk_blk(outerBody));
}

// the left side of one label's `==`: the FIRST time `x` is needed and it is
// not already a bare name, this embeds the real `N_VAR` marker
// `teko_ternary.mc`'s own `tk_tern_hoist_var` hoists -- every later use is
// just a fresh, cheap read of the name it (will) declare.
i64 tk_switch_xleft(uptr xname, i64 marker, uptr usedFlag) {
    if (marker != 0 && ld64(usedFlag) == 0) {
        st64(usedFlag, 1);
        return marker;
    }
    return tk_id(xname);
}

// `x switch { ... }` -- a chain of `tk_ternary` placeholders, one per arm,
// folded from the LAST arm outward; the last arm's own condition (its guard
// included) is never tested -- it is the chain's unconditional base, which
// is why at least one `_` arm, ideally the last one written, is required.
i64 tk_switch_infix(i64 x) {
    i64 line = p_line();
    uptr fl = p_file();
    p_expect(K_LBRACE, "expected { after switch");    // the core already consumed `switch`

    i64 simple = nd_kind(x) == N_IDENT;
    uptr xname = 0;
    i64 marker = 0;
    if (simple) {
        xname = nd_name(x);
    } else {
        xname = gensym_new();
        tk_line = line;
        tk_file = fl;
        marker = tk_var(TY_I64, xname, x);
    }
    i64 markerUsed = 0;

    i64 condAt[TK_SWITCH_MAXARM];
    i64 exprAt[TK_SWITCH_MAXARM];
    i64 narm = 0;
    i64 hasDefault = 0;

    loop {
        if (p_id() == K_RBRACE) break;
        if (narm == TK_SWITCH_MAXARM) err_at(fl, line, "teko: too many switch expression arms");
        i64 lline = p_line();
        uptr lfl = p_file();
        i64 cond = 0;
        if (tk_kw("_")) {
            p_next();
            cond = tk_int(1);
            hasDefault = 1;
        } else {
            i64 v = tk_switch_label_val(lfl, lline);
            cond = tk_bin(K_EQ, tk_switch_xleft(xname, marker, &markerUsed), v);
            loop {
                if (!tk_kw("or")) break;
                p_next();
                i64 lline2 = p_line();
                uptr lfl2 = p_file();
                i64 v2 = tk_switch_label_val(lfl2, lline2);
                cond = tk_bin(K_OROR, cond, tk_bin(K_EQ, tk_switch_xleft(xname, marker, &markerUsed), v2));
            }
        }
        if (tk_word("when")) {
            p_next();
            cond = tk_bin(K_ANDAND, cond, parse_expr(0));
        }
        p_expect(K_ARROW, "expected '=>' in a switch arm");
        i64 e = parse_expr(0);
        st64(condAt + narm * 8, cond);
        st64(exprAt + narm * 8, e);
        narm = narm + 1;
        if (!p_accept(K_COMMA)) break;
    }
    p_expect(K_RBRACE, "expected } after switch arms");
    if (narm == 0) err_at(fl, line, "teko: a switch expression needs at least one arm");
    if (!hasDefault) err_at(fl, line, "teko: a switch expression needs a `_` arm");

    i64 chain = ld64(exprAt + (narm - 1) * 8);
    i64 i = narm - 2;
    loop {
        if (i < 0) break;
        i64 c = ld64(condAt + i * 8);
        i64 e = ld64(exprAt + i * 8);
        tk_line = line;
        tk_file = fl;
        chain = tk_call("tk_ternary", list_append(list_append(c, e), chain));
        tk_ntern = tk_ntern + 1;
        i = i - 1;
    }
    return chain;
}
