#!/usr/bin/env sh
# scripts/ci_phase_clock.sh — one-line-per-phase elapsed-time reporting, SOURCED (never run
# directly) by every stage of the producer pipeline: scripts/produce_assets.sh and
# scripts/build_with_seed_fallback.sh.
#
# THE DEFECT THIS CLOSES. Measured on a real producer run: a job took 880s (x86_64) / 534s
# (arm64) and NOTHING in the log said how that time was spent. The dry build's ~5.5 minutes was
# indistinguishable from "the glibc asset build" to a reader without prior knowledge of the
# pipeline, because no phase boundary was ever timestamped. `phase_mark` fixes that: it names
# each boundary and reports both how long that phase took on its own and how much of the job's
# total wall clock has elapsed.
#
# THE EPOCH crosses a PROCESS boundary. `scripts/produce_assets.sh` calls
# `scripts/build_with_seed_fallback.sh` as a separate `sh` child, not a sourced-in-place script —
# so a plain shell variable set before the fork would not survive it. `TEKO_CI_PHASE_EPOCH` is
# therefore an EXPORTED environment variable, set once by whichever caller sources this file
# first, and left untouched by every later stage (`phase_clock_init` is deliberately idempotent).
#
# THE LAST-MARK STATE lives in a small file, not a variable, for the SAME reason: a mark set by
# `build_with_seed_fallback.sh` (four ladder marks, sequential, single process) must still be
# readable by `produce_assets.sh`'s OWN marks after that child process has exited. This file is
# NEVER written to concurrently — the only concurrent phase in the pipeline (the parallel Linux
# asset builds in produce_assets.sh) deliberately times ITSELF locally instead of calling
# `phase_mark`, precisely to avoid two processes racing to update one file.
#
# Usage (sourced, never executed):
#   . scripts/ci_phase_clock.sh
#   phase_clock_init              # idempotent — a no-op once TEKO_CI_PHASE_EPOCH is set
#   phase_mark "ladder gen1"      # -> "phase: ladder gen1                     +71s  (t=76s)"
#
# POSIX sh only.

PHASE_CLOCK_STATE="${TEKO_CI_PHASE_STATE:-.ci-phase-clock}"

# phase_clock_init — sets and exports TEKO_CI_PHASE_EPOCH to now, UNLESS a parent process already
# set it (the common case: every stage after the very first inherits the same epoch). Also seeds
# the last-mark state file so the FIRST phase_mark call reports its own duration rather than the
# time since the Unix epoch.
phase_clock_init() {
    if [ -z "${TEKO_CI_PHASE_EPOCH:-}" ]; then
        TEKO_CI_PHASE_EPOCH="$(date +%s)"
        export TEKO_CI_PHASE_EPOCH
    fi
    [ -f "$PHASE_CLOCK_STATE" ] || printf '%s' "$TEKO_CI_PHASE_EPOCH" > "$PHASE_CLOCK_STATE"
}

# phase_mark NAME — logs NAME's own duration (since the previous mark, or since init) and the
# cumulative time since the job's epoch, to stderr. Safe to call without phase_clock_init having
# run first — a missing epoch or state file degrades to "now", it never fails the caller.
phase_mark() {
    pm_name="$1"
    pm_now="$(date +%s)"
    pm_epoch="${TEKO_CI_PHASE_EPOCH:-$pm_now}"
    pm_last="$pm_epoch"
    [ -f "$PHASE_CLOCK_STATE" ] && pm_last="$(cat "$PHASE_CLOCK_STATE")"
    pm_delta=$((pm_now - pm_last))
    pm_total=$((pm_now - pm_epoch))
    printf 'phase: %-36s +%4ds  (t=%ds)\n' "$pm_name" "$pm_delta" "$pm_total" >&2
    printf '%s' "$pm_now" > "$PHASE_CLOCK_STATE"
}
