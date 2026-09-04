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
lang.mc`) adds only the delta:

| hook | word(s) | what it does |
|---|---|---|
| `type_alias` | `bool` | the one primitive the core does not already have (`teko_type.mc`) |
| `syntax` | `class` `type` `interface` `namespace` `import` `using` | reserved, honest-stop (`teko_class.mc`) |
| `syntax_stmt` | `var` `const` `match` `when` | reserved, honest-stop (`teko_stmt.mc`) |
| `syntax_expr` | `new` | reserved, honest-stop (`teko_expr.mc`) |

"Honest-stop" means the word is registered (so a `.tk` source that reaches for it
gets `teko: <word> not taught yet` instead of the core's generic "type expected
at top level") but does not yet lower to anything — those are later entregas
(`docs/design/port-teko-mc.md` §3/§6).

`lib/rt.mc` is the runtime `mc`-taught teko **programs** link against: a bump
arena and the print/panic helpers, trimmed from `examples/lang/lib/rt.mc` to what
this entrega needs (no reference counting yet — that arrives with the class
system). `tests/hello.tk` does not call it yet; it is staged for the entrega that
needs allocation.

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
changes either.

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

What was validated locally instead, with `mc0` (the frozen C seed, built via
`make stage0` in the `mc` clone): `mc0` compiling `mc`'s own `src/core.mc` +
this directory's `host_linux_x86_64.mc` + `teko.mc` (with all four hook
modules) into a Mach-O object, exit 0 — the strongest static check `mc0` alone
can give, since it cannot self-host `mc build` (M14, self-hosted-only) or run a
Mach-O binary on this Linux sandbox. `tests/hello.tk` was cross-checked two
ways: compiled by *plain* `mc0` (no teko hooks), it fails exactly at `bool` —
`type expected at top level` — proving the fixture genuinely exercises the one
hook this entrega teaches; and, with `bool` swapped for `i64`, the rest of its
control flow (the function call, the comparison, the `if`/`return`) compiles
clean, exit 0.
