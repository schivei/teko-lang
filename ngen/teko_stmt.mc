// teko_stmt.mc -- statement-position words teko adds beyond the core's own
// `if`/`loop`/`break`/`continue`/`return`/typed-local grammar (all inherited
// unchanged, D213). `var`/`const` (BindKind, DECISION_LOG's "so existe VAR e
// CONST") and `match`/`when` are still future entregas (docs/design/
// port-teko-mc.md §3): each word is reserved and stops with a clear message
// instead of falling through to the core's "not a statement".
//
// And the BLOCK, which is not a word teko adds but a position teko has to own:
// `K_LBRACE` is not a core keyword, so `syntax_stmt("{")` makes this module the
// owner of EVERY block -- one in statement position, a function body and a
// `#rule`'s `block` hole alike (mc docs/reference/hooks.md § syntax_stmt, M21.5).
// That is what gives teko_struct.mc's table of locals a SCOPE, which it did not
// have: a name declared inside a block stops answering at the `}` that closes
// it, so `if (1) { Ledger s = ...; }` no longer decides what the OUTER `s` is.

i64 tk_stop_var()   { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: var not taught yet");   return 0; }
i64 tk_stop_const() { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: const not taught yet"); return 0; }
i64 tk_stop_match()  { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: match not taught yet"); return 0; }
i64 tk_stop_when()   { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: when not taught yet");  return 0; }

// `{ statement... }`, the core's own block rebuilt around a scope mark. The
// statements are read one by one with parse_stmt(): calling parse_block() here
// would come straight back to this handler forever (hooks.md § parse_block).
// The node is the same N_BLOCK the core builds, at the same position and with
// the same children, so a program that declares nothing keeps the tree it had.
i64 tk_block() {
    i64 line = p_line();
    uptr fl = p_file();
    p_expect(K_LBRACE, "expected {");
    i64 mark = tk_nlocal;
    i64 head = 0;
    loop {
        if (p_id() == K_RBRACE) break;
        if (p_id() == T_EOF) err_at(fl, line, "unterminated block");
        head = list_append(head, parse_stmt());
    }
    p_next();                                    // }
    tk_nlocal = mark;                            // the block's locals die here
    i64 b = node_new(N_BLOCK, line, fl);
    set_nd_a(b, head);
    return b;
}
