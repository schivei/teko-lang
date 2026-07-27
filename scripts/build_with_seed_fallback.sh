#!/usr/bin/env sh
# scripts/build_with_seed_fallback.sh — build gen1 (the tip's compiler) from the released
# seed, with a STAGED BOOTSTRAP fallback for when the seed cannot compile the tip directly.
#
# INVARIANT (owner ruling 2026-07-24, replacing the older "seed builds the tip" rule): the
# released seed only has to build the PR's BASE lineage. A compiler built from an ancestor is
# itself a valid seed for a newer commit, so when the RAW released seed cannot compile the tip
# (a genuine language/codegen capability jump landed since the seed was cut), this script walks
# a chain of intermediate generations until one of them reaches the tip. "We run the tests
# under gen1, not under the seed" (the .30 ruling) extends naturally: gen1 may come from a few
# extra hops when the seed alone cannot reach it.
#
# FAST PATH (unchanged, zero extra cost): the seed builds the tip directly. This is the
# common case and costs exactly what it always did — one build, no git history probing.
#
# FALLBACK (only entered when the fast path fails) — the LADDER, and it is PINNED, not probed
# (owner ruling 2026-07-25: "se sabe quais compilam, seja determinístico e os utilize"):
#
#   for each rung in $LADDER_RUNGS, in order:  build it with the compiler in hand
#   then:                                      build the tip with the last rung's compiler
#
# The pair in LADDER_RUNGS was DISCOVERED by the probing algorithm (still here, behind
# TEKO_LADDER_DISCOVER=1) and CONFIRMED by a green run (PR #92, run 30158725410): rung 1 is the
# last commit the released 0.3.0.30 seed can build, and rung 2 is the last commit that rung 1's
# compiler can build. Both are required — the log of that run shows the seed probing and REJECTING
# rung 2, so the pair is irreducible and nobody should "optimize" the first one away.
#
# WHY PINNING: the probe is a linear walk that builds one project per candidate commit and reads
# the failure. In that same run it cost ~75 failed builds and ~2.5 minutes per lane BEFORE any
# useful work — on every lane, every platform, every PR, multiplied again under qemu. The rungs are
# a property of the train's own history, not of the runner, so they belong in the file.
#
# WHY IT NEVER SILENTLY FALLS BACK TO PROBING: if a pinned rung fails to build, the pin is stale
# and this script FAILS LOUD, naming the commit and how to rediscover the pair. Quietly reverting
# to a probe is exactly how the determinism would be lost again (M.3 — a fallback that hides the
# obsolete pin is worse than no pin).
#
# Why the pair had to be discovered in the first place: a fixed rung (the merge-base with the base
# branch) is not necessarily buildable by the previous generation. Proven on the .31 train — the
# cast-width wagon ADDED the W-RULE to the checker and then DELETED the now-redundant manual casts
# from the corpus, so its own head requires a W-RULE-capable compiler while an older generation dies
# on it with B.22 ("operands must be the same type"). The buildable rung is the older wagon that has
# the new CAPABILITY but not yet the corpus that DEPENDS on it.
#
# A push directly to main never enters any of this: main's own merge-base with itself is itself, so
# the "no bootstrap gap" guard fails loud instead of pretending a fallback exists — a released seed
# that cannot build main is a real regression, not a capability gap.
#
# TRANSITIONAL: the whole ladder is a .31 measure. Owner ruling — the first wagon of .32 cuts the
# .31 release as the seed and REMOVES this file's reason to exist.
#
# The intermediate builds are DRY (`--no-verify`, no test gate) because CI already gated each of
# those commits on the PR that landed it; owner ruling 2026-07-24: the dry intermediate build is
# a TRANSITIONAL .31 measure, to be undone in .32 and not repeated.
#
# Usage:   sh scripts/build_with_seed_fallback.sh [OUT_DIR]
#          OUT_DIR defaults to "bin" and receives the SAME gen1 binary the direct call to
#          `<seed> . -o OUT_DIR --no-verify --release` would have produced — callers do not
#          need to know which path was taken.
#
# Env:
#   TEKO_SEED_FALLBACK_SEED_BIN     the seed command to invoke (default: teko, resolved on PATH)
#   TEKO_SEED_FALLBACK_BASE_BRANCH  the branch the fallback bootstraps from (default: GITHUB_BASE_REF
#                                   when Actions sets it — a STACKED PR bootstraps from its BASE
#                                   branch, the predecessor wagon — else main)
#   TEKO_LADDER_DISCOVER            set to 1 to REDISCOVER the rungs by probing instead of using the
#                                   pins. A HUMAN tool, for refreshing LADDER_RUNGS when a pin goes
#                                   stale; never set in CI (that would restore the cost the pins
#                                   removed, and hide the staleness the pinned path reports).
set -eu

OUT_DIR="${1:-bin}"
SEED_BIN="${TEKO_SEED_FALLBACK_SEED_BIN:-teko}"
BASE_BRANCH="${TEKO_SEED_FALLBACK_BASE_BRANCH:-${GITHUB_BASE_REF:-main}}"

WORKTREE_DIR=""

log() { printf '%s\n' "teko-ci: $*" >&2; }

# cleanup — removes the scratch worktree this script creates while bootstrapping the fallback,
# on any exit path (success or failure). Every stage's output lives INSIDE that worktree, so
# removing it reclaims all of them.
cleanup() {
  [ -n "$WORKTREE_DIR" ] && [ -d "$WORKTREE_DIR" ] && git worktree remove --force "$WORKTREE_DIR" >/dev/null 2>&1
  return 0
}
trap cleanup EXIT

# build_project BIN DIR OUT LOGFILE [RT_DIR] — runs "BIN . -o OUT --no-verify --release"
# with cwd DIR, tees combined output to LOGFILE, and returns the build's own exit status.
# RT_DIR, when non-empty, pins TK_RT_DIR for the build: the compiler otherwise locates
# teko_rt.{h,c} relative to ITS OWN binary (argv[0]), and every fallback stage breaks under
# that rule — CI provisions the seed into <tip>/.seed, so an ancestor probe would compile
# ancestor-generated C against the TIP's runtime header (proven: probes died on the exact
# tk_rt_datetime_* symbols the time-redesign wagon removed), and an intermediate compiler
# lives inside the probe worktree, so the tip build would symmetrically pick the ANCESTOR's
# runtime. Each stage passes the runtime dir of the tree it is actually compiling.
build_project() {
  bin="$1"; proj_dir="$2"; out="$3"; logfile="$4"; rt_dir="${5:-}"
  if [ -n "$rt_dir" ]; then
    ( cd "$proj_dir" && TK_RT_DIR="$rt_dir" "$bin" . -o "$out" --no-verify --release ) >"$logfile" 2>&1
  else
    ( cd "$proj_dir" && "$bin" . -o "$out" --no-verify --release ) >"$logfile" 2>&1
  fi
}

# rt_dir_of BASE — echoes BASE's in-tree runtime dir (src/runtime, then the bundled-install
# runtime layout), or empty when neither exists (the caller then leaves the compiler's own
# argv[0]-relative resolution in charge).
rt_dir_of() {
  base="$1"
  if [ -f "$base/src/runtime/teko_rt.h" ]; then
    printf '%s\n' "$base/src/runtime"
  elif [ -f "$base/runtime/teko_rt.h" ]; then
    printf '%s\n' "$base/runtime"
  else
    printf '%s\n' ""
  fi
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
log "seed FAILED to build the tip directly — engaging the staged bootstrap ladder"

# ── RUNG 0: THE COMMITTED SEED. Tried BEFORE the pinned SHA ladder, and the ordering is the
# whole point (owner acceptance criterion 2026-07-26 — the counter-machine's PR on the ORG must
# go green FIRST TRY).
#
# The pinned ladder below stands on two intermediate commits of this train. The merge train
# SQUASH-merges its wagons, so those SHAs never become reachable from `main`, and
# `ensure_full_history`'s `git fetch --unshallow origin` fetches BRANCHES — not the refs of a
# squashed-and-deleted wagon. On the org, therefore, the ladder's first `git checkout <rung>` is
# a guaranteed failure: the ladder depends on repository state that the merge strategy destroys.
#
# `bootstrap/seeds/` does not. It is IN THE TREE, sha256-verified against SEEDS.sha256 before
# it is decompressed, and it is cut FROM the train's own tip — so it builds the tip DIRECTLY,
# with no rungs at all. pr.yml's `seed-debut` proves exactly that on all five hosts on every
# full run, which is why this is a rung and not a hope.
#
# It is deliberately rung 0 and not rung -1: the newest RELEASED seed is still tried first (fast
# path above), because the committed blob is a TRANSITIONAL .31 measure that the first .32 wagon
# deletes. When it goes, this rung self-disables — `commit_seed_bin` finds no manifest and says so
# — and the fast path plus the ladder are what remain, unchanged.
#
# host_seed_label — the committed seeds are keyed by HOST THAT RUNS THE COMPILER, not by release
# target, so this derives the host from uname rather than taking a label from the caller: a seam
# the caller could get wrong is a seam that silently picks the wrong blob.
host_seed_label() {
  hs_os="$(uname -s 2>/dev/null || echo unknown)"
  hs_arch="$(uname -m 2>/dev/null || echo unknown)"
  case "$hs_os" in
    Linux)
      case "$hs_arch" in
        x86_64|amd64)  printf '%s' "linux-x86_64" ;;
        aarch64|arm64) printf '%s' "linux-arm64" ;;
        *)             printf '%s' "" ;;
      esac ;;
    Darwin) printf '%s' "macos-arm64" ;;
    MINGW*|MSYS*|CYGWIN*|Windows_NT)
      case "$hs_arch" in
        x86_64|amd64)  printf '%s' "windows-x86_64" ;;
        *)             printf '%s' "" ;;
      esac ;;
    *) printf '%s' "" ;;
  esac
}

# commit_seed_rung — provision bootstrap/seeds/'s blob for THIS host and build the tip with it.
# Returns 0 only when the tip actually built; every giving-up path logs WHY, because a rung that
# fails silently is indistinguishable from a rung that was never tried.
commit_seed_rung() {
  cs_label="$(host_seed_label)"
  if [ -z "$cs_label" ]; then
    log "rung 0: no committed host seed exists for $(uname -s)/$(uname -m) — skipping"
    return 1
  fi
  cs_manifest="${TEKO_SEEDS_DIR:-bootstrap/seeds}/SEEDS.sha256"
  if [ ! -f "$cs_manifest" ]; then
    log "rung 0: no committed seed manifest at $cs_manifest — skipping (expected once .32 drops it)"
    return 1
  fi
  cs_log="$(mktemp)"
  if ! TEKO_SEED_PREFER_COMMITTED=1 sh scripts/ci_provision_teko.sh "$cs_label" >"$cs_log" 2>&1; then
    log "rung 0: the committed seed for '$cs_label' could not be provisioned:"
    sed 's/^/teko-ci:   | /' "$cs_log" >&2
    rm -f "$cs_log"
    return 1
  fi
  rm -f "$cs_log"
  cs_bin=""
  if [ -x .seed/teko ]; then cs_bin="$PWD/.seed/teko"
  elif [ -x .seed/teko.exe ]; then cs_bin="$PWD/.seed/teko.exe"
  else
    log "rung 0: provisioning reported success but no .seed/teko[.exe] is executable — skipping"
    return 1
  fi
  cs_tip_log="$(mktemp)"
  if build_project "$cs_bin" "$PWD" "$OUT_DIR" "$cs_tip_log" "$(rt_dir_of "$PWD")"; then
    cat "$cs_tip_log"
    rm -f "$cs_tip_log"
    log "rung 0: the COMMITTED seed ('$cs_label') built the tip directly — no SHA ladder needed"
    return 0
  fi
  log "rung 0: the committed seed ('$cs_label') could not build the tip either:"
  tail -20 "$cs_tip_log" | sed 's/^/teko-ci:   | /' >&2
  rm -f "$cs_tip_log"
  return 1
}

if commit_seed_rung; then
  rm -f "$FAST_LOG"
  exit 0
fi
log "rung 0 did not reach the tip — falling through to the PINNED SHA ladder (canonical-repo only:"
log "its rungs are unreachable wherever the wagons were squash-merged)"

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

# The ladder worktree and every stage's output live INSIDE the workspace, not the system temp
# dir: on the Windows runners the C toolchain demonstrably works on the workspace drive but
# fails opaquely when teko builds from %TEMP% (proven by probe logs — Teko compiles, cc dies).
# Same-workspace scratch also keeps everything on one filesystem.
WORKTREE_DIR="${GITHUB_WORKSPACE:-$PWD}/.teko-seed-fallback-wt"
rm -rf "$WORKTREE_DIR"
git worktree remove --force "$WORKTREE_DIR" >/dev/null 2>&1 || true
git worktree add --detach "$WORKTREE_DIR" "$MERGE_BASE_SHA" >/dev/null

MAX_STAGES=4
MAX_PROBES=64
CURRENT_BIN="$SEED_BIN"
CURRENT_DESC="the released seed"
STAGE=0
PROBE_FROM="$MERGE_BASE_SHA"
LAST_RUNG=""
TIP_RT_DIR="$(rt_dir_of "$PWD")"
TIP_LOG="$(mktemp)"

# THE PINNED PAIR. Discovered by the probe below, confirmed green in PR #92 (run 30158725410):
# rung 1 is the last commit the released 0.3.0.30 seed can build; rung 2 is the last commit rung 1's
# compiler can build. BOTH are needed — that run shows the seed probing and rejecting rung 2 — so
# do not "optimize" the first one away. Refresh with TEKO_LADDER_DISCOVER=1 (by hand, never in CI)
# when a pin goes stale, and paste the rungs the discovery log names back into this line.
LADDER_RUNGS="71c763d0ccec64df9fcd6c285a6782c642254e38 071c9c172f70c4fec5ff495e285cfc9cdef97fcb"

# build_rung SHA STAGE — check the ladder worktree out at SHA and build it with $CURRENT_BIN into a
# stage-private output dir, echoing that dir. Returns the build's own status.
build_rung() {
  rung_sha="$1"; rung_stage="$2"
  RUNG_OUT="$WORKTREE_DIR/.rung-out-$rung_stage"
  git -C "$WORKTREE_DIR" checkout -q --detach "$rung_sha"
  # Clean stale artifacts of the previous stage, but PRESERVE every .rung-out-* — those hold the
  # compilers this ladder is standing on (including the one about to run).
  git -C "$WORKTREE_DIR" clean -fdxq -e '.rung-out-*'
  rm -rf "$RUNG_OUT"
  mkdir -p "$RUNG_OUT"
  build_project "$CURRENT_BIN" "$WORKTREE_DIR" "$RUNG_OUT" "$RUNG_LOG" "$(rt_dir_of "$WORKTREE_DIR")"
}

# stale_pin_fatal SHA — the M.3 heart of the pinned ladder: a pinned rung that will not build means
# the pin is OBSOLETE, and this says so and stops. It deliberately does NOT fall back to probing:
# a silent fallback restores the ~75 wasted builds per lane AND hides the staleness, which is how
# the determinism the owner asked for would quietly evaporate.
stale_pin_fatal() {
  log "FATAL: pinned ladder rung $1 FAILED to build — the pin in LADDER_RUNGS is obsolete."
  log "This script does NOT fall back to probing (that is what the pins removed). To refresh:"
  log "  TEKO_LADDER_DISCOVER=1 sh scripts/build_with_seed_fallback.sh   # run by hand, not in CI"
  log "then paste the rung SHAs it reports into LADDER_RUNGS in this file."
  log "----- pinned rung build log ($1) -----"
  sed 's/^/teko-ci:   | /' "$RUNG_LOG" >&2
  exit 1
}

if [ "${TEKO_LADDER_DISCOVER:-0}" != "1" ] && [ -n "$LADDER_RUNGS" ]; then
  log "using the PINNED ladder (set TEKO_LADDER_DISCOVER=1 to rediscover the rungs instead)"
  RUNG_LOG="$(mktemp)"
  for RUNG_SHA in $LADDER_RUNGS; do
    STAGE=$((STAGE + 1))
    if ! build_rung "$RUNG_SHA" "$STAGE"; then stale_pin_fatal "$RUNG_SHA"; fi
    if ! NEXT_BIN="$(resolve_bin "$RUNG_OUT")"; then
      log "FATAL: pinned rung $RUNG_SHA built but no teko/teko.exe was found in $RUNG_OUT"
      exit 1
    fi
    CURRENT_BIN="$NEXT_BIN"
    CURRENT_DESC="gen$STAGE(pinned rung $RUNG_SHA)"
    log "ladder stage $STAGE: built pinned rung $RUNG_SHA — that compiler is the new rung"
  done
  rm -f "$RUNG_LOG"
  if ! build_project "$CURRENT_BIN" "$PWD" "$OUT_DIR" "$TIP_LOG" "$TIP_RT_DIR"; then
    log "FATAL: the tip does not build with $CURRENT_DESC — the last pinned rung no longer reaches"
    log "this tip, i.e. the pins are stale for the capability this branch adds. Refresh them with"
    log "TEKO_LADDER_DISCOVER=1 (by hand) and update LADDER_RUNGS."
    log "----- tip build with the last pinned rung (failure) -----"
    cat "$TIP_LOG"
    exit 1
  fi
  cat "$TIP_LOG"
  rm -f "$FAST_LOG" "$TIP_LOG"
  log "staged bootstrap complete — tip built by $CURRENT_DESC after $STAGE PINNED ladder stage(s)"
  exit 0
fi

log "DISCOVERY MODE: probing for the ladder rungs (TEKO_LADDER_DISCOVER=1) — report the rungs this"
log "run names back into LADDER_RUNGS; CI must never take this path."

while :; do
  # Does the compiler we currently hold reach the tip?
  if build_project "$CURRENT_BIN" "$PWD" "$OUT_DIR" "$TIP_LOG" "$TIP_RT_DIR"; then
    cat "$TIP_LOG"
    rm -f "$FAST_LOG" "$TIP_LOG"
    log "staged bootstrap complete — tip built by $CURRENT_DESC after $STAGE ladder stage(s)"
    exit 0
  fi
  if [ "$STAGE" -eq 0 ]; then
    log "confirmed: the released seed cannot build the tip — probing back from merge-base $MERGE_BASE_SHA"
  else
    log "stage $STAGE compiler ($CURRENT_DESC) still cannot build the tip — probing for a newer rung"
    log "tip build error tail:"
    tail -8 "$TIP_LOG" | sed 's/^/teko-ci:   | /' >&2
  fi

  STAGE=$((STAGE + 1))
  if [ "$STAGE" -gt "$MAX_STAGES" ]; then
    log "FATAL: the tip is still unreachable after $MAX_STAGES ladder stages — the capability gap"
    log "between the released seed and this tip is deeper than the ladder is allowed to climb."
    log "----- seed build of the tip (failure) -----"
    cat "$FAST_LOG"
    log "----- last stage's build of the tip (failure) -----"
    cat "$TIP_LOG"
    rm -f "$TIP_LOG"
    exit 1
  fi

  # Find the NEWEST first-parent ancestor at-or-before $PROBE_FROM that $CURRENT_BIN can build.
  RUNG_SHA="$PROBE_FROM"
  RUNG_OUT="$WORKTREE_DIR/.rung-out-$STAGE"
  RUNG_LOG="$(mktemp)"
  PROBES=0
  RUNG_RT_DIR=""
  while :; do
    git -C "$WORKTREE_DIR" checkout -q --detach "$RUNG_SHA"
    # Clean stale artifacts of the previous probe, but PRESERVE every .rung-out-* — those hold
    # the compilers this ladder is standing on (including the one about to run).
    git -C "$WORKTREE_DIR" clean -fdxq -e '.rung-out-*'
    rm -rf "$RUNG_OUT"
    mkdir -p "$RUNG_OUT"
    RUNG_RT_DIR="$(rt_dir_of "$WORKTREE_DIR")"
    if build_project "$CURRENT_BIN" "$WORKTREE_DIR" "$RUNG_OUT" "$RUNG_LOG" "$RUNG_RT_DIR"; then
      break
    fi
    if grep -q "cc failed to build the generated C" "$RUNG_LOG"; then
      log "FATAL: rung candidate $RUNG_SHA compiles at the Teko level but its C compile FAILED — this"
      log "is an ENVIRONMENTAL toolchain problem, not a capability gap; walking further back cannot fix it."
      log "----- full rung build log ($RUNG_SHA) -----"
      sed 's/^/teko-ci:   | /' "$RUNG_LOG" >&2
      rm -f "$RUNG_LOG" "$TIP_LOG"
      exit 1
    fi
    PROBES=$((PROBES + 1))
    if [ "$PROBES" -ge "$MAX_PROBES" ]; then
      log "FATAL: no buildable rung found within $MAX_PROBES first-parent steps of $PROBE_FROM (stage $STAGE)"
      log "----- last rung candidate $RUNG_SHA (failure) -----"
      cat "$RUNG_LOG"
      rm -f "$RUNG_LOG" "$TIP_LOG"
      exit 1
    fi
    if ! PARENT_SHA="$(git -C "$WORKTREE_DIR" rev-parse -q --verify "$RUNG_SHA^" 2>/dev/null)"; then
      log "FATAL: ran out of history walking back from $PROBE_FROM (stage $STAGE) — no buildable rung"
      log "----- last rung candidate $RUNG_SHA (failure) -----"
      cat "$RUNG_LOG"
      rm -f "$RUNG_LOG" "$TIP_LOG"
      exit 1
    fi
    log "probe: $CURRENT_DESC cannot build $RUNG_SHA — stepping back to $PARENT_SHA"
    RUNG_SHA="$PARENT_SHA"
  done
  rm -f "$RUNG_LOG"

  # NO-PROGRESS GUARD. If this stage's probe landed on the very commit the previous stage
  # already built, a newer compiler reached no further — every commit in the base lineage that
  # is buildable at all is already behind us, so the tip needs a capability the tip ITSELF
  # introduces. That is a genuine bootstrap impossibility for this commit, not something more
  # stages can fix: say so instead of burning the remaining stages rebuilding the same rung.
  if [ "$RUNG_SHA" = "$LAST_RUNG" ]; then
    log "FATAL: no progress — stage $STAGE's probe landed on $RUNG_SHA again, the same rung stage"
    log "$((STAGE - 1)) already built. The tip requires a capability that no buildable ancestor"
    log "provides, i.e. one the tip itself introduces; a staged bootstrap cannot bridge that."
    log "----- seed build of the tip (failure) -----"
    cat "$FAST_LOG"
    log "----- last stage's build of the tip (failure) -----"
    cat "$TIP_LOG"
    rm -f "$TIP_LOG"
    exit 1
  fi
  LAST_RUNG="$RUNG_SHA"

  if ! NEXT_BIN="$(resolve_bin "$RUNG_OUT")"; then
    log "FATAL: rung build reported success but no teko/teko.exe binary was found in $RUNG_OUT"
    rm -f "$TIP_LOG"
    exit 1
  fi
  log "ladder stage $STAGE: built $RUNG_SHA ($PROBES probe(s) back from $PROBE_FROM) — that compiler is the new rung"

  # The next stage probes the window BETWEEN this rung and the tip: a newer rung than the one
  # we just built is the only thing that can carry us further, and re-probing from the same
  # point would rebuild what we already hold. Walking forward is not possible with a
  # first-parent walk, so the next probe starts again at the merge-base — but the compiler in
  # hand is strictly newer, so it reaches strictly further before being rejected. Convergence
  # is bounded by MAX_STAGES.
  CURRENT_BIN="$NEXT_BIN"
  CURRENT_DESC="gen$STAGE(rung $RUNG_SHA)"
  PROBE_FROM="$MERGE_BASE_SHA"
done
