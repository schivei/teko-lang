// teko_loop.mc -- `while`, `do ... while` and `for` as C# spells them (D218,
// D221, D226), lowered at PARSE TIME to the core's own `loop {}` / `if` /
// `break N` (kept, D221) -- exactly the shape lib/prelude.mc already shows
// for `while`/`for`, but built here through syntax_stmt so the lowering can
// run the extra rewrite `do`/`for` need (below) and so a plain `.tk` source
// gets the words without an `#include`.
//
//   while (c) stmt        ->  loop { if (!(c)) break; stmt }
//   do stmt while (c);    ->  loop { loop { stmt break; } if (!(c)) break; }
//   for (i;c;s) stmt      ->  { i loop { if (!(c)) break; loop { stmt break; } s; } }
//
// `while`'s body sits at the SAME loop depth the programmer wrote it at, so a
// `break`/`continue` inside it needs nothing done to it -- it already names
// the one loop the desugaring built. `do` and `for` are different: their own
// body is wrapped in an extra one-shot `loop { ... break; }` so that
// `continue` (which always restarts the INNERMOST loop, language.md § 4) ends
// up at the condition check / the step instead of back at the top of the
// body, which is what makes `continue` run the step in a C# `for` and jump to
// the condition in a C# `do`. That extra loop is invisible to the source, so
// every `break`/`continue` written inside such a body is walked and adjusted
// by tk_loop_rewrite_stmt before the wrapping happens (§ below).
//
// `x++;`, `x--;`, `x += e;` and `x -= e;` as their own statements -- and as a
// `for` loop's step -- are the other half of C#'s surface. The core has no
// operator-assignment and no statement starting with a bare identifier
// followed by anything but `=` (parse.mc's parse_stmt_core: past `ident =
// expr`, the only other thing tried is `rule_find` on the next token, which
// only a `#rule` populates). The clean lowering is therefore the same one
// `+=`/`-=`/`++`/`--` in lib/prelude.mc already use -- `x += e;` becomes
// `x = x + e;`, going through the ordinary assignment/binary-operator path
// (and so through whatever `operator+` a class declares, teko_ops.mc) -- and
// the `#rule` text those four lines need is pushed onto the lexer once, from
// user_init(), before a byte of the real source is read (mc docs/reference/
// hooks.md § Record and replay: p_push_source has `#include`'s own semantics,
// and driver.mc's drv_parse calls user_init() after lex_init() but before
// parse_unit()'s first token, so the pushed frame is read first and the
// lexer pops back to the real entry file on its own once it is exhausted).
// The four tokens themselves are registered directly with word_add -- the
// same call syntax/syntax_stmt/syntax_infix make internally -- so no
// `#token` line is needed in the pushed text, and a `for` step can read
// `++`/`--`/`+=`/`-=` with the plain token ids word_add hands back.

// four literals joined at runtime -- a global's initializer has to be a
// constant (mc docs/reference/language.md § Globals), and adjacent string
// literals do not concatenate the way C's do
uptr tk_loop_rule_text() {
    uptr a = "#rule stmt: ident $x += expr $e ; => $x = $x + $e;\n";
    uptr b = "#rule stmt: ident $x -= expr $e ; => $x = $x - $e;\n";
    uptr c = "#rule stmt: ident $x ++ ;         => $x = $x + 1;\n";
    uptr d = "#rule stmt: ident $x -- ;         => $x = $x - 1;\n";
    return tk_join(tk_join(a, b), tk_join(c, d));
}

i64 tk_incr_tok = 0;
i64 tk_decr_tok = 0;
i64 tk_pluseq_tok = 0;
i64 tk_minuseq_tok = 0;
i64 tk_plus_tok = 0;
i64 tk_minus_tok = 0;

// how many REAL loops (`while`/`do`/`for`) the parser is lexically inside of
// right now -- what `switch`'s own `continue` rewrite (teko_switch.mc) reads
// to tell "no enclosing loop at all" from "some real loop, maybe reached
// through the core's own out-of-range check". `switch` does not bump it: its
// own one-lap loop is never a valid `continue` target from outside itself.
i64 tk_realloop_depth = 0;

// registers the four compound-assignment tokens and pushes the `#rule` text
// that lowers the free-standing statement form; called once, from
// user_init(), before parse_unit() reads its first token.
void tk_loop_init() {
    tk_pluseq_tok = word_add("+=");
    tk_minuseq_tok = word_add("-=");
    tk_incr_tok = word_add("++");
    tk_decr_tok = word_add("--");
    tk_plus_tok = word_add("+");
    tk_minus_tok = word_add("-");
    uptr text = tk_loop_rule_text();
    p_push_source("<teko-loop-prelude>", text, cstrlen(text));
}

// ---- node builders the core has none of already (tk_if/tk_blk/tk_stmt/... are teko_struct.mc's) ----

i64 tk_loop_of(i64 body) {
    i64 n = tk_nd(N_LOOP);
    set_nd_a(n, body);
    return n;
}

i64 tk_break_lvl(i64 lvl) {
    i64 n = tk_nd(N_BREAK);
    set_nd_val(n, lvl);
    return n;
}

i64 tk_not(i64 e) {
    i64 n = tk_nd(N_UNARY);
    set_nd_op(n, K_BANG);
    set_nd_a(n, e);
    return n;
}

// `if (!(cond)) break;` -- the guard every one of the three forms opens its
// own loop with.
i64 tk_loop_guard(i64 cond) { return tk_if(tk_not(cond), tk_break_lvl(1)); }

// ---- the jump rewrite `do`/`for` need (§ header) ----
//
// Walks a body BEFORE it is wrapped in the extra one-shot loop, counting how
// many `N_LOOP`s of the body's OWN sit between it and each `break`/
// `continue`: `depth` is that count, 0 at the body's own top. A `break k`
// that already reaches past everything the body itself opened (`k > depth`)
// also has to reach past the wrapper this pass is about to add, so it becomes
// `break k+1`; one that stays within the body's own nested loops (`k <=
// depth`) is untouched -- it already names the right one.
//
// `continue k` (mc 0.14.1, level-counted the same way `break k` is) cannot
// just gain the same `+1` and stay a `continue`, though: the level that
// exactly reaches this `for`/`do` (`k == depth + 1`) would restart the
// wrapper loop itself, which skips the step/condition check entirely -- the
// same bug the ORIGINAL bare-`continue` conversion (still correct at
// `depth == 0`) was written to avoid. So that ONE level converts to
// `break depth + 1`, landing exactly where the wrapper's own trailing
// `break;` would have, right on the condition check or the step -- generalising
// the old `depth == 0 -> break 1` case, which is what `k == 1` at `depth == 0`
// reduces to. A level reaching further out (`k > depth + 1`) is not this
// for/do's business: it stays a `continue`, gains the same `+1` `break` gets
// (this for/do's own extra wrapper layer), and is left for whatever loop it
// eventually lands on -- a plain `while` (no wrapper, so a raw `continue`
// already reaches it directly, the `k <= depth` case below never triggers a
// rewrite for it) or another `for`/`do`'s own later, separate rewrite call
// (which sees it at a greater depth and repeats this same test). Applied once
// per `do`/`for`, from the innermost outward as parsing unwinds, this composes
// correctly across nesting: an inner `for`'s own rewrite already bumped what
// needed bumping for ITS wrapper, and this pass only bumps again what still
// reaches past the depth it can see -- `docs/design/plano-ngen-entrega4.md`
// §29 walks the two-level nested case this relies on.
void tk_loop_rewrite_stmt(i64 n, i64 depth);

void tk_loop_rewrite_list(i64 n, i64 depth) {
    loop {
        if (n == 0) break;
        tk_loop_rewrite_stmt(n, depth);
        n = nd_next(n);
    }
}

void tk_loop_rewrite_stmt(i64 n, i64 depth) {
    if (n == 0) return;
    i64 k = nd_kind(n);
    if (k == N_BLOCK) { tk_loop_rewrite_list(nd_a(n), depth); return; }
    if (k == N_LOOP) { tk_loop_rewrite_stmt(nd_a(n), depth + 1); return; }
    if (k == N_IF) {
        tk_loop_rewrite_stmt(nd_b(n), depth);
        tk_loop_rewrite_stmt(nd_c(n), depth);
        return;
    }
    if (k == N_BREAK) {
        i64 lvl = nd_val(n);
        if (lvl > depth) set_nd_val(n, lvl + 1);
        return;
    }
    if (k == N_CONTINUE) {
        i64 lvl = nd_val(n);
        if (lvl == 0) lvl = 1;
        if (lvl == depth + 1) {
            set_nd_kind(n, N_BREAK);
            set_nd_val(n, depth + 1);
            return;
        }
        if (lvl > depth + 1) set_nd_val(n, lvl + 1);
    }
}

// ---- while (cond) stmt ----
i64 tk_while() {
    i64 line = p_line();
    uptr fl = p_file();
    p_next();                                    // `while`
    p_expect(K_LPAR, "expected ( after while");
    i64 cond = parse_expr(0);
    p_expect(K_RPAR, "expected ) after while condition");
    tk_realloop_depth = tk_realloop_depth + 1;
    i64 body = parse_stmt();
    tk_realloop_depth = tk_realloop_depth - 1;
    tk_line = line;
    tk_file = fl;
    return tk_loop_of(tk_blk(list_append(tk_loop_guard(cond), body)));
}

// ---- do stmt while (cond); ----
i64 tk_do() {
    i64 line = p_line();
    uptr fl = p_file();
    p_next();                                    // `do`
    tk_realloop_depth = tk_realloop_depth + 1;
    i64 body = parse_stmt();
    tk_realloop_depth = tk_realloop_depth - 1;
    i64 wtok = word_add("while");
    p_expect(wtok, "expected while after do body");
    p_expect(K_LPAR, "expected ( after do-while");
    i64 cond = parse_expr(0);
    p_expect(K_RPAR, "expected ) after do-while condition");
    p_expect(K_SEMI, "expected ; after do-while");
    tk_loop_rewrite_stmt(body, 0);
    tk_line = line;
    tk_file = fl;
    i64 once = tk_loop_of(tk_blk(list_append(body, tk_break_lvl(1))));
    return tk_loop_of(tk_blk(list_append(once, tk_loop_guard(cond))));
}

// `i`, `i++`, `i--`, `i += k`, `i -= k`, `i = expr` or a bare call, read up to
// the `)` that closes the `for` clauses -- there is no trailing `;` here for
// parse_stmt() to expect, which is why the step is not read through it.
i64 tk_for_step() {
    i64 line = p_line();
    uptr fl = p_file();
    i64 ex = parse_expr(0);
    i64 tok = p_id();
    if (tok == K_ASSIGN) {
        if (nd_kind(ex) != N_IDENT) err_at(fl, line, "teko: the left side of a for step must be a name");
        uptr name = nd_name(ex);
        p_next();
        i64 v = parse_expr(0);
        tk_line = line;
        tk_file = fl;
        i64 n = tk_nd(N_ASSIGN);
        set_nd_name(n, name);
        set_nd_a(n, v);
        return n;
    }
    if (tok == tk_pluseq_tok || tok == tk_minuseq_tok) {
        if (nd_kind(ex) != N_IDENT) err_at(fl, line, "teko: the left side of a for step must be a name");
        uptr name = nd_name(ex);
        i64 op = tk_plus_tok;
        if (tok == tk_minuseq_tok) op = tk_minus_tok;
        p_next();
        i64 v = parse_expr(0);
        tk_line = line;
        tk_file = fl;
        i64 n = tk_nd(N_ASSIGN);
        set_nd_name(n, name);
        set_nd_a(n, tk_bin(op, tk_id(name), v));
        return n;
    }
    if (tok == tk_incr_tok || tok == tk_decr_tok) {
        if (nd_kind(ex) != N_IDENT) err_at(fl, line, "teko: `++`/`--` need a name");
        uptr name = nd_name(ex);
        i64 op = tk_plus_tok;
        if (tok == tk_decr_tok) op = tk_minus_tok;
        p_next();
        tk_line = line;
        tk_file = fl;
        i64 n = tk_nd(N_ASSIGN);
        set_nd_name(n, name);
        set_nd_a(n, tk_bin(op, tk_id(name), tk_int(1)));
        return n;
    }
    tk_line = line;
    tk_file = fl;
    return tk_stmt(ex);
}

// ---- for (init; cond; step) stmt ----
i64 tk_for() {
    i64 line = p_line();
    uptr fl = p_file();
    p_next();                                    // `for`
    p_expect(K_LPAR, "expected ( after for");
    i64 init = 0;
    if (p_id() == K_SEMI) p_next();
    else init = parse_stmt();                    // a declaration or `expr ;`, own `;` included
    i64 cond = 0;
    if (p_id() != K_SEMI) cond = parse_expr(0);
    p_expect(K_SEMI, "expected ; after for condition");
    i64 step = 0;
    if (p_id() != K_RPAR) step = tk_for_step();
    p_expect(K_RPAR, "expected ) after for clauses");
    tk_realloop_depth = tk_realloop_depth + 1;
    i64 body = parse_stmt();
    tk_realloop_depth = tk_realloop_depth - 1;
    tk_loop_rewrite_stmt(body, 0);
    tk_line = line;
    tk_file = fl;
    i64 once = tk_loop_of(tk_blk(list_append(body, tk_break_lvl(1))));
    i64 head = 0;
    if (cond != 0) head = tk_loop_guard(cond);
    if (head == 0) head = once;
    else head = list_append(head, once);
    if (step != 0) head = list_append(head, step);
    i64 outer = tk_loop_of(tk_blk(head));
    if (init == 0) return outer;
    return tk_blk(list_append(init, outer));      // the init's own scope: this block, and no more
}
