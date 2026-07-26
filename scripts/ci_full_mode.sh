#!/usr/bin/env sh
# scripts/ci_full_mode.sh — decide, from PRIMARY SOURCES, whether this pull request must run the
# FULL gate or may run the LIGHT one. Prints `full=true` or `full=false` on stdout and the reasons
# on stderr.
#
# ── THE DEFECT THIS REPLACES (measured on PR #95, 2026-07-26) ─────────────────────────────────
# The old rule was ONE line: `full = (base_ref == "main")`. On a STACKED TRAIN every wagon's base
# is the wagon below it, so NO WAGON EVER RAN FULL. The consequence was not theoretical: on #95,
# two of nine test lanes ran, and of the four lanes the owner had just ordered into existence
# (linux-arm64-musl, both riscv64, windows-arm64) exactly zero ran. Every one of them would have
# DEBUTED at the drain — the least reversible moment there is. Worse, both sanitizer aggregators
# reported SUCCESS having validated nothing, because "all legs skipped" is indistinguishable from
# "all legs passed" to a gate that only asks `!= failure`.
#
# ── THE RULE ──────────────────────────────────────────────────────────────────────────────────
# The question the split must answer is NOT "is this the landing wagon". It is "CAN THIS CHANGE
# BREAK THIS LANE". The light tier exists to skip lanes whose axis the change cannot touch — an
# ARCH delta, a LIBC delta, an OS delta. A change to Teko-level compiler source (src/checker,
# src/build, …) is architecture-neutral by construction: it is compiled by the same emitter into
# the same C on every target, so linux-x86_64-glibc + windows-x86_64 genuinely cover it. A change
# to the things BELOW that line cannot make that claim:
#
#   .github/workflows/**  the lane definitions themselves — a new or edited lane MUST debut on the
#                         wagon that writes it, not at the drain. This is the case that made the
#                         old rule visibly wrong.
#   scripts/**            the build/CI shell surface: seeding, the ladder, asset production,
#                         packaging, the per-format object checks. All of it is per-platform.
#   src/runtime/**        teko_rt.c is the C linked into EVERY artifact — the arena, the alignment
#                         rules, the win32 shims. The __int128 under-alignment this repo already
#                         fixed was invisible on a max_align_t>=16 host and fatal on arm64.
#   src/assert/**         same seam, same reason.
#   src/backend/**        the own-AOT emitters ARE the per-arch/per-format code (ELF/Mach-O/COFF,
#                         x86_64/arm64/riscv64). A change here is the definition of "can break a
#                         lane the light tier does not run".
#   bootstrap/**          the committed host seeds are per-HOST blobs.
#   teko.tkp              the version the binaries embed and the tag the release derives.
#
# base_ref == main is KEPT as an additional trigger, not as the only one: the landing wagon is the
# last chance before the drain and pays the full gate regardless of what it touched.
#
# ── WHY THE GATES CALL THIS AND NOT `plan`'s OUTPUT ───────────────────────────────────────────
# A gate that accepts "skipped" on the word of the job that decided to skip cannot detect a broken
# planner — which is exactly the failure this repo has now hit six times. `plan` decides the mode
# with dorny/paths-filter; the gates re-derive it HERE, from the pull request's own file list over
# the API, and FAIL when the two disagree. Two independent mechanisms, one requirement.
#
# ── FAIL-CLOSED ───────────────────────────────────────────────────────────────────────────────
# If the file list cannot be obtained, this exits non-zero with `full=unknown`. It does NOT guess
# `false`: an unverifiable mode is the precise state in which a gate must refuse to approve, and
# guessing the cheap answer is how the vacuous green got here in the first place.
#
# Usage:   ci_full_mode.sh
# Env:     BASE_REF     the PR's base branch (github.base_ref)
#          PR_NUMBER    the pull request number (github.event.pull_request.number)
#          GITHUB_REPOSITORY, GH_TOKEN   for the file-list API call
#
# POSIX sh only.
set -eu

BASE_REF="${BASE_REF:-}"
PR_NUMBER="${PR_NUMBER:-}"
REPO="${GITHUB_REPOSITORY:-}"

log() { printf '%s\n' "ci_full_mode: $*" >&2; }

# The FULL-triggering path globs, one per line. Kept as a literal list rather than a regex so the
# rule reads as the rule.
FULL_PREFIXES='.github/workflows/
scripts/
src/runtime/
src/assert/
src/backend/
bootstrap/'
FULL_FILES='teko.tkp'

REASONS=""

if [ "$BASE_REF" = "main" ]; then
    REASONS="$REASONS base_ref=main(landing-wagon)"
fi

if [ -z "$PR_NUMBER" ] || [ -z "$REPO" ]; then
    log "no PR number / repository in the environment — the changed-file list cannot be read"
    printf 'full=unknown\n'
    exit 1
fi

FILES="$(gh api --paginate "repos/${REPO}/pulls/${PR_NUMBER}/files" --jq '.[].filename' 2>/dev/null || true)"
if [ -z "$FILES" ]; then
    # An empty list is NOT "nothing changed" — a PR always changes something. It means the call
    # failed or was truncated, and an unverifiable mode must not be turned into a cheap one.
    log "the changed-file list for PR #$PR_NUMBER came back EMPTY — cannot verify the mode."
    log "This is fail-closed on purpose: guessing 'light' here is the vacuous green this exists"
    log "to prevent."
    printf 'full=unknown\n'
    exit 1
fi

log "PR #$PR_NUMBER changed $(printf '%s\n' "$FILES" | wc -l | tr -d ' ') file(s); base_ref='$BASE_REF'"

for f in $FILES; do
    for p in $FULL_PREFIXES; do
        case "$f" in
            "$p"*) REASONS="$REASONS $f(matches $p*)" ;;
        esac
    done
    for x in $FULL_FILES; do
        [ "$f" = "$x" ] && REASONS="$REASONS $f(full-triggering file)"
    done
done

if [ -n "$REASONS" ]; then
    log "FULL — reasons:"
    for r in $REASONS; do log "    $r"; done
    printf 'full=true\n'
else
    log "LIGHT — nothing changed under the workflow definitions, the build scripts, the runtime C,"
    log "the assert seam, the native backends, the committed seeds or the manifest, and the base is"
    log "not main. The arch/libc/OS axes cannot be affected by this change."
    printf 'full=false\n'
fi
exit 0
