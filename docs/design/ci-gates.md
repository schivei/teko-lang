# CI gating — the light/full split, and the memory/UB tiers

**Status:** ratified (owner ruling 2026-07-14 for the memory/UB tiers; owner proposal 2026-07-24
for the light/full split, implemented 2026-07-25; owner ruling 2026-07-26 for the HOST set — every
published host gets a build lane AND a test lane).

## The axis: `light` vs `full`

```
full  <=> github.base_ref == 'main'      (the wagon that LANDS a stacked train, or a hotfix)
light <=> any other base                 (an intermediate wagon PR)
```

Delivery is a **stacked train**: each wagon is a PR based on the previous wagon's branch, and the
whole train lands in ONE integration at the top wagon, retargeted to `main`. So the only complete
CI run that matters is the landing's; running the whole matrix on every intermediate wagon burns
runners to re-prove what the landing proves again.

`github.base_ref` is the axis, **never** `github.ref`: on a `pull_request` event `github.ref` is
`refs/pull/N/merge` and matches no branch name. That exact mistake was a live bug here — see
"The J2 bug" below.

| track | light | full |
|---|---|---|
| `tests.yml` `test` | linux-x86_64, windows-x86_64 | + linux-arm64, macos-arm64, windows-arm64 |
| `native.yml` `build-test` | linux-x86_64, windows-x86_64 | + linux-arm64, macos-arm64, windows-arm64 |
| `native.yml` `gen1-checks` | ubuntu-latest | + macos-latest |
| `native.yml` `riscv64-qemu` | — | yes |
| `native.yml` `ar-elf-macho-coff-validation` | — | yes |
| `native.yml` `release-cross-smoke` | — | yes |
| `native.yml` `seeds` | — | yes |
| `native.yml` `seed-debut` | — | yes (five hosts) |
| `sanitizers.yml` (all four jobs) | — | yes |
| `codeql.yml` / `sast.yml` | unchanged (see below) | unchanged |

What survives on the light path is precisely what catches a compiler break: gen1 builds, `teko
test .` passes (which since D6 includes the ten `.tkr` regressors), and the own==C differential on
linux.

### The HOST set (owner ruling 2026-07-26)

> "Sobre windows-arm64 (e outros hosts), tem que fazer o build inicial e tem que colocar lane de
> teste se não houverem."

This **reverses** the 2026-07-06 exclusion of `windows-arm64` from `build-test`. Every host that
ships a published artifact now has BOTH a build lane (`native.yml` `build-test`) and a test lane
(`tests.yml` `test`). The gap the ruling closed:

| host | build lane before | test lane before |
|---|---|---|
| linux-x86_64 | yes | yes |
| linux-arm64 | yes | **none** |
| macos-arm64 | yes | yes |
| windows-x86_64 | yes | yes |
| windows-arm64 | **none** | **none** |

`teko-linux-arm64-{glibc,musl}.tar.gz` and `teko-windows-arm64.zip` were published without
anything ever having run `teko test .` on that hardware.

The two tracks now carry the **same** host set and the **same** split, so a host can no longer be
built without being tested. The light tier is `linux-x86_64` + `windows-x86_64`; the three arm
hosts are full-only, because each adds an ARCH delta on top of a platform the light tier already
covers, and `windows-11-arm` is the slowest runner in the set
(docs/design/compile-time-architecture.md §1.1).

`tests.yml` is a **matrix**, not one job per host: five hand-written jobs is the shape in which the
fifth is forgotten out of the aggregator's `needs:` list — and a job outside that list runs, goes
red, and does not block the merge. A matrix job has one name in `needs:`, and its aggregate result
is `success` only when every leg succeeded.

`codeql.yml` is deliberately NOT gated: it feeds the `code_scanning` ruleset rule, which requires a
completed analysis for the PR head, so skipping it leaves the PR blocked "waiting for CodeQL".
`sast.yml` is already narrowly path-filtered to `src/runtime/**` + `src/assert/**` (clang-tidy over
two C files) and is left as-is.

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

The heavy sanitizer lane (`asan-default` in `.github/workflows/sanitizers.yml`) rebuilds gen1
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
- **As of 2026-07-25 this job, like every other job in `sanitizers.yml`, is FULL-only** (see the
  light/full split above): `tsan` carries a 90-min budget and `windows-selfhost` a 100-min one, so
  the whole workflow belongs to the landing. What guards an intermediate wagon PR instead is
  `test / linux-x86_64` + `test / windows-x86_64` plus the native self-build gate in `native.yml`. The `TEKO_MEM_PARANOID` self-host
  is also part of every wagon's LOCAL closing ritual, so the oracle still runs per wagon — off the
  shared runners.

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
