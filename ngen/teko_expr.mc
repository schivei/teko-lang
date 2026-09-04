// teko_expr.mc -- expression-position words teko adds beyond the core's own
// Pratt grammar (arithmetic, comparisons, calls, casts: all inherited
// unchanged, D213). `new` (object construction) belongs to the class system
// (docs/design/port-teko-mc.md §3), a future entrega: reserved here and
// stopped with a clear message instead of falling through to the core's
// "identifier expected".

i64 tk_stop_new() { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: new not taught yet"); return 0; }
