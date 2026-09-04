// rt.mc -- the runtime a teko-taught compiler's PROGRAMS link against: a
// bump arena and the print helpers, trimmed from examples/lang/lib/rt.mc to
// what this entrega needs. Program code, not compiler code: it compiles
// with the CORE language only (docs/build.md), so it is unaffected by
// whichever teko hooks are or are not taught yet. Reference counting
// arrives with the class system (a later entrega); until then there is
// nothing to free, only to bump-allocate.
//
// tests/hello.tk does not call any of this yet -- it is staged for the
// entrega that needs allocation (str/list, docs/design/port-teko-mc.md §6
// step A5).

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
