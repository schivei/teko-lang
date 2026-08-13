# Macro facility — design proposal set (the SEALED two-family model)

Status: **DESIGN — PROPOSAL SET for owner deliberation.** Read-only on product code;
this document writes no `.tks`, triggers no reseed, runs no build, and — per the task
constraint — **`teko test` was NOT run in any form** (the `monomorph` leak crashes the
container). Author: architect. Branch base: `origin/fix/retirement`.

> **What is SEALED (do not reopen).** `docs/design/mudancas-superficie-0.3.1.md` §14 (HEAD
> `8962edca`) seals the shape: Teko gains macros in **two keyword-identified families,
> both `@`-called, split by pipeline STAGE**, and the stage FIXES the arg model — there is
> **no single-construct hybrid** (the prior `plano-macro` 1A/1B/1C hybrid is SUPERSEDED and
> is gone from this rewrite):
>
> - **Family A — syntactic** *(provisional keyword `macro`)* — expands on the **AST BEFORE
>   type-check** (`parse → AST → [expand] → TYPE-CHECK of the result → lower`). Args = **raw
>   AST only** (`.node`/`.source`/`.len`); types do NOT exist yet, so **no `.type`/`.value()`
>   in the body**. Produces **code** (AST), type-checked afterward. Rust/Lisp model. Needs
>   hygiene + a `quote`/`${}` splice+unquote mechanism.
> - **Family B — comptime evaluation** *(provisional keyword `comptime`)* — runs **AFTER
>   type-check** (`parse → AST → TYPE-CHECK → [comptime eval] → lower`). Args = **typed
>   evaluable values**; computes an inlined **value**. Zig comptime model.
> - **FFI** = `extern` + the family (`extern macro` for function-like C macros, `extern
>   comptime` for C constants like `O_RDONLY`).
> - The keyword names `macro`/`comptime` are **PROVISIONAL** — owner-pending final
>   confirmation; only the semantics (two families + the stage split) are sealed.

> **Output-shape contract (owner's standing feedback).** Every sub-design below is presented
> as **3+ complete, fully-specified alternatives**, and **each alternative carries a concrete
> Teko code example — the macro body PLUS its call site PLUS what it achieves**, followed by
> **my recommendation**. There are **zero bare open questions**: a genuine judgment call is
> framed as N concrete, runnable options with a recommendation, never as a question. The
> owner deliberates over the proposals.

---

## 0. Current-state map (file:line — what each proposal reuses)

**Lexer / tokens.**
- `Hash` token (`#` attribute marker) — `src/lexer/token.tks:133`. There is **no `At`/`@`
  operator token today.**
- `@` is recognized ONLY as a verbatim/raw **string** prefix (`@"…"`, `$@"…"`) —
  `src/lexer/lexer.tks:429-453` (the `has_verbatim` body scanner; `@` = "NO escape
  processing"). A bare `@` **not** followed by `"` falls through to `read_symbol` →
  "unexpected character". **`@name` is an unclaimed lexical slot** — free for the macro call
  marker.
- Keyword classifier `keyword_kind` — `src/lexer/lexer.tks:331-372`; `extern` at `:354`,
  `const` at `:338`. Neither `macro` nor `comptime` is reserved today (`git grep` finds
  none). Reserving them is additive. Contextual-keyword precedent noted at `:322`/`:370`
  (`params`/`from` are position-significant, not lexer-reserved).

**Parser / AST (Family A operates on THIS tree — pre-type-check).**
- `Expr = struct { kind: ExprKind; line: u32; col: u32 }` — `src/parser/ast.tks:293`;
  `ExprKind = variant Number | Var | Call | … | Block` — `:292`. Every node already
  carries `line`/`col` (C1-POS) — the stable key for hygiene gensym (§A.3, §5.4) and for
  `.source` reconstruction (§A.1).
- `Call = struct { callee: Path; args: []Expr; arg_names: []str; owner_type_args;
  callee_type_args }` — `:199`. The template for a new `MacroCall` node.
- `Function = struct { name; type_params; …; is_extern; c_symbol; from_lib; os_guard; … }`
  — `:519`, with `is_extern`/`c_symbol`/`from_lib` at `:533-535` and `os_guard` at `:536`.
  The `extern fn` split is the exact template for `extern macro`/`extern comptime` (§7).
- `Param = struct { …; is_params: bool; has_default; default_expr }` — `:511` (C#-style
  variadic modifier, **trailing-only**, `type_ann` must be a Slice). The variadic precedent
  for `...args` (§5.2).
- `Decl = variant Function | TypeDecl | ConstDecl` — `:807`; `Item`/`Program` —
  `:824-826` (the flat item list the checker consumes — Family A rewrites `Program` in
  place).
- `extern fn` parsing — `src/parser/parse_decl.tks:401-489`. **No `macro`/`comptime`/`@`
  parsing exists yet.**

**Checker / comptime (Family B operates AFTER this — post-type-check).**
- **General comptime evaluator already mandated (reach-C)** — `docs/design/comptime-fold-design.md`
  (owner ruling 2026-07-19). Engine `eval_const(e: TExpr, table: TypeTable, env: Env, agg:
  AggConstMap): ConstValue | error` — `src/checker/comptime_fold.tks:306`;
  `predicate_folds_const(e: TExpr): bool` — `:339`; the `ConstValue` domain
  (`CVInt|CVFloat|CVBool|CVBytes|CVAgg`) — `:18-36`; the literal reconstructor
  `literal_of(v: ConstValue, ty: Type, line, col): TExpr | null` — `:1997`. **This is
  Family B's engine verbatim.**
- Module-const inlining `inline_consts(prog): TProgram | error` — `src/checker/consteval.tks:531`,
  wired at `src/build/project.tks:367` — **after** `monomorph` (`:353-354`), **before**
  lowering. **This is the exact pipeline slot for Family B's expand pass.**
- `Env = struct { … }` — `src/checker/scope.tks:72` (the binding env `eval_const` consumes).
- TAST: `TExpr` — `src/checker/tast.tks:10`; `TCall` — `:67`; `TExprKind` variant — `:148`;
  `TItem = variant TFunction | TypeDecl | UseDecl | TStatement | TConstDecl` — `:290`;
  `TProgram` — `:291`. Family B rewrites `TProgram` in place.

**Conditional-compilation / program-transform precedent.**
- `#os("…")` prunes non-matching function variants at `src/build/project.tks:116-126`, inside
  `frontend_check` (`:464`). This is the closest existing precedent for a **compile-time
  program-transform pass** — the shape both families' expand passes copy (visit items, rewrite
  the item list, keep it deterministic).

**FFI extern-macro resolver (the C-macro side).**
- `docs/design/star-ref-and-ffi-0.3.1.md:147-194` — a teko-native, **no-`cc`** 4-tier
  resolver: **Tier 0** object-like CONSTANT (`O_RDONLY`, `INT_MAX`; mini C-constant
  evaluator, value inlined — ships first, zero runtime → maps to `extern comptime`);
  **Tier 1** symbol-alias (`#define htonl(x) __bswap_32(x)` → bind real symbol); **Tier 2**
  simple-body expression expansion (→ own-backend IR); **Tier 3** arbitrary C → **HONEST
  ERROR**. Tiers 1-3 map to `extern macro`. Today's sketch surface: `extern macro fn N(p): R
  = "MACRO" from header "h.h"`, plain-called `N(x)` — this rewrite migrates the CALL to `@`.

**Fixpoint / seed precedent.**
- `src/checker/lower_const.tks` derives every rodata symbol as a **pure function of stable
  keys** (`const_leaf_symbol`, `const_rodata_symbol`), never a global counter, so generations
  are byte-identical. Any macro-introduced name MUST follow the same rule (§5.4).
- Seed discipline: module-level `const` (#594) landed, THEN a seed bump (`DECISION_LOG.md`
  D40/D41), and only after may the corpus use it. Macros inherit this sequence (§5.5).

---

## 1. The sealed model, restated as the design frame

The two families are **not two flavours of one construct** — they are two passes at two
stages, and the stage decides everything downstream. The whole document is organized under
that split:

| axis | **Family A — `macro`** (syntactic) | **Family B — `comptime`** (evaluation) |
|---|---|---|
| mechanism | **copy-in-place** — the body is pasted at the use site, ENLARGING the AST (not a call, not dispatch) | **evaluate** — the body is folded to one value, inlined |
| stage | **before** type-check (on `parser::Program`) | **after** type-check (on `checker::TProgram`) |
| args are | **raw, ANONYMOUS AST** — `.node`/`.source`/`.len`, un-typed until materialized | **typed evaluable values** — a `ConstValue` |
| variadics | **free** — anonymous args have no type, so arity is unconstrained (§A.0) | trailing typed pack (`is_params` precedent) |
| body may use | node reflection + `quote`/`${}` | `eval_const` arithmetic over typed values |
| body may NOT use | `.type`, `.value()` (no types yet) | `.node`/`.source` (AST is gone; it's a value) |
| produces | **code** (an AST fragment), type-checked after | a **value**, inlined as a literal |
| needs | hygiene, quote/unquote (§A.2, §A.3) | nothing new — reuses `eval_const`/`literal_of` |
| engine | new `expand_macros_syntactic` pre-pass (§5.1) | `expand_comptime` at `project.tks:367` (§B.1) |
| FFI form | `extern macro` (Tiers 1-3) | `extern comptime` (Tier 0 constants) |
| owner example | `@count(a, b)` counts nodes = `2` | `@sum(2,3,5)` folds to `10` |

Both are invoked `@name(...)`. The `@` prefix + the **declared family keyword** is what lets
the compiler know, at the call site's resolution, WHICH pass owns the expansion — the dev
*says* the family at declaration, the compiler *knows* the stage.

The sub-designs below split into **Family A detail** (§A), **Family B detail** (§B), and the
**cross-cutting mechanics** (`@` placement, variadics, errors, fixpoint/seed — §5), then FFI
(§7), fixtures (§8), crumbs (§9), risks (§10).

---

# §A — FAMILY A (`macro`, syntactic) detail

## A.0 — the DEFINING mechanism: copy-in-place, enlarge the AST (the spine)

Per the sharpened seal (`mudancas-superficie-0.3.1.md` §14.1, HEAD `3976a8be`), a Family-A
macro is defined by ONE mechanism, and every sub-design below serves it:

> **A Family-A macro COPIES ITSELF into the use site and ENLARGES the AST.** It is **inline
> expansion** — the body is *copied* at the call position, the args *substituted* in, and the
> AST *grows* there. It is **NOT a call** (no frame, no return) and **NOT dispatch** (no
> vtable, no runtime selection). This copy-in-place is exactly what distinguishes a macro from
> a function or a trait.

Three consequences fix the whole section:

1. **Enlarge-the-AST, not invoke.** The expander does not *evaluate* the body — it *pastes*
   it. So the surfaces the body needs are (a) how it *reads* the pasted-in args (§A.1 — the
   `Node` reflection), (b) how it *writes* the fragment being pasted (§A.2 — `quote`/`${}`
   splice), and (c) how the paste avoids clobbering the site it grows into (§A.3 — hygiene).
   The pipeline slot (§5.1-A) runs the paste **before type-check**, so the enlarged AST is
   type-checked as if the dev had written it by hand — a badly-typed expansion fails the
   normal type-check, pointing back at the site.

```teko
/**
 * twice — Family A, the mechanism in one macro: `@twice(work())` does not CALL twice — it
 * COPIES `work()` twice into the use site, enlarging the AST to two statements. No frame,
 * no dispatch; the caller's AST literally grows.
 *
 * @param x  the expression node, copied verbatim into each slot
 * @return   a statement fragment holding two copies of `x`
 * @since 0.4-macros
 */
macro twice(x): stmt { quote { ${x}; ${x} } }
// call site:  @twice(step())  →  the AST at the site BECOMES:  step(); step()
```

2. **No explicit types ⇒ free arity (variadics are a consequence, not a bolt-on).** A
   Family-A arg carries **no declared type** — it is an **anonymous node**, un-typed until it
   is materialized into the AST and the whole enlarged tree is type-checked. Because nothing
   about an arg is typed at paste time, the macro does not care how MANY args there are:
   **untypedness is what sustains variadics.** `...args` is not a special variadic feature —
   it is the natural shape of "a pack of anonymous nodes" (see §5.2, framed as this
   consequence).

```teko
/**
 * count — the sealed example, re-read through the mechanism: because args are anonymous
 * nodes (no type), arity is FREE — `.len` is just how many nodes were pasted in. Untypedness
 * is what lets `@count` accept any number of arguments.
 *
 * @param args  a pack of anonymous, un-typed call-site nodes
 * @return      the structural node count
 * @since 0.4-macros
 */
macro count(...args): usize { args.len }
// call site:  @count(a, b, c())  →  3   (no arg was typed; arity came for free)
```

3. **A named Family-A macro ≈ a `structural trait`, as an encapsulating HELPER.** It is the
   **explicit, visible, no-shadow heir** of the retired structural trait (§9.4 of the surface
   doc): the structural trait *encapsulated a structural pattern and pasted a synthesized body
   the dev never saw*; a named macro encapsulates a structural pattern too, but the body is
   **written by hand, visible, and copied in-place at the use site** — no compiler shadow.
   Where the structural trait was magic the dev could not read, the macro is the same
   capability made honest (M.3).

```teko
/**
 * eq_by_fields — Family A as the honest structural-trait heir: it ENCAPSULATES the "compare
 * two values field-by-field" pattern the retired structural `Eq` synthesized invisibly, but
 * here the pattern is written out and COPIED into the use site — visible, no shadow.
 *
 * @param a       the left value node
 * @param b       the right value node
 * @param fields  the field-name nodes to compare
 * @return        a boolean expression fragment ANDing the per-field comparisons
 * @since 0.4-macros
 */
macro eq_by_fields(a, b, ...fields): bool {
    var acc = quote { true }
    for f in fields { acc = quote { ${acc} && ${a}.$(f.source) == ${b}.$(f.source) } }
    acc
}
// call site:  @eq_by_fields(p, q, x, y)  →  true && p.x == q.x && p.y == q.y   (visible, copied in)
```

The three open sub-designs — the **AST reflection surface** the body reads (§A.1), the
**splice/quote mechanism** the body writes with (§A.2), and **hygiene** for the paste (§A.3)
— are exactly the three faces of this one copy-in-place mechanism.

## A.1 — AST reflection surface (`Node`): how much of the AST to expose

The body inspects `parser::Expr` subtrees. The `Node` handle reuses the compiler's own AST
(`src/parser/ast.tks:293`); the tension is M.0 (small language) — every field exposed is
permanent surface. Three tiers, smallest-first.

### A.1-Tier-1 — opaque handle: `.len`, `.source`, indexing only — *RECOMMENDED as the seed floor*

`Node` is opaque. The only operations: a node pack has `.len`; `pack[i]` yields a `Node`;
a `Node` has `.source` (the verbatim source slice it spans, from `line`/`col` + the source
map) and `.len` (child arity). **No `.kind`, no field accessors.** This is enough for the two
canonical Family-A jobs — counting and stringifying — and exposes zero AST internals.

```teko
/**
 * count — Family A: returns how many arguments the call site passed, counting AST nodes
 * WITHOUT evaluating any of them. Types do not exist at this stage, so `args` is a raw
 * node pack and `.len` is pure structural arity.
 *
 * @param args  the verbatim call-site argument nodes (raw AST, unevaluated)
 * @return      the number of arguments the call site supplied
 * @since 0.4-macros
 */
macro count(...args): usize { args.len }
// call site:  @count(f(), g())  →  expands to the literal  2   (f/g NEVER run)

/**
 * stringify — Family A: expands to a string literal holding the VERBATIM SOURCE TEXT of its
 * single argument node. `.source` is the exact bytes the caller wrote — no evaluation, no
 * type. The classic "print the expression you were given" macro.
 *
 * @param x  the argument node whose source text is captured
 * @return   a string-literal AST fragment holding `x`'s source
 * @since 0.4-macros
 */
macro stringify(x): str { quote { $string(x.source) } }
// call site:  @stringify(a + b * 2)  →  expands to the literal  "a + b * 2"
```

- **Unlocks:** `@count`, `@stringify`, and (with §A.2's splice) any macro that only needs
  to re-emit a node verbatim — e.g. a `@dbg(x)` that prints `"x = " ++ @stringify(x)` then
  the value. **Cost:** minimal, honours M.0. **Limit:** a macro cannot branch on node shape
  (can't ask "is this a call?").

### A.1-Tier-2 — tagged handle: add `.kind` + typed child accessors

Add a read-only `.kind` (a `NodeKind` enum mirroring `ExprKind`, `ast.tks:292`) and
shape-specific accessors that return `Node | null` (e.g. `.callee`, `.arg(i)`, `.lhs`,
`.rhs`). The body can now pattern-match on structure.

```teko
/**
 * assert_eq — Family A: given a call-site expression the author expects to be `lhs == rhs`,
 * expands to a runtime check that, on failure, prints BOTH operands' source text. Uses
 * `.kind` to verify the argument really is an equality and `.lhs`/`.rhs` to split it.
 *
 * @param cond  the condition node; must be a `Binary` `==` or the macro emits its own error
 * @return      a statement fragment: the check plus a source-quoting failure message
 * @throws      a compile-time `@error` if `cond` is not an `==` comparison
 * @since 0.4-macros
 */
macro assert_eq(cond): stmt {
    if cond.kind != NodeKind::Binary { @error("assert_eq expects `a == b`") }
    quote {
        if !($(cond.lhs) == $(cond.rhs)) {
            panic($string(cond.lhs.source) ++ " != " ++ $string(cond.rhs.source))
        }
    }
}
// call site:  @assert_eq(total == expected)
//   → on failure prints:  "total != expected"   (the SOURCE, not the values)
```

- **Unlocks:** structure-directed macros (`assert_eq`, a `@matches(x, Pattern)` sugar, a
  `@pipe(a, f, g)` left-fold). **Cost:** `NodeKind` + ~6 accessors become permanent surface —
  a real M.0 increase, but bounded and read-only.

### A.1-Tier-3 — full builder: `.kind` + mutation + a `Node`-construction API

Add constructors (`Node::call(callee, args)`, `Node::binary(op, l, r)`, …) so a macro can
BUILD arbitrary AST programmatically instead of only via `quote`. This is the Lisp
"AST-as-data" maximum.

```teko
/**
 * tuple_swap — Family A: given `(a, b)` node-pack, BUILDS a two-statement block that swaps
 * them via a fresh temp, constructing every node through the builder API rather than quoting.
 *
 * @param args  exactly two lvalue nodes to swap
 * @return      a statement fragment performing the swap
 * @since 0.4-macros
 */
macro tuple_swap(...args): stmt {
    var tmp = Node::fresh_local()                    // hygienic gensym (§A.3)
    Node::block([
        Node::bind(tmp, args[0]),                    // var <tmp> = a
        Node::assign(args[0], args[1]),              // a = b
        Node::assign(args[1], tmp)                    // b = <tmp>
    ])
}
// call site:  @tuple_swap(x, y)  →  var _m = x; x = y; y = _m
```

- **Unlocks:** everything (arbitrary codegen). **Cost:** the LARGEST surface — a second way
  to build every node kind, doubling the AST's public API and fighting M.0 hardest.

**Recommendation: ship Tier-1 in the seed, add Tier-2 as a gated follow-on, defer Tier-3.**
Tier-1 satisfies the sealed `@count` example and `@stringify` with near-zero surface. Tier-2
(`kind` + read accessors) is the first thing real macros want (`assert_eq`), and read-only
accessors are honest surface; land it as its own crumb once demand is shown. Tier-3's
builder is redundant with `quote` (§A.2) for almost all cases and pays the M.0 cost twice —
defer until a concrete macro cannot be written with `quote` + Tier-2.

## A.2 — splice / quote mechanism: how a Family-A body PRODUCES code

The body returns an AST fragment. Three surfaces for constructing it.

### A.2-Var-1 — `quote { … }` + `${expr}` unquote — *RECOMMENDED*

`quote { … }` is a literal AST fragment; inside it `${expr}` (or the shorthand `$(expr)` for a
single node) **splices** the `Node` that `expr` evaluates to. `$string(x)` splices a string
literal built from a `str`. This is the Rust `quote!`/Lisp quasiquote surface — the fragment
reads like the code it produces.

```teko
/**
 * unless — Family A: expands to an `if` whose condition is the NEGATION of the spliced
 * call-site argument, producing statements at the call site. `${cond}` splices the caller's
 * verbatim condition node so it resolves in the CALLER's scope; the `if`/`!` scaffolding is
 * quoted structure.
 *
 * @param cond  the condition node, spliced verbatim into the negation
 * @param body  the block node, spliced as the `if` body
 * @return      a statement fragment inlined at the call site
 * @since 0.4-macros
 */
macro unless(cond, body): stmt {
    quote { if !(${cond}) { ${body} } }
}
// call site:  @unless(done, { retry() })  →  if !(done) { retry() }

/**
 * log_expr — Family A: a LAZY-ARG macro — the argument `x` is spliced into a branch that
 * only runs when logging is on, so a costly `x` is never evaluated when disabled. This is
 * the reason args are raw nodes, not values: laziness is free.
 *
 * @param x  the expression node, spliced UNEVALUATED into the guarded branch
 * @return   a statement fragment guarding the evaluation behind `log_on()`
 * @since 0.4-macros
 */
macro log_expr(x): stmt {
    quote { if log_on() { print($string(x.source) ++ " = " ++ str(${x})) } }
}
// call site:  @log_expr(expensive())  →  expensive() runs ONLY when log_on() is true
```

- **Cost:** a `quote` sub-parser + the `${}`/`$string` splice grammar (a distinguished
  region the parser captures as a fragment template with holes). Moderate, but it is the
  surface every macro author already expects, and it keeps the produced code READABLE (M.3 —
  you can read what the macro emits). **Splice is verbatim-node**, so hygiene (§A.3) is owed.

### A.2-Var-2 — builder API over `Node` (no quote grammar)

No new grammar; the body constructs the fragment with the Tier-3 builder calls
(`Node::if_(...)`, `Node::call(...)`). Same power, but the fragment is written as nested
constructor calls.

```teko
/**
 * unless_built — Family A: the `@unless` of A.2-Var-1, expressed WITHOUT a quote grammar —
 * every node is a builder call. Functionally identical; syntactically opaque.
 *
 * @param cond  the condition node
 * @param body  the block node
 * @return      the same `if !(cond) { body }` fragment, built explicitly
 * @since 0.4-macros
 */
macro unless_built(cond, body): stmt {
    Node::if_(Node::unary(TokenKind::Bang, cond), body)
}
// call site:  @unless_built(done, { retry() })  →  if !(done) { retry() }
```

- **Cost:** zero new grammar, but the produced code is UNREADABLE at the definition site
  (you cannot see the `if` — it's a tree of calls), which cuts against M.3. Reuses A.1-Tier-3
  surface. Good as a fallback when `quote` cannot express a shape; poor as the primary.

### A.2-Var-3 — string-mixin (compile-and-splice a string)

The body returns a `str` of Teko source; the compiler re-parses it and splices the resulting
AST (Zig `@import`/D `mixin` style).

```teko
/**
 * getters — Family A: emits a getter method per named field by BUILDING SOURCE TEXT and
 * handing it back as a string the compiler re-parses. Maximum flexibility, minimum safety.
 *
 * @param names  the field-name nodes to generate getters for
 * @return       a source string that is re-parsed and spliced as methods
 * @since 0.4-macros
 */
macro getters(...names): stmt {
    var src = ""
    for n in names { src = src ++ "fn get_" ++ n.source ++ "(): auto { self." ++ n.source ++ " }\n" }
    mixin(src)                                        // compiler re-parses `src`
}
// call site:  @getters(x, y)  →  fn get_x() { self.x }  fn get_y() { self.y }
```

- **Cost:** trivial to implement (concatenate + re-parse), but it is **stringly-typed**: no
  structural checking of the fragment until re-parse, easy quoting bugs, and hygiene is
  almost impossible (names are strings). Contradicts M.3's "readable, checkable logic".
  Rejected as primary; noted because it is the cheapest bootstrap if `quote` slips.

**Recommendation: A.2-Var-1 (`quote` + `${}`), with Var-2's builder available underneath as
the fallback for shapes `quote` can't express.** `quote` keeps produced code readable
(M.3), matches author expectation, and composes with §A.3 hygiene cleanly. Var-3 string-mixin
is rejected (stringly-typed, unhygienic) but recorded as the emergency-cheap path.

## A.3 — hygiene: preventing accidental capture in spliced code

Family A splices code into the caller's scope; a `var tmp` the macro introduces can collide
with a caller `tmp`. Three schemes, each shown on a macro that WOULD capture without it.

### A.3-Scheme-1 — unhygienic (splice identifiers as written)

Macro-body bindings bind literally at the call site. Simple, C-like.

```teko
/**
 * swap_bad — Family A, UNHYGIENIC: introduces a literal `tmp`. If the caller also has a
 * `tmp`, the expansion SILENTLY captures it — the classic macro bug.
 *
 * @param a  first lvalue node
 * @param b  second lvalue node
 * @return   a swap fragment using a literal `tmp` binding
 * @since 0.4-macros
 */
macro swap_bad(a, b): stmt {
    quote { var tmp = ${a}; ${a} = ${b}; ${b} = tmp }
}
// call site with a caller `tmp`:
//   var tmp = 99
//   @swap_bad(x, tmp)   →  var tmp = x; x = tmp; tmp = tmp   ← CAPTURE: wrong result
```

- **Rejected.** Silent capture is exactly the unreadable logic **M.3 forbids** — the same
  principle that bans preprocessor magic in the bootstrap (`TEKO_LEGISLATION.md:606-609`).

### A.3-Scheme-2 — gensym / auto-rename of macro-introduced bindings (stable-key) — *RECOMMENDED*

Every binding **introduced by the expansion** (a `var` in the quoted body, or
`Node::fresh_local()`) is renamed to a fresh deterministic symbol; SPLICED call-site nodes
(`${a}`) are inserted verbatim, so they still resolve in the caller's scope. The fresh name
is a **pure function of stable keys** — `__m_<macroname>_<callsite_line>_<callsite_col>_<idx>`
— never a global mutable counter, mirroring `lower_const`'s symbol determinism (fixpoint-safe,
§5.4).

```teko
/**
 * swap — Family A, HYGIENIC via gensym: the introduced temp is renamed to a stable fresh
 * symbol, so a caller `tmp` is never captured. Spliced args stay verbatim.
 *
 * @param a  first lvalue node
 * @param b  second lvalue node
 * @return   a capture-free swap fragment
 * @since 0.4-macros
 */
macro swap(a, b): stmt {
    quote { var tmp = ${a}; ${a} = ${b}; ${b} = tmp }   // `tmp` → __m_swap_<line>_<col>_0
}
// call site with a caller `tmp`:
//   var tmp = 99
//   @swap(x, tmp)  →  var __m_swap_12_4_0 = x; x = tmp; tmp = __m_swap_12_4_0   ← correct
```

- **Medium cost**, reuses the existing name-mangling infra. Upholds M.3 (no silent capture)
  with a bounded change, and the stable-key rule keeps expansion byte-identical across
  self-host generations.

### A.3-Scheme-3 — fully hygienic scoping (colored identifiers)

Every identifier carries its introduction context; macro-body names resolve in the macro's
DEFINITION scope, call-site names in the caller's. The resolver (`src/checker/resolve.tks`)
threads hygiene contexts through name resolution.

```teko
/**
 * with_lock — Family A, FULLY HYGIENIC: references a helper `unlock` from the MACRO's own
 * module. Colored hygiene resolves `unlock` in the macro's definition scope even though the
 * expansion lands in a caller that has its own unrelated `unlock`.
 *
 * @param lk    the lock node, spliced verbatim (caller scope)
 * @param body  the guarded block, spliced verbatim (caller scope)
 * @return      a lock/run/unlock fragment; `unlock` binds in the MACRO's scope
 * @since 0.4-macros
 */
macro with_lock(lk, body): stmt {
    quote { lock(${lk}); ${body}; unlock(${lk}) }   // `unlock` = the macro-module's unlock
}
// call site in a module with a DIFFERENT `unlock`:
//   @with_lock(m, { work() })  →  still calls the macro-module's unlock, not the caller's
```

- **Most correct, largest change** (resolver surgery). Deferred: gensym (Scheme-2) covers
  the capture-of-introduced-binding case, which is >90% of real hygiene needs; definition-scope
  reference resolution is the residual Scheme-3 buys.

**Recommendation: A.3-Scheme-2 (stable-key gensym); Scheme-1 rejected (M.3); Scheme-3
deferred.** Gensym is the bounded change that kills silent capture and stays fixpoint-safe.
Note: because **Family B produces a VALUE, it owes NO hygiene** — hygiene is a Family-A-only
obligation, which is another reason the stage split is clean.

---

# §B — FAMILY B (`comptime`, evaluation) detail

Family B runs after type-check; args are typed values; it computes a value inlined as a
literal. It is the Zig `comptime`. Its engine already exists — `eval_const` +
`literal_of` (`comptime_fold.tks:306`, `:1997`). The open sub-design is **how typed args and
value inlining wire into the pipeline** (§B.1), plus where `@sizeof`/`@typeof` live (§B.2).

## B.1 — comptime integration: how typed args + value inlining wire in

Three placements for the `comptime` expansion pass.

### B.1-Opt-1 — a pre-lower pass reusing `eval_const`, slotted at `project.tks:367` — *RECOMMENDED*

A new pass `expand_comptime(prog: TProgram): TProgram | error` runs **immediately before**
`inline_consts` at `src/build/project.tks:367` — after `monomorph` (`:353-354`), so types are
fully resolved. It: (1) finds each `@name(...)` `comptime` call in the TAST; (2) binds the
macro's params to the callers' argument `ConstValue`s via `eval_const` over an `Env`
(`scope.tks:72`); (3) evaluates the body with `eval_const`; (4) replaces the call `TExpr`
with `literal_of(result)`. Downstream (`inline_consts`, lowering) is UNTOUCHED — comptime
macros lower to ordinary literals.

```teko
/**
 * sum — Family B: folds a variadic pack of COMPILE-TIME integer values into their sum at
 * comptime, inlined as one literal. Each arg is a typed evaluable value (post-type-check);
 * a non-const arg is a call-site error.
 *
 * @param args  the call-site arguments, each required to be a compile-time constant
 * @return      the comptime sum, inlined as a literal at the call site
 * @throws      a call-site diagnostic if any argument is not compile-time-constant
 * @since 0.4-macros
 */
comptime sum(...args): usize {
    var total = 0 to usize
    for a in args { total = total + a }             // `a` is a typed value, not a node
    total
}
// call site:  @sum(2, 3, 5)  →  eval_const folds body → literal_of → the literal  10

/**
 * crc_table — Family B: builds a 256-entry CRC-32 lookup table at comptime, inlined as a
 * constant array. The loop runs entirely in `eval_const`; the emitted program contains only
 * the finished table (zero runtime init). The canonical "expensive table at comptime" case.
 *
 * @return  a 256-element `u32` array, inlined as a constant aggregate (CVAgg)
 * @since 0.4-macros
 */
comptime crc_table(): [256]u32 {
    var t = [0 to u32; 256]
    var i = 0 to usize
    loop while i < 256 {
        var c = i to u32
        var k = 0
        loop while k < 8 { c = if (c & 1) != 0 { 0xEDB88320 ^ (c >> 1) } else { c >> 1 }; k = k + 1 }
        t[i] = c; i = i + 1
    }
    t
}
// call site:  const CRC = @crc_table()  →  the 256-entry table baked into rodata
```

- **Clean layering, maximal reuse** — `eval_const` + `literal_of` + `Env` are the whole
  engine; no typer surgery. The `CVAgg` domain (`comptime_fold.tks`) already carries the
  aggregate the table needs.

### B.1-Opt-2 — typer-lazy (fold on demand inside the typer)

The typer expands a `comptime` call the moment it types the surrounding expression, so the
macro's result type participates in inference immediately.

```teko
/**
 * bit_width — Family B, typer-lazy rationale: a macro whose RESULT drives the type of the
 * surrounding declaration, so it must fold DURING type-check, not after.
 *
 * @param n  a compile-time count
 * @return   the number of bits needed to hold `n`
 * @since 0.4-macros
 */
comptime bit_width(n: usize): usize {
    var w = 0 to usize; var v = n
    loop while v > 0 { w = w + 1; v = v >> 1 }
    w
}
// call site:  var flags: u@bit_width(300) = 0   ← the macro result names a TYPE width
//   → needs the fold to complete mid-typecheck (Opt-1's post-pass is too late here)
```

- **Cost:** couples expansion into `typer.tks` with careful ordering (a comptime call may
  reference another). Necessary ONLY if a comptime result must feed type inference (the
  `u@bit_width(300)` case above). Heavier; reserve for when that capability is actually
  requested.

### B.1-Opt-3 — two-phase (collect, then a fixpoint expand loop)

A first phase collects every `comptime` call and its dependency edges; a second phase expands
in dependency order to a fixpoint, so a comptime macro may call another comptime macro whose
result it consumes.

```teko
/**
 * pow2 — Family B, two-phase rationale: a comptime macro that CALLS another comptime macro;
 * the expander must fold `pow2` before the caller that uses `@pow2(k)` can fold.
 *
 * @param k  a compile-time exponent
 * @return   2 raised to `k`, at comptime
 * @since 0.4-macros
 */
comptime pow2(k: usize): usize { 1 to usize << k }
comptime mask(k: usize): usize { @pow2(k) - 1 }      // comptime calling comptime
// call site:  const M = @mask(8)  →  expander folds pow2(8)=256 first, then mask → 255
```

- **Cost:** a dependency-ordered fixpoint loop with cycle detection (bounded depth, §5.4).
  Most general; needed once comptime-calls-comptime is common. Opt-1 already handles a single
  level (the inner call is just another `@` the same pass visits) — Opt-3 is the escalation
  when nesting/ordering gets real.

**Recommendation: B.1-Opt-1 (pre-lower pass at `project.tks:367`) as the base, escalate to
Opt-3 (two-phase fixpoint) when comptime-calls-comptime lands; Opt-2 (typer-lazy) only if a
comptime result must name a type.** Opt-1 is one isolated pass reusing the entire mandated
comptime engine — lowest risk, satisfies `@sum`/`@crc_table` immediately.

## B.2 — where do `@sizeof(T)` / `@typeof(x)` live — Family B, or a builtin?

`@sizeof`/`@typeof` need TYPE info, which exists only post-type-check — so they are **Family
B** (`comptime`) if user-definable, or compiler **builtins** if not. Three placements.

### B.2-Opt-a — compiler builtin intrinsics (`@sizeof`/`@typeof` are reserved comptime names) — *RECOMMENDED*

They are reserved `@`-names the comptime pass resolves against the type table directly — like
`svc<T>` is a comptime intrinsic (`mudancas-superficie §7`). Not user-authored, because they
need type-table access no user body has.

```teko
// no user declaration — reserved comptime intrinsics resolved against the TypeTable:
// call site:  const N = @sizeof(u32)      →  the literal  4
// call site:  const NM = @sizeof(Point)   →  the struct's size in bytes, inlined
// call site:  @typeof(x)                  →  the static type of `x`, usable in a type position
```

- **Cost:** two reserved names + a type-table query. **Unlocks** portable size math with zero
  user surface. Honest: they cannot be user bodies (no user has the type table), so making
  them builtins is truthful about the capability, not a limitation.

### B.2-Opt-b — a user-authored Family B macro over a reflected `Type` value

`comptime` bodies get a first-class `Type` value with a `.size()` method, so `@sizeof` is
ordinary library code.

```teko
/**
 * sizeof — Family B: a LIBRARY comptime macro over a reflected `Type` value. Requires
 * exposing `Type` reflection (`.size()`) into comptime bodies — larger surface, but
 * `@sizeof` stops being magic.
 *
 * @param T  a type argument, reflected as a comptime `Type` value
 * @return   the type's size in bytes, inlined
 * @since 0.4-macros
 */
comptime sizeof(T: type): usize { T.size() }
// call site:  const N = @sizeof(u32)  →  4
```

- **Cost:** a `type`-as-comptime-value reflection surface (`Type.size()`, later `.align()`,
  `.fields()`) — an M.0 increase. Powerful (opens user reflection) but pays surface for what
  Opt-a gives free.

### B.2-Opt-c — keep `sizeof` OUT of macros entirely — a plain `size_of<T>()` generic builtin

No `@` at all; `size_of<T>()` is an ordinary generic intrinsic function the checker folds.

```teko
// call site:  const N = size_of<u32>()   ← a generic builtin, no macro family involved
```

- **Cost:** zero macro surface, but it fragments the story ("some comptime facts are `@`,
  some are `foo<T>()`"). Cleaner if the owner wants `@` reserved strictly for user macros.

**Recommendation: B.2-Opt-a (builtin comptime intrinsics `@sizeof`/`@typeof`).** They need the
type table, so they honestly cannot be user bodies; making them reserved comptime names keeps
the `@` surface uniform without paying for full `Type` reflection (Opt-b) before a user macro
demands it. Revisit Opt-b when a user macro genuinely needs `.fields()`/`.align()`.

---

# §5 — CROSS-CUTTING MECHANICS (both families)

## 5.1 — the `@` pre-pass placement (per family, per stage)

The sealed model puts the two families at two stages, so there are **two passes**:

- **5.1-A — Family A pre-pass (before type-check).** `expand_macros_syntactic(prog:
  parser::Program): parser::Program | error` runs in `frontend_check`
  (`src/build/project.tks:464`) **immediately after `#os` pruning** (`:116-126`) and
  **before** `type_program_with_deps_pre_mono`. It rewrites `@macro`-calls in the untyped
  AST into their produced fragments, then the normal type-check sees only ordinary AST. This
  is the sealed `parse → AST → [expand] → TYPE-CHECK` order, literally.
- **5.1-B — Family B pass (after type-check).** `expand_comptime(prog: TProgram): TProgram |
  error` at `project.tks:367` (§B.1-Opt-1), after `monomorph`, before `inline_consts`. This
  is the sealed `TYPE-CHECK → [comptime eval] → lower` order.

```teko
/**
 * expand_macros_syntactic — Family A pre-pass: rewrite every `@macro`-call in the UNTYPED
 * program into its produced AST fragment, to a bounded fixpoint, before type-check runs.
 * Deterministic (stable-key gensym) so self-host generations are byte-identical.
 *
 * @param prog  the parsed, os-pruned program (types do NOT exist yet)
 * @return      the program with all Family-A calls expanded, or the first expansion error
 * @throws      an expansion-depth-exceeded diagnostic, or a macro body `@error`
 * @since 0.4-macros
 */
fn expand_macros_syntactic(prog: parser::Program): parser::Program | error { /* §9 crumb 3 */ }
```

**Recommendation:** two named passes at the two sealed slots — no attempt to merge them (the
stages are different tables, `parser::Program` vs `TProgram`). Reject any single-pass design:
it would force one family to run at the wrong stage, breaking the seal.

## 5.2 — variadic mechanics (a CONSEQUENCE of untypedness for Family A)

For Family A, variadics are **not a bolt-on feature** — they fall out of §A.0's untypedness:
an anonymous node has no declared type, so a *pack* of anonymous nodes has no declared arity,
so `...args` is simply "the pack" and any count is legal for free. For Family B the args ARE
typed, so its variadic pack reuses the ordinary trailing-`is_params` discipline. Three shapes:

- **5.2-A — RECOMMENDED:** `...args` binds one **trailing** node/value pack after zero-or-more
  fixed params, reusing the established **trailing-only** `is_params` discipline
  (`ast.tks:511`, typer precedent). Family A → `args: []Node` (arity free, §A.0); Family B →
  `args: []<typed>` (each element const-foldable).

```teko
/**
 * printf_like — Family A: a fixed leading `fmt` node plus a trailing variadic pack, mirroring
 * the trailing-only `is_params` rule. Demonstrates fixed-then-pack binding.
 *
 * @param fmt   the leading format-string node
 * @param args  the trailing pack of argument nodes
 * @return      a print fragment
 * @since 0.4-macros
 */
macro printf_like(fmt, ...args): stmt { quote { do_print(${fmt}, $count(args.len)) } }
// call site:  @printf_like("x={}", a, b)  →  fmt fixed, args.len == 2
```

- **5.2-B:** a single pack only (no fixed leading params) — simpler, but can't express
  `@printf_like(fmt, ...)`. Rejected as too weak.
- **5.2-C:** splat forwarding `@m(...other)` to pass a pack through. Deferred (a later add).

**Recommendation: 5.2-A** (fixed params + one trailing pack, trailing-only). Forwarding
(5.2-C) is a later crumb.

## 5.3 — error model

- **5.3-A — RECOMMENDED primary:** macro errors are ordinary checker diagnostics at the
  **call site** (`@name` line/col), with a note pointing at the macro decl — reuse the
  `err_at` family. Family A errors surface either during expansion (bad node shape) or when
  the PRODUCED code fails the subsequent type-check (the sealed "ill-typed generated code
  fails normal type-check, pointing at the expansion"). Family B errors surface as
  `eval_const` failures (non-const arg, overflow) at the call site.
- **5.3-B:** report at the macro DEFINITION with a call-site backtrace note — better for buggy
  bodies, worse for the common misuse case. Secondary.
- **5.3-C — RECOMMENDED author intrinsic:** a `@error("msg")` intrinsic lets a macro body
  raise its OWN honest diagnostic (used in §A.1-Tier-2's `assert_eq`), the native mirror of
  the FFI **Tier-3 honest stop** (`star-ref-and-ffi-0.3.1.md:183-187`) — a direct M.3
  expression.

```teko
/**
 * require_ident — Family A: rejects, with an author-authored honest diagnostic, any argument
 * that is not a bare identifier. Demonstrates `@error` (5.3-C) at the call site (5.3-A).
 *
 * @param x  the argument node; must be a `Var`
 * @return   a fragment binding a shadow of `x`
 * @throws   a compile-time `@error` naming the offending node's source
 * @since 0.4-macros
 */
macro require_ident(x): stmt {
    if x.kind != NodeKind::Var { @error("expected an identifier, got `" ++ x.source ++ "`") }
    quote { var shadow = ${x} }
}
// call site:  @require_ident(a + 1)  →  compile error: expected an identifier, got `a + 1`
```

**Recommendation: 5.3-A primary + 5.3-C intrinsic.** FFI-unresolvable C macros (Tier 3) emit
the honest error the FFI doc already specifies.

## 5.4 — fixpoint & seed safety (both families)

**Fixpoint (self-host byte-identity).** Expansion MUST be deterministic and produce
byte-identical output across generations, exactly as `lower_const` guarantees symbol
byte-identity:
- Family B folds to literals via `literal_of` — inherently deterministic.
- Family A gensym (§A.3-Scheme-2) names are a **pure function of stable keys** (macro name +
  call-site line/col + local index — all available on `Expr.line`/`col`, `ast.tks:293`),
  **never** a global mutable counter — mirroring `const_leaf_symbol`/`const_rodata_symbol`.
- **Expansion depth is bounded**: a macro that expands (directly or mutually) past a fixed
  depth is a **compile error** (a REJECT fixture, §8) — no unbounded/nondeterministic
  expansion reaches type-check (Family A) or lowering (Family B).

```teko
/**
 * loop_macro — Family A, the fixpoint HAZARD, caught: a macro that re-emits a call to itself
 * must hit the bounded-depth guard and become a compile error rather than expand forever.
 *
 * @param x  any node
 * @return   a fragment that (illegally) re-invokes the same macro
 * @since 0.4-macros
 */
macro loop_macro(x): stmt { quote { @loop_macro(${x}) } }
// call site:  @loop_macro(a)  →  compile error: macro expansion depth exceeded
```

**Seed safety (bootstrap ordering).** The bootstrap seed is the previously released `teko`
binary; the corpus must not USE a feature its seed lacks. Therefore:
1. Land lexer + parser + checker + **both** expansion passes — `src/**.tks` does **not** yet
   use `@`/`macro`/`comptime`.
2. **SEED BUMP** — release a seed that understands the two families (same discipline as
   module-const #594 → D40/D41).
3. **Only after the bump** may the corpus use `@`/`macro`/`comptime`. The FFI side gates the
   same way: **`extern comptime` Tier-0 constants first** (value inlining, no IR), then
   `extern macro` Tiers 1-2 with the own backend, Tier 3 the always-present honest error.

---

# §7 — FFI: `extern macro` (function-like C) + `extern comptime` (C constants)

The sealed rule: **FFI = `extern` + the family.** The 4-tier resolver
(`star-ref-and-ffi-0.3.1.md:147-194`) splits cleanly across the two families — Tier 0
(object-like constants) is a VALUE, so it is `extern comptime`; Tiers 1-3 (function-like) are
`extern macro`. Declaration reuses the `extern fn` `Function` fields verbatim
(`is_extern`/`c_symbol`/`from_lib`, `ast.tks:533-535`); the CALL migrates to `@`.

```teko
/**
 * O_RDONLY — FFI Tier 0 as `extern comptime`: the resolver reads the header, extracts the
 * `#define`, evaluates the C constant-expression with its mini evaluator, and inlines the
 * value. No body, no runtime, no `cc`. Ships FIRST (value inlining only).
 *
 * @return  the resolved integer constant, inlined at each `@O_RDONLY`
 * @since 0.3.1
 */
extern comptime O_RDONLY: i32 = "O_RDONLY" from header "fcntl.h"
// call site:  var f = open(path, @O_RDONLY)   →  the resolved constant, inlined

/**
 * htonl — FFI Tiers 1-2 as `extern macro`: the resolver classifies the function-like
 * `#define` — Tier 1 binds a real symbol, Tier 2 translates the C expression body to
 * own-backend IR. Tier 3 (arbitrary C) is the honest error.
 *
 * @param x  the u32 argument
 * @return   the byte-swapped result
 * @throws   Tier-3 honest error if the macro is not mechanically resolvable
 * @since 0.3.1
 */
extern macro htonl(x: u32): u32 = "htonl" from header "arpa/inet.h"
// call site:  var n = @htonl(host_order)   →  Tier-1 symbol call OR Tier-2 inlined IR
```

**Recommendation:** `extern comptime` for Tier 0, `extern macro` for Tiers 1-3; **`@` marks
every invocation** (native and FFI, both families) — this honours the sealed universal `@`
call marker and supersedes the FFI doc's plain-call `N(x)` sketch (the tier LOGIC is
unchanged; only the call marker becomes `@`). Update `star-ref-and-ffi-0.3.1.md` to `@`-call
+ the family split when the FFI side lands (Tier 0 / `extern comptime` first).

---

## 8. Regression fixtures (inputs → expected native exit codes)

Named fixtures (pattern `macro_a_*/`, `macro_b_*/`, `extern_*/`), gated on the **native**
engine (never `teko test` here — design only):

| Fixture | Family | Input shape | Expectation |
|---|---|---|---|
| `macro_a_count/` | A | `macro count(...args): usize { args.len }` + `@count(f(), g())` returned from `main` | ACCEPT; native **exit 2** (sealed example; f/g never run) |
| `macro_a_count_empty/` | A | `@count()` | ACCEPT; native **exit 0** |
| `macro_a_stringify/` | A | `@stringify(a + b)` compared to `"a + b"`, exit = match | ACCEPT; **exit 0** on match |
| `macro_a_unless/` | A | `@unless(cond, { … })` expands to `if !(cond) {…}` | ACCEPT; exit proves the negated branch ran |
| `macro_a_hygiene_no_capture/` | A | `@swap(x, tmp)` with a caller `tmp` (§A.3-Scheme-2) | ACCEPT; gensym prevents capture; exit proves caller `tmp` intact |
| `macro_a_error_intrinsic_rejected/` | A | `@require_ident(a + 1)` (5.3-C) | REJECT — author `@error` "expected an identifier" (`EXPECT_COMPILE_FAIL`) |
| `macro_a_depth_rejected/` | A | `@loop_macro(a)` self-expanding (§5.4) | REJECT — "macro expansion depth exceeded" |
| `macro_b_sum/` | B | `comptime sum(...args): usize { … }` + `@sum(2,3,5)` | ACCEPT; native **exit 10** |
| `macro_b_crc_table/` | B | `const CRC = @crc_table()`, exit = `CRC[i] & 0xFF` | ACCEPT; exit = the table entry (proves comptime fold) |
| `macro_b_sizeof/` | B | `@sizeof(u32)` (B.2-Opt-a intrinsic) returned from `main` | ACCEPT; native **exit 4** |
| `macro_b_nonconst_rejected/` | B | `@sum(a, b)` on runtime locals | REJECT — "argument N is not a compile-time constant" (`EXPECT_COMPILE_FAIL`) |
| `macro_unknown_rejected/` | both | `@nope(x)` with no such macro | REJECT — unknown macro at call site |
| `macro_arity_mismatch_rejected/` | both | fixed-param macro called with wrong arity | REJECT — arity diagnostic |
| `extern_comptime_o_rdonly/` | FFI B | `extern comptime O_RDONLY: i32 = "O_RDONLY" from header "fcntl.h"` + `@O_RDONLY` (Tier 0) | ACCEPT; exit = the resolved constant |
| `extern_macro_htonl/` | FFI A | `extern macro htonl(x: u32): u32 = "htonl" …` + `@htonl(k)` (Tier 1/2) | ACCEPT; exit = the byte-swapped value |
| `extern_macro_tier3_rejected/` | FFI A | an arbitrary/statement C macro (Tier 3) | REJECT — honest "not mechanically resolvable" |

Each new/changed line carries **100% delta coverage** (D32, `DECISION_LOG.md:312`), measured
on the native gate, with any genuinely-unreachable arm listed with a one-line justification.

---

## 9. Ordered crumb sketch (recommended composite)

Each crumb is independently gate-able; **ritual points** (full C+self-host+native gate +
FIXPOINT) are marked ★.

1. **Lex `@` + the two family keywords.** Add `At` TokenKind (`token.tks`); emit it for a
   bare `@` not starting a string (`lexer.tks` `read_string_body`/`read_symbol`, guarding the
   `@"…"` verbatim path at `:429-453`); reserve `macro`/`comptime` in `keyword_kind`
   (`lexer.tks:331-372`). Gate: lexer tests.
2. **AST + parse (both families).** Add `MacroDecl`/`ComptimeDecl` (native + `extern` forms)
   to `Decl` (`ast.tks:807`) and a `MacroCall` node to `ExprKind` (`:292`) + `Node`
   scaffolding (A.1-Tier-1); parse the decls reusing the `extern fn` path
   (`parse_decl.tks:401-489`) and the `@`-prefix call in `parse_expr`. Honest-stop bodies
   that compile. Gate: parser tests.
3. ★ **Family A pre-pass (syntactic, §5.1-A).** `expand_macros_syntactic` after `#os` prune,
   before type-check (`project.tks:464`/`:116-126`); A.1-Tier-1 surface (`.len`/`.source`),
   A.2-Var-1 `quote`/`${}`, A.3-Scheme-2 gensym, 5.3-A/5.3-C errors, 5.4 bounded depth. Gate:
   FULL gate + `macro_a_count` (exit 2), `macro_a_stringify`, `macro_a_hygiene_no_capture`,
   `macro_a_depth_rejected`, `macro_a_error_intrinsic_rejected`.
4. ★ **Family B pass (comptime, §B.1-Opt-1).** `expand_comptime` before `inline_consts`
   (`project.tks:367`); bind typed args, `eval_const` the body, `literal_of` the result;
   `@sizeof`/`@typeof` intrinsics (B.2-Opt-a); 5.3-A errors. Gate: FULL gate + `macro_b_sum`
   (exit 10), `macro_b_crc_table`, `macro_b_sizeof` (exit 4), `macro_b_nonconst_rejected`.
5. ★ **FFI Tier 0 (`extern comptime`).** Object-like C-constant resolver (mini C-constant
   evaluator), `@O_RDONLY` inlined; update `star-ref-and-ffi-0.3.1.md` to `@`-call + family
   split. Gate: FULL gate (`extern_comptime_o_rdonly`, `extern_macro_tier3_rejected`).
6. ★ **SEED BUMP #1.** Release a seed understanding both families; only after may `src/**.tks`
   use `@`/`macro`/`comptime` (D40/D41 discipline). **Ritual.**
7. **Family A Tier-2 surface (post-seed).** `.kind` + read accessors (A.1-Tier-2) →
   `@assert_eq`; A.3-Scheme-3 colored hygiene only if demanded. Gate: FULL gate +
   `macro_a_unless`, structure-directed fixtures.
8. **FFI Tiers 1-2 (`extern macro`)** (symbol-alias, C-expr→own-IR); Tier 3 stays the honest
   error; B.1-Opt-3 two-phase if comptime-calls-comptime lands. Gate: FULL gate, coupled to
   the own backend (`extern_macro_htonl`).

**Ritual points (full gate must pass):** crumbs **3** (Family A pre-pass — new pre-typecheck
program transform), **4** (Family B comptime pass — shared comptime engine), **5** (FFI
resolver), **6** (seed bump).

---

## 10. Risks & law tensions (each with a recommended resolution)

| Risk / tension | Where it bites | Recommended resolution |
|---|---|---|
| **D33 / `TEKO_LEGISLATION.md:607` "no macros" seal** | The facility exists at all | Owner override (§14 seal) — noted, not relitigated. On landing, amend D33's reference + the legislation line to "superseded for the macro facility by owner ruling" (doc-sync, not a code change here). |
| **M.0 small-language surface** | Family A `Node` reflection; Family B `Type` reflection | Stage the surface: A.1-Tier-1 (`.len`/`.source`) in the seed, Tier-2 (`.kind`/accessors) as a gated crumb, Tier-3 builder deferred; B.2-Opt-a builtins over B.2-Opt-b `Type` reflection. Each tier its own crumb. |
| **M.3 honesty vs. silent capture** | Family A splice hygiene | A.3-Scheme-1 rejected; **A.3-Scheme-2 gensym** adopted; Family B owes NO hygiene (produces a value). |
| **Fixpoint (self-host byte-identity)** | Family A gensym; expansion determinism | **Stable-key gensym** (pure fn of macro name + call-site loc, `Expr.line`/`col`), bounded depth, `literal_of` determinism — mirrors `lower_const`. |
| **Seed ordering** | Corpus using macros before the seed knows them | **Land-then-seed-bump** (crumb 6); corpus abstains until then; FFI Tier-0 (`extern comptime`) first. |
| **Two passes at two stages** | Family A on `parser::Program`, B on `TProgram` | Do NOT merge — two named passes at the two sealed slots (§5.1). Merging would force a family to the wrong stage, breaking the seal. |
| **Provisional keyword names** | `macro`/`comptime` are owner-pending | Write them throughout, isolated to `keyword_kind` (`lexer.tks:331-372`) so a final rename is a one-site change; note the pending confirmation (does NOT block any other crumb). |

**No genuine unresolved tension remains — no HALT.** Every judgment call above is resolved
law-first with a concrete recommendation, and every proposal carries a runnable Teko example
(macro body + call site + effect). The owner deliberates over the alternatives, not over open
questions. The one owner-pending item — the final spelling of `macro`/`comptime` — is
explicitly a naming confirmation, not a design question, and is isolated to a single lexer
site so it blocks nothing.
