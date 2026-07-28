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

### The `nNN` axis — ALIASES and NESTING (owner, 2026-07-28)

*"tem outro caso que precisa de atenção e acredito que falhe hoje: `type a = i32 | null` / `let b:
u64 | a | null`. São formas válidas que acredito que o lowering falhe ou em outro ponto."*

Neither axis was covered by the first 29 cases, and the `bulk` hunt proved one of them the hard way:
`q004` stops with *"a `null` in this position needs the null-union wrapper the placement does not
declare a type for"*, and that shape — a union inside a struct field inside another union — appears
nowhere in `mNN`/`tNN`.

| case | axis |
|---|---|
| `n01_alias_of_union`        | a NAMED alias that IS a union (`type A = i32 \| null`), used alone |
| `n02_union_of_alias`        | that alias as a MEMBER of another union (`u64 \| A \| null`) — present |
| `n03_union_of_alias_absent` | same, absent — and it asks whether the DUPLICATE null collapses to one |
| `n04_nested_field_union`    | the `q004` shape: union field inside a struct that is itself a union member |
| `n05_nested_field_inner_null` | same nesting, but the INNER union is the one holding null |

`n02`/`n03` are the interesting pair: `u64 | A | null` expands to `u64 | (i32 | null) | null`, so it
tests BOTH whether a named union member is flattened and whether two nulls collapse. If the answer
differs between them, the flattening is position-dependent.

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

## The `tNN` control group — the same shapes with the binding ANNOTATED

Owner ruling 2026-07-28: *"precisa de uma duplicata do mesmo teste, mas nesta, fixe os tipos (sem
auto tipos), logo os lets precisam declarar o tipo que esperam do match. Sei que vai dizer que isso
funciona já, mas tem fundamento de grupo de controle se já funciona (é o que esperamos)."*

Each `tNN` is its `mNN` twin with one character-level difference: `let r = match …` becomes
`let r: u64 = match …`. Nothing else moves.

That is the whole point of a control. The expectation IS that the annotated form works, and the
expectation being MET is the signal — a control that surprises you invalidates the experiment, and a
control that behaves as predicted is what licenses reading the treatment group at all.

| reading | what it isolates |
|---|---|
| `tNN` LOWERED, `mNN` STOPPED | INFERENCE is the variable — the annotation supplies what the match's own type does not |
| both STOPPED | inference is NOT the variable — drop it and look at the representation |
| `tNN` STOPPED, `mNN` LOWERED | the annotation itself introduces the stop; unexpected, and would be the finding |

Three `mNN` have no counterpart, and their absence is deliberate rather than an oversight: `m02`
(match as a statement), `m10` (no match at all) and `m16` (both arms `return`) bind nothing from the
match, so there is no type to annotate — a `tNN` twin would be a byte-identical copy.

## Three generations, and why the third is the one that matters

Owner ruling 2026-07-28: *"testar o mesmo conjunto com o 0.3.0.30-beta (a última lançada), se o
comportamento for o mesmo, dará trabalho encontrar, se não, então o erro está no trem."*

gen1 and gen2 are BOTH this train — gen1 is `cc` over wagon 15's committed C, gen2 is what gen1
builds from the tip. Comparing only those two can show that they disagree; it can never say WHEN the
behaviour started, because both live inside the window under suspicion.

`0.3.0.30-beta` is the last PUBLISHED binary, cut before this train existed. It is a fixed point
OUTSIDE the window, and it halves the search:

### Four points, read as three adjacent pairs

Owner ruling 2026-07-28: *"comparar a versão .30, a versão do teko.c do vagão 15 e a gen2 … vejamos
onde há erro, entre .30 e vagão 15 ou vagão 15 e nativo."*

`gen1` is NOT wagon 15. `build_with_seed_fallback.sh` cc's the committed `bootstrap/teko.c` into
wagon 15's compiler at `.rung-c/teko`, and THAT binary then builds the tip — so `gen1` and `gen2`
are both today's source, one generation apart. Wagon 15's own compiler is an intermediate the
pipeline discards, and it is exactly the point the bisection needs: the last compiler proven to walk
the whole ladder green.

| pair | question it answers |
|---|---|
| `.30` → `wagon15` | released binary vs the last green ladder — did the train's own history break it? |
| `wagon15` → `gen1` | wagon 15's compiler vs the tip it builds — did TODAY's source break it? |
| `gen1` → `gen2` | one self-host generation apart — does the compiler disagree with itself? |

Whichever pair flips a case is the half the defect lives in. A row identical across all four is a
gap that was always there, and belongs on the `.32` list rather than in a bisect.

## Running

    sh examples/probes/match_shapes/run.sh <path-to-teko> [label]

The label names which compiler produced the table (`gen1`, `gen2`, `seed-0.3.0.30`) and keeps the
per-case logs from overwriting each other between generations.

Each case is built with `TEKO_BACKEND=native`; the script prints one row per case with the verdict
and, on a stop, the site the compiler names. It never fails — it REPORTS. Reading the table is the
point.
