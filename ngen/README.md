# ngen — teko ported onto mc

`src/` is **frozen** (DECISION_LOG.md D211/D212). teko no longer grows there: every
new construct from here on is taught to [`mc`](https://github.com/schivei/mc) as a
module of its own, the same way `mc`'s `examples/lang` teaches it a class system
without touching `mc`'s `src/`. This directory is that module, built with `mc`'s own
project tooling (`mc.toml`, `mc build`, `[compiler]`).

Design: `docs/design/port-teko-mc.md`. Rationale and rulings: `DECISION_LOG.md`
D211 (the port), D212 (this directory replaces the discarded `ngen/` sketch),
D213 (reuse the mc core's own syntax; teach only the delta).

## What this entrega teaches

D213 (dono 2026-09-04): the mc core already parses functions, primitive types,
`if`/`loop`/`return` and the whole expression grammar — `examples/lang` does not
re-teach any of that either, it only adds classes/generics/interfaces on top. So
the core's own C-flavoured spelling (`i64 name(params) { ... return e; }`) **is**
the teko-over-mc spelling here, unchanged. `teko.mc` (mirroring `examples/lang/
lang.mc`) adds only the delta, entrega by entrega:

| hook | word(s) | what it does | entrega |
|---|---|---|---|
| `type_alias` | `bool` | the one primitive the core did not already have (`teko_type.mc`) | 1 |
| `syntax` | `class` `type` `interface` `namespace` `import` `using` | reserved, honest-stop (`teko_class.mc`) | 1 |
| `syntax_stmt` | `var` `const` `match` `when` | reserved, honest-stop (`teko_stmt.mc`) | 1 |
| `syntax_expr` | `new` | reserved, honest-stop (`teko_expr.mc`) | 1 |
| `type_alias` | `char` `byte` `isize` `usize` `ptr` `str` | more aliases over core type ids — no new representation (`teko_type.mc`) | 2 |
| library `<float>` (M24) | `f32` `f64` | the bundled `<float>` library, wired (not reimplemented) into `f32`/`f64` (`teko_float.mc`) | 2 |

"Honest-stop" means the word is registered (so a `.tk` source that reaches for it
gets `teko: <word> not taught yet` instead of the core's generic "type expected
at top level") but does not yet lower to anything — those are later entregas
(`docs/design/port-teko-mc.md` §3/§6).

Every entrega-2 primitive is a *reuse*, not a new mechanism (D213): `u8/u16/u32/
u64/i64/uptr/void` need no entry at all (already the same word the core gives);
`isize`/`usize` collapse to `i64`/`u64` (DECISION_LOG D131: alias IS identity, so
the "implicit coercion" the teko surface calls for costs nothing — there is no
second type to convert between); `byte` is `u8`; `char` is the mc host's own
scalar `u32` (not teko-classic's fat char — D213/D205, functionality over
syntactic fidelity); `ptr` collapses to the same single opaque pointer the core
already names `uptr` (this core draws no signed/unsigned pointer distinction to
preserve); `str` is the same `uptr` mc's own C-flavoured strings already are.
`f32`/`f64` are the one case that is not an alias: `<float>` (M24, already
proven upstream by `mc/tests/float/` and `scripts/check-float.sh`) registers two
new machine-backed types via `type_new`, spelled `f32`/`f64` in the library
itself, so `teko_float.mc` only wires `float_init()` and the two derived machine
tables — nothing is re-taught.

`lib/rt.mc` is the runtime `mc`-taught teko **programs** link against: a bump
arena, the print/panic helpers, and — since entrega 2 — the ordinary functions
the new primitives need that are not parser hooks: `tk_str_len`/`tk_str_slice`
(pointer arithmetic only, a zero-copy view — DECISION_LOG D197 forbids
surfacing a view as a function that copies) and `tk_f64_bits`/`tk_f64_from_bits`,
the concrete instance `wrap`/`unwrap` (D131/D132) needs for a bit-exact
`f64`↔`u64` reinterpret — a round trip through `ld64`/`st64` at a shared offset,
because no cast does this (`(u64) x` on a float *value* converts it, it does not
reinterpret its bits; `mc/docs/specs/M24.md`). The fully generic
`ptr::unwrap<T>`/`.wrap<T>()` spelling needs record/replay generics, which is a
later, "tipos" entrega (D214); this is the concrete realization primitives need
now. Reference counting arrives with the class system (a later entrega too).

## Building

Needs the [`mc`](https://github.com/schivei/mc) toolchain on `PATH` (`.github/
workflows/ngen.yml` always fetches the latest release). From the repository root:

```
mc build ngen
ngen/build/teko-hello   # exits 42
```

Two steps come out of `mc build` (`docs/build.md` in the `mc` repository):
first it links `teko.mc` into a taught compiler (`ngen/build/mc-teko`), then it
uses THAT binary to compile `ngen/tests/hello.tk` into `ngen/build/teko-hello`.
Nothing in `mc`'s own `src/` changes, and nothing in this repository's `src/`
changes either. `.github/workflows/ngen.yml` then uses that same `ngen/build/
mc-teko` directly (`--exe`, bypassing `mc.toml`'s single `[project]` entry) to
compile and run each `ngen/tests/primitives_*.tk` and `ngen/tests/types_*.tk` fixture, one
process per fixture — see § Fixtures below.

## Fixtures (entregas 1-5)

Each fixture is a program the taught compiler builds and RUNS on the five CI legs; the
`// expect-exit: N` header is the oracle. Generated from `ngen/tests/*.tk` at drain time.

| fixture | exit |
|---|---|
| `tests/hello.tk` | 42 |
| `tests/primitives_float.tk` | 42 |
| `tests/primitives_ptr.tk` | 42 |
| `tests/primitives_scalar.tk` | 42 |
| `tests/primitives_str.tk` | 42 |
| `tests/surface_abstract.tk` | 42 |
| `tests/surface_default_method.tk` | 42 |
| `tests/surface_generics.tk` | 42 |
| `tests/surface_iface_default.tk` | 42 |
| `tests/surface_operator.tk` | 42 |
| `tests/surface_overload_free.tk` | 42 |
| `tests/surface_overload_method.tk` | 42 |
| `tests/surface_params.tk` | 42 |
| `tests/surface_partial.tk` | 42 |
| `tests/surface_property.tk` | 42 |
| `tests/surface_reclaim.tk` | 42 |
| `tests/surface_scope.tk` | 42 |
| `tests/surface_typeof_expr.tk` | 42 |
| `tests/surface_typeof_param.tk` | 42 |
| `tests/types_class.tk` | 42 |
| `tests/types_interface.tk` | 42 |
| `tests/types_struct.tk` | 42 |
| `tests/types_trait.tk` | 42 |


## Why the CI is a separate workflow

`.github/workflows/ngen.yml` builds only with `mc` — none of the gen0→gen3 `.c`
escada the rest of this repository's CI runs, because `ngen/` does not touch
`src/` at all. It triggers on `ngen/**` and on itself.

## Local validation without network (this entrega)

The sandbox this entrega was authored in has no access to `github.com` (403,
organization egress policy) and no pre-existing `mc` binary, so neither of
`mc`'s two normal local paths — downloading a release, or `make bootstrap`
against `/home/user/schivei/mc`'s own clone — produces a *runnable* `mc` there:
`make bootstrap` is the macOS chain (frozen C seed emits Mach-O only); the
Linux chain, `make bootstrap-linux`, itself needs an *existing* `mc` binary as
its seed (`scripts/bootstrap-linux.sh`'s own header: "there is no `mc0` here").
That gap closes in CI, which has network access to fetch the release the same
way `ngen.yml` does.

**Wiring, with `mc0`.** `mc0` compiling `mc`'s own `src/core.mc` + this
directory's `host_linux_x86_64.mc` + `teko.mc` (all six hook modules,
`teko_float.mc` included, `#include <name>` swapped for the equivalent quoted
relative path since `mc0` has no Tier 3) into a Mach-O object, exit 0. This is
a *real* check on entrega 2's own code, not just a syntax check: `type_alias`,
`float_init`, `machine_arm64_float_init` and `machine_x86_64_float_init` are
ordinary functions textually present in the same compilation (`hooks.mc`,
`lib/float.mc`, the two `lib/machine_*_float.mc`), so `mc0`'s frontend
type-checks every call this entrega adds to `user_init()` against their real
signatures — exactly the assembly `mc build ngen` performs in CI, core first.

**Primitives, by two different arguments — `mc0` alone cannot judge a `.tk`
fixture using a taught word**, `type_alias`/`type_new` being Tier 3 (`.mc`-only,
`docs/surface.md`): `mc0` has no dynamic word table, so it rejects `bool`,
`char`, `str`, `f64`, … identically, independent of what a module registers.
That gap is what CI's downloaded, self-hosted release binary closes (same as
entrega 1's `bool`); locally, the argument splits by whether the new word is a
`type_alias` (an existing core type under a new name, D213) or a `type_new`
(a genuinely new, machine-backed type — only `f32`/`f64` in this entrega):

- **`char`/`byte`/`isize`/`usize`/`ptr`/`str`** (`tests/primitives_scalar.tk`,
  `primitives_ptr.tk`, `primitives_str.tk`, and `lib/rt.mc`'s `tk_str_len`/
  `tk_str_slice`): each fixture, compiled by *plain* `mc0`, fails exactly at
  the new word (`type expected at top level` / `in parameter`) — proof the
  fixture genuinely exercises a hook `mc0` alone does not have. Then, since
  `type_alias` touches nothing but `type_of_token` (`docs/surface.md` — the
  alias *is* the aliased type, not a converted one), a scratch copy with every
  teko word replaced by its core base (`isize`→`i64`, `usize`→`u64`,
  `byte`→`u8`, `char`→`u32`, `ptr`→`uptr`, `str`→`uptr`) is *the same program*
  once hooked: `mc0` compiles that copy clean, exit 0, for all four fixtures
  and for `lib/rt.mc`'s two `str` functions in isolation.
- **`f32`/`f64`** (`tests/primitives_float.tk`, `lib/rt.mc`'s `tk_f64_bits`/
  `tk_f64_from_bits`): `type_new` is not an alias — an `f64` is a distinct,
  machine-backed type (SSE2/NEON), so no core substitution preserves its
  semantics, and the wiring check above is as far as `mc0` can independently
  go. What closes the rest of the gap without running anything: `<float>`'s
  `ldf64`/`stf64`/`ld64`/`st64` accessors are exactly the shape `mc`'s own
  proven test corpus already uses for the identical reinterpret idiom
  (`mc/tests/float/018-array.mc`, part of `scripts/check-float.sh`'s legs) —
  `tk_f64_bits`/`tk_f64_from_bits` call them the same way, one store and one
  load through a shared eight-byte scratch cell. Running `primitives_float.tk`
  end to end needs the same runnable, self-hosted `mc` the § above is missing;
  CI has it.
