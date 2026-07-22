# 0.3.0.30 HEADLINE — REGRESSIVES FULL-STACK (integrated plan)

Umbrella: `remodel/emit-throughput` (post-0.3.0.29). Owner ruling: the regressive
SUPPORT, the `.tkr`/`.tkb` FORMATS, and the REAL regressives are developed TOGETHER.
DESIGN ONLY; this doc is the ordered crumb sequence.

**Owner revision (2026-07-22):** do NOT migrate the ~66 shell-harness fixtures 1:1.
Consolidate the whole regressive surface to **(B) the COMPILER ITSELF as the primary
full-stack regressor** PLUS **(A) 2-3 curated `.tkb` projects** that cover ONLY what the
compiler-regressor does not assert. The old fixtures are re-mapped: most are SUBSUMED by
the compiler-regressor, a handful fold into the curated projects, the rest are DROPPED.

Companion: `docs/design/tkb-regression-format.md` (the `.tkb` format, reconstructed).

---

## 0. Ground truth (verified in-tree)

- The runner + `.tkr` model shipped inert in 0.3.0.29: `src/build/regression.tks`
  (`run_regression_phase`, `run_regression_sources`, `run_one_regressive`, `check_run`,
  `check_compile_fail`, `match_stream`, `dir_first_tkr`, `discover_source`,
  `compile_regressive`, `run_captured`) and `src/build/tkr.tks` (`parse_tkr` → `Tkr` +
  `TkrKind`/`TkrMatch`/`TkrExpect`/`TkrGolden`).
- `project.tks::test_project` already calls `run_regression_phase(exe, m)` when
  `m.test_regression_dirs.len > 0` (~:3288). Zero-cost no-op today (no manifest declares it).
- Manifest support complete: `[tests] regression` / `gate` / `targets`.
- **CRUX (bootstrap-safe, verified):** the BUILD gate does NOT run regressives.
  `teko . -o bin` → `compile_project_g → run_gate → run_native_gate` (no
  `run_regression_phase`); `--no-verify` → `build_ungated` (gate skipped). ONLY
  `test_project` (the `teko test` path) runs the regression phase. So declaring
  `[tests] regression` in `teko.tkp` activates regressives EXCLUSIVELY in the `teko test .`
  self-test lane (`tests.yml`) — never in the seed→gen1 build, never in `release.yml`
  (`--no-verify --release`). This is exactly the owner boundary: TEST lane, gen1 self-test,
  no gen2-in-testing.
- Anti-recursion: `TEKO_IN_REGRESSION` sentinel + the structural belt (each regressive is
  `teko build`-ed → `build_ungated`, no nested test phase).

Existing shell harnesses (analysis + revised fate in §4):

| harness | what it does | revised fate |
|---|---|---|
| `scripts/compile_fail_regressions.sh` | 31 `EXPECT_COMPILE_FAIL` dirs | RETIRE (6 diagnostic pins → `compile_fail_diag`; 25 failure-only DROPPED) |
| `scripts/positive_regressions.sh` | 35 `EXPECT_EXIT` dirs | RETIRE (SUBSUMED by compiler-regressor; dirs kept as sanitizer corpus) |
| `scripts/native_regressions.sh` | `cd <proj> && teko build .` #64 CWD-runtime guard | KEEP (distinct invocation shape) |
| `scripts/crossmodule_regressions.sh` | 1 dep/consumer `.tkl` fixture | KEEP (needs `packages/` provisioning; §4.4) |
| `scripts/diff_c_own.sh` | C-vs-own BACKEND differential + `.o` checks | KEEP (backend differential) |
| `scripts/validate_wasm_own.sh` | own-wasm == C-native BACKEND differential | KEEP (backend differential) |
| sanitizers.yml build-all loops | build every fixture under ASan/paranoid | KEEP (sanitizer corpus) |

---

## 1. The model: (B) compiler-as-regressor + (A) 2-3 curated projects

### 1.B — The compiler IS the primary full-stack regressor (R0)

The self-host chain — the previous released seed compiling the PR's compiler sources into
gen1, gen1 running its OWN `#test` suite (`teko test .`), and gen1 re-compiling the whole
project to a byte-identical fixpoint — is ALREADY the largest real full-stack regressive in
the tree. It exercises the ENTIRE language (every construct the compiler source uses:
generics, traits, classes, interfaces, const folding, closures, match, defer, ownership,
arenas, the whole host surface) on a huge real program, and asserts, per PR, on three OSes:

1. **The compiler still compiles the whole language** — the seed builds gen1 from the PR
   sources (`teko . -o bin`, native.yml `build-test` + tests.yml + sanitizers.yml). A
   language regression that broke ANY construct the compiler uses breaks this build.
2. **The compiler still passes its own gate** — `./bin/teko test .` (tests.yml, one runner
   per OS) runs the full `#test` suite (600+ tests) + the D4 coverage floors. This is the
   functional oracle for every checker/lowering/codegen unit.
3. **The compiler reaches a byte-identical fixpoint** — gen1 builds gen2
   (sanitizers.yml mem-paranoid `./bin/teko . -o gen2-mp`; the release fixpoint), and the
   own==C / own==wasm backend differentials (diff_c_own.sh, validate_wasm_own.sh) confirm
   the emitted machine code agrees across backends. A codegen regression that changed the
   emitted bytes breaks the fixpoint / the differential.

**Concrete mechanism (decided): R0 is the EXISTING self-host + `teko test .` + fixpoint
lanes, NAMED and COUNTED as the compiler-regressor — NO new fixture, NO `[tests] regression`
self-entry.** A self-entry would be circular (a `teko test .` regressive whose project is
`teko` itself would re-enter the whole gate; the `TEKO_IN_REGRESSION` sentinel would just
no-op it). The design's job here is purely to NAME this existing capability as R0 and to
document what it SUBSUMES, so the curated fixtures (A) do not duplicate it.

**What R0 subsumes:** any fixture whose sole purpose is "language feature X still
compiles / still runs / is still rejected" — because the compiler's own source USES X (so a
break fails the gen1 build) and/or its `#test` suite already covers X (so a break fails
`teko test .`). This is the bulk of the old 66 (see §3).

**What R0 does NOT assert** (and therefore the curated projects (A) must):
- The **runtime-operand law**: R0's tests may run over CONSTANT operands the folder
  collapses; R0 does not guarantee an op is EXECUTED on fold-opaque (argv/stdin) operands.
- **Observable process behavior of an arbitrary built binary**: exit code, stdout/stderr
  TEXT and BYTE/HEX goldens, from a program compiled by gen1 and RUN as a child — R0 asserts
  the COMPILER's own exit/tests, not a fixture program's observable streams.
- **Host surface end-to-end from a built binary**: env/io/fs/process as OBSERVED by a
  separately-built program (R0 uses the host surface internally, but does not pin a built
  program's observable host effects).
- **The runner's own features**: CLI args passthrough into a regressive.
- **The exact compile-fail DIAGNOSTIC wording** (the 6 unique pins) — a contract on the
  MESSAGE text a rejection emits, which R0's rejection unit-tests do not all pin.

### 1.A — The 2-3 curated `.tkb` projects (the residual surface)

Exactly what R0 does not cover, in as few entry points as possible. **THREE projects**
(justified in §2), each a single `.tkb` using `Scenario Outline`s for breadth:

- **P1 `rt_behavior`** — the runtime-operand law. ONE `.tkp` compiled ONCE; Outlines drive N
  executed runs over fold-opaque argv operands across the op families (arith / bitwise /
  shift-masked / compare / concat / foreach / `ids[i]=v`). Oracles: exit code + stdout.
- **P2 `host_cli_io`** — observable host + CLI + IO. ONE `.tkp` compiled once; Outlines vary
  env / stdin / argv (including the `--` passthrough) and a file-IO roundtrip. Oracles:
  exit + stdout/stderr TEXT + one byte/HEX golden.
- **P3 `compile_fail_diag`** — the negative path. ONE project whose `.tkb` Outline names a
  distinct failing source per row (`Given source "cases/<f>.tks"`) and pins the required
  `diagnostic`. The 6 unique diagnostic pins. No run phase (compile-per-scenario).

Everything else from the old 66 is SUBSUMED by R0 or DROPPED (§3).

---

## 2. Why THREE curated projects (count justification)

The three projects partition by **oracle TYPE**, not by feature — that is what keeps the
surface minimal and non-overlapping with R0:

- P1's oracle is a value COMPUTED from opaque operands (exit/stdout of pure computation).
- P2's oracle is an observable HOST EFFECT + stream bytes (env/io/cli/process).
- P3's oracle is a BUILD-TIME diagnostic with NO run phase at all.

P3 cannot merge into P1/P2: a compile-fail has no built artifact to run, and needs distinct
FAILING sources — a fundamentally different runner shape. P1 and P2 COULD merge into one
"behavior" project, but they are kept separate because (a) P2 needs environmental provisioning
(env vars, a scratch file, the `--` tail) that P1 does not, and (b) one focused Outline per
concern reads far better than a 30-row grab-bag. If the owner prefers TWO, merge P1+P2 into a
single `rt_and_host` project — acceptable, at the cost of a busier `.tkb`. Recommend three.

Net regressive surface: **3 `.tkb` projects (~3 entry points, ~2-3 dozen scenario rows) +
R0 (existing, zero new lane).** Down from 66 shell-harness fixtures.

---

## 3. Re-mapping the old 66 fixtures under this model

### 3.1 — 31 `EXPECT_COMPILE_FAIL`

- **FOLD into P3 `compile_fail_diag`** (the 6 with a UNIQUE diagnostic pin — 5 unique
  messages): `const_ns_qualified_visibility_rejected` ("is private to"),
  `iface_value_struct_rejected` ("a `struct` value cannot be used as the interface"),
  `lit_neg_unsigned` ("a negative literal cannot be annotated as an unsigned type (M.1)"),
  `lit_slice_overflow` / `lit_u8_overflow` (share "literal out of range for the annotated
  type (M.1 — fail early)" — keep BOTH constructs as two rows, one shared message),
  `lit_trunc_cast` ("constant out of range for the cast target (M.1 — fail early)"). Each
  becomes a `cases/<name>.tks` + an Outline row pinning the message.
- **DROP** the 25 failure-only fixtures (no diagnostic pin): `adopt_break_outside_loop`,
  `adopt_break_unknown_label`, `adopt_return_type_mismatch`, `arena_manual_leak`,
  `discard_*` (6), `free_*` (3), `loop_label_lowercase`, `loop_range_counter_leak`,
  `loop_three_part_nonbool_cond`, `member_const_interface_rejected`,
  `member_const_override_rejected`, `member_const_visibility_leak_rejected`, `must_free_leak`,
  `ref_in_collection_rejected`, `ref_local_unnamed_source_rejected`, `ref_returned_rejected`,
  `underscore_as_x_rejected`, `unsafe_field_in_safe_struct`. Rationale: a failure-only oracle
  passes on ANY build failure (a weak guard), and the REJECTION of each of these constructs
  is a CHECKER concern already exercised by the compiler's `#test` suite (R0). REPORTED UP
  (not turned into new issues by the architect): where a specific rejection is NOT yet a
  checker `#test`, it should become one — that is stronger than a diagnostic-less regressive.
  The fixture DIRS may be deleted with the harness (owner, §4).

### 3.2 — 35 `EXPECT_EXIT`

- **SUBSUMED by R0** (DROP as regressives; the DIRS stay as sanitizer positive corpus): all
  the const-folding / member-const / interface-value / literal-context fixtures assert
  language features the compiler source USES and its `#tests` cover —
  `comptime_fold_*` (5), `const_agg_*` (3), `const_pub_export_survives`, `const_scalar_inline`,
  `const_variant_match_subject`, `member_const_*` (5), `iface_*` (7), `lit_*` (5),
  `null_union_basic`, `list_grow_bridge`, `stored_borrow_sound`, `cf4_field_len_fold`,
  `cf4_index_fold`.
- **FOLD into P1 `rt_behavior`** (as opaque-operand SCENARIOS, not 1:1 dirs): the
  fixed-literal arithmetic fixtures `own_arith_exit` / `own_sub_exit` become Outline rows
  whose operands come from argv (the runtime-operand upgrade). The fixed-literal versions are
  then DROPPED as regressives (they remain in the diff_c_own / validate_wasm BACKEND corpora,
  which reference them by dir name — do NOT delete those dirs).

### 3.3 — char_ops (no marker)

- **FOLD into P1** as a str-ops Outline row (UTF-8 codepoint ops over an argv-supplied
  string), OR DROP as subsumed (str_slice_chars/char_at are used in the compiler and
  unit-tested). Recommend a single P1 row so the runtime-operand STR path is covered.

### 3.4 — 1 cross-module (`const_crossmodule_inline`)

- **KEEP** under `crossmodule_regressions.sh` this wave (dep/consumer, no top-level `.tkp`;
  §4.4). Not folded into A.

**Result:** old 66 → **P3 (~6 rows) + P1 (~2 folded rows among its op-family rows) + 0 from
the 35 EXPECT_EXIT as standalone regressives.** The rest is R0-subsumed or dropped.

---

## 4. Revised CI-shrink SPEC (owner applies `.github`/`scripts`)

The architect does NOT edit `.github`/`scripts`; this is the exact delta for the owner,
applied at C6 AFTER the curated projects are green under `teko test .`.

### 4.1 — `native.yml` `gen1-checks` job

REMOVE (now subsumed by R0 + the curated projects):
- "EXPECT_COMPILE_FAIL regression check (issue #610)" — `scripts/compile_fail_regressions.sh`
- "EXPECT_EXIT positive const regression check (issue #594 8f)" — `scripts/positive_regressions.sh`

KEEP: "CWD build regression check (#64)" (native_regressions.sh — §4.3), "E2E cross-module
(#594 8d)" (crossmodule_regressions.sh — §4.4), "C-vs-own differential" + the wasm
differential (backend differentials).

### 4.2 — scripts + markers

- DELETE after C6 green: `scripts/compile_fail_regressions.sh`, `scripts/positive_regressions.sh`.
- `EXPECT_EXIT` markers: removable (harness gone); the DIRS stay (sanitizer positive corpus,
  built by sanitizers.yml's build-all loops — no coverage lost).
- `EXPECT_COMPILE_FAIL` markers: sanitizers.yml's build-all loops SKIP on them
  (`if [ -f EXPECT_COMPILE_FAIL ]`). Two owner options: (a) delete the 25 dropped failure-only
  dirs + their markers (they are used by NOTHING once the harness retires — recommended
  cleanup), keeping only the 6 pinned constructs as P3 `cases/`; or (b) leave the dirs as
  negative sanitizer corpus and keep the markers. Recommend (a) — the harness was their only
  consumer; the 6 pinned constructs move to P3.
- KEEP: `native_regressions.sh`, `crossmodule_regressions.sh`, `diff_c_own.sh`,
  `validate_wasm_own.sh`.

### 4.3 — #64 CWD guard NOT subsumed — KEEP

`native_regressions.sh` checks `cd <project> && teko build .` (the CWD-relative runtime
resolution shape, #64/#66). The regression runner builds by PATH (`teko <dir> -o bin`,
compiler-relative resolution) and does NOT reproduce the cd-into shape. Orthogonal — stays.

### 4.4 — Cross-module NOT subsumed this wave — KEEP

`const_crossmodule_inline` is a `dep/`+`consumer/` pair with no top-level `.tkp`; the runner
lacks the build-dep-to-`.tkl` + provision-`packages/` capability. DESIGN-AHEAD (not this
wave): a `.tkb` `Background` step `Given dependency "<dep-subdir>"` the runner builds to a
`.tkl` and provisions into a scratch `packages/` before the consumer compile (mirroring
`crossmodule_regressions.sh`). Until then KEEP the harness + its `native.yml` step.

### 4.5 — R0 needs NO new lane

The compiler-regressor is the EXISTING self-host (native.yml build-test, tests.yml
`teko test .`, sanitizers fixpoint, diff_c_own, validate_wasm). This design only NAMES it;
no workflow edit is required for R0.

---

## 5. Revised crumb sequence (shorter under the smaller surface)

Each crumb builds under the previous seed (0.3.0.29 released binary is the C0 seed; the
whole workstream uses only str/list/match/struct/enum/loop — all present in 0.3.0.29).
`[RITUAL]` = full gate (seed→gen1 + `teko test .` green on all three OSes AND the tests.yml
duration delta measured, §7 R1).

- **C0** — Docs + `tkb.tks` scaffold + FORMALIZE R0 (doc only). Compiles, dormant.
- **C1** — Real `.tkb` parser + lowering + Outline expand + discovery(`.tkb`/`.tkr`) +
  `tkb_test.tkt` unit tests. Not yet wired to a live project. `[RITUAL]`
- **C2** — Author **P1 `rt_behavior`** + turn `teko.tkp` non-inert. First light. `[RITUAL]`
- **C3** — Args passthrough (`--` tail + `[tests] args`, §6). `[RITUAL]`
- **C4** — Author **P2 `host_cli_io`** (host surface + `--` passthrough + a HEX golden). `[RITUAL]`
- **C5** — Author **P3 `compile_fail_diag`** + the compile-fail-per-scenario-source runner
  extension (`Given source "<file>"`). `[RITUAL]`
- **C6** — CI-shrink + harness-retirement SPEC (owner applies §4). `[RITUAL final]`

Seven crumbs (C0-C6), simpler than the pre-revision 8: the three heavy 1:1-migration crumbs
collapse into the authoring crumbs C2/C4/C5 (one per curated project).

### C0 — Docs + scaffold + R0 formalization

Files: `docs/design/tkb-regression-format.md` (done), this doc, NEW `src/build/tkb.tks`
skeleton (types + honest-stop `parse_feature`; dormant — not called by discovery). R0 is
DOC-ONLY: name the self-host + `teko test .` + fixpoint lanes as the compiler-regressor in
this doc; no code, no fixture. Gate: light (must compile). NOT `[RITUAL]`.

Type signatures for `tkb.tks` (copy-paste ready, full Javadoc):

```teko
/**
 * TkbPhase — the Gherkin step PHASE: `Given` (opaque runtime inputs), `When` (the entry verb
 * that classifies the kind), or `Then` (the expected observable output). `And`/`But` inherit
 * the preceding step's phase and never appear as a distinct value here.
 *
 * @since 0.3.0.30
 */
pub type TkbPhase = enum { Given; When; Then }

/**
 * TkbStep — one lowered Gherkin step: the resolved `phase`, the `noun` phrase selecting a
 * `Tkr` field (e.g. "args", "exit", "stdout sha256", "source"), the entry `verb` for a When,
 * the `osarch` route from an `on "<os-arch>"` prefix, and the raw TOML `value` text (already
 * placeholder-substituted, still in surface form so the reused `mf_read_*` lexers decode it).
 *
 * @since 0.3.0.30
 */
pub type TkbStep = struct {
    /** the resolved phase (And/But inherit the previous step's). */
    phase: TkbPhase
    /** the noun-phrase selecting the target `Tkr` field ("" for an entry-verb When). */
    noun: str
    /** the entry verb ("built and run" | "compiled" | "compilation fails" | ""). */
    verb: str
    /** the os-arch route from an `on "<os-arch>"` prefix ("" = top-level expectation). */
    osarch: str
    /** the raw TOML value text right of `=` ("" for a valueless entry verb). */
    value: str
}

/**
 * TkbExamples — a Scenario Outline's parameter table: `headers` (placeholder names) and
 * `rows` (one `[]str` of column values per case). Empty `headers` ⇒ a plain scenario (one
 * implicit row).
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
 * TkbScenario — one scenario: `name`, whether it is an `outline`, its ordered `steps`
 * (Background Given steps prepended), and its `examples` table (empty for a plain scenario).
 *
 * @since 0.3.0.30
 */
pub type TkbScenario = struct {
    /** the scenario's free-text label (report line). */
    name: str
    /** true for a `Scenario Outline` (expanded per Examples row). */
    outline: bool
    /** the ordered steps (Background Given steps prepended). */
    steps: []TkbStep
    /** the parameter table (empty headers ⇒ a single implicit row). */
    examples: TkbExamples
}

/**
 * TkbFeature — one parsed `.tkb` file: the `name` free text and the `scenarios` (Background
 * already folded into each scenario's steps). One feature = one regressive project.
 *
 * @since 0.3.0.30
 */
pub type TkbFeature = struct {
    /** the Feature free-text label. */
    name: str
    /** the scenarios, each carrying the Background Given steps. */
    scenarios: []TkbScenario
}

/**
 * parse_feature — read a `.tkb` source into a `TkbFeature`, or fail honestly (M.3). Reuses
 * the manifest value lexers (`mf_read_quoted`/`mf_read_array`/`mf_read_int`) verbatim for
 * every right-hand side; introduces only keyword-line + Examples-table + HEX-docstring
 * parsing. Background Given steps are prepended to every scenario.
 *
 * @param src  the `.tkb` file contents
 * @return     the parsed feature, or an `error` on any malformed line / unknown noun-phrase
 * @throws     on a malformed keyword line, an unknown noun-phrase, or a malformed value
 * @since 0.3.0.30
 */
fn parse_feature(src: str) -> TkbFeature | error {
    // C0: honest-stop scaffold — the real body lands in C1.
    error { message = "tkb: parse_feature not yet implemented (0.3.0.30 C1)" }
}
```

### C1 — Real `.tkb` parser + run loop + discovery

Files: `src/build/tkb.tks` (implement `parse_feature` + the lowering + the run loop),
`src/build/regression.tks` (generalise discovery to `.tkb`/`.tkr`), `src/build/tkb_test.tkt`
(NEW unit tests: parse, lower, outline expand, HEX docstring, verdict PARITY vs the
equivalent `.tkr`).

New signatures (copy-paste ready):

```teko
/**
 * tkb_lower_scenario — lower one PLAIN scenario to a `Tkr`, folding Given/When/Then steps
 * into the reused `Tkr` model (`Given`→opaque inputs, `When`→kind, `Then`→expectations,
 * routing `on "<os-arch>"` into `TkrExpect`/`TkrGolden`). M.3 honest error on a bad value.
 *
 * @param sc  the scenario (steps already Background-prepended)
 * @return    the lowered spec, or an `error` on a malformed step value / unknown phrase
 * @throws    on a malformed value or an unknown noun-phrase
 * @since 0.3.0.30
 */
fn tkb_lower_scenario(sc: TkbScenario) -> Tkr | error { /* C1 */ }

/**
 * tkb_expand_outline — expand a `Scenario Outline` into one `Tkr` per Examples row by
 * textually substituting each `<name>` placeholder with the row's matching column value
 * BEFORE the reused `mf_read_*` lexers see it, then lowering. A plain scenario yields a
 * single-element list.
 *
 * @param sc  the (outline) scenario
 * @return    one lowered spec per row, or an `error` on a width mismatch / bad value
 * @throws    on a row width mismatch, an unknown placeholder, or a malformed value
 * @since 0.3.0.30
 */
fn tkb_expand_outline(sc: TkbScenario) -> []Tkr | error { /* C1 */ }

/**
 * tkb_read_hex_docstring — decode a `"""<hex>"""` docstring to raw bytes, ignoring ALL ASCII
 * whitespace between the fences. Carries CONTROL bytes (`\n`, `\0`, opcodes) LITERALLY into a
 * `Literal` `TkrMatch`, closing the `.tkr` newline-in-quoted-literal gap. M.3 on bad hex.
 *
 * @param s  the source starting at the opening `"""`
 * @param p  the cursor at the first `"`
 * @return   the decoded bytes, or an `error` on malformed hex / an unterminated docstring
 * @throws   on a non-hex nibble, an odd nibble count, or a missing closing `"""`
 * @since 0.3.0.30
 */
fn tkb_read_hex_docstring(s: str, p: u64) -> str | error { /* C1 */ }

/**
 * SpecRef — a discovered spec file: its `name` (basename in the regressive dir) and `is_tkb`
 * (true for a `.tkb`, false for a legacy `.tkr`). Lets `run_one_regressive` dispatch on
 * format without re-scanning.
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
 * dir_first_spec — the first spec in `dir`, PREFERRING a `.tkb` over a `.tkr`, or `null` when
 * `dir` is unreadable or holds no spec. Supersedes `dir_first_tkr` in discovery
 * (`dir_first_tkr` retained for the legacy `.tkr`-only meta-tests).
 *
 * @param dir  the directory to inspect
 * @return     the preferred spec ref, or `null`
 * @since 0.3.0.30
 */
fn dir_first_spec(dir: str) -> SpecRef? { /* C1 */ }

/**
 * run_one_tkb — parse, then run a `.tkb` regressive. For RUN scenarios: compile the dir's
 * `.tkp` ONCE, expand each scenario (outline ⇒ N `Tkr`s), and run the built binary per
 * lowered `Tkr` through the UNCHANGED `check_run`. For COMPILE-FAIL scenarios: compile each
 * scenario's `Given source "<file>"` standalone and check via `check_compile_fail` (no run,
 * so no shared artifact — per-scenario compile is inherent to the negative path). The verdict
 * is the FIRST failure, else pass.
 *
 * @param exe       the absolute path to this compiler binary
 * @param regr_dir  the regressive project directory
 * @param tkb_name  the `.tkb` basename inside `regr_dir`
 * @param prefix    the scratch-file prefix for this regressive
 * @return          the aggregate verdict
 * @since 0.3.0.30
 */
fn run_one_tkb(exe: str, regr_dir: str, tkb_name: str, prefix: str) -> RegrOutcome { /* C1 */ }
```

Discovery/dispatch: replace `dir_first_tkr` with `dir_first_spec` in
`run_regression_sources`; when `is_tkb`, call `run_one_tkb`, else `run_one_regressive`.

### C2 — P1 `rt_behavior` + `teko.tkp` non-inert (first light)

Files: NEW `examples/regressions/rt_behavior/` (`.tkp` + `src/main.tks` reading argv +
`rt_behavior.tkb`), and `teko.tkp`:
```
[tests]
regression = ["examples/regressions"]
gate = true
targets = ["host"]
```
`discover_source` scans all subdirs and runs only those carrying a spec — with only
`rt_behavior` holding a `.tkb`, only P1 runs; P2/P3 join automatically when authored (no
further `teko.tkp` edit). Resolve tree-cleanliness (R2) here. `[RITUAL]` — the inert→live
transition; confirm three OSes green + measure the (tiny) duration delta.

### C3 — Args passthrough

See §6. Files: `manifest.tks` (`Manifest.test_regression_args`), `project.tks` (`--` tail in
the `test` subcommand → thread to `run_regression_phase`), `regression.tks` (append after the
spec `args`). `[RITUAL]`.

### C4 — P2 `host_cli_io`

Files: NEW `examples/regressions/host_cli_io/` (`.tkp` + `src` exercising env/stdin/args/file
IO + a `host_cli_io.tkb` whose Outline varies env/stdin/argv incl. the `--` passthrough, with
a HEX-docstring byte golden on one stream). `[RITUAL]`.

### C5 — P3 `compile_fail_diag`

Files: NEW `examples/regressions/compile_fail_diag/` (`compile_fail_diag.tkb` + a `cases/`
dir of the ~6 failing snippets) + the `run_one_tkb` compile-fail-per-source path (already in
the C1 signature; implement here with the fixtures that exercise it). Add the `source` noun
to `tkb_lower_scenario`. `[RITUAL]`.

### C6 — CI-shrink SPEC (owner applies)

Deliver §4 as the owner's exact edit list. `[RITUAL final]` — full gate after the owner
applies the CI deltas; confirm R0 + the three curated projects cover the retired harnesses'
verdicts with no coverage loss.

---

## 6. Args passthrough (task 5) — both designs + recommendation (unchanged)

Option A — `[tests] args = [...]` → `Manifest.test_regression_args`, appended to every spec's
`args`. Declarative, in-repo; static, and it must not be confused with a spec's own opaque
`args`.

Option B — `teko test . -- <args>` → the tail after `--` threaded through
`test_project → run_regression_phase → argv_with_args`. Ad-hoc, Cargo/go-test convention,
zero manifest surface; ephemeral, needs the `test` subcommand to stop flag-parsing at `--`.

RECOMMENDATION: BOTH, with documented precedence `spec.args ++ m.test_regression_args ++
passthrough` — the spec's opaque operands ALWAYS first (the fold-opaque contract holds:
passthrough args are ADDITIONAL argv, never a replacement). If only one: prefer B (no manifest
field, no authority ambiguity). Contract:
```teko
/**
 * Manifest.test_regression_args — `[tests] args` — extra argv appended (after each spec's own
 * opaque `args`, before the `--` CLI tail) to EVERY regressive run. `[]` when absent. The
 * fold-opaque contract holds: additional runtime operands, never a replacement.
 * @since 0.3.0.30
 */
test_regression_args: []str
```
`run_regression_phase(exe, m, passthrough: []str)` gains the tail parameter.

---

## 7. Risks / open questions / law tensions

- **R1 (perf — now DRAMATICALLY smaller).** Under the 3-project model the added
  `teko test .` cost is: P1 = 1 compile + ~10-12 runs; P2 = 1 compile + ~6 runs; P3 = ~6
  tiny FAILING compiles (no run). ≈ **2 full compiles + ~18 fast runs + 6 failing compiles ≈
  well under one minute** added on each OS — versus the ~70 full build+run cycles the 1:1
  migration would have added. R0 itself is the EXISTING gate (zero new cost). Windows's
  ~90-min cap is no longer a concern from this workstream. Still MEASURE at each `[RITUAL]`,
  but the pressure is gone. No HALT.
- **R2 (tree cleanliness).** `compile_regressive` builds with `-o bin` relative to the
  (chdir'd) regressive dir, writing `examples/regressions/<name>/bin/` into the working tree —
  can perturb the fixpoint/byte-identity checks. RESOLUTION (C2): add
  `examples/regressions/**/bin/` (and `examples/regression-fixture/**/bin/`) to `.gitignore`
  (a repo edit the implementer applies; NOT a `.github`/`scripts` edit). Deferred alternative:
  redirect `compile_regressive` output to `bin/.regr-work/<sanitized>/`.
- **R3 (dropped failure-only rejections).** The 25 dropped negative fixtures had weak
  (diagnostic-less) oracles. Their rejection is a checker concern; where a construct's
  rejection is NOT already a checker `#test`, it should BECOME one (stronger than a
  diagnostic-less regressive). REPORTED UP — the architect does not open issues; this is a
  finding for the owner to route. Not a HALT.
- **R4 (discovery breadth).** `regression = ["examples/regressions"]` scans all subdirs each
  `teko test .` (a cheap `list_dir` probe per subdir). Intended ergonomics (add a spec → get
  a regressive), documented so it is a choice not a surprise. With only 3 spec-bearing dirs
  the run set stays tiny.
- **R5 (compile-fail model bend).** P3 compiles per scenario (each `Given source "<file>"`),
  which relaxes the "one `.tkp` compiled once" rule for the negative path ONLY — justified
  because a compile-fail has no shared built artifact and needs distinct failing sources.
  Documented in the format doc §5/§10.
- **Open (owner, non-blocking): two vs three curated projects** (§2 — recommend three;
  P1+P2 merge is acceptable) and the **args-passthrough shape** (§6 — recommend both). Proceed
  with the recommendations unless overridden. Not a HALT.

No genuine LAW TENSION surfaced: Teko-only (all new code in `.tks`/`.tkt`/`.tkb`/`.tkr`),
W15 full-Javadoc (all signatures above), M.3 honest-stops (parser errors), and the owner's
TEST-lane / no-gen2 / no-1:1-migration boundaries are all satisfied. No HALT required.
