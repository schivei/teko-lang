# examples/probes/match_shapes — WHICH `match` SHAPES CAN THE NATIVE BACKEND LOWER?

TEMPORARY. Delete this directory when the null-union lowering lands; it exists to answer one
question with data instead of by reading code.

## Why it exists

The `bulk` regressor stops the native backend with a type, and no site and no line:

    native backend N1: arithmetic operand of type `null | u64` … [in `bulk::q001_…::entry`]

That message was wrong twice over. `prim_kind_of` is asked "which single PrimKind is this?" from
NINE places, and the text said "arithmetic operand" for all of them — so a stop from a COMPARISON
read as a stop in a SUM. Two hypotheses were built on that reading (`q001`'s `let`s, then a generic
`get`), and code-reading eliminated both while the real site stayed hidden.

Owner ruling 2026-07-28: *"não estaria forçando algo que tem atribuição dinâmica a se comportar como
fixa?"* — and: *"adicione saídas de log (temporárias) onde quer experimentar (já que ainda não temos
debugger), monte um projeto de teste com diversas variações e possibilidade com match e aplique a
teoria."*

That is what this is. Each case is its OWN project, so one case failing does not hide the next —
the `bulk` build stops at the first error and 216 fixtures go unmeasured behind it.

## The question each case isolates

A `u64 | null` has no single `PrimKind`: it carries a TAG resolved at run time. Any lowering that
demands one PrimKind from it is treating a dynamic value as a fixed scalar. These cases vary the
axes that could trigger that demand:

| case | axis |
|---|---|
| `m01_match_value_scalar`     | the `bulk` q001 shape — match to a value, arms yield the scalar |
| `m02_match_stmt_scalar`      | same union, match as a STATEMENT (no value) — is the value position the trigger? |
| `m03_match_no_arith`         | match to a value, result never enters arithmetic — is the `+` the trigger? |
| `m04_match_arms_literal`     | arms yield bare literals, no `to` cast — is the CAST the trigger? |
| `m05_match_i64`              | `i64 | null` instead of `u64` — is it width/sign specific? |
| `m06_match_bool`             | `bool | null` — non-arithmetic scalar |
| `m07_match_struct`           | `Box | null` — an aggregate member instead of a scalar |
| `m08_match_param`            | the union arrives as a PARAMETER, not a local |
| `m09_match_nested`           | a match whose arm body is another match |
| `m10_null_only_compare`      | no match at all — only `x == null` |

### The divergence axis (owner, 2026-07-28)

*"Faltou um m11, onde uma das pernas faz exit() panic() sem saída, e um m12 onde faz return de uma
função. Até com break e continue em laço."*

This axis is the ONLY place `type_match` takes a branch, and the first ten cases missed it entirely:

    if !tblock_diverges(ai.body) { let t = tblock_type(ai.body); … join … }

A DIVERGING arm is skipped in the join. So `tblock_diverges` decides which arms contribute a type at
all — and it decides it from the arm's LAST statement (`TReturn`, `TBreakStmt`, `TContinueStmt`, or a
trailing `panic`/`exit` call via `texpr_diverges`). Misjudge any of those and the joined set changes
under you, with no error anywhere.

| case | diverging arm |
|---|---|
| `m11_arm_exits`        | `null => exit(1)` — a trailing `exit` call |
| `m12_arm_returns`      | `null => return 7 to u64` — `TReturn` inside a value match |
| `m13_arm_panics`       | `null => panic("absent")` — the other `texpr_diverges` shape |
| `m14_arm_breaks`       | `null => break` — `TBreakStmt`, inside a loop |
| `m15_arm_continues`    | `null => continue` — `TContinueStmt`, inside a loop |
| `m16_all_arms_diverge` | BOTH arms `return` — `have_type` never set, the match must be void |

`m16` is the boundary the loop's own comment names (*"If ALL arms diverge the match type is void"*),
so it is the case where a wrong `have_type` shows up as a type out of nowhere.

## Running

    sh examples/probes/match_shapes/run.sh <path-to-teko>

Each case is built with `TEKO_BACKEND=native`; the script prints one row per case with the verdict
and, on a stop, the site the compiler names. It never fails — it REPORTS. Reading the table is the
point.
