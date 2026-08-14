# §14 — Parametric type-macros / comptime-computed types (design v1)

> **Status:** DESIGN. PASSO A of the owner's ruling: this document + the crumb-plan are the artefact to
> REVIEW before any build. No `.tks` edited, no build, no reseed until the owner approves. Companion:
> `section14-macro-comptime-impl-v1.md` (A0–A4 / B1–B5, the engine this extends),
> `plano-9d-capabilities-crumbs-r1.md` (the §9.D sweep that consumes this feature).

## 1. Goal

Let a Family-A **type-macro** compute its lowered type from **parameters** and **compile-time body logic**,
then splice a **computed string** as the lowered `TypeExpr`. The driving consumer is the §9.D sweep's
remaining 14 variants (all cross-namespace, incl. the 6 roots), authored as:

```teko
/** Type — the checker's resolved-type union (§9.D sweep). */
macro Type(local: bool = false) {
    var q = if local { "" } else { "checker::" }
    var out = ""
    var i = 0
    loop {
        if i >= members.len { break }
        if i > 0 { out = teko::str::concat(out, " | ") }
        out = teko::str::concat(out, teko::str::concat(q, members[i]))
        i++
    }
    lowering { ${out} }
}
```

so a root union of N members is written ONCE as a bare member list, prefixed with its home namespace by
default (`if !local` ⇒ qualified `checker::Named | checker::Prim | …`, which the checker
self-qualification fix, `check_named`, resolves at BOTH same- and cross-namespace use sites). `@Type()`
yields the qualified union; `@Type(true)` yields the bare union (an escape hatch for a same-file spot that
must be bare). The three engine gaps this closes are exactly the three "later crumb" TODOs already named in
the code (`single_lowering_frag`, `frag_has_hole`, `expand_macro_type`).

## 2. The load-bearing decision: WHERE the body evaluates (phase tension)

- Family-A `@Type()` expansion is a **PRE-type-check syntactic pass**: `expand_macros_syntactic`
  (`src/parser/macro_expand.tks`) rewrites the **untyped** `parser::Program` in `frontend_check` BEFORE
  `checked_program_of`. It has NO `TypeTable` and NO typed AST.
- Family-B's `eval_const` (`src/checker/comptime_fold.tks`) is **POST-check**: it evaluates a **typed**
  `TExpr` against a `TypeTable` into a `ConstValue`. B3's own bodies do not yet execute locals/loops
  (`cx_body_value` handles a single value expression only — "a comptime with locals or control flow is a
  later crumb").

**Therefore the type-macro body cannot call `eval_const` directly** — it must be evaluated by a
syntactic-phase interpreter over **untyped** `parser::Statement`/`parser::Expr`, in a **restricted value
domain**. "Reuse the B3 engine" is thus *partial*, and this is the decision to confirm:

- **NEW (unavoidable):** the DRIVER — executing a statement sequence (`var`/`if`/`loop`) over the untyped
  AST with a small value environment. Call it `macro_eval` (new, in `src/parser/`, pre-check).
- **SHARED with B3 (recommended):** the VALUE PRIMITIVES — integer arithmetic, string concat, comparison,
  boolean logic. `eval_const`'s `ConstValue`-level operator cases can be factored into a phase-agnostic
  helper set (operating on a plain value union, no `TypeTable`) that BOTH `eval_const` (wrapping its
  `ConstValue`) and `macro_eval` call, so the two evaluators never drift on `1+1` or `concat`.
  Alternatively `macro_eval` mirrors them (smaller diff, risk of drift). **Recommendation: factor the
  shared core** — but this is an owner call (it touches B3's internals). The restricted domain is small:
  `bool`, `i64`, `str`, `[]str` — no floats, no structs, no user calls beyond a pure whitelist.

The value domain is deliberately minimal: enough for "prefix a member list and join with ` | `", not a
general comptime language. Everything outside the whitelist is a located "not yet supported in a
type-macro body" error.

## 3. Crumb sequence (each additive + INERT + fixpoint gen1==gen2==gen3)

**PT1 — Typed, defaulted macro parameters.**
- `parse_macro_params` (`src/parser/parse_decl.tks`) today forces every macro param untyped, no default.
  Extend it to accept an OPTIONAL `: <type>` and `= <default-expr>` (`has_type`/`type_ann`,
  `has_default`/`default_expr` already exist on `Param`); a bare untyped param stays legal (value macros).
- `expand_macro_type` (`src/parser/macro_expand.tks`) today ignores `mt.args`. Bind each param → value:
  the call's positional `mt.args[i]` when present, else the param's `default_expr`; evaluate via
  `macro_eval`'s arg evaluator (restricted literals: `true`/`false`, int, str). Produces the initial value
  env.
- INERT: no `src/` type-macro declares a param yet.

**PT2 — Comptime body execution.**
- Replace `single_lowering_frag` with `split_macro_body`: a body = zero-or-more leading `Statement`s + one
  trailing `LoweringFrag`. A value macro (expr position) keeps the "exactly one lowering" rule; only the
  TYPE-position path gains leading statements.
- Add `macro_eval(stmts, env) -> ValueEnv | error`: execute `var` bindings, `if`, and bounded `loop`
  (guarded by `max_macro_depth`-style iteration cap to forbid non-termination), evaluating expressions in
  the restricted domain (str/bool/i64/[]str; `teko::str::concat`, array literal/index/`.len`, comparisons,
  bool ops). Starts from PT1's param env, returns the final env.
- INERT: no `src/` macro has a non-`lowering` body statement yet.

**PT3 — `${name}` → type splice.**
- In `expand_macro_type`, drop the `frag_has_hole` rejection for the value-env case: for each `${name}` in
  the lowering tokens, look up `name` in the PT2 value env, require a `str` value, `lexer::tokenize` its
  content to tokens, splice them in place of `${name}`, then `parse_type` the fragment (reentrant, as the
  path already does) and `walk_type` the result (recursion-safe, depth-guarded). A `${name}` naming no
  env value, or a non-`str` value, is a located error.
- INERT: no `src/` type-macro uses `${}` yet.

**PT4 — RESEED (inert).** Re-harvest `bootstrap/teko.c` while PT1–PT3 are inert, via the SAME C-route
self-reproduction just proven (cc the emitted teko.c → rebuild → byte-identical; native gen1==gen2==gen3;
provenance gate). This is done BEFORE any parametric use enters `src/`, so CI's C lane always has a capable
seed — the exact lesson from the §14 reseed that motivated this doc.

**PT5+ — Author the roots + finish the sweep.** Only now rewrite the 14 remaining variants as parametric
macros (`if !local`, default qualified). Complete 15/32 → 32/32, one root per fixpointed increment
(leaves→roots). Attempt `Decl` (#3): `Parsed<@Decl()>` = a generic over an inline union — if the union
type-arg mangle surfaces the `cg_opt_mangle_texpr` buffer-twin gap, fix it in scope (mirror the `_str`
twin, `SliceType`+`UnionType`); if the monomorph/byte-agreement shape genuinely breaks, revert and report.

## 4. `if !local` semantics (default cross-ns-safe)

`local` defaults to `false` ⇒ the body prefixes each member with its home namespace (`checker::`,
`parser::`, …). Qualified members resolve everywhere (the `check_named` self-qualification fix), so the
DEFAULT is always correct at any use site — the author never thinks about it. `@X(true)` drops the prefix
for the rare same-file position that needs bare members. No use site ever needs a self-`use`.

## 5. Risks / open questions for the owner

1. **Shared value-core vs mirror (§2).** Factor `eval_const`'s value ops into a phase-agnostic core shared
   with `macro_eval` (no drift, but edits B3), or mirror them in `macro_eval` (smaller, risk of drift)?
   Recommendation: factor. **Owner call.**
2. **Body domain scope.** Proposed whitelist: `str`/`bool`/`i64`/`[]str`, `var`/`if`/bounded-`loop`,
   `teko::str::concat`, array literal/index/`.len`, comparisons, bool logic. Anything else → honest error.
   Is this enough for the roots (yes, for prefix+join), and is the cap on `loop` iterations acceptable as
   the non-termination guard?
3. **Reseed cadence (PT4).** Confirm the reseed lands while inert, before PT5 — mirroring the fix just
   applied — so the C lane never breaks on a parametric use.
4. **Is the feature worth it vs literal qualified members?** With the `check_named` fix, literal
   `checker::A | checker::B | …` already works at every site (proven, TFSpecKind). The parametric macro's
   value is DRY authoring of 15–26-member root unions (write the bare member list once) + the `local`
   toggle. The owner has ruled to build it; recorded here so the trade-off is explicit.

**No law tension:** Teko-only (all Teko-side, `teko_rt.*` frozen); W15/Javadoc on every new declaration;
each crumb additive/inert with a byte-identical fixpoint; the reseed precedes any parametric use. The one
item RELAYED for the owner is the shared-value-core decision (risk 1).
