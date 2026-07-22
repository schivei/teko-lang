# The `.tkb` BDD regression format (Caso-2)

Status: RATIFIED (reconstructed after a container recycle lost the original). Canonical
authoring format for first-class regressives from 0.3.0.30. The 0.3.0.29 `.tkr` compact
form is RETAINED as an accepted legacy form and, structurally, becomes the *lowering IR*
of `.tkb`.

## 1. What `.tkb` is

`.tkb` is "Teko-native Gherkin": a Behaviour-Driven regressive spec that uses the Gherkin
step KEYWORDS (`Feature`, `Background`, `Scenario`, `Scenario Outline`, `Examples`,
`Given`, `When`, `Then`, `And`, `But`) with **TOML-value right-hand sides** read by the
EXISTING manifest value lexers (`mf_read_quoted` / `mf_read_array` / `mf_read_int`,
`src/build/manifest.tks`) verbatim. There is ZERO new value-lexing: the same `"…"`,
`[ "a", "b" ]`, and bare-integer grammars the `.tkp`/`.tkr` readers already use.

One `.tkb` file describes ONE regressive project: the directory's single `.tkp` entry
point, compiled ONCE. Each `Scenario` is (semantically) one `Tkr` — so the entire `.tkr`
CHECK layer (`check_run`, `check_compile_fail`, `match_stream`, `resolve_stream`,
`tkr_expect_for`, `RegrOutcome`) is REUSED unchanged. A `Scenario Outline` + `Examples`
table is a *parametrised* family of `Given` cases: the program is compiled once and RUN
N times (one run per Examples row), each row lowering to its own `Tkr`.

The design principle: `.tkb` adds a human-legible, table-parametrised *front-end* over the
already-shipped `Tkr` model + runner. It introduces new *parsing* (keyword lines, the
Examples table, HEX docstrings) and a new *compile-once / run-per-row* loop, and NOTHING
new in the check/verdict layer.

## 2. Grammar (line-oriented, minimal — M.5)

A `.tkb` file is a sequence of lines. Leading whitespace is insignificant. A line is one of:

- blank / `#`-comment — skipped (M.2).
- `Feature: <free text>` — opens the file; the free text is documentation only.
- `Background:` — opens a step block whose `Given` steps are PREPENDED to every scenario
  (shared opaque inputs: common `args`, `stdin`, `targets`).
- `Scenario: <free text>` — opens a single-run scenario.
- `Scenario Outline: <free text>` — opens a parametrised scenario; MUST be followed
  (after its steps) by an `Examples:` block.
- `Examples:` — opens the parameter table. The FIRST non-blank line after it is the header
  row (`| a | b | exit |`); each subsequent `| … | … |` line is one parameter row.
- `<Keyword> <noun-phrase> = <toml-value>` — a step. `<Keyword>` is
  `Given`/`When`/`Then`/`And`/`But`. `And`/`But` inherit the phase (Given/When/Then) of
  the preceding step. The noun-phrase (§3) selects WHICH `Tkr` field the value sets; the
  `<toml-value>` is read by the existing `mf_read_*` lexers.
- A `When` entry-verb line has NO `= value` (e.g. `When built and run`, `When compiled`,
  `When compilation fails`) — it classifies the scenario's `TkrKind` (§4).

Placeholders `<name>` inside a Scenario Outline step value are substituted from the
matching Examples column before the step is lowered (§5).

## 3. Noun-phrases (Given inputs / Then expectations)

Every noun-phrase maps to exactly one field of the reused `Tkr` (or a `[expect.<os-arch>]`
override when prefixed by `on "<os-arch>"`, §6). The phrase set:

Given (opaque runtime inputs — the runtime-operand law, §7):
- `args` = `[ "…", … ]` → `Tkr.args` (the fold-opaque argv operands)
- `stdin` = `"…"` → `Tkr.stdin` (literal bytes)
- `stdin bytes` = `"""<hex>"""` → `Tkr.stdin` decoded from a HEX docstring (§8)
- `targets` = `[ "…" ]` / `"all"` → `Tkr.targets`

When (entry verb — classifies the kind, no value):
- `built and run` → `TkrKind::Exit`/`Print` (the observable-output kind; the presence of a
  `Then stdout*` phrase promotes Exit→Print semantics — both check exit, Print also checks
  stdout via the reused `check_run`)
- `compiled` → same as `built and run` but asserts only the build + declared streams
- `compilation fails` → `TkrKind::CompileFail` (assert the BUILD fails)
- `it traps` may be written as `Then it traps` → `TkrKind::Trap` (non-zero exit only)

Then (expected observable outputs):
- `exit` = `<int>` → `Tkr.exit`
- `stdout` = `"…"` → `Tkr.stdout` (Literal)
- `stdout bytes` = `"""<hex>"""` → `Tkr.stdout` (Literal, control-byte-safe, §8)
- `stdout sha256` = `"<hex>"` → `Tkr.stdout` (Sha256)
- `stderr` = `"…"` / `stderr sha256` = `"…"` → `Tkr.stderr` (Literal/Sha256)
- `artifact sha256` = `"<hex>"` → a `[golden.<os-arch>]` entry (opt-in byte golden; requires
  an `on "<os-arch>"` prefix)
- `diagnostic` = `"…"` → `Tkr.diagnostic` (the required stderr substring for
  `compilation fails`)

Unknown noun-phrases are an HONEST error (M.3 — no silent coercion), unlike unknown *root
keys* in `.tkr` (which stay forward-compatible); a `.tkb` phrase is a closed, curated set.

## 4. Scenario ↔ Tkr, and the kind

A single `Scenario` lowers to ONE `Tkr`:
- its `Given` steps set the opaque inputs (`args`/`stdin`/`targets`), after the
  `Background` Given steps are prepended;
- its `When` entry verb sets `Tkr.kind`;
- its `Then`/`And`/`But` steps set the expectations (`exit`/`stdout`/`stderr`/`diagnostic`
  /golden).

The verdict is produced by the UNCHANGED check layer: `check_compile_fail(t, comp)` for
`compilation fails`, else `check_run(t, target, run)`.

## 5. Scenario Outline + Examples (parametrised GIVEN cases)

A `Scenario Outline` carries `<placeholder>` tokens inside its step values. Its `Examples:`
table has a header row naming the placeholders and one data row per case:

```
  Scenario Outline: bitand of two runtime operands
    Given args = ["<a>", "<b>"]
    When built and run
    Then exit = <r>
    Examples:
      | a  | b  | r |
      | 12 | 10 | 8 |
      | 255| 1  | 1 |
      | 0  | 99 | 0 |
```

Lowering: the PROGRAM (the dir's `.tkp`) is compiled ONCE. For each Examples row, every
`<name>` in every step value is textually replaced by that row's column value, the
substituted steps are lowered to a `Tkr`, and the built binary is RUN with that `Tkr`'s
`args`/`stdin`; the verdict is checked per row. N rows ⇒ 1 compile + N runs. This is the
"replace many unit golden vectors with one executing regressive" lever (§7).

Substitution is purely textual and happens BEFORE the `mf_read_*` value lexers see the
line, so a placeholder may sit anywhere inside a quoted string or an array element.

## 6. Per-os-arch routing: the `on "<os-arch>"` prefix

A `Then` step may be prefixed `on "<os-arch>"` to route the expectation into a
`[expect.<os-arch>]` override (reusing `TkrExpect`) instead of the top-level expectation:

```
    Then exit = 6
    Then on "wasm" exit = 7
    Then on "x86_64-linux" artifact sha256 = "…"
```

`on "…" exit/stdout/stderr` populate a `TkrExpect`; `on "…" artifact sha256` populates a
`TkrGolden`. Resolution at run time is the existing `tkr_expect_for` / `resolve_stream`.

## 7. The runtime-operand law (why `.tkb` scenarios EXECUTE the op)

Every `Given args`/`stdin` value is a FOLD-OPAQUE runtime operand: it arrives through the
process boundary (argv/stdin) and is therefore unknown at compile time, so the compiler
CANNOT const-fold the operation under test. The emitted native code must actually perform
the arithmetic / bitwise / shift / compare / concat / index-store at run time, and the
regressive observes the real result. A `Scenario Outline` with an N-row `Examples` table
thus exercises N genuinely-executed cases through the production codegen path, replacing N
separate comptime-fold golden unit vectors with one breadth-covering executing regressive.

## 8. HEX docstrings (`"""48 89 e5"""`) — control-byte goldens

A triple-quoted docstring holds space- and newline-tolerant hexadecimal: all ASCII
whitespace between the `"""` fences is ignored, the remaining hex nibble-pairs decode to
raw bytes. This carries CONTROL bytes (`\n`, `\0`, opcode bytes) LITERALLY — fixing the
`.tkr` gap where a quoted literal could not embed a newline (`mf_read_quoted` stops at the
first `"` and never interprets escapes). Used for:
- `stdin bytes` / `stdout bytes` — a byte-exact stream whose bytes you READ (short records,
  opcode sequences).

Reconciliation rule (HEX vs SHA256):
- HEX docstring for bytes you READ and reason about (opcodes, short binary records) — the
  spec doubles as human-legible documentation of the exact bytes.
- `sha256` for bytes you only FIX and never read (large or opaque streams / artifacts).

## 9. Relationship to `.tkr` (the lowering IR)

- `.tkb` is CANONICAL for new regressives.
- `.tkr` is RETAINED as the accepted legacy compact form and, structurally, IS the lowering
  IR: `parse_feature` → `[]Scenario` → (per scenario / per Examples row) `Tkr`, then the
  SAME `check_run`/`check_compile_fail` verdict. `Tkr` + the check layer are frozen contract.
- Discovery accepts a directory carrying EITHER a `.tkb` (preferred) OR a `.tkr` (legacy).
  A directory with both prefers the `.tkb` and the `.tkr` is treated as an additional
  legacy sibling only when explicitly named (never double-run).

## 10. Fixtures the format ships with (0.3.0.30)

- 5 passing `.tkb` + 2 failing `.tkb` (the failing pair proves the runner REPORTS a
  mismatch, mirroring the `regressions-fail/` demo).
- Meta-tests (`#test` in `src/build/tkb_test.tkt`) asserting: parse round-trips; a
  Scenario lowers to the expected `Tkr`; an Outline expands to the expected `[]Tkr`;
  verdict PARITY between a `.tkb` scenario and the equivalent hand-written `.tkr`
  (identical `RegrOutcome`); and that a retained `.tkr` still passes unchanged.
