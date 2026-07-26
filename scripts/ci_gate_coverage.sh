#!/usr/bin/env sh
# scripts/ci_gate_coverage.sh — prove, from the workflow file itself, that the aggregators
# actually aggregate.
#
# ── THE LAW THIS ENFORCES, AND WHY IT IS A SCRIPT AND NOT A CONVENTION ────────────────────────
# Seven times on this train a job has run, gone red, and NOT blocked the merge, because a
# hand-maintained `needs:` list had fallen behind the job list. A list cannot fall behind a check
# that DERIVES it. This reads the workflow, extracts every job key and every aggregator's `needs:`,
# and fails when the two do not line up. It is called BY each aggregator, so it cannot be skipped
# without skipping the required check itself.
#
# Four assertions:
#
#   1. COVERAGE — every job that is not an aggregator appears in at least one aggregator's
#      `needs:`. A job outside every `needs:` runs, can fail, and blocks nothing.
#   2. NO PHANTOMS — no aggregator `needs:` a job the workflow does not define. (GitHub rejects
#      this at parse time, but catching it here names the aggregator instead of the file.)
#   3. ROOTED — every aggregator `needs:` the ARTIFACT ROOT. This is the owner's ruling of
#      2026-07-26 ("todas as lanes de pull_request precisam depender dessa lane de artefatos")
#      made structural: an aggregator that does not transitively stand on the root can report
#      success while the root that every lane consumes was never green. It is also what stops an
#      aggregator from being vacuous in LIGHT mode — the root runs in both tiers, so every gate
#      always has at least one job it is genuinely asserting.
#   4. NAMES — the five required-check names exist VERBATIM. They are the only required checks on
#      `main`, and `main` has no bypass: a name that stops reporting does not go red, it goes
#      PENDING FOREVER and jams the drain. A typo in a `name:` is therefore a merge-blocking
#      outage, and it is checked here where it is cheap.
#
# FAIL-CLOSED BY CONSTRUCTION: if the job-key extraction ever broke it would not find the
# aggregators either, and assertion 0 stops the run rather than passing an empty comparison.
#
# Usage:  ci_gate_coverage.sh <workflow.yml> <root-job> <gate-job>…
#
# POSIX sh + awk only — no PyYAML, which is not guaranteed on every runner image.
set -eu

WF="${1:?usage: ci_gate_coverage.sh <workflow.yml> <root-job> <gate-job>...}"
ROOT="${2:?missing root-job}"
shift 2
GATES="$*"
[ -n "$GATES" ] || { echo "ci_gate_coverage: no gate jobs named" >&2; exit 1; }
[ -f "$WF" ] || { echo "ci_gate_coverage: no such workflow: $WF" >&2; exit 1; }

# The five required-check names. Duplicated here ON PURPOSE rather than read from the file: an
# independent statement of the requirement is the only thing a broken workflow cannot satisfy by
# agreeing with itself.
REQUIRED_NAMES='CI gate
Test suite gate
Sanitizer gate
Heavy sanitizer gate (main)
SAST gate'

# ── extraction ────────────────────────────────────────────────────────────────────────────────
# Job keys are the only 2-space-indented bare-key lines inside the top-level `jobs:` block; a key
# at column 0 ends the block. A job's `needs:` is the first 4-space-indented `needs:` after its
# key. Both flow forms are accepted: `needs: a` and `needs: [a, b]`.
extract() {
    awk '
      /^jobs:[[:space:]]*$/ { inj = 1; next }
      inj && /^[^[:space:]#]/ { inj = 0 }
      inj && /^  [A-Za-z0-9_-]+:[[:space:]]*$/ {
          job = $0; sub(/:[[:space:]]*$/, "", job); sub(/^  /, "", job)
          print "JOB " job
          next
      }
      inj && job != "" && /^    needs:/ {
          line = $0
          sub(/^[[:space:]]*needs:[[:space:]]*/, "", line)
          gsub(/[][,]/, " ", line)
          print "NEEDS " job " " line
      }
    ' "$WF"
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM
extract > "$TMP/raw"

awk '$1 == "JOB" { print $2 }' "$TMP/raw" | sort -u > "$TMP/jobs"

echo "jobs defined in $WF:"
sed 's/^/  /' "$TMP/jobs"

FAILED=0

# ── 0. the extraction found the aggregators it was told about ────────────────────────────────
for g in $GATES; do
    if ! grep -qx "$g" "$TMP/jobs"; then
        echo "ERROR: the job-key extraction did not find the aggregator '$g' — either the" >&2
        echo "extraction is broken or the aggregator was renamed. A broken extraction must not be" >&2
        echo "allowed to pass a comparison." >&2
        FAILED=1
    fi
done
if ! grep -qx "$ROOT" "$TMP/jobs"; then
    echo "ERROR: the artifact root '$ROOT' is not a job in $WF." >&2
    FAILED=1
fi
[ "$FAILED" = "0" ] || { echo "ci_gate_coverage FAILED (extraction)"; exit 1; }

# ── per-gate needs ────────────────────────────────────────────────────────────────────────────
: > "$TMP/covered"
for g in $GATES; do
    awk -v g="$g" '$1 == "NEEDS" && $2 == g { for (i = 3; i <= NF; i++) print $i }' "$TMP/raw" \
        | sort -u > "$TMP/needs.$g"
    echo "needs of $g:"
    sed 's/^/  /' "$TMP/needs.$g"
    cat "$TMP/needs.$g" >> "$TMP/covered"

    # 2. NO PHANTOMS
    while read -r n; do
        [ -n "$n" ] || continue
        grep -qx "$n" "$TMP/jobs" || {
            echo "ERROR: $g needs '$n', which $WF does not define." >&2
            FAILED=1
        }
    done < "$TMP/needs.$g"

    # 3. ROOTED
    if ! grep -qx "$ROOT" "$TMP/needs.$g"; then
        echo "ERROR: $g does not need the artifact root '$ROOT'." >&2
        echo "  Every aggregator must stand on the root: it is the lane every other lane consumes," >&2
        echo "  and it is the one job that runs in BOTH tiers — without it an aggregator whose own" >&2
        echo "  legs are all full-only reports SUCCESS in light mode having asserted nothing." >&2
        FAILED=1
    fi
done

# ── 1. COVERAGE ───────────────────────────────────────────────────────────────────────────────
sort -u "$TMP/covered" > "$TMP/covered.s"
printf '%s\n' $GATES | sort -u > "$TMP/gates"
comm -23 "$TMP/jobs" "$TMP/gates" > "$TMP/expected"
MISSING="$(comm -23 "$TMP/expected" "$TMP/covered.s")"
echo "jobs to be covered : $(tr '\n' ' ' < "$TMP/expected")"
echo "jobs covered       : $(tr '\n' ' ' < "$TMP/covered.s")"
if [ -n "$MISSING" ]; then
    echo "ERROR: these jobs run but no aggregator needs them — they cannot block a merge:" >&2
    printf '%s\n' "$MISSING" | sed 's/^/  /' >&2
    FAILED=1
fi

# ── 4. NAMES ──────────────────────────────────────────────────────────────────────────────────
printf '%s\n' "$REQUIRED_NAMES" | while IFS= read -r nm; do
    [ -n "$nm" ] || continue
    if ! grep -qF "name: $nm" "$WF"; then
        echo "ERROR: the required-check name '$nm' does not appear in $WF." >&2
        echo "  It is a required check on main, and main has no bypass — a name that stops" >&2
        echo "  reporting goes PENDING FOREVER, it does not go red." >&2
        exit 1
    fi
    echo "required-check name present: $nm"
done || FAILED=1

if [ "$FAILED" = "1" ]; then
    echo "ci_gate_coverage FAILED."
    exit 1
fi
echo "ci_gate_coverage OK — every job is covered, every aggregator is rooted at '$ROOT', all five required names present."
