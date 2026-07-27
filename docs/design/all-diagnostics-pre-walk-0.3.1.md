# All-diagnostics: the six checker PRE-WALK passes (0.3.1, issue #68)

**Ruling (owner, 0.3.1).** The compiler reports **every** diagnostic of a build in **one**
execution. It does not abort at the first error.

That ruling is already honoured at two boundaries:

* the **FILE boundary** — `src/build/assemble.tks` collects one lex/parse diagnostic per file and
  carries them on `ParsedFront.diags` instead of raising, so the checker still runs over the files
  that did parse;
* the **DECLARATION boundary** — `typer.tks::type_program_with_deps_pre_mono`'s item walk records
  `diag_text_at(...)` per failing top-level declaration on `PreMono.diags` and continues to the
  next item.

It is **not** honoured by the passes that run **before** that item walk. Each of those still
`return`s its first error, and that first error masks every diagnostic behind it — including all
of the item walk's. This document names those passes, states the evidence, and fixes the
**cut boundary** of each one.

## 1. The six pre-walk passes

They are the fallible calls in `typer.tks::type_program_with_deps_pre_mono`, in call order
(`canon_class_bases` and `instantiate_types` are infallible and are not passes in this sense;
`seed_from_dep` is fallible but consumes an **already-checked dependency**, so it can never carry
a diagnostic about the project under compilation — it is a dependency-integrity check, not a
project pass).

| # | Pass | Declared at | Produces | Today |
|---|------|-------------|----------|-------|
| 1 | `fold_traits` | `src/checker/collect.tks:1500` | the trait-folded `parser::Program` | first error aborts |
| 2 | `collect_with_seed` / `collect` | `src/checker/collect.tks:1761` / `:1730` | `(TypeTable, Env)` | first error aborts |
| 3 | `check_modules` | `src/checker/check_modules.tks:192` | nothing (pure validator) | first error aborts |
| 4 | `check_di` | `src/checker/di.tks:222` | nothing (pure validator) | first error aborts |
| 5 | `build_di_registry` | `src/checker/di.tks:57` | `DiRegistry` | first error aborts |
| 6 | `register_instance_methods` | `src/checker/collect.tks:301` | `Env` | first error aborts |

None of the six accumulates today. Every one of them is a `return e` on its first failing
sub-check.

## 2. The evidence — measured, not assumed

`examples/regressions/const_ns_qualified_visibility_rejected` is the checker-stage compile-fail
fold host: thirty-seven rejected constructs, one namespace per construct, **one** failing build.
Four further constructs sit outside that fold, in `cases/*.tks`, each paying for its **own** build,
with the file's own header saying why: *"their diagnostics come from a checker PRE-WALK pass, which
still stops at the FIRST error"*.

Folding all four back in and building (gen1 = `teko 0.3.0.30-beta` built from this branch's seed)
reproduces the mask exactly. Parking one case at a time and rebuilding gives the whole chain:

| folded cases present | the ONLY diagnostic emitted | emitted by |
|---|---|---|
| c55, c25, c42, c53 | `struct/class 'Wrapper' has an unsafe-typed field 'buf' …` | `collect` → `validate_type_decls` → `reject_unsafe_field_contagion` (`collect.tks:1597`) |
| c25, c42, c53 | `` a `_` discard parameter is only allowed when implementing … `` | `collect` → `func_type` (`collect.tks:90`) |
| c42, c53 | `` a reference cannot target another reference (`Ref<Ref<T>>` is invalid) `` | `collect` → `func_type` → `resolve_type` → `resolve_generic_inst` (`resolve.tks:1883`) |
| c53 | `src/c53/case.tks:14:7: a flags type may declare at most 64 members …` | `check_modules` → `check_flags_member_cap` (`check_modules.tks:185`) |
| (none) | all 39 item-walk diagnostics | the declaration-boundary item walk |

So the residual is real, it is exactly the shape the ruling forbids, and it is produced by pass 2
(`collect`, three of the four) and pass 3 (`check_modules`, one of the four).

### 2.1 What the four `cases/*.tks` rows are NOT

They are not scenarios that *chose* a private build. Read the table above again: with all four
folded in, the build emits **one** diagnostic and hides thirty-seven. Moving those files into the
project without changing the compiler does not save four builds — it turns thirty-seven passing
scenarios into thirty-seven failures, because the diagnostics they pin are never printed. The
regressor arrangement is downstream of the compiler behaviour, not the other way round.

The DECLARATION-boundary item walk, by contrast, *had* already been converted (owner ruling
2026-07-26, `PreMono.diags`): that is exactly why thirty-seven scenarios already shared one build.
The ruling was half-implemented — the item walk, yes; the six passes before it, no.

## 2.2 After — measured on the same fixtures

Rebuilt with the pre-walk passes collecting:

| fixture arrangement | diagnostics in ONE build |
|---|---|
| all four `cases/*.tks` folded into `src/` | all FOUR pre-walk diagnostics (was: one) |
| c55 + c53 folded, c25 + c42 left standalone | all THIRTY-NINE folded scenarios (52 diagnostic lines), every pinned substring present |
| c25 + c42 merged into ONE `cases/prewalk_signature_stops.tks` | BOTH signature-walk diagnostics |

The last row is the HARD boundary at work: `c25` and `c42` are rejected by the signature walk, which
leaves an undefined binding behind, so the build cuts before the item walk and cannot report the
thirty-nine alongside them. They can, however, share one build with each other — the signature walk
collects both — and a standalone `Given source` build is cached per (file, declared env)
(`regr_src_key`).

Build delta for `const_ns_qualified_visibility_rejected`:

| | before | after |
|---|---|---|
| the project's own failing build | 1 | 1 |
| pre-walk `cases/*.tks` builds | 4 | 1 |
| `F8 target` config build (`TEKO_TARGET=x86_64-solaris`, cached across 3 scenarios) | 1 | 1 |
| **total** | **6** | **3** |

Three builds saved, not five. The `F8 target` build is not attributable to this issue: its subject
IS the build configuration, and a differing configuration is a different build by construction.

## 3. The cut boundary of each pass

A pass may keep collecting only while **the state it produces stays usable by the next pass**.
When an error poisons that state — a name that did not resolve, whose type the following pass
would read — the pass accumulates internally and then **cuts at its own boundary**; it never hands
invalid state forward.

That gives two boundary classes:

* **SOFT** — the pass produces nothing the next pass reads, or produces it completely regardless of
  the failure. It accumulates every diagnostic and **continues** into the item walk, so its
  diagnostics are reported together with the declaration-boundary ones in the same build.
* **HARD** — the pass produces state that a failure leaves incomplete. It accumulates every
  diagnostic **within** the pass and then **cuts**: the build reports what has been collected so
  far and stops before the next pass.

| # | Pass | Class | Boundary, and why |
|---|------|-------|-------------------|
| 1 | `fold_traits` | HARD | It rewrites the program: a deriver whose fold fails is missing the folded fields/methods. Continuing would make `collect` and the item walk report "unknown field" about members the source never wrote. Accumulates across independent derivers, structural syntheses and requirement checks; cuts before `collect`. |
| 2a | `collect` → `validate_type_decls` | SOFT | A pure validator over an **already-built** `TypeTable`. It registers nothing and removes nothing: the table the next pass reads is byte-identical whether or not a decl was rejected. Accumulates over every decl (and every field within a decl) and continues. |
| 2b | `collect` → signature/const/method walk | HARD | Each successful item **defines a binding** (`define_fn`/`define_const`). A rejected signature contributes no binding, so a later call to that name would be reported as "undefined", blaming the caller for the callee's defect — a cascade lie (M.3). Accumulates over every item; cuts at `collect`'s boundary. |
| 3 | `check_modules` | SOFT | Produces nothing at all (`-> error \| null`). Accumulates over every item and continues. |
| 4 | `check_di` | SOFT | Produces nothing (`-> error \| null`); its overlay check only reads the registry. Accumulates over every `#inject` overlay and continues. |
| 5 | `build_di_registry` | HARD | Produces the `DiRegistry` the sealed env carries. A rejected registration leaves a provider slot empty, and every `#inject` of that interface would then be reported as unprovided — again blaming the use for the declaration's defect. Accumulates over every item; cuts. |
| 6 | `register_instance_methods` | HARD | Produces the `Env` the item walk types against. A stamped instance method whose signature fails to resolve is not defined, so a dot-call on it cascades to "unknown method". Accumulates over every instance and every method; cuts. |

Passes 2a and 3 are SOFT, and those are exactly the passes behind two of the four residual builds
(c55 and c53) — those two fold into the shared build. Passes 2b (c25, c42) are HARD, so the two
constructs behind them share **one** build with each other, not with the item walk.

## 4. Report order is part of the contract

Diagnostic order is pinned, because an unstable order re-surfaces as a broken fixpoint — the same
defect class as the `readdir` ordering bug this wagon already fixed. The order is:

1. **source file**, then
2. **line**, then
3. **column**.

Every pass walks `program.items`, which the assembly already delivers in sorted path order, and
every intra-item walk (fields, params, methods, members) walks in declaration order. So each pass
emits in (file, line, col) order by construction, and pass-ordered concatenation is deterministic.

The build layer keeps its existing two-level order: FILE-boundary (parse) diagnostics first, then
everything the checker collected (`project.tks::append_diags`).

## 5. What this does NOT change

* Monomorphization is still never attempted with a non-empty diagnostic list — `PreMono.prog` is a
  partial program by construction whenever anything failed.
* No diagnostic text changes. The regressives pin text by substring, so accumulation cannot alter
  what a scenario asserts, only how many builds it takes to assert it.
* The `F8 target` rows keep their own build: their subject *is* the build configuration
  (`TEKO_TARGET=<unsupported>`), and a differing configuration is a different build by
  construction. That build is not attributable to this issue.
