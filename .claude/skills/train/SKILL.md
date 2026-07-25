---
name: train
description: Operate the teko-lang stacked train — engage a new wagon (PR based on the previous wagon), equalize the chain, drive the TOP to green, and prepare the LIFO drain. Invoke when the owner talks about "vagão", "trem", "engatar", "equalizar", "dreno/squash do trem", or when you are about to push a fix that belongs to a wagon.
---

# Operate the stacked train

Delivery is a **train**: each wagon is a PR whose **base is the previous wagon's branch**, never
`main`. Rules live in `docs/memory/teko-stacked-train-discipline.md` — read it once per session;
this skill is the operating procedure.

## The one rule that changes everything

**A red wagon whose child is green becomes green when the child drains into it.** So:

| situation | action |
|---|---|
| fix is small/punctual | commit it on the **TOP wagon** (last engaged) |
| fix is large | **new wagon on top** |
| wagon below is red | **do nothing** — it goes green in the drain |
| wagon is closed/green | **never touch it, never push to it** |
| a merge errors during the drain | only then a wagon reopens |
| lane full-only vai estrear na aterrissagem | **vagão próprio de correção antes do W15**, com retarget da base para `main` para forçar o run |

**Never cascade a fix downward.** `update_pull_request_branch` across the chain is the wrong
reflex: it burns runners and hours for what the drain does for free. The only gate that matters
is **the TOP of the train green**.

## Engage a wagon

1. Worktree off the CURRENT top branch: `git worktree add <dir> -b <new-branch> origin/<top-branch>`.
2. Dispatch the implementer with the design doc AND every **numeric ruling as a binding target**
   (a number in a ruling is a target, not a description — otherwise you get a fraction back).
3. Tell it: local commits only, no push, no PR; `export TK_RT_DIR="$PWD/src/runtime"` on EVERY
   compiler invocation; which prebuilt compiler to use as seed (a newer wagon's `bin/teko` when
   the corpus already depends on a capability the released seed lacks).
4. On delivery: **equalize** (below), then push `-u`, open a **draft** PR with
   `base = <top-branch>`, then `subscribe_pr_activity`.

## Equalize (bring the top up to date with everything below)

One local pass, never a per-PR API cascade:

```sh
git fetch origin --quiet
git checkout -B eqz origin/<child>
git merge --no-edit origin/<parent>   # abort and report on conflict
git push -q origin HEAD:<child>
```

Then **validate the merged tree before pushing** — a clean merge is not a green build.

## Close a wagon (the gate that actually proves)

`teko test .` alone is NOT enough: it does not exercise `examples/regressions/` the way the
scripts do, and a silent miscompilation has slipped through exactly that gap. Minimum:

```sh
export TK_RT_DIR="$PWD/src/runtime"
<seed> . -o bin --no-verify --release        # gen1
./bin/teko test .
TEKO=bin/teko scripts/positive_regressions.sh
TEKO=bin/teko scripts/compile_fail_regressions.sh
scripts/diff_c_own.sh                         # when the toolchain exists
# fixpoint: gen1 -> gen2 -> gen3, gen2 == gen3 byte-identical (sha256 of teko AND teko.c)
TEKO_MEM_PARANOID=1 ./bin/teko . -o bin-mp --no-verify --release
git diff <wagon-base> -- '*.tks' '*.tkt' | grep '^+.*//'   # must be empty (W15)
```

Deleting a regression directory requires a **proof of equivalence**: the folded scenario must
assert the same diagnostic/exit **actually observed**, and must not lose what the directory
proved (a differential fixture folded into a single-backend build is coverage LOSS). Grep the
`scripts/*.sh` CORPUS arrays before deleting any dir — they hardcode directory names and the CI
runs them.

## Commit hygiene (applies to the integrator too)

No `Co-Authored-By:`, no "Generated with Claude Code" line — clean Conventional-Commits body.
**Force-push is disabled**: never rewrite pushed history to fix a trailer; forward-only. A PR
body may keep a generation note.

## Drain (owner-only) — ONE merge, not N

Everything stays **draft**. The **bump is the contra-máquina**: its own wagon at the very top,
after the W15 support car. When it closes green, **only it leaves draft** — that is the owner's cue.

The top branch already contains every wagon below it, so the drain is a single merge:

1. retarget the contra-máquina's base to `main` (its diff becomes the whole train);
2. squash-merge → `main` gets one commit with everything, bump included;
3. the other PRs are **closed**, not merged;
4. delete the wagon branches — under squash that is what actually closes them (the original SHAs
   never become reachable from `main`, so GitHub cannot auto-detect; it closes a PR when its base
   or head branch disappears). `Closes #N` keywords close ISSUES, never PRs.

O mesmo retarget-para-`main` serve como **ensaio**: o vagão de correção de estreia usa-o para forçar
um run completo contra a árvore quase-final, e depois devolve a base ao vagão anterior. É o único
lugar onde a base de um vagão muda duas vezes, e é deliberado.

Never merge, never un-draft anything else, never squash on the owner's behalf.

## CI reading

Only the **top** wagon's checks matter. Compare each webhook's `HeadSHA` against the branch's
current head and discard echoes from superseded heads — after an equalization the whole chain
re-fires and most events are stale. On a real failure of the top: diagnose from the job log,
fix on the top wagon, push. The seed-fallback ladder logs `teko-ci:` lines that name the stage,
the rung SHA and the probe walk — read those before theorizing.
