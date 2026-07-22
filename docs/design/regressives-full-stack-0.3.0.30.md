# 0.3.0.30 HEADLINE — REGRESSIVES FULL-STACK (integrated plan)

Umbrella: `remodel/emit-throughput` (post-0.3.0.29). Owner ruling: the regressive
SUPPORT, the `.tkr`/`.tkb` FORMATS, and the REAL regressives are developed TOGETHER —
each format capability lands WITH real fixtures that use it, never "the runner abstractly
then specs later". DESIGN ONLY; this doc is the ordered crumb sequence.

Companion: `docs/design/tkb-regression-format.md` (the `.tkb` format, reconstructed).

---

## 0. Ground truth (what exists, verified in-tree)

- The runner + `.tkr` model already SHIPPED inert in 0.3.0.29:
  `src/build/regression.tks` (discover→compile→run→check: `run_regression_phase`,
  `run_regression_sources`, `run_one_regressive`, `check_run`, `check_compile_fail`,
  `match_stream`, `dir_first_tkr`, `discover_source`, `compile_regressive`, `run_captured`)
  and `src/build/tkr.tks` (`parse_tkr` → `Tkr` + `TkrKind`/`TkrMatch`/`TkrExpect`/`TkrGolden`).
- `src/build/project.tks::test_project` ALREADY calls `run_regression_phase(exe, m)` when
  `m.test_regression_dirs.len > 0` (lines ~3288-3291). It is a zero-cost no-op today only
  because no consumed manifest declares `[tests] regression`.
- Manifest support is complete: `[tests] regression` / `gate` / `targets`
  (`Manifest.test_regression_dirs` / `test_gate` / `test_default_targets`, `manifest.tks`).
- CRUX (bootstrap-safety, verified): the BUILD gate does NOT run regressives. `teko . -o bin`
  goes `compile_project_g → run_gate → run_native_gate` (no `run_regression_phase`);
  `--no-verify` takes `build_ungated` (gate skipped entirely). ONLY `test_project` (the
  `teko test` path) runs the regression phase. Therefore declaring `[tests] regression` in
  `teko.tkp` activates regressives EXCLUSIVELY in the `teko test .` self-test lane
  (`tests.yml`) — never in the seed→gen1 build, never in `release.yml` (`--no-verify
  --release`). This is precisely the owner boundary: regressives run in the TEST lane, on
  gen1 self-test, with NO gen2-in-testing step.
- The anti-recursion sentinel `TEKO_IN_REGRESSION` (`run_regression_phase`) is the backstop;
  the structural belt is that each regressive is `teko build`-ed (`compile_regressive` runs
  `teko <dir> -o bin --no-verify` → `build_ungated`, no nested test phase).

Existing shell harnesses and their fate (full analysis in §6):

| harness | what it does | fate |
|---|---|---|
| `scripts/compile_fail_regressions.sh` | globs 31 `EXPECT_COMPILE_FAIL` dirs, asserts build fails (+ `EXPECT_STDERR`) | RETIRE (migrated to `.tkr` compile_fail) |
| `scripts/positive_regressions.sh` | 35 single-project `EXPECT_EXIT` dirs, build+run+exit | RETIRE (migrated to `.tkr` exit) |
| `scripts/native_regressions.sh` | `cd <proj> && teko build .` CWD-runtime-resolution guard (#64), char_ops | KEEP (distinct invocation shape; see §6.3) + migrate char_ops content |
| `scripts/crossmodule_regressions.sh` | 1 dep/consumer `.tkl` fixture (`const_crossmodule_inline`) | KEEP this wave (needs `packages/` provisioning the runner lacks; §6.4 design-ahead) |
| `scripts/diff_c_own.sh` | C-vs-own BACKEND differential + `.o` host-tool checks | KEEP (backend differential, not a plain regressive) |
| `scripts/validate_wasm_own.sh` | own-wasm == C-native BACKEND differential | KEEP (backend differential) |
| sanitizers.yml build-all loops | build every fixture under ASan/paranoid | KEEP (sanitizer corpus, not a verdict check) |

Migration inventory (exact per-fixture mapping) is §5.

---

## 1. Crumb sequence overview

Each crumb builds under the previous seed (the 0.3.0.29 released binary is the C0/C1 seed;
no crumb introduces a compiler-source feature absent from its seed — the whole workstream
uses only str/list/match/struct/enum/loop, all present in 0.3.0.29). `[RITUAL]` marks a
full-gate point (build gen1 + `teko test .` on all three OSes green AND within the tests.yml
budget — the duration delta MUST be measured at each ritual, see §7 risk R1).

- **C0** — Docs + `.tkb` scaffold (`tkb.tks` skeleton, honest-stop). Compiles, dormant.
- **C1** — First light: migrate ~5 fixtures to `.tkr`, turn `teko.tkp` non-inert. `[RITUAL]`
- **C2** — Migrate the 31 `EXPECT_COMPILE_FAIL` fixtures to `.tkr` compile_fail. `[RITUAL]`
- **C3** — Migrate the 35 `EXPECT_EXIT` + char_ops to `.tkr` exit. `[RITUAL]`
- **C4** — Real `.tkb` parser + compile-once/run-per-scenario loop + discovery for `.tkb`. `[RITUAL]`
- **C5** — Full-stack `fs_*` matrix as `.tkb` (runtime-operand law) + parity meta-tests. `[RITUAL]`
- **C6** — `teko test . -- <args>` passthrough (and `[tests] args`). `[RITUAL]`
- **C7** — CI-lane-shrink + harness-retirement SPEC (owner applies `.github`/`scripts`). `[RITUAL final]`

---

## 2. Per-crumb detail

### C0 — Docs + `.tkb` scaffold (design-ahead, dormant)

Files:
- `docs/design/tkb-regression-format.md` (done, this wave)
- `docs/design/regressives-full-stack-0.3.0.30.md` (this doc)
- NEW `src/build/tkb.tks` — the type contracts + an honest-stop `parse_feature`. Compiles
  today; NOT yet called by discovery, so behaviour is unchanged (dormant scaffold).

New type signatures (copy-paste ready, full Javadoc):

```teko
/**
 * TkbPhase — the Gherkin step PHASE a line belongs to: `Given` (opaque runtime inputs),
 * `When` (the entry verb that classifies the kind), or `Then` (the expected observable
 * output). `And`/`But` do not appear here — they inherit the preceding step's phase.
 *
 * @since 0.3.0.30
 */
pub type TkbPhase = enum { Given; When; Then }

/**
 * TkbStep — one lowered Gherkin step: the resolved `phase`, the `noun` phrase that selects
 * a `Tkr` field (e.g. "args", "exit", "stdout sha256"), and the raw value TEXT (already
 * placeholder-substituted, still in its TOML surface form so the reused `mf_read_*` lexers
 * decode it). An entry-verb `When` step carries an empty `noun` and its verb in `verb`.
 *
 * @since 0.3.0.30
 */
pub type TkbStep = struct {
    /** the resolved phase (And/But inherit the previous step's). */
    phase: TkbPhase
    /** the noun-phrase selecting the target `Tkr` field ("" for an entry-verb When). */
    noun: str
    /** the entry verb for a When ("built and run" | "compiled" | "compilation fails" | ""). */
    verb: str
    /** the os-arch route from an `on "<os-arch>"` prefix ("" = top-level expectation). */
    osarch: str
    /** the raw TOML value text to the right of `=` ("" for a valueless entry verb). */
    value: str
}

/**
 * TkbExamples — a Scenario Outline's parameter table: the `headers` (placeholder names) and
 * the `rows` (one `[]str` of column values per case). Empty `headers` ⇒ a non-outline
 * scenario (a single implicit row).
 *
 * @since 0.3.0.30
 */
pub type TkbExamples = struct {
    /** the placeholder column names from the header row. */
    headers: []str
    /** one row of column values per parametrised case. */
    rows: [][]str
}

/**
 * TkbScenario — one scenario: its free-text `name`, whether it is an `outline`, its ordered
 * `steps` (Background Given steps already prepended), and its `examples` table (empty for a
 * plain scenario). Lowers to one `Tkr` (plain) or one `Tkr` per Examples row (outline).
 *
 * @since 0.3.0.30
 */
pub type TkbScenario = struct {
    /** the scenario's free-text label (documentation / report line). */
    name: str
    /** true for a `Scenario Outline` (expanded per Examples row). */
    outline: bool
    /** the ordered steps (Background Given steps prepended). */
    steps: []TkbStep
    /** the parameter table (empty headers ⇒ a single implicit row). */
    examples: TkbExamples
}

/**
 * TkbFeature — one parsed `.tkb` file: the `name` free text and the list of `scenarios`
 * (Background already folded into each scenario's steps). One feature = one regressive
 * project (the dir's single `.tkp`), compiled once.
 *
 * @since 0.3.0.30
 */
pub type TkbFeature = struct {
    /** the Feature free-text label. */
    name: str
    /** the scenarios, each already carrying the Background Given steps. */
    scenarios: []TkbScenario
}

/**
 * parse_feature — read a `.tkb` source into a `TkbFeature`, or fail honestly (M.3). Reuses
 * the manifest value lexers (`mf_read_quoted`/`mf_read_array`/`mf_read_int`) verbatim for
 * every right-hand side; introduces only keyword-line + Examples-table + HEX-docstring
 * parsing. Background Given steps are prepended to every scenario's steps.
 *
 * @param src  the `.tkb` file contents
 * @return     the parsed feature, or an `error` on any malformed line / unknown noun-phrase
 * @throws     on a malformed keyword line, an unknown noun-phrase, or a malformed value
 * @since 0.3.0.30
 */
fn parse_feature(src: str) -> TkbFeature | error {
    // C0: honest-stop scaffold — the real body lands in C4.
    error { message = "tkb: parse_feature not yet implemented (0.3.0.30 C4)" }
}
```

Fixtures added: none. Gate: light (must compile; `tkb.tks` unreferenced so `teko test .`
is unchanged). NOT a `[RITUAL]`.

### C1 — First light: turn `teko.tkp` non-inert

The smallest end-to-end slice, using ONLY the 0.3.0.29 runner + `parse_tkr` (no new code).

Files touched:
- Add `test.tkr` to a first batch of 5 fixtures that are ALSO in the backend corpora
  (so we validate the mechanism on already-trusted programs):
  `own_arith_exit` (`exit = 42`), `own_sub_exit` (`exit = 42`),
  `native_lir_exit`, `native_lir_neg`, `native_lir_div` (their `.tkp` document the exit).
- `teko.tkp`: add
  ```
  [tests]
  regression = ["examples/regressions"]
  gate = true
  targets = ["host"]
  ```
  `discover_source("examples/regressions")` scans all 207 subdirs and runs EXACTLY those
  carrying a spec file; with only 5 `.tkr` present, only 5 run. Every later crumb that adds
  a `.tkr`/`.tkb` is picked up automatically — no further `teko.tkp` edit.

Type signatures: none (existing runner). Behaviour: `teko test .` (tests.yml) now discovers
and runs 5 regressives with gen1 as the compiler.

Fixtures: the 5 `.tkr` above. Note each fixture build writes `<dir>/bin/` — see risk R2
(gitignore) — resolve in this crumb.

`[RITUAL]` — the moment the base goes live. Confirm: (a) all three `teko test .` OSes green;
(b) tests.yml duration delta acceptable (windows headroom under the 90-min cap); (c) the
committed tree stays clean after a run (R2).

### C2 — Migrate the 31 `EXPECT_COMPILE_FAIL` fixtures

Files touched: add `test.tkr` to each of the 31 dirs (full list §5.1). Shape:
```
kind = "compile_fail"
diagnostic = "<EXPECT_STDERR substring, or omit for failure-only>"
```
The 5 fixtures with a pinned `EXPECT_STDERR` (§5.1) carry `diagnostic`; the other 26 omit it
(assert build-failure only — `check_compile_fail` passes on any non-zero build exit).

KEEP the `EXPECT_COMPILE_FAIL` marker files in place — sanitizers.yml's build-all loops skip
on them (`if [ -f EXPECT_COMPILE_FAIL ]`). Removing them is a separate owner-owned CI edit
(§6); the `.tkr` is purely additive.

Type signatures: none. Fixtures: 31 `.tkr`. `[RITUAL]` — gen1 self-test now runs 31 compile_fail
regressives; verify green + duration.

### C3 — Migrate the 35 `EXPECT_EXIT` single-project fixtures + char_ops

Files touched: add `test.tkr` (`exit = <N>`) to each of the 35 single-project `EXPECT_EXIT`
dirs (§5.2) and to `char_ops` (`exit = 6`, from its `.tkp` documentation). `const_crossmodule_inline`
is EXCLUDED here (dep/consumer shape — §6.4). Keep the `EXPECT_EXIT` marker files (harness
retirement is C7/owner).

Type signatures: none. Fixtures: 36 `.tkr`. `[RITUAL]`.

### C4 — Real `.tkb` parser + compile-once/run-per-scenario loop + discovery

Files touched:
- `src/build/tkb.tks` — implement `parse_feature` (replace the C0 honest-stop) + the lowering
  + the run loop. Reuse `mf_*` (manifest.tks) and the whole `Tkr` check layer (tkr.tks +
  regression.tks) unchanged.
- `src/build/regression.tks` — generalise discovery to accept `.tkb` OR `.tkr` (see
  `dir_first_spec` below) and dispatch `run_one_regressive` on the spec kind.
- `src/build/tkb_test.tkt` — NEW unit tests (parse, lower, outline expand).

New function signatures (copy-paste ready):

```teko
/**
 * tkb_lower_scenario — lower one PLAIN scenario (no outline) to a `Tkr`, folding its
 * Given/When/Then steps into the reused `Tkr` model. `Given` steps set the opaque inputs,
 * the `When` entry verb sets `kind`, `Then` steps set the expectations (routing `on
 * "<os-arch>"` steps into `TkrExpect`/`TkrGolden`). M.3 honest error on a malformed value.
 *
 * @param sc  the scenario (its steps already Background-prepended)
 * @return    the lowered spec, or an `error` on a malformed step value
 * @throws    on a malformed value or an unknown noun-phrase
 * @since 0.3.0.30
 */
fn tkb_lower_scenario(sc: TkbScenario) -> Tkr | error { /* C4 */ }

/**
 * tkb_expand_outline — expand one `Scenario Outline` into one `Tkr` per Examples row by
 * textually substituting each `<name>` placeholder with the row's matching column value
 * BEFORE the reused `mf_read_*` lexers see the value, then lowering the substituted steps.
 * A plain scenario (empty `examples.headers`) yields a single-element list.
 *
 * @param sc  the (outline) scenario
 * @return    one lowered spec per Examples row, or an `error` on a malformed row / value
 * @throws    on a row width mismatch, an unknown placeholder, or a malformed value
 * @since 0.3.0.30
 */
fn tkb_expand_outline(sc: TkbScenario) -> []Tkr | error { /* C4 */ }

/**
 * tkb_read_hex_docstring — decode a `"""<hex>"""` docstring to raw bytes, ignoring ALL
 * ASCII whitespace between the fences (space/tab/CR/LF). Carries control bytes (`\n`, `\0`,
 * opcode bytes) LITERALLY into a `Literal` `TkrMatch`, closing the `.tkr` gap where a quoted
 * literal cannot embed a newline. M.3 honest error on an odd nibble count / non-hex byte.
 *
 * @param s  the source line/block starting at the opening `"""`
 * @param p  the cursor at the first `"`
 * @return   the decoded bytes, or an `error` on malformed hex / an unterminated docstring
 * @throws   on a non-hex nibble, an odd nibble count, or a missing closing `"""`
 * @since 0.3.0.30
 */
fn tkb_read_hex_docstring(s: str, p: u64) -> str | error { /* C4 */ }

/**
 * SpecRef — a discovered regressive spec file: its `name` (basename inside the regressive
 * dir) and `is_tkb` (true for a `.tkb`, false for a legacy `.tkr`). Returned by
 * `dir_first_spec` so `run_one_regressive` dispatches on the format without re-scanning.
 *
 * @since 0.3.0.30
 */
type SpecRef = struct {
    /** the spec file basename inside the regressive directory. */
    name: str
    /** true for a `.tkb` (canonical), false for a `.tkr` (legacy). */
    is_tkb: bool
}

/**
 * dir_first_spec — the first regressive spec in `dir`, PREFERRING a `.tkb` over a `.tkr`
 * (a dir with both runs the `.tkb`; the `.tkr` is not double-run), or `null` when `dir` is
 * unreadable or holds no spec. Supersedes `dir_first_tkr` in discovery (`dir_first_tkr` is
 * retained for the legacy `.tkr`-only meta-tests).
 *
 * @param dir  the directory to inspect
 * @return     the preferred spec ref, or `null`
 * @since 0.3.0.30
 */
fn dir_first_spec(dir: str) -> SpecRef? { /* C4 */ }

/**
 * run_one_tkb — parse, compile-ONCE, then run-per-scenario a `.tkb` regressive: expand each
 * scenario (outline ⇒ N `Tkr`s), compile the dir ONCE, and for each lowered `Tkr` run the
 * built binary with its opaque `args`/`stdin` and check it through the UNCHANGED
 * `check_run`/`check_compile_fail`. The verdict is the FIRST failure, else pass.
 *
 * @param exe       the absolute path to this compiler binary
 * @param regr_dir  the regressive project directory
 * @param tkb_name  the `.tkb` basename inside `regr_dir`
 * @param prefix    the scratch-file prefix for this regressive
 * @return          the aggregate verdict
 * @since 0.3.0.30
 */
fn run_one_tkb(exe: str, regr_dir: str, tkb_name: str, prefix: str) -> RegrOutcome { /* C4 */ }
```

Discovery/dispatch change in `run_regression_sources`/`run_one_regressive`: replace the
`dir_first_tkr` call with `dir_first_spec`; when `is_tkb`, call `run_one_tkb`, else the
existing `run_one_regressive`.

Fixtures added in C4: none of the migration set; just `tkb_test.tkt` unit fixtures (in-source
`.tkb` strings). `[RITUAL]`.

### C5 — Full-stack `fs_*` matrix (runtime-operand law) + parity meta-tests

Files touched: NEW fixture dirs under `examples/regressions/` (each a `.tkp` + `src` + a
`.tkb`). The matrix (§4) — one `.tkb` per op family, each a `Scenario Outline` whose
`Given args` are fold-opaque and whose `Examples` table drives N executed runs. Plus:
- 5 passing `.tkb` + 2 failing `.tkb` (the failing pair asserts the runner REPORTS a
  mismatch), per the format doc §10.
- Meta-tests (`src/build/tkb_test.tkt`): VERDICT PARITY — a `.tkb` scenario and the
  equivalent hand-written `.tkr` produce an identical `RegrOutcome`; and a retained `.tkr`
  still passes unchanged.

`[RITUAL]`.

### C6 — Args passthrough

See §8. Implement the recommended `[tests] args` + `--` CLI merge. Files: `manifest.tks`
(new `Manifest.test_regression_args`), `project.tks` (parse `--` tail in the `test`
subcommand, thread to `run_regression_phase`), `regression.tks` (append passthrough args
after the spec `args`). `[RITUAL]`.

### C7 — CI-lane-shrink + harness-retirement SPEC (owner applies)

Deliver §6 as an exact edit list for the owner. `[RITUAL final]` — full gate after the owner
applies the CI deltas; confirm the migrated verdicts are covered by `teko test .` and no
coverage is lost.

---

## 3. Ritual points (summary)

`[RITUAL]` at the end of C1, C2, C3, C4, C5, C6, and C7 (final). At every ritual the full
gate is: seed builds gen1 (`teko . -o bin --no-verify --release`) on all three OSes, then
`teko test .` green on all three, AND the tests.yml wall-clock delta recorded (R1). C1's
ritual is the load-bearing one: it is where the base transitions inert→live.

---

## 4. The runtime-operand-law matrix (`fs_*` design)

Each `fs_*` program reads its operands from argv (fold-opaque), performs the op, and either
`exit(code)`s with a derived value or prints a result stream. Each ships as ONE `.tkb`
`Scenario Outline` with an `Examples` table — one compile, N executed runs — replacing N
comptime-fold golden unit vectors.

| fixture | op family | operand shape (all fold-opaque via argv) | oracle |
|---|---|---|---|
| `fs_arith` | `+ - * / %` | two argv ints `a b` | `exit((a+b-*/% combined) & 0xff)` |
| `fs_bitwise` | `& \| ^ ~` | two argv ints | `exit` of combined |
| `fs_shift` | `<< >>` with MASKED counts | value + a runtime shift count (count itself from argv, so the mask is applied to a non-const) | `exit` |
| `fs_compare` | `< <= > >= == !=` | two argv ints | `exit` = packed truth bits |
| `fs_concat` | `~` string concat | two argv strings | `stdout` literal OR `stdout sha256` |
| `fs_hostsurface` | env/args-len/stdin | an env var + argv count + `Given stdin` | `exit` derived from all three |
| `fs_foreach` | `foreach` accumulation | a `Given args` array folded at run time | `exit` = accumulated sum |
| `fs_index_store` | `ids[i] = v` | runtime index `i` and value `v` from argv into a mut list | `exit` = `ids[i]` read back |

Coverage note: `fs_shift`'s shift COUNT is itself an argv operand, so the masked-shift-count
lowering (`masked_shift_counts`, `masked_compound_assign_shift`) is exercised on a
non-constant count — the case a comptime golden cannot reach. `fs_index_store` exercises the
`ids[i] = v` store with BOTH `i` and `v` opaque (the runtime-operand form of
`cf4_index_fold`, which is a comptime fold). This is the "executes the op" guarantee.

Each `fs_*` `.tkb` (skeleton, C5):

```
Feature: fs_arith — integer arithmetic on fold-opaque runtime operands
  Scenario Outline: a op b through real codegen
    Given args = ["<a>", "<b>"]
    When built and run
    Then exit = <r>
    Examples:
      | a   | b  | r  |
      | 7   | 13 | 20 |
      | 250 | 10 | 4  |     # (250+10) & 0xff = 4 — wraps, proving no fold
      | 9   | 3  | 3  |
```

---

## 5. Migration inventory (exact per-fixture mapping)

### 5.1 `EXPECT_COMPILE_FAIL` → `.tkr` `kind = "compile_fail"` (31 fixtures, C2)

5 carry a pinned diagnostic (`diagnostic = "<substring>"`); 26 assert failure-only.

Pinned (carry `diagnostic`):
- `const_ns_qualified_visibility_rejected` → `"is private to"`
- `iface_value_struct_rejected` → `"a \`struct\` value cannot be used as the interface"`
- `lit_neg_unsigned` → `"a negative literal cannot be annotated as an unsigned type (M.1)"`
- `lit_slice_overflow` → `"literal out of range for the annotated type (M.1 — fail early)"`
- `lit_trunc_cast` → `"constant out of range for the cast target (M.1 — fail early)"`
- `lit_u8_overflow` → `"literal out of range for the annotated type (M.1 — fail early)"`
  (six pins; `lit_slice_overflow`/`lit_u8_overflow` share the same message)

Failure-only (omit `diagnostic`): `adopt_break_outside_loop`, `adopt_break_unknown_label`,
`adopt_return_type_mismatch`, `arena_manual_leak`, `discard_as_alias`, `discard_bare_read`,
`discard_double_underscore`, `discard_let_binding`, `discard_param_plain_fn`,
`discard_typed_target`, `free_aliased_rejected`, `free_captured_by_container_rejected`,
`free_field_extract_rejected`, `loop_label_lowercase`, `loop_range_counter_leak`,
`loop_three_part_nonbool_cond`, `member_const_interface_rejected`,
`member_const_override_rejected`, `member_const_visibility_leak_rejected`, `must_free_leak`,
`ref_in_collection_rejected`, `ref_local_unnamed_source_rejected`, `ref_returned_rejected`,
`underscore_as_x_rejected`, `unsafe_field_in_safe_struct`.

Recommendation: PIN more of these over time (the failure-only ones are weak oracles — they
pass on ANY build failure, including an unrelated one). Not blocking for this wave; the six
pins already upgrade the strongest cases. Report finding, do not expand scope now.

### 5.2 `EXPECT_EXIT` → `.tkr` `exit = N` (35 single-project + char_ops, C3)

`exit=0`: `comptime_fold_format_oracle`, `comptime_fold_interp_hex`, `iface_value_field`,
`iface_value_hetero_slice`, `iface_value_return`.
`exit=42`: `comptime_fold_local`, `comptime_fold_local_guard`, `comptime_fold_scalar`,
`const_pub_export_survives`, `const_scalar_inline`, `list_grow_bridge`,
`member_const_aggregate`, `member_const_cross_ns_pub_inherit`, `member_const_inherit_shadow`,
`member_const_scalar`, `member_const_trait_fold`, `own_arith_exit`, `own_sub_exit`.
Other single values: `cf4_field_len_fold`=50, `cf4_index_fold`=31, `const_agg_bytes_rodata`=4,
`const_agg_struct_rodata`=104, `const_agg_variant_rodata`=12, `const_variant_match_subject`=33,
`iface_constraint_struct_ok`=7, `iface_value_name_collision_factory`=10,
`iface_value_param_dispatch`=7, `iface_value_stateful_class`=3, `lit_arg_if_over_i64`=7,
`lit_byte_ctx`=73, `lit_i128_if_assign`=7, `lit_if_match_ctx`=14, `lit_redundant_cast`=5,
`null_union_basic`=8, `stored_borrow_sound`=70.
Plus `char_ops` → `exit = 6` (from its `.tkp` doc; not marker-driven today).

EXCLUDED (dep/consumer shape, §6.4): `const_crossmodule_inline` (`exit=42`).

Note: `own_arith_exit` / `own_sub_exit` already migrated in C1 — do NOT double-add.

### 5.3 char_ops (C3) and the `.tkb` showcase set (C5)

`char_ops` → `.tkr exit = 6`. The `fs_*` matrix (§4) + the 5-pass/2-fail `.tkb` are NEW dirs,
authored in C5 to exercise the `.tkb` format itself.

---

## 6. CI-lane-shrink SPEC (owner applies `.github`/`scripts`)

The architect does NOT edit `.github`/`scripts`. This is the exact delta for the owner,
applied at C7 AFTER C2/C3 prove the migrated verdicts under `teko test .`.

### 6.1 `.github/workflows/native.yml` — `gen1-checks` job

REMOVE these two steps (now subsumed by `teko test .` regressives in tests.yml):
- "EXPECT_COMPILE_FAIL regression check (issue #610, on gen1)" (`scripts/compile_fail_regressions.sh`)
- "EXPECT_EXIT positive const regression check (issue #594 crumb 8f, on gen1)"
  (`scripts/positive_regressions.sh`)

KEEP (distinct concerns, §6.3/6.4):
- "CWD build regression check (issue #64, on gen1)" (`scripts/native_regressions.sh`)
- "E2E cross-module const regression check (issue #594 crumb 8d, on gen1)"
  (`scripts/crossmodule_regressions.sh`)
- "C-vs-own differential" (`scripts/diff_c_own.sh`) and the wasm differential — backend
  differentials, NOT plain regressives.

### 6.2 Scripts

DELETE after C7 green: `scripts/compile_fail_regressions.sh`, `scripts/positive_regressions.sh`.
KEEP: `scripts/native_regressions.sh`, `scripts/crossmodule_regressions.sh`,
`scripts/diff_c_own.sh`, `scripts/validate_wasm_own.sh`.

Marker files: the `EXPECT_COMPILE_FAIL` / `EXPECT_EXIT` marker files may be REMOVED once the
harnesses are gone, EXCEPT `EXPECT_COMPILE_FAIL` which sanitizers.yml's build-all loops still
read to SKIP negative fixtures. Two options for the owner:
  (a) keep the `EXPECT_COMPILE_FAIL` markers (cheap, sanitizers unchanged); or
  (b) switch sanitizers.yml's skip predicate to "dir has a `.tkr`/`.tkb` with
      `kind = compile_fail`" and drop the markers. Recommend (a) for this wave (smaller
      blast radius); (b) is a follow-up cleanup.

### 6.3 The #64 CWD guard is NOT subsumed — KEEP it

`native_regressions.sh` checks `cd <project> && teko build .` — a CWD-RELATIVE runtime
resolution invocation shape (the #64/#66 bug). The regression runner's `compile_regressive`
runs `teko <regr_dir> -o bin` (path argument, compiler-relative runtime resolution), which
does NOT reproduce the cd-into-project shape. So the #64 guard is ORTHOGONAL and must stay.
Optional future enhancement (NOT this wave): a `.tkr`/`.tkb` `compile.cwd = true` mode that
`cd`s into the dir before building; deferred — the tiny `native_regressions.sh` is cheaper
and lower-risk. char_ops's exit-code content moves to a `.tkr` (C3); the #64 shape stays in
the shell guard.

### 6.4 Cross-module fixture is NOT subsumed this wave — KEEP the harness

`const_crossmodule_inline` is a `dep/`+`consumer/` pair with NO top-level `.tkp`; the runner
would need to build the dep to a `.tkl`, provision a scratch `packages/`, then build the
consumer — a capability `run_one_regressive`/`compile_regressive` do NOT have. DESIGN-AHEAD
contract for a later wave (do not implement now): a `.tkb` `Background` step
`Given dependency "<dep-subdir>"` that the runner builds-to-`.tkl` and provisions into a
scratch `packages/` before the consumer compile — mirroring
`crossmodule_regressions.sh::build_dep_tkl`+`provision_consumer_copy`. Until then KEEP
`scripts/crossmodule_regressions.sh` and its `native.yml` step; `const_crossmodule_inline`
is excluded from the C3 migration.

---

## 7. Args passthrough (§task 5) — both designs + recommendation

The owner offered two shapes; design both:

Option A — dedicated manifest section: `[tests] args = ["--fast", "--seed=1"]` →
`Manifest.test_regression_args`, appended to every spec's `args` at run time. Declarative,
version-controlled, applies uniformly; but static (cannot vary per invocation) and it
COLLIDES conceptually with each spec's own opaque `args` (which set is authoritative?).

Option B — `--` CLI passthrough: `teko test . -- --fast --seed=1` → the tail after `--` is
threaded through `test_project → run_regression_phase → argv_with_args`, appended after the
spec `args`. Ad-hoc, matches Cargo/go-test convention, zero manifest surface; but ephemeral
(not captured in the repo) and needs the `test` subcommand to stop flag-parsing at `--`.

RECOMMENDATION: implement BOTH with a clear precedence — `[tests] args` (base, declarative) THEN
the `--` tail (per-invocation override), both appended AFTER the spec's own `args` so a
spec's opaque operands always come first (the fold-opaque contract is preserved: passthrough
args are ADDITIONAL argv, never a replacement). Rationale: they are complementary, not
rival; the manifest form pins a project-wide default in-repo, the `--` form lets a dev/CI
lane add a one-off without a commit. Precedence is documented and testable. If only one is
wanted, prefer Option B (`--`) — it adds no manifest field and no authority ambiguity.

Contract (C6):
```teko
/**
 * Manifest.test_regression_args — `[tests] args` — extra argv appended (after each spec's own
 * opaque `args`, before the `--` CLI tail) to EVERY regressive run. `[]` when absent. The
 * fold-opaque contract holds: these are additional runtime operands, never a replacement.
 * @since 0.3.0.30
 */
test_regression_args: []str
```
`run_regression_phase(exe, m, passthrough: []str)` gains the `--`-tail parameter; append
order is `spec.args ++ m.test_regression_args ++ passthrough`.

---

## 8. Risks / open questions / law tensions

- **R1 (perf — the real risk).** Migrating 31 + 35 + 1(char_ops) + fs_* ≈ 70+ regressive
  COMPILE+RUN cycles into `teko test .` adds real wall-clock to tests.yml on three OSes;
  windows already runs near a 90-min cap (per its own comment). Each compile is a full
  front-end + cc link (~1-3s). MITIGATION: the crumbs stage the migration (C1 5, C2 +31,
  C3 +36, C5 +fs_*) with a `[RITUAL]` after each so the duration delta is MEASURED
  incrementally; if windows breaches its cap, the fix is the perf work already in flight
  for the other lanes (do NOT silently drop regressives). Owner boundary respected:
  regressives run in tests.yml (parallel with diagnostics), not in the gen1-checks/build
  jobs. No new HALT — flagged for measurement.
- **R2 (tree cleanliness).** `compile_regressive` builds each regressive with `-o bin`
  RELATIVE to the (chdir'd) regressive dir, writing `examples/regressions/<name>/bin/`
  into the WORKING TREE. Left uncleaned this dirties the tree and can perturb the
  fixpoint/byte-identity checks. RESOLUTION (in C1): add `examples/regressions/**/bin/` (and
  the existing `examples/regression-fixture/**/bin/`) to `.gitignore` — a repo edit the
  architect specifies and the implementer applies (NOT a `.github`/`scripts` edit).
  ALTERNATIVE (heavier, deferred): change `compile_regressive` to emit into the shared
  `bin/.regr-work/<sanitized>/` scratch instead of the fixture dir. Recommend the
  `.gitignore` route now; note the scratch-out redirect as a clean follow-up.
- **R3 (weak compile_fail oracles).** 26 of 31 negative fixtures assert failure-ONLY (no
  diagnostic) — they pass on any build failure, masking a WRONG-reason regression. Migrated
  faithfully as-is (no scope creep). REPORTED up: consider pinning `diagnostic` on the
  remaining 26 in a follow-up. Not a HALT.
- **R4 (discovery breadth).** `regression = ["examples/regressions"]` scans all 207 subdirs
  every `teko test .`. That is a `list_dir` per subdir to probe for a spec — cheap, but it
  also means ANY future dir that gains a `.tkr`/`.tkb` auto-joins the gate. That is the
  INTENDED ergonomics (add a spec, get a regressive) but it makes the gate implicitly
  grow; documented so it is a choice, not a surprise.
- **R5 (`.tkb` both-present precedence).** A dir carrying BOTH a `.tkb` and a `.tkr` runs the
  `.tkb` only (`dir_first_spec` prefers `.tkb`). The retained-`.tkr`-still-passes meta-test
  (C5) drives `parse_tkr` DIRECTLY on an in-source string, so it does not depend on
  discovery preference — no conflict.
- **Open (owner, non-blocking): args-passthrough shape.** §7 recommends BOTH with documented
  precedence; if the owner wants exactly one, prefer Option B. Proceed with the
  recommendation unless overridden — not a HALT.

No genuine LAW TENSION surfaced: Teko-only (all new code in `.tks`/`.tkt`/`.tkb`/`.tkr`),
W15 full-Javadoc (all signatures above are Javadoc), law-first M.3 honest-stops (parser
errors), and the owner's TEST-lane / no-gen2 boundary are all satisfied by the design. No
HALT required.
