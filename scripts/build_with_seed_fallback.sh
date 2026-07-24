#!/usr/bin/env sh
# scripts/build_with_seed_fallback.sh — build gen1 (the tip's compiler) from the released
# seed, with a staged bootstrap fallback for when the seed cannot compile the tip directly.
#
# INVARIANT (owner ruling 2026-07-24, replacing the older "seed builds the tip" rule): the
# released seed only has to build `main` (the merge-base). gen1(main) is itself a valid seed
# for the tip, so when the RAW released seed cannot compile the tip (a genuine language/
# codegen capability jump landed on this branch since the seed was cut), this script bridges
# the gap in one extra generation: seed -> gen1(merge-base) -> gen1(tip). "We run the tests
# under gen1, not under the seed" (the .30 ruling) extends naturally: gen1 just may come from
# one extra hop when the seed alone cannot reach it.
#
# FAST PATH (unchanged, zero extra cost): the seed builds the tip directly. This is the
# common case and costs exactly what it always did — one build, no git history probing.
#
# FALLBACK (only entered when the fast path fails): fetches enough history to compute the
# merge-base between the tip and $TEKO_SEED_FALLBACK_BASE_BRANCH (default `main`), builds
# THAT commit with the seed (a dry build — `--no-verify`, no test gate — because CI already
# gated it on the PR that landed it on main; owner ruling 2026-07-24: the dry intermediate
# build is a TRANSITIONAL .31 measure, to be undone in .32 and not repeated), then builds
# the tip with the resulting binary. A push directly to
# main never needs this: main's own merge-base with itself is itself, so the "no bootstrap
# gap" guard below fails loud instead of pretending a fallback exists — a released seed that
# cannot build main is a real regression, not a capability gap this script can paper over.
#
# Usage:   sh scripts/build_with_seed_fallback.sh [OUT_DIR]
#          OUT_DIR defaults to "bin" and receives the SAME gen1 binary the direct call to
#          `<seed> . -o OUT_DIR --no-verify --release` would have produced — callers do not
#          need to know which path was taken.
#
# Env:
#   TEKO_SEED_FALLBACK_SEED_BIN     the seed command to invoke (default: teko, resolved on PATH)
#   TEKO_SEED_FALLBACK_BASE_BRANCH  the branch the fallback bootstraps from (default: main)
set -eu

OUT_DIR="${1:-bin}"
SEED_BIN="${TEKO_SEED_FALLBACK_SEED_BIN:-teko}"
BASE_BRANCH="${TEKO_SEED_FALLBACK_BASE_BRANCH:-main}"

WORKTREE_DIR=""
GEN1_BASE_DIR=""

log() { printf '%s\n' "teko-ci: $*" >&2; }

# cleanup — removes the scratch worktree/output directory this script creates while
# bootstrapping the fallback, on any exit path (success or failure).
cleanup() {
  [ -n "$WORKTREE_DIR" ] && [ -d "$WORKTREE_DIR" ] && git worktree remove --force "$WORKTREE_DIR" >/dev/null 2>&1
  [ -n "$GEN1_BASE_DIR" ] && rm -rf "$GEN1_BASE_DIR"
  return 0
}
trap cleanup EXIT

# build_project BIN DIR OUT LOGFILE — runs "BIN . -o OUT --no-verify --release" with cwd
# DIR, tees combined output to LOGFILE, and returns the build's own exit status.
build_project() {
  bin="$1"; proj_dir="$2"; out="$3"; logfile="$4"
  ( cd "$proj_dir" && "$bin" . -o "$out" --no-verify --release ) >"$logfile" 2>&1
}

# resolve_bin DIR — echoes the built teko binary under DIR (teko or teko.exe), failing if
# neither is an executable file.
resolve_bin() {
  dir="$1"
  if [ -x "${dir}/teko" ]; then
    printf '%s\n' "${dir}/teko"
  elif [ -x "${dir}/teko.exe" ]; then
    printf '%s\n' "${dir}/teko.exe"
  else
    return 1
  fi
}

# logical_head — the commit whose ancestry should be compared against $BASE_BRANCH. A
# `pull_request`-triggered checkout sits on GitHub's synthetic merge commit (parent 1 = the
# base branch, parent 2 = the PR's own tip) — merge-basing against parent 1 would trivially
# return the base branch itself, defeating the fallback. When a second parent exists, use it;
# otherwise (a plain, non-merge HEAD) use HEAD as-is.
logical_head() {
  if head2="$(git rev-parse -q --verify HEAD^2 2>/dev/null)"; then
    printf '%s\n' "$head2"
  else
    git rev-parse HEAD
  fi
}

# ensure_full_history — the fast path never needs git history at all, so CI checkouts stay
# shallow by default; the fallback does need enough of it to compute a merge-base, so this
# unshallows once, only now that the fallback has actually been engaged.
ensure_full_history() {
  if [ "$(git rev-parse --is-shallow-repository)" = "true" ]; then
    git fetch --unshallow origin
  fi
  git fetch origin "$BASE_BRANCH"
}

FAST_LOG="$(mktemp)"
if build_project "$SEED_BIN" "$PWD" "$OUT_DIR" "$FAST_LOG"; then
  cat "$FAST_LOG"
  log "seed builds the tip directly — fast path, no fallback engaged"
  rm -f "$FAST_LOG"
  exit 0
fi
log "seed FAILED to build the tip directly — probing the staged fallback"

if ! ensure_full_history; then
  log "FATAL: could not fetch '$BASE_BRANCH' history from origin — no fallback path exists"
  log "----- seed build of the tip (failure) -----"
  cat "$FAST_LOG"
  exit 1
fi

HEAD_FOR_MERGE_BASE="$(logical_head)"
if ! MERGE_BASE_SHA="$(git merge-base "$HEAD_FOR_MERGE_BASE" FETCH_HEAD)"; then
  log "FATAL: no merge-base found between the tip and origin/$BASE_BRANCH — no fallback path exists"
  log "----- seed build of the tip (failure) -----"
  cat "$FAST_LOG"
  exit 1
fi

if [ "$MERGE_BASE_SHA" = "$HEAD_FOR_MERGE_BASE" ]; then
  log "FATAL: the tip IS the merge-base with origin/$BASE_BRANCH — there is no bootstrap gap to bridge"
  log "the seed-fallback invariant only requires the seed to build $BASE_BRANCH; a failure here is a real regression, not a capability gap"
  log "----- seed build of the tip (failure) -----"
  cat "$FAST_LOG"
  exit 1
fi

log "seed fallback engaged (seed cannot build tip; bootstrapping via gen1 of merge-base $MERGE_BASE_SHA)"

WORKTREE_DIR="$(mktemp -d)"
rmdir "$WORKTREE_DIR"
git worktree add --detach "$WORKTREE_DIR" "$MERGE_BASE_SHA" >/dev/null

GEN1_BASE_DIR="$(mktemp -d)"
BASE_LOG="$(mktemp)"
if ! build_project "$SEED_BIN" "$WORKTREE_DIR" "$GEN1_BASE_DIR" "$BASE_LOG"; then
  log "FATAL: seed failed to build merge-base $MERGE_BASE_SHA — the fallback's own bootstrap step failed"
  log "----- seed build of the tip (failure) -----"
  cat "$FAST_LOG"
  log "----- seed build of merge-base $MERGE_BASE_SHA (failure) -----"
  cat "$BASE_LOG"
  rm -f "$BASE_LOG"
  exit 1
fi
rm -f "$BASE_LOG"

if ! GEN1_BASE_BIN="$(resolve_bin "$GEN1_BASE_DIR")"; then
  log "FATAL: merge-base build reported success but no teko/teko.exe binary was found in $GEN1_BASE_DIR"
  exit 1
fi

TIP_LOG="$(mktemp)"
if ! build_project "$GEN1_BASE_BIN" "$PWD" "$OUT_DIR" "$TIP_LOG"; then
  log "FATAL: gen1 of merge-base $MERGE_BASE_SHA still failed to build the tip"
  log "----- seed build of the tip (failure) -----"
  cat "$FAST_LOG"
  log "----- gen1(merge-base $MERGE_BASE_SHA) build of the tip (failure) -----"
  cat "$TIP_LOG"
  rm -f "$TIP_LOG"
  exit 1
fi
cat "$TIP_LOG"
rm -f "$FAST_LOG" "$TIP_LOG"
log "seed fallback complete — tip built via gen1 of merge-base $MERGE_BASE_SHA"
