#!/usr/bin/env sh
# scripts/originating_pr_run.sh — "which pull request produced this commit, and which CI run
# gated it?" Resolved ONCE, here, because two workflows need the answer and neither may guess.
#
# ── WHY THE QUESTION IS HARD, AND WHY THE OBVIOUS ANSWER IS WRONG ────────────────────────────
# Under SQUASH-MERGE the commit that lands on a branch is a NEW commit: the checks ran on the pull
# request's head, a different sha. So "look up the checks at $GITHUB_SHA" finds nothing — that is
# the bug tag-on-version-bump.yml was carrying in 2026-07-25, where a 60-minute loop waited for
# runs that could not exist, burned the deadline and never minted a tag. The resolution is:
#   merge commit --> the merged PR it came from --> that PR's head sha --> the run at that head.
#
# ── TWO CALLERS ──────────────────────────────────────────────────────────────────────────────
#   tag-on-version-bump.yml  needs the run's CONCLUSION — do not mint a version tag from a commit
#                            whose gates were not green.
#   nightly.yml              needs the run's ID — to download the artifacts that run PUBLISHED and
#                            assert they are byte-identical to what this commit rebuilds.
# One resolver, so the two can never disagree about which run gated a commit.
#
# ── OUTPUT ───────────────────────────────────────────────────────────────────────────────────
# key=value lines on stdout, diagnostics on stderr:
#   pr=<number>  head_sha=<sha>  run_id=<id>  run_status=<status>  run_conclusion=<conclusion>
# `run_*` are empty when the PR resolved but no run of the named workflow exists at its head.
#
# EXIT CODES, because the callers need to tell the cases apart:
#   0  a pull request was resolved (inspect run_* for whether a run exists)
#   3  the commit does not resolve to a merged pull request — a direct push, or a non-squash merge
#   1  the API could not be read at all
#
# Usage:  originating_pr_run.sh <merge-sha> [workflow-name]
#         workflow-name defaults to "Pull Request CI" (the consolidated pull_request workflow).
# Env:    GITHUB_REPOSITORY, GH_TOKEN
#
# POSIX sh only.
set -eu

SHA="${1:?usage: originating_pr_run.sh <merge-sha> [workflow-name]}"
WF="${2:-Pull Request CI}"
REPO="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is not set}"

log() { printf '%s\n' "originating_pr_run: $*" >&2; }

PR="$(gh api "repos/${REPO}/commits/${SHA}/pulls" \
        --jq '[.[] | select(.merged_at != null)] | sort_by(.merged_at) | last | .number' 2>/dev/null || true)"
if [ -z "$PR" ] || [ "$PR" = "null" ]; then
    log "$SHA does not resolve to a merged pull request."
    log "Under squash-merge the checks ran on the PR head, not on this commit — with no PR there"
    log "is nothing to resolve. This is a direct push, or a merge this lookup does not model."
    printf 'pr=\nhead_sha=\nrun_id=\nrun_status=\nrun_conclusion=\n'
    exit 3
fi

HEAD_SHA="$(gh api "repos/${REPO}/pulls/${PR}" --jq '.head.sha' 2>/dev/null || true)"
if [ -z "$HEAD_SHA" ] || [ "$HEAD_SHA" = "null" ]; then
    log "PR #$PR resolved but its head sha could not be read"
    exit 1
fi
log "$SHA came from PR #$PR, head $HEAD_SHA"

# The LAST run by run_number: a re-run creates a new run at the same head, and the newest is the
# one whose verdict stands.
INFO="$(gh api --paginate "repos/${REPO}/actions/runs?head_sha=${HEAD_SHA}" \
          --jq "[.workflow_runs[] | select(.name==\"$WF\")] | sort_by(.run_number) | last | \"\(.id)|\(.status)|\(.conclusion)\"" 2>/dev/null || true)"

if [ -z "$INFO" ] || [ "$INFO" = "null" ]; then
    log "no '$WF' run found at head $HEAD_SHA"
    printf 'pr=%s\nhead_sha=%s\nrun_id=\nrun_status=\nrun_conclusion=\n' "$PR" "$HEAD_SHA"
    exit 0
fi

RUN_ID="${INFO%%|*}"
REST="${INFO#*|}"
STATUS="${REST%%|*}"
CONCLUSION="${REST##*|}"
log "'$WF' run $RUN_ID — status=$STATUS conclusion=$CONCLUSION"
printf 'pr=%s\nhead_sha=%s\nrun_id=%s\nrun_status=%s\nrun_conclusion=%s\n' \
    "$PR" "$HEAD_SHA" "$RUN_ID" "$STATUS" "$CONCLUSION"
exit 0
