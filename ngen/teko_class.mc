// teko_class.mc -- top-level declarations teko adds beyond a plain function
// or global. `class`/`type`/`interface`/`namespace`/`import`/`using` are
// real teko surface (docs/design/port-teko-mc.md §3), but D211 §6 A3-A4
// stages them for a later entrega -- this file only reserves the six words
// and stops with a clear message, so a `.tk` source that reaches for one of
// them gets `teko: <word> not taught yet` instead of the core's generic
// "type expected at top level".
//
// Everything mc's core already parses on its own -- functions, primitive
// types, `return`, the whole expression grammar -- is NOT re-taught here
// (D213, dono 2026-09-04): the core's own C-flavoured spelling IS the
// teko-over-mc spelling for those, until a later entrega decides otherwise.

void tk_stop_class()     { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: class not taught yet"); }
void tk_stop_type()      { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: type not taught yet"); }
void tk_stop_interface() { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: interface not taught yet"); }
void tk_stop_namespace() { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: namespace not taught yet"); }
void tk_stop_import()    { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: import not taught yet"); }
void tk_stop_using()     { i64 l = p_line(); uptr f = p_file(); p_next(); err_at(f, l, "teko: using not taught yet"); }
