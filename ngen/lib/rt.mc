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

// the method table of interface `id` inside the class whose vtable is `vt`.
// The vtable's word 0 is the class's interface table, `{ count, (id, methods)* }`
// in declaration order (ngen/teko_iface.mc), and an interface call is
// `callp(ld64(tk_itab(ld64(obj), ID) + j * 8), obj, ...)` -- a linear walk over
// a table with one row per interface the class declares, so a class implements
// an interface at any vtable slot and two unrelated classes answer the same
// interface.
uptr tk_itab(uptr vt, i64 id) {
    uptr t = ld64(vt);
    if (t == 0) rt_panic("interface dispatch on a class with no interface table");
    i64 n = ld64(t);
    i64 i = 0;
    loop {
        if (i >= n) break;
        if (ld64(t + 8 + i * 16) == id) return ld64(t + 16 + i * 16);
        i = i + 1;
    }
    rt_panic("interface not implemented by this class");
    return 0;
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

// the argument list of a `params` call (teko_params.mc). `total(a, b, c)` is
// rewritten into `total(tk_va3(a, b, c), 3)`: the packer writes the arguments
// into this buffer and hands back its address, and the callee reads them with
// tk_va_at. Twelve slots because MAXPARAMS is twelve -- a call cannot carry a
// thirteenth argument to pack.
//
// STATIC, so it does NOT reenter: the compiler refuses a variadic call nested
// in another one, and one inside the body of a variadic function, which is
// exactly the pair of shapes that would overwrite a list still being read. Each
// packer delegates to the one below it, so the twelve are a single chain and
// the offset of a slot is written in exactly one place.
u8 tk_va_buf[96];

uptr tk_va1(i64 a0) { st64(tk_va_buf, a0); return tk_va_buf; }
uptr tk_va2(i64 a0, i64 a1) { tk_va1(a0); st64(tk_va_buf + 8, a1); return tk_va_buf; }
uptr tk_va3(i64 a0, i64 a1, i64 a2) { tk_va2(a0, a1); st64(tk_va_buf + 16, a2); return tk_va_buf; }
uptr tk_va4(i64 a0, i64 a1, i64 a2, i64 a3) { tk_va3(a0, a1, a2); st64(tk_va_buf + 24, a3); return tk_va_buf; }
uptr tk_va5(i64 a0, i64 a1, i64 a2, i64 a3, i64 a4) { tk_va4(a0, a1, a2, a3); st64(tk_va_buf + 32, a4); return tk_va_buf; }
uptr tk_va6(i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5) { tk_va5(a0, a1, a2, a3, a4); st64(tk_va_buf + 40, a5); return tk_va_buf; }
uptr tk_va7(i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5, i64 a6) { tk_va6(a0, a1, a2, a3, a4, a5); st64(tk_va_buf + 48, a6); return tk_va_buf; }
uptr tk_va8(i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5, i64 a6, i64 a7) { tk_va7(a0, a1, a2, a3, a4, a5, a6); st64(tk_va_buf + 56, a7); return tk_va_buf; }
uptr tk_va9(i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5, i64 a6, i64 a7, i64 a8) { tk_va8(a0, a1, a2, a3, a4, a5, a6, a7); st64(tk_va_buf + 64, a8); return tk_va_buf; }
uptr tk_va10(i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5, i64 a6, i64 a7, i64 a8, i64 a9) { tk_va9(a0, a1, a2, a3, a4, a5, a6, a7, a8); st64(tk_va_buf + 72, a9); return tk_va_buf; }
uptr tk_va11(i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5, i64 a6, i64 a7, i64 a8, i64 a9, i64 a10) { tk_va10(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9); st64(tk_va_buf + 80, a10); return tk_va_buf; }
uptr tk_va12(i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5, i64 a6, i64 a7, i64 a8, i64 a9, i64 a10, i64 a11) { tk_va11(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10); st64(tk_va_buf + 88, a11); return tk_va_buf; }

// xs[i] of a `params` list: the i-th word of the packed buffer
i64 tk_va_at(uptr xs, i64 i) {
    return ld64(xs + i * 8);
}
