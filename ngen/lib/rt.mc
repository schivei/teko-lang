// rt.mc -- the runtime a teko-taught compiler's PROGRAMS link against: a
// bump arena, the print helpers, and (since entrega 2) the primitive
// operations `str`/`f32`/`f64` need that are ordinary functions rather than
// parser hooks -- trimmed from examples/lang/lib/rt.mc to what this entrega
// needs. Program code, not compiler code: `#include`d from the `.tk` source
// itself, so it compiles under the SAME taught vocabulary as that source
// (`str`/`char`/`f64`/... are already reserved program-wide by the time any
// file is parsed, teko.mc's `user_init`, docs/surface.md § Tier 3). Reference
// counting arrives with the class system (a later entrega); until then there
// is nothing to free, only to bump-allocate.
//
// tests/hello.tk does not call any of this yet -- list/collections are
// staged for a later entrega (docs/design/port-teko-mc.md §6 step A5).

#include <sys>

#define RT_ARENA 4194304   // 4 MiB, in __bss -- the same floor examples/lang uses

u8  rt_heap[RT_ARENA];
i64 rt_hp = 0;

// aborts with a message on stderr; there is no recovery
void rt_panic(uptr msg) {
    write(2, "teko: ", 6);
    write(2, msg, strlen(msg));
    write(2, "\n", 1);
    exit(70);
}

// bump-allocates `n` bytes, 16-byte aligned; panics on exhaustion instead of
// growing -- a fixed static heap, like the core's own arena.mc
uptr rt_alloc(i64 n) {
    i64 sz = (n + 15) & ~15;
    if (rt_hp + sz > RT_ARENA) rt_panic("arena exhausted");
    uptr p = rt_heap + rt_hp;
    rt_hp = rt_hp + sz;
    return p;
}

// length of a NUL-terminated `str`. teko's `str` names the same `uptr` mc
// already gives a C string (`type_alias`, teko_type.mc) -- no separate
// length field -- so this is `strlen` under teko's own spelling, call-
// compatible with it because the alias IS the identity, not a new type.
i64 tk_str_len(str s) {
    return strlen(s);
}

// a view of `s` starting at byte `from`, to the same NUL terminator: pointer
// arithmetic only, zero copy (D197 -- surfacing a view must never regress
// into a copy).
str tk_str_slice(str s, i64 from) {
    return s + from;
}

// the eight bytes `wrap`/`unwrap` (DECISION_LOG D131/D132) name for a
// reinterpret between an `f64` and its bit pattern: no cast does this (a
// `(u64) x` cast on a float VALUE converts it, `docs/specs/M24.md`), so the
// port's own mapping table (docs/design/port-teko-mc.md §3) names the
// mechanism as a round trip through `ld64`/`st64` at a shared offset,
// through the `<float>` library's own `ldf64`/`stf64` accessors
// (teko_float.mc) -- no magic, no compiler intrinsic beyond what `<float>`
// already registers. The generic `ptr::unwrap<T>`/`.wrap<T>()` spelling
// needs record/replay generics (a later, "tipos" entrega, D214); this is the
// concrete instance primitives need now.
u8 tk_f64_bits_scratch[8];

u64 tk_f64_bits(f64 x) {
    stf64(tk_f64_bits_scratch, x);
    return ld64(tk_f64_bits_scratch);
}

f64 tk_f64_from_bits(u64 bits) {
    st64(tk_f64_bits_scratch, bits);
    return ldf64(tk_f64_bits_scratch);
}
