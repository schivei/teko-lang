# Macro facility — design proposal set (native + FFI)

Status: **DESIGN — PROPOSAL SET for owner deliberation.** Read-only on product code;
this document writes no `.tks`, triggers no reseed, runs no build, and — per the task
constraint — **`teko test` was NOT run in any form** (the `monomorph` leak crashes the
container). Author: architect. Branch base: `origin/fix/retirement`.

> **Output-shape contract.** Every decision point below is presented as **3+ complete,
> fully-specified alternatives** (mechanics + example Teko in full-Javadoc style +
> tradeoffs) with **my recommendation**. There are **zero open questions to the owner**:
> where a point is genuinely a judgment call it is framed as N concrete options with a
> recommendation, never as a bare question. The owner deliberates over these proposals.

---

## 0. The mandate and what is FIXED (do not relitigate)

The owner deliberately **overrides the metaprogramming seal**: `DECISION_LOG.md:320`
(D33 — "metaprogramação = comptime geral / macros … pós-1.0, fora da LTS") and
`TEKO_LEGISLATION.md:607` ("Teko has no macros" — a rule about the C **bootstrap's**
preprocessor discipline, but the language-level seal is D33). Both are noted as
**superseded by owner ruling for this facility**; this document designs *around* the
override and does not re-argue it.

**Fixed surface (design AROUND these, do not reopen):**

- **Declaration:** `macro name(...args): T { body }` — `macro` keyword, **variadic**
  params (`...args`), a return type `T`, a body.
- **Call site:** `@name(a, b)` — the `@` prefix marks a macro invocation, **verbatim**.
- **Both FFI and native:** an FFI variant resolves C macros (`O_RDONLY`, function-like
  C macros — the `extern macro` sketch in `docs/design/star-ref-and-ffi-0.3.1.md:147-194`);
  a native variant has a Teko body evaluated at comptime.
- Owner's canonical example: `macro minha_macro(...args): usize { args.len }`, called
  `@minha_macro(a, b)`, yields `2`.
- Owner **leans toward args passed verbatim (unevaluated)** — alternatives fully
  proposed below regardless.

---

## 1. Current-state map (file:line — what each proposal reuses)

**Lexer / tokens.**
- `Hash` token (`#` attribute marker) — `src/lexer/token.tks:133`. There is **no `At`/`@`
  operator token today.**
- `@` is recognized ONLY as a verbatim-**string** prefix (`@"…"`, `$@"…"`) —
  `src/lexer/lexer.tks:561`, `:580`, `:715`, and `is_string_start` (`:573-585`). A bare
  `@` **not** followed by `"` currently falls through to `read_symbol` → "unexpected
  character". **`@name` is therefore an unclaimed lexical slot.**
- `macro` is **not** a reserved keyword (keyword classifier `src/lexer/lexer.tks:318-364`;
  `git grep macro src/lexer` finds none). Reserving it is additive.

**Parser / AST.**
- `Call = struct { callee: Path; args: []Expr; arg_names: []str; owner_type_args; callee_type_args }`
  — `src/parser/ast.tks:199`. `ExprKind` variant — `:292`; `Expr = struct { kind; line; col }`
  — `:293`.
- `Function` struct — `src/parser/ast.tks:519`, with `is_extern`/`c_symbol`/`from_lib`
  (`:533-535`) and `os_guard` (`:536`). `Decl = variant Function | TypeDecl | ConstDecl`
  — `:807`. `Item`/`Program` — `:825-826`.
- `Param = struct { … is_params: bool … }` — `:511` (C#-style variadic modifier,
  **trailing-only**, type must be a Slice).
- `extern fn` parsing — `src/parser/parse_decl.tks:401-489` (`is_extern`, `= "SYM"`,
  `from "lib"`). **No `macro`/`@` parsing exists** (`git grep macro src/parser` empty).

**Checker / comptime.**
- **General comptime evaluator already mandated (reach C):** `docs/design/comptime-fold-design.md`
  (owner ruling 2026-07-19). Its engine: `eval_const(e: TExpr, table, env, agg): ConstValue | error`
  — `src/checker/comptime_fold.tks:306`; `predicate_folds_const` — `:339`; the `ConstValue`
  domain (`CVInt|CVFloat|CVBool|CVBytes|CVAgg`) — `:36-...`; and the literal reconstructor
  `literal_of(v, ty, line, col): TExpr | null` — `:1997`.
- Module-const inlining: `inline_consts(prog): TProgram | error` — `src/checker/consteval.tks:531`,
  wired in the pipeline at `src/build/project.tks:367` (**after** monomorph, **before**
  lowering).
- `Env = struct { base_slots; bindings; cur_ns; owner_type; file; base_index }` —
  `src/checker/scope.tks:72` (the binding environment `eval_const` consumes).
- TAST: `TExpr` — `src/checker/tast.tks:10`; `TCall` — `:67`; `TItem = variant TFunction |
  TypeDecl | UseDecl | TStatement | TConstDecl` — `:290`; `TProgram` — `:291`.
- Variadic lowering precedent: `is_params` → `variadic` in the typer — `src/checker/typer.tks:2704`.

**Conditional-compilation / item-selection precedent.**
- `#os("…")` prunes non-matching function variants at `src/build/project.tks:116-126`
  (compile-time item selection), inside `frontend_check` (`:464`). This is the closest
  existing precedent for a **compile-time program-transform pass**, which macro expansion is.

**FFI extern-macro resolver (the C-macro side).**
- `docs/design/star-ref-and-ffi-0.3.1.md:147-194` — a **teko-native, no-`cc`** 4-tier
  resolver: **Tier 0** object-like CONSTANT (`O_RDONLY`, `INT_MAX`; mini C-constant
  evaluator, value inlined — ships first, zero runtime); **Tier 1** symbol-alias
  (`#define htonl(x) __bswap_32(x)` → bind real symbol); **Tier 2** simple-body
  expression expansion (`htonl` bit-twiddle → own-backend IR); **Tier 3** arbitrary C →
  **HONEST ERROR**. Sketch surface today: `extern macro fn N(params): R = "MACRO" from
  header "h.h"`, called as an ordinary `N(x)`.

**Fixpoint / seed precedent.**
- `lower_const.tks` derives every rodata symbol as a **pure function of stable keys**
  (never a global counter) so generations are byte-identical (`const_leaf_symbol`,
  `const_rodata_symbol`). Any macro-introduced name MUST follow the same rule.
- Seed discipline: module-level `const` (#594) landed, THEN a seed bump (`DECISION_LOG.md`
  D40/D41), and only after may the corpus use it. Macros inherit this sequence.

---

## 2. Decision Point 1 — what `args` IS

### Proposal 1A — verbatim AST/token nodes (unevaluated) — *the owner's lean*

`args` is a comptime slice `[]Node`, where `Node` is an opaque comptime handle to a
call-site argument's **AST subtree** (the un-type-checked, un-evaluated `Expr`). `.len` =
call-site **arity**; `args[i]` = the i-th argument's node; iteration yields nodes. The
macro may inspect node kind, read its source text, and **splice** it into produced code.

```teko
/**
 * count_args — returns how many arguments the call site passed, without evaluating any
 * of them. `args` is a verbatim node pack: `.len` is structural arity, so `@count_args(f(),
 * g())` is `2` even though `f()`/`g()` are never run at comptime.
 *
 * @param args  the verbatim call-site argument nodes (unevaluated AST subtrees)
 * @return      the number of arguments the call site supplied
 * @since 0.4-macros
 */
macro count_args(...args): usize { args.len }
```

- **Enables:** `stringify(x)`, `debug_assert(cond)` that prints the source of `cond`,
  lazy-argument macros (an arg spliced into a branch that may never run).
- **Cost:** requires exposing an **AST-reflection surface** in comptime (node kind
  enum, field accessors) — a real surface-area increase (M.0 small-language tension).
- **Matches owner's `.len` example exactly** (arity is structural, no evaluation).

### Proposal 1B — comptime-evaluated values

`args` is a comptime pack of the **evaluated ConstValue** of each argument. Every argument
must be const-foldable (via `eval_const`, `comptime_fold.tks:306`). `.len` = arity;
`args[i]` = the typed comptime value; iteration yields values.

```teko
/**
 * sum_all — folds a variadic pack of COMPILE-TIME-CONSTANT integers into their sum at
 * comptime. Each argument is evaluated to a `ConstValue`; a non-const argument is a
 * compile error at the call site.
 *
 * @param args  the call-site arguments, each required to be a compile-time constant
 * @return      the comptime sum, inlined as a literal at the call site
 * @throws      a call-site diagnostic if any argument is not a compile-time constant
 * @since 0.4-macros
 */
macro sum_all(...args): usize {
    var total = 0 to usize
    for a in args { total = total + a }
    total
}
```

- **Enables:** pure comptime arithmetic/tables; **directly reuses `eval_const`** with
  zero new reflection surface.
- **Cost / conflict:** **cannot accept runtime arguments.** `@minha_macro(a, b)` where
  `a`/`b` are runtime locals is REJECTED. This **contradicts the owner's canonical
  example** unless `a`/`b` are const — a hard mismatch with the fixed spec.

### Proposal 1C — hybrid typed-node model — *RECOMMENDED*

`args` is a slice `[]MacroArg`. Each `MacroArg` carries **three projections**, each
resolved lazily and only on demand:

- `.node` — the verbatim AST subtree (1A's power, for splicing/introspection);
- `.type` — the argument's **static type** (resolved by the checker; never forces
  evaluation);
- `.value()` — the argument's **comptime ConstValue** *iff* it is const-foldable, else a
  call-site error (1B's power, opt-in).

`.len` is structural arity — it **never** forces type resolution or evaluation, so the
owner's example holds verbatim.

```teko
/**
 * MacroArg — one call-site argument as seen by a native macro body: a verbatim AST node,
 * its statically-resolved type, and — on demand — its comptime value. Structural queries
 * (`.type`, `.node`) never evaluate; `value()` forces a const-fold and errors if the
 * argument is not a compile-time constant. This is the hybrid model: a superset of the
 * pure-node (1A) and pure-value (1B) designs.
 *
 * @field node  the verbatim, unevaluated call-site AST subtree
 * @field type  the argument's static type (resolved, non-evaluating)
 * @since 0.4-macros
 */
type MacroArg = struct { node: Node; type: Type }

/**
 * value — the argument's comptime ConstValue, computed via the reach-C evaluator, or a
 * call-site error if the argument is not a compile-time constant. Opt-in: a macro that
 * only inspects `.type`/`.node` never triggers evaluation.
 *
 * @param self  the argument view
 * @return      the argument's comptime value, or a diagnostic if it is not const
 * @throws      a call-site diagnostic when the argument does not fold at comptime
 * @since 0.4-macros
 */
fn value(self): ConstValue | error { /* reuses comptime_fold::eval_const */ }
```

- **Enables:** the owner's `.len`; type-directed macros (`@sizeof(x)`, `@typename(x)`);
  value macros (opt-in `.value()`); and — with Decision Point 2's code-expansion — full
  splicing. It is a **strict superset** of 1A and 1B.
- **Cost:** largest surface; delivered **staged** (see the crumb sketch): the
  structural+type layer ships first (covers the fixed example), `.value()` rides the
  existing `eval_const`, and node-splicing rides the code-expansion stage.

**Recommendation: 1C (hybrid), staged.** It is the only model that satisfies the fixed
example (`.len` structural), the owner's verbatim lean (`.node`), AND value macros
(`.value()`) without forcing every argument to be const (1B's fatal conflict). Reduced-
surface fallback: **1A** (drop `.value()` until the code-expansion stage needs it).

---

## 3. Decision Point 2 — what a macro PRODUCES

### Proposal 2A — value-inlining only (expression macro)

The body is a comptime expression; its `ConstValue` result replaces the call site as a
**literal** (reusing `literal_of`, `comptime_fold.tks:1997`). Return type `T` is the
value's type. Owner's `: usize { args.len }` → the literal `2`.

- **Zero hygiene obligation** (produces a value, not code). **Reuses `eval_const` +
  `literal_of` wholesale.** Satisfies the canonical example on day one.
- **Limit:** cannot expand to statements/control flow.

### Proposal 2B — code-expansion (expr/stmt splice)

The body **returns a code fragment** (an AST node / block) spliced at the call site. The
return type is a reserved code type (`expr` / `stmt`). Needs quasi-quote and hygiene.

```teko
/**
 * unless — expands to an `if` whose condition is the negation of the spliced call-site
 * argument, producing STATEMENTS at the call site rather than a value. `cond.node` is the
 * verbatim caller expression, so it resolves in the caller's scope.
 *
 * @param cond  the condition node, spliced verbatim into the expansion
 * @param body  the block node, spliced as the `if` body
 * @return      an `stmt` fragment inlined at the call site
 * @since 0.4-macros
 */
macro unless(cond: MacroArg, body: MacroArg): stmt {
    quote { if !$(cond.node) { $(body.node) } }
}
```

- **Most powerful** (assert-with-message, logging, lazy args). **Biggest machinery**
  (quasi-quote + Decision Point 3 hygiene).

### Proposal 2C — both, discriminated by return type — *RECOMMENDED*

One construct; the **declared return type selects the lowering**. A normal value type `T`
→ value-inlining (2A). The reserved `expr`/`stmt` type → code-expansion (2B). This is
exactly the owner's phrasing ("a return type" that "declares which").

**Recommendation: 2C, delivered 2A-first.** Value macros (return `T`) ship in the seed —
the canonical example is a value macro, so the fixed spec is satisfied immediately with
zero hygiene debt. Code-expansion (`expr`/`stmt`) lands as a **second, post-seed stage**
gated on quasi-quote + hygiene (Decision Point 3). One keyword, two lowerings, chosen by
the return type — no grammar fork.

---

## 4. Decision Point 3 — hygiene (only for the code-expansion stage)

Value macros (2A) produce a literal and need **no** hygiene; this decision binds only when
2B/2C code-expansion is enabled.

### Proposal 3A — unhygienic (splice as-is)

Macro-body identifiers bind at the call site; a `let tmp` in the expansion can capture or
shadow a caller `tmp`. Simple, C-like. **Rejected:** silent capture is exactly the
"logic you cannot read" that **M.3 (honesty) forbids** — the same principle that bans
preprocessor magic in the seed (`TEKO_LEGISLATION.md:606-609`).

### Proposal 3B — gensym / auto-rename of macro-introduced bindings — *RECOMMENDED*

Every binding **introduced by the expansion** (a `let`/`mut` in the macro body's produced
code) is renamed to a fresh, deterministic symbol; spliced call-site nodes (`arg.node`)
are inserted verbatim, so they still resolve in the **caller's** scope. Prevents accidental
capture without rebuilding the resolver. The fresh name is a **pure function of stable
keys** — `__macro_<macroname>_<callsite_line>_<callsite_col>_<localidx>` — never a global
mutable counter, mirroring `lower_const`'s symbol determinism (fixpoint-safe, §7).

- **Medium cost**, reuses the existing name-mangling infra. Upholds M.3 (no silent
  capture) with a bounded change.

### Proposal 3C — fully hygienic scoping (colored identifiers)

Every identifier carries its introduction context; macro-body names resolve in the macro's
**definition** scope, call-site names in the caller's. Requires the resolver
(`src/checker/resolve.tks`) to thread hygiene contexts through name resolution. **Most
correct, largest change.** Deferred.

**Recommendation: 3B (gensym) for the code-expansion stage; 3A rejected (M.3); 3C
deferred.** Because value macros (2A) ship first and need no hygiene, **day-one carries
zero hygiene debt** — 3B is only owed when code-expansion lands.

---

## 5. Decision Point 4 — FFI ↔ native unification

Two surfaces to unify: native `macro name(...args): T { body }` and the FFI C-macro
resolver (`star-ref-and-ffi-0.3.1.md:147-194`, today sketched as `extern macro fn N(p): R
= "MACRO" from header "h.h"` with a plain call).

### Proposal 4A — one keyword `macro`, `extern` modifier selects the FFI resolver

`macro name(...): T { body }` = native (Teko body). `extern macro name(...): T = "C_NAME"
from header "h.h"` = FFI (no body; the 4-tier C resolver IS the body). Mirrors the existing
`extern fn` split (a `Function` with `is_extern`, `c_symbol`, `from_lib` — `ast.tks:533-535`)
one-for-one. **Minimal grammar delta.**

### Proposal 4B — two constructs sharing a resolver-neutral core

Keep `extern macro fn` (FFI, plain-call site, args are ordinary typed values fed to the
resolved C expression) and `macro` (native, `@`-call site, args are nodes) as **distinct
constructs**, unified only by the concept "comptime-resolved callable." Honest about the
genuinely different arg models, but **leaves two call syntaxes** and contradicts the owner
fixing `@` as THE macro-call marker.

### Proposal 4C — single construct, unified `@`-call for both

`extern macro htonl(x: u32): u32 = "htonl" from header "arpa/inet.h"` invoked `@htonl(x)`;
`@O_RDONLY` (no parens) for object-like constants. `macro`+optional-`extern` for
declaration; `@` for **every** invocation. The 4-tier C resolver becomes the lowering of an
`extern macro`. Cleanest conceptual unification and honors the owner's fixed `@` as the
universal marker. **Surface note:** this **supersedes** the FFI doc's `extern macro fn
htonl(x)` plain-call sketch — the tier logic is preserved unchanged; only the call marker
becomes `@`.

**Recommendation: composite 4A (declaration) + 4C (call site).** One `macro` keyword; an
`extern` modifier + `= "C" from header "h.h"` selects the FFI 4-tier resolver in place of
a Teko body; **`@` marks every invocation** (native and FFI), honoring the owner's fixed
call syntax and unifying the two surfaces into one construct. Update
`star-ref-and-ffi-0.3.1.md` to `extern macro` + `@`-call when the FFI side lands
(Tier 0 first). Rejected: 4B (two call syntaxes fight the fixed `@`).

---

## 6. Decision Point 5 — comptime integration

The native macro body is Teko evaluated at comptime; it must ride the **reach-C general
comptime evaluator** already mandated (`comptime-fold-design.md`, owner 2026-07-19):
`eval_const` (`comptime_fold.tks:306`) over the `ConstValue` domain, with `literal_of`
(`:1997`) reconstructing literals, and `inline_consts` (`consteval.tks:531`) as the
existing comptime program-transform, wired at `project.tks:367`.

### Proposal 5A — macro expansion is a NEW pre-pass, then the existing pipeline runs unchanged — *RECOMMENDED for value macros*

A new pass `expand_macros(prog): TProgram | error` runs **immediately before**
`inline_consts` at `project.tks:367` (after monomorph, so types are resolved for `.type`/
`.value()`). It: (1) binds each macro's params into an `Env` (`scope.tks:72`) — for 1C,
`.node` from the call-site `TCall.args` (`tast.tks:67`), `.type` from the checked arg
types; (2) evaluates the body with `eval_const`; (3) replaces each `@name(...)` `TExpr`
with `literal_of(result)`. Downstream (inline_consts, lowering) is **untouched** — macros
lower to ordinary AST.

- **Clean layering, maximal reuse** (eval_const + literal_of + Env). No typer surgery.

### Proposal 5B — macros folded lazily inside the typer

The typer requests expansion when it reaches a `MacroCall` `TExpr`, interleaved with
type-checking. Necessary only if a macro's `.type` queries must resolve **before** the
surrounding expression types. More complex ordering; more coupling to `typer.tks`.

### Proposal 5C — two-phase (structural pre-pass + typed phase)

A structural/arity phase near parsing (handles `.len`, node splicing needing no types) plus
a typed phase in/after the typer (handles `.type`/`.value()`/code-expansion). Matches 1C's
staged delivery.

**Recommendation: 5A now, escalate to 5C when 1C's `.type`/`.value()` and code-expansion
land.** 5A is a single, well-isolated pass reusing the whole comptime engine; slot it at
`project.tks:367`. Reject 5B as primary (unnecessary typer coupling for the common case).

---

## 7. Decision Point 6 — variadics, `@` semantics, error model, fixpoint/seed safety

### 7.1 Variadic mechanics

- **6V-A:** `...args` binds a single node pack `args: []MacroArg` (owner's `.len`).
  Mirrors `is_params` (`ast.tks:511`, `typer.tks:2704`), node-typed, comptime.
- **6V-B — RECOMMENDED:** fixed leading params + one **trailing** pack
  (`macro m(tag: str, ...args)`), reusing the existing **trailing-only** variadic
  discipline (matches DEFARGS/`is_params`).
- **6V-C:** splat forwarding `@m(...other)` to pass a pack through. Richer; **deferred**.

**Recommendation: 6V-B** — one trailing node pack after zero-or-more fixed params, trailing-
only (reuse the established rule). Forwarding (6V-C) is a later add.

### 7.2 Call-site `@` semantics

- **6@-A — RECOMMENDED:** `@` is a **distinct prefix token** (`At`, new in `token.tks`),
  parsed into a new `MacroCall = struct { name: Path; args: []Expr; arg_names: []str }`
  expr kind (parallel to `Call`, `ast.tks:199`). Legal only directly before a macro-name
  path; args captured **unevaluated** as nodes.
- **6@-B:** `@name` desugars into a normal `Call` flagged `is_macro: bool` — less new AST,
  but muddies the clean node/value distinction and every `Call` consumer must learn the
  flag.
- **6@-C — RECOMMENDED (position rule, code-expansion stage):** allow `@m(...)` in **both**
  statement and expression position; a statement-position call may expand to statements
  (code-expansion), an expression-position call to a value.

**Recommendation: 6@-A + 6@-C.** A clean `MacroCall` node keeps node-vs-value honest; the
position rule lets value macros and (later) statement macros coexist under one `@`.

### 7.3 Error model

- **6E-A — RECOMMENDED (primary):** macro errors are ordinary checker diagnostics at the
  **call site** (`@name` line/col), with a note pointing at the macro decl — reuse the
  `err_at` family. E.g. "argument 2 is not a compile-time constant" for a `.value()` on a
  runtime arg; arity mismatch; unknown macro.
- **6E-B:** errors reported at the macro **definition** with a call-site backtrace note
  (better for buggy macro bodies; worse for the common misuse case).
- **6E-C — RECOMMENDED (author intrinsic):** a comptime `@error("msg")`/`comptime_error`
  intrinsic lets a macro body raise its OWN honest diagnostic — the native mirror of the
  FFI **Tier-3 honest stop** (`star-ref-and-ffi-0.3.1.md:183-187`) and a direct expression
  of **M.3**.

**Recommendation: 6E-A primary + 6E-C intrinsic.** FFI-unresolvable C macros (Tier 3) emit
the honest error the FFI doc already specifies.

### 7.4 Fixpoint & seed safety

**Fixpoint (self-host byte-identity).** Macro expansion MUST be **deterministic** and
produce byte-identical AST across generations, exactly as `lower_const` guarantees
symbol byte-identity:
- Value macros fold to literals via `literal_of` — inherently deterministic.
- Gensym (3B) names MUST be a **pure function of stable keys** (macro name + call-site
  line/col + local index), **never** a global mutable counter — mirroring
  `const_leaf_symbol`/`const_rodata_symbol` (`lower_const.tks`).
- **Recursion/expansion depth is bounded**: a macro that expands (directly or mutually) to
  itself past a fixed depth is a **compile error** (a REJECT fixture, §8) — no unbounded
  or nondeterministic expansion reaches lowering.

**Seed safety (bootstrap ordering).** The bootstrap seed is the previously released
`teko` binary; the corpus must not USE a feature its seed lacks. Therefore:
1. Land lexer + parser + checker + expansion for **value macros** (2A/5A) — `src/**.tks`
   does **not** use `macro`/`@` yet.
2. **SEED BUMP** — release a seed that understands macros (the same discipline as
   module-const #594 → D40/D41).
3. **Only after the bump** may the corpus use `@`/`macro`. The FFI side gates the same way:
   **Tier 0 constants first** (value inlining, no IR), Tiers 1-2 with the own backend,
   Tier 3 the always-present honest error.

---

## 8. Regression fixtures (inputs → expected native exit codes)

Named fixtures for the recommended composite (pattern `macro_*/`, `extern_macro_*/`),
gated on the **native** engine (never `teko test` here — design only):

| Fixture | Input shape | Expectation |
|---|---|---|
| `macro_arity_len/` | `macro minha_macro(...args): usize { args.len }` + `@minha_macro(a, b)` returned from `main` | ACCEPT; native **exit 2** (the canonical example) |
| `macro_variadic_empty/` | `@minha_macro()` | ACCEPT; native **exit 0** |
| `macro_value_fold/` | value macro computing a const (e.g. `@sum_all(2,3,5)`) | ACCEPT; native **exit 10** |
| `macro_type_query/` | `@sizeof(x)` / `@typename(x)` using `.type` (no evaluation) | ACCEPT; exit = the queried size |
| `macro_nonconst_arg_rejected/` | `.value()` on a runtime local | REJECT — "argument N is not a compile-time constant" (`EXPECT_COMPILE_FAIL`) |
| `macro_unknown_rejected/` | `@nope(x)` with no such macro | REJECT — unknown macro at call site |
| `macro_arity_mismatch_rejected/` | fixed-param macro called with wrong arity | REJECT — arity diagnostic |
| `macro_recursion_bounded_rejected/` | macro expanding to itself past depth | REJECT — expansion-depth exceeded (fixpoint safety) |
| `extern_macro_const_o_rdonly/` | `extern macro O_RDONLY: i32 = "O_RDONLY" from header "fcntl.h"` + `@O_RDONLY` (Tier 0) | ACCEPT; exit = the resolved constant |
| `extern_macro_tier3_rejected/` | an arbitrary/statement C macro (Tier 3) | REJECT — honest "not mechanically resolvable" |
| `macro_hygiene_no_capture/` (code-expansion stage) | expansion introduces `let tmp` while caller also has `tmp` | ACCEPT; gensym prevents capture; exit proves the caller's `tmp` intact |

Each new/changed line carries **100% delta coverage** (D32, `DECISION_LOG.md:312`),
measured on the native gate, with any genuinely-unreachable arm listed with a one-line
justification.

---

## 9. Recommended composite design (one paragraph)

**Declaration:** one `macro` keyword; `extern` modifier + `= "C" from header "h.h"`
selects the FFI resolver in place of a Teko body (**4A**). **Call site:** `@name(...)`
for everything, a distinct `At` token → a `MacroCall` node (**4C + 6@-A**), position rule
for value-vs-statement (**6@-C**). **`args`:** the hybrid `[]MacroArg` (`.node`/`.type`/
`.value()`), `.len` structural, staged (**1C**). **Produces:** value-inlining now,
code-expansion later, discriminated by return type (**2C, 2A-first**). **Hygiene:** none
owed until code-expansion, then gensym with stable-key names (**3B**). **Integration:** a
`expand_macros` pre-pass at `project.tks:367` reusing `eval_const`/`literal_of`/`Env`
(**5A → 5C**). **Variadics:** one trailing node pack after fixed params (**6V-B**).
**Errors:** call-site diagnostics + a `@error` author intrinsic (**6E-A + 6E-C**).
**Safety:** deterministic expansion, stable-key gensym, bounded depth, land-then-seed-bump,
Tier-0-first FFI (**§7.4**).

---

## 10. Proposed crumb sketch (recommended composite)

Each crumb is independently gate-able; **ritual points** (full C+self-host+native gate +
FIXPOINT) are marked ★.

1. **Lex `@` + `macro`.** Add `At` TokenKind (`token.tks`); emit it for a bare `@` not
   starting a string (`lexer.tks` `next_token`/`read_symbol`); reserve `macro` in the
   keyword classifier (`lexer.tks:318-364`). Gate: lexer tests.
2. **AST + parse.** Add `MacroDecl` (native + `extern` forms) to `Decl` and `MacroCall`
   to `ExprKind`/`MacroArg`/`Node` scaffolding in `ast.tks`; `parse_decl` macro form
   (reuse the `extern fn` path `parse_decl.tks:401-489`); `parse_expr` `@`-prefix. Honest-
   stop bodies that compile. Gate: parser tests.
3. **Collect + resolve.** Register macros in a macro table (collect/resolve); resolve
   `@name`; arity/variadic check (trailing pack, 6V-B); structural `MacroArg` API
   (`.len`, indexing, `.type`). Gate: checker ACCEPT/REJECT fixtures.
4. ★ **Expand pass (value macros, 5A/2A).** `expand_macros` before `inline_consts`
   (`project.tks:367`); evaluate via `eval_const`; inline via `literal_of`; `.value()`
   opt-in; error model 6E-A + `@error`. Gate: FULL gate + native fixtures (`macro_arity_len`
   → exit 2, `macro_value_fold`, `macro_nonconst_arg_rejected`, `macro_recursion_bounded_rejected`).
5. ★ **FFI Tier 0 (4C/extern).** `extern macro` object-like constant resolver (the mini
   C-constant evaluator), `@O_RDONLY` inlined; update `star-ref-and-ffi-0.3.1.md` to
   `@`-call. Gate: FULL gate (`extern_macro_const_o_rdonly`, `extern_macro_tier3_rejected`).
6. ★ **SEED BUMP #1.** Release a seed understanding macros; only after may `src/**.tks`
   use `@`/`macro` (D40/D41 discipline). **Ritual.**
7. ★ **Code-expansion stage (post-seed, 2B/2C/3B/5C).** `expr`/`stmt` return types,
   quasi-quote, gensym hygiene (stable-key names), typed phase, statement-position `@`.
   Gate: FULL gate + `macro_hygiene_no_capture`.
8. **FFI Tiers 1-2** (symbol-alias, expression→own-IR); Tier 3 stays the honest error.
   Gate: FULL gate, coupled to the own backend.

**Ritual points (full gate must pass):** crumbs **4** (value macros — shared checker +
comptime program-transform), **5** (FFI resolver), **6** (seed bump), **7** (resolver-
touching hygiene/code-expansion).

---

## 11. Risks & law tensions (each with a recommended resolution)

- **D33 / `TEKO_LEGISLATION.md:607` "no macros" seal.** Owner override — noted, not
  relitigated. Resolution: on landing, amend D33's reference and the legislation line to
  "superseded for the macro facility by owner ruling" (doc-sync, not a code change here).
- **M.0 small-language surface.** Macros add surface. Resolution: **staging** — value
  macros (minimal: `@`, `macro`, `.len`/`.type`/`.value()`) first; node-reflection and
  code-expansion only when demanded, each its own gated crumb.
- **M.3 honesty vs. unhygienic capture.** Resolution: **3A rejected**, **3B gensym**
  adopted for code-expansion; value macros carry no hygiene debt.
- **Fixpoint (self-host byte-identity).** Risk: nondeterministic gensym/expansion.
  Resolution: **stable-key gensym** (pure function of macro name + call-site loc), bounded
  expansion depth, `literal_of` determinism — mirrors `lower_const`.
- **Seed ordering.** Risk: corpus using macros before the seed knows them. Resolution:
  **land-then-seed-bump**, corpus abstains until crumb 6; FFI Tier-0-first.
- **1B conflict with the fixed example.** `@minha_macro(a, b)` on runtime locals cannot
  work under a pure-value arg model. Resolution: **1C** (structural `.len` never
  evaluates), which is why 1B is rejected as the base model.

**No genuine unresolved tension remains — no HALT.** Every judgment call above is resolved
law-first with a concrete recommendation; the owner deliberates over the alternatives, not
over open questions.
