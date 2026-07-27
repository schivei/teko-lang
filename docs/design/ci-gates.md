# CI gating — the light/full split, and the memory/UB tiers

**Status:** ratified (owner ruling 2026-07-14 for the memory/UB tiers; owner proposal 2026-07-24
for the light/full split, implemented 2026-07-25; owner rulings 2026-07-26 for the HOST set — every
published host gets a build lane AND a test lane — and for the CONSOLIDATION, below).

> **Read this first.** The 2026-07-26 consolidation moved every `pull_request` job into a single
> workflow and put an artifact root under all of them. The tiers, the mode assertions and the
> ruleset table below are unchanged in substance; only their addresses moved. See
> "The consolidation" for the mapping.

## The axis: `light` vs `full`

```
full  <=> github.base_ref == 'main'      (the wagon that LANDS a stacked train, or a hotfix)
      OR the PR touches CI, build scripts, the runtime/assert C, the backend, the
         bootstrap seeds, or teko.tkp
light <=> any other base AND none of the above touched
```

The base is not the only trigger, because it was never a sound one on its own: a wagon that
rewrites `scripts/` or `src/backend/` is exactly the wagon whose full audits matter, and its base
is another wagon. The criterion lives in `scripts/ci_full_mode.sh` so the gates can re-derive it
INDEPENDENTLY — over the PR's own file list, without `dorny` — and fail on disagreement. That
independent re-derivation is what makes a `skipped` leg trustworthy rather than merely quiet.

Delivery is a **stacked train**: each wagon is a PR based on the previous wagon's branch, and the
whole train lands in ONE integration at the top wagon, retargeted to `main`. So the only complete
CI run that matters is the landing's; running the whole matrix on every intermediate wagon burns
runners to re-prove what the landing proves again.

`github.base_ref` is the axis, **never** `github.ref`: on a `pull_request` event `github.ref` is
`refs/pull/N/merge` and matches no branch name. That exact mistake was a live bug here — see
"The J2 bug" below.

| job in `pr.yml` | light | full |
|---|---|---|
| `artifact / <producer>` | the producers minting the two light assets | all seven producers |
| `test / <label>` | linux-x86_64-glibc, windows-x86_64 | all nine assets |
| `cli surface / <label>` | linux-x86_64-glibc | + macos-arm64 |
| `TSan`, `ASan+UBSan smoke` | yes | yes |
| `regressor / all capabilities` | — | yes |
| `ar validation / <label>` | — | yes |
| `cross-arch determinism` | — | yes |
| `seed debut / <label>` | — | yes |
| `ASan+UBSan+LSan / default dispatch` | — | yes |
| `Memory paranoid (native self-host)` | — | yes |
| `clang-tidy audit` | path-filtered (`sast_run`) | path-filtered |
| `codeql.yml` | independent — see below | independent |

The **producer** matrix narrows with the asset set rather than building nine and discarding seven:
`produces` is per-leg, so the light path mints one Linux asset instead of six. The two layers are
cross-checked against each other and against the mode's required set in `Test suite gate`, so a
host can no longer be built without being tested, nor tested without being built.

What runs on the light path is precisely what catches a compiler break: the assets build, both
light assets pass `teko test .` on their own hardware (which since D6 includes the `.tkr`
regressors), and the sanitizer smoke lanes link the emitted C.

### No trigger-level path filter — the freeze the drain cannot survive

`pr.yml` has `branches:` and no `paths:`, deliberately. The five aggregators are required on
`main`, `main` has no bypass, and a required check that never REPORTS does not go red — it stays
"Expected — waiting for status" forever. A PR whose ENTIRE diff against its base falls outside a
trigger filter would produce no run at all and freeze — and the only PR pointing at `main` in a
stacked train is the drain.

**The unit is the PR, not the commit**, and getting that backwards is easy enough that this
document did, at first. On a `pull_request` event GitHub evaluates `paths:` against every file the
PR changes relative to its base, not against the head commit; `dorny/paths-filter` does the same.
So pushing a documentation-only COMMIT onto a PR that also changes `src/**` keeps matching and
keeps running. Measured live on the wagon-20 branch: a `docs/memory/**` commit still produced
`run=true`.

That makes the hole narrower than "any docs commit" — and it is still a hole worth closing, because
the endgame closes with a W15 wagon and a bump, and a wagon that turned out to be documentation-only
would freeze the one PR that matters.

The cost the filter used to buy is bought one level down instead: `plan` computes `run` with a
docs-only guard over the PR's file list, and **every** lane including the artifact root consults it.
A documentation-only PR costs one ~12s job. The aggregators do not merely tolerate the resulting
skips — they **require** them, so the cheap path cannot degrade into a vacuous green. All three
directions are exercised: docs-only passes, `run=true` with a skipped root fails, and a root that
ran when `run=false` fails too.

### `cancelled` reads as `failure`, by omission

The aggregators are `if: always()` and demand `success`, so a run cancelled by `cancel-in-progress`
reports all five as FAILED. On a superseded SHA that is harmless — nothing gates it — but it does
mean each superseded push emits five red notifications. Distinguishing `cancelled` and exiting
quietly is a one-line change per gate and deliberately NOT made: an exception carved into a gate is
how a gate stops gating, and `cancelled` is also what a runner timeout and a manual stop look like.
Left as a declared cost, not an oversight.

## The consolidation (owner ruling 2026-07-26)

> *"todas as lanes de pull_request precisam depender dessa lane de artefatos, a lane de artefatos
> deve executar build seco `teko build . --no-verify --release` e ela deve ter a inteligência sobre
> o que deve gerar (a escada)"* — and, on the workflow count: *"se não atravessa, transforme em um
> único yaml, os únicos independentes são os que esperam push"*.

Three rulings, one shape:

1. **One artifact root.** `artifact / <producer>` builds the compiler and stages that producer's
   published assets. Every other `pull_request` job downloads them. No lane rebuilds the compiler
   to test it; the thing under test is the thing that ships.
2. **One workflow.** `needs:` cannot cross workflows, so an artifact root and five separate YAMLs
   are mutually exclusive. `native.yml`, `tests.yml`, `sanitizers.yml` and `sast.yml` dissolved into
   `pr.yml`. Only the `push`-triggered workflows stay independent, because nothing gates them.
3. **`codeql.yml` stays independent** — a different reason from the `push` ones: it analyses the C
   runtime, so it does not want a Teko artifact at all, and teaching it (and Dependabot) to read
   Teko is post-linker work.

### Zig is dead

The assets were cross-compiled with zig. They are now built by the **target's own toolchain**: an
Alpine or manylinux container of the target architecture, on that architecture's own runner. The
compiler is not cross. Measured on `theory/native-runner-probe` before the redesign.

The immediate payoff was a real defect: `dladdr` lives in `libdl` until glibc 2.33 and folds into
`libc` at 2.34, so linking against the runner's own glibc 2.39 hid a dependency that the glibc-2.28
floor requires. Cross-compiling had made the floor invisible; building on the floor made it fail
loudly, and `-ldl` is now passed on exactly the glibc legs.

### `release.yml` compiles nothing

> *"se o binário gerado na primeira etapa, que passou todos os gates é válido, release só precisa
> promover, não tem que recompilar e 'jogar fora' algo provado."*

`release.yml` finds the build that already passed the gates, renames its assets to the release
naming, republishes them under the correct tag, and deletes the transient build. A recompile at
release time would discard the artifact the gates actually proved and ship a different one.

### The HOST set (owner ruling 2026-07-26)

> "Sobre windows-arm64 (e outros hosts), tem que fazer o build inicial e tem que colocar lane de
> teste se não houverem."

The RULE that ruling established stands: every host that ships a published artifact has BOTH a
build lane (`artifact / <producer>`) and a test lane (`test / <label>`). Its windows-arm64
INSTANCE does not — that host, and `linux-riscv64` with it, was removed from the published set in
0.3.1 by a later owner ruling ("remover completamente suporte a Windows arm64 e Linux riscv64,
apagar todos os vestígios, sem dead code"), so the rule now has nothing to say about them. The gap
the earlier ruling closed, over the hosts that survive:

| host | build lane before | test lane before |
|---|---|---|
| linux-x86_64 | yes | yes |
| linux-arm64 | yes | **none** |
| macos-arm64 | yes | yes |
| windows-x86_64 | yes | yes |

`teko-linux-arm64-{glibc,musl}.tar.gz` was published without anything ever having run
`teko test .` on that hardware.

The two tracks now carry the **same** host set and the **same** split, so a host can no longer be
built without being tested. The light tier is `linux-x86_64` + `windows-x86_64`; the two arm
hosts are full-only, because each adds an ARCH delta on top of a platform the light tier already
covers.

`test` is a **matrix**, not one job per host: nine hand-written jobs is the shape in which the ninth
is forgotten out of the aggregator's `needs:` list — and a job outside that list runs, goes red, and
does not block the merge. A matrix job has one name in `needs:`, and its aggregate result is
`success` only when every leg succeeded. The matrices are computed in `plan` and consumed via
`fromJSON`, so adding an asset adds its build lane and its test lane in the same edit.

`codeql.yml` is deliberately NOT gated: it feeds the `code_scanning` ruleset rule, which requires a
completed analysis for the PR head, so skipping it leaves the PR blocked "waiting for CodeQL".
`clang-tidy audit` is narrowly path-filtered to `src/runtime/**` + `src/assert/**` (clang-tidy over
two C files) and keeps that filter, expressed as the `sast_run` output of `plan`.

### Gates assert the MODE, not merely the absence of failure

Every aggregator (`CI gate`, `Sanitizer gate`, `Heavy sanitizer gate (main)`, `Test suite gate`)
demands a specific result per job:

1. `changes.run != 'true'` (a docs-only change) — passes, as before.
2. A **light-path** job must be `success`. `skipped` is an **ERROR**: it means a broken condition,
   not "nothing to do".
3. A **full-only** job must be `success` in full mode and `skipped` in light mode. An unexpected
   `success` on the light path is reported as a warning (a wrong condition, but nothing hidden).
4. The gate prints `mode=light|full` on every run.

Without (2) and (3) the split would trade cost for blindness: one bad `if:` would skip everything
and every gate would still report green.

### The J2 bug the split exposed and fixed

`mem-paranoid` and `asan-default` gated on `github.ref == 'refs/heads/main'` in a workflow whose
only trigger is `pull_request`. The condition could therefore never match: **both lanes had never
run once**, and `Heavy sanitizer gate (main)` — which treated a skip as approval — was vacuously
green (confirmed live on PR #91: both `skipped`, gate `success`). They now gate on
`github.base_ref == 'main'` and the aggregator DEMANDS their success in full mode. The split did
not reduce coverage here; it **restored** two lanes.

## Problem

The heavy sanitizer lane (`asan-default`, then in `sanitizers.yml`, today in `pr.yml`) rebuilds gen1
under `-fsanitize=address,undefined` through a `cc` wrapper and then runs the whole self-host
gate plus the regressions corpus under ASan shadow memory. That is a genuine, valuable UB audit —
it surfaced and drove root fixes for the #291 trait-vtable function-pointer mismatch, the
`tk_mul_u16` signed-overflow, and the systemic `__int128` arena under-alignment. But it costs
~24 min of wall-clock and heavy runner memory, and it ran on **every** lane PR into `remodel/**`.

Owner question that started this: *"Pq os gates de ASan e UBSan são tão lentos e pesados?"* —
followed by the key realization that the heavy lane's value is a **main/release-boundary audit**,
not a per-lane-PR gate, especially now that the frozen C mirror is retired (#524/#548) and there
is no second implementation to differentially catch these UBs.

## Decision

Split memory/UB gating into two tiers with **distinct, stable required-check names** so the
branch rulesets can require the right tier on the right branch.

### LIGHT tier — the SEED protector (the arena oracle)

Job `mem-paranoid` (check name **`Memory paranoid (native self-host)`**), aggregated by the
**`Sanitizer gate`** check.

- Builds gen1 **natively** (no clang, no sanitizer flags) — the seed compiles the PR sources.
- Runs the self-host gate and native-builds the regressions corpus with **`TEKO_MEM_PARANOID=1`**.
- `TEKO_MEM_PARANOID` is teko's **own** arena oracle (`src/runtime/teko_rt.c`, #148 Level-2): a
  runtime `getenv` that **poisons every freed arena block and never reuses it**, so any
  use-after-free / arena-reuse-after-free in the compiler's own C aborts loud.
- Cost ≈ one native self-host.
- **This job is FULL-only** (see the light/full split above), because it costs a full self-host.
  What guards an intermediate wagon PR instead is the nine `test / <label>` lanes — every published
  asset runs `teko test .` on its own hardware on every PR — plus the `TSan` and `ASan+UBSan smoke`
  lanes, which link the emitted C. The `TEKO_MEM_PARANOID` self-host is also part of every wagon's
  LOCAL closing ritual, so the oracle still runs per wagon — off the shared runners.

### HEAVY tier — the MAIN audit (ASan + UBSan + LSan)

Job `asan-default` (check name **`ASan+UBSan+LSan / default dispatch`**), aggregated by the
**`Heavy sanitizer gate (main)`** check.

- Body unchanged (the proven ASan+UBSan native-path audit; LSan off-by-construction, as its long
  in-file comment explains).
- Body unchanged.
- **Trigger, as of 2026-07-25:** `if: needs.changes.outputs.run == 'true' &&
  needs.changes.outputs.full == 'true'` — the PR whose BASE is `main`, i.e. the landing (or a
  hotfix). The earlier spelling (`github.ref == 'refs/heads/main'`) described a `push:` trigger
  this workflow does not have and so never fired at all; see "The J2 bug" above.
- On a light-path PR it is **skipped**, and `Heavy sanitizer gate (main)` now REQUIRES that skip
  rather than tolerating it — so a broken condition cannot pass as "nothing to do".

No nightly, no cron schedule, no auto-issue.

> **Trade-off (accepted).** The heavy audit runs once per train, at the landing, rather than on
> every wagon PR. The light native self-build gate plus `test / linux-x86_64` and `test / windows-x86_64` guard each wagon.

## Why this is safe

- The seed a lane PR produces is still gated for memory correctness on **every** lane PR, by
  teko's own arena oracle. A use-after-free cannot reach the seed silently.
- The heavy ASan/UBSan audit of the emitted C + runtime seam still runs — at the exact boundary
  where main is about to become a release (umbrella → main), and again on the merge commit. Any
  UB is caught before it ships in a released seed.
- The two tiers have separate aggregator checks, so the rulesets express the intent directly and
  no required check ever "hangs" a branch by never reporting (the classic failure mode when a
  required job stops running on a branch).

## Required rulesets (apply manually — no ruleset API is exposed to CI)

Branch rulesets are **not** changed by this PR (GitHub does not expose a ruleset/branch-protection
mutation to the workflow token here). Apply these by hand after merge:

| Branch pattern        | Required checks to REQUIRE                     | Required checks to REMOVE                          |
|-----------------------|-----------------------------------------------|----------------------------------------------------|
| `remodel/**`, lanes   | `Sanitizer gate` (light)                       | any direct requirement on `ASan+UBSan+LSan / default dispatch`; and, if present, the old single required check should now point at `Sanitizer gate` (which is the light aggregator under the new design) |
| `main`                | `Heavy sanitizer gate (main)` **and** `Sanitizer gate` | — |

Notes:
- `Sanitizer gate` keeps its name but now aggregates the **light** tier — so a lane ruleset that
  already required `Sanitizer gate` stays green with no rename; it simply no longer waits on the
  heavy lane.
- On `main`, require **both** aggregators: `Heavy sanitizer gate (main)` enforces the ASan/UBSan
  audit, and `Sanitizer gate` keeps the light suite (mem-paranoid + tsan + windows) required there
  too.
- Do **not** require the raw job names (`ASan+UBSan+LSan / default dispatch`, `Memory paranoid …`)
  directly on lanes — require the aggregators, which are `if: always()` and therefore always report.
