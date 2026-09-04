// teko_stmt.mc -- statement-position words teko adds beyond the core's own
// `if`/`loop`/`break`/`continue`/`return`/typed-local grammar (all inherited
// unchanged, D213). `var`/`const` (BindKind, DECISION_LOG's "so existe VAR e
// CONST") and `match`/`when` are still future entregas (docs/design/
// port-teko-mc.md §3): each word is reserved and stops with a clear message
// instead of falling through to the core's "not a statement".

i64 tk_stop_var()   { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: var not taught yet");   return 0; }
i64 tk_stop_const() { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: const not taught yet"); return 0; }
i64 tk_stop_match()  { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: match not taught yet"); return 0; }
i64 tk_stop_when()   { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: when not taught yet");  return 0; }
