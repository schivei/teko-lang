#!/usr/bin/env sh
# scripts/theory_generation_decay.sh — DOES A COMPILER GET SLOWER EACH TIME IT REBUILDS ITSELF?
#
# THE QUESTION, and why the ordinary CI numbers cannot answer it. Owner observation, 2026-07-27:
# *"cada degrau fica mais lento, e pior, eu notei isso até buildando o mesmo código na mesma
# sessão, a próxima geração parece ficar mais lenta"*.
#
# The producing lane's ladder LOOKS like it confirms this — gen1 24s, gen2 84s, gen3 90s of codegen
# on arm64 — but it cannot, because each of those stages compiles a DIFFERENT tree: rung 1, then
# rung 2, then the tip. "Slower" is ambiguous between *did more work* and *did the same work
# slower*, and no amount of staring at those numbers separates the two.
#
# THIS EXPERIMENT REMOVES THE AMBIGUITY by holding the input constant. Every generation compiles
# THE SAME TREE — this checkout, unmodified — so the work is identical by construction and the only
# thing that varies is WHICH COMPILER does it:
#
#     seed  ──build(tree)──▶  gen1  ──build(tree)──▶  gen2  ──build(tree)──▶  gen3  ──▶ …
#
# Any difference in wall clock, per-phase time or peak RSS between two generations is therefore
# degradation, full stop. There is nothing else left for it to be.
#
# WHY THIS BRANCH IS CUT FROM `main` AND CARRIES NOTHING ELSE (owner order: *"abra uma theory com o
# código da main, nada mais"*). `main` is at the SAME version as the released seed (0.3.0.30), so
# the seed builds it DIRECTLY — no ladder, no rungs, no fallback. On a wagon the seed fails and the
# ladder engages, which would put three different trees back into the measurement and reintroduce
# exactly the ambiguity this exists to remove.
#
# ── IT ALSO TESTS THE FIXPOINT, FOR FREE ─────────────────────────────────────────────────────
# Each generation's emitted C and binary are hashed. The project's law is that the compiler
# rebuilds itself to a byte-identical fixpoint, so from some generation onward `bin_sha256` must
# STOP CHANGING. Once two consecutive generations are byte-identical they are THE SAME PROGRAM,
# and the same program doing the same work MUST take the same time. So:
#
#   * times differ while the bytes still differ   → ordinary convergence, not a defect
#   * times differ AFTER the bytes stop changing  → the measurement is lying, or the fixpoint is
#   * bytes never stop changing                   → there is no fixpoint, which is a bigger finding
#     than the timing question that prompted this
#
# Usage:  theory_generation_decay.sh [generations]      (default 4)
#
# Env:  TEKO_DECAY_SEED_BIN  a seed binary to use instead of provisioning one (for a hand run)
#
# POSIX sh only.
set -eu

GENS="${1:-4}"
WORK="$PWD/.decay"
log() { printf '%s\n' "decay: $*" >&2; }

# sha256_of FILE — the portable digest, same helper shape the sibling scripts use.
sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}'
    else printf '%s' "no-digest-tool"; fi
}

# field_of LOG PATTERN — the seconds figure the compiler's own phase report prints for PATTERN.
# Reads the compiler's report rather than timing the phases externally: those numbers are what a
# reader will compare against the CI logs, and a second, differently-derived clock would invite
# exactly the "which one is right" argument this experiment exists to end.
field_of() {
    sed -n "s/^ *$2 .*[^0-9.]\([0-9][0-9.]*\)s .*/\1/p" "$1" | tail -n1
}

rm -rf "$WORK"; mkdir -p "$WORK"

if [ -n "${TEKO_DECAY_SEED_BIN:-}" ]; then
    CURRENT="$TEKO_DECAY_SEED_BIN"
    log "seed: $CURRENT (from TEKO_DECAY_SEED_BIN)"
else
    sh scripts/ci_provision_teko.sh "${TEKO_DECAY_SEED_LABEL:?set TEKO_DECAY_SEED_LABEL or TEKO_DECAY_SEED_BIN}"
    CURRENT="$PWD/.seed/teko"
    [ -f "$CURRENT" ] || CURRENT="$PWD/.seed/teko.exe"
fi
[ -x "$CURRENT" ] || { log "FATAL: no runnable seed at '$CURRENT'"; exit 1; }

log "seed version: $("$CURRENT" --version 2>&1 | head -n1)"
log "measuring $GENS generations, every one compiling THIS SAME TREE"

# THE RUNTIME DIR IS PINNED TO THIS TREE, and that is load-bearing. A compiler locates
# `teko_rt.{h,c}` relative to its OWN binary (argv[0]); each generation lives in its own output
# directory, so without this every generation after the first would compile against a runtime
# beside itself rather than the one in the tree under test — a different input, which is the one
# thing this experiment must not have.
RT_DIR="$PWD/src/runtime"

printf '%s\n' "gen  total   checker  codegen  cc      peakRSS    teko.c sha256   binary sha256" > "$WORK/table.txt"

i=1
while [ "$i" -le "$GENS" ]; do
    out="$WORK/gen$i"
    lg="$WORK/gen$i.log"
    start="$(date +%s)"
    if ! ( TK_RT_DIR="$RT_DIR" "$CURRENT" . -o "$out" --no-verify --release ) >"$lg" 2>&1; then
        log "FATAL: generation $i failed to build the tree — the log follows"
        sed 's/^/decay:   | /' "$lg" >&2
        exit 1
    fi
    elapsed=$(($(date +%s) - start))

    nxt="$out/teko"; [ -f "$nxt" ] || nxt="$out/teko.exe"
    [ -x "$nxt" ] || { log "FATAL: generation $i reported success but produced no binary in $out"; exit 1; }

    csha="$(sha256_of "$out/teko.c" 2>/dev/null || printf '%s' 'no-c')"
    bsha="$(sha256_of "$nxt")"
    rss="$(sed -n 's/^teko: memory: peak \(.*\)$/\1/p' "$lg" | tail -n1)"

    log "----- generation $i build report -----"
    sed 's/^/decay:   | /' "$lg" >&2

    printf 'g%-3s %-7s %-8s %-8s %-7s %-10s %.12s   %.12s\n' \
        "$i" "${elapsed}s" "$(field_of "$lg" checker)s" "$(field_of "$lg" codegen)s" \
        "$(field_of "$lg" cc)s" "${rss:-?}" "$csha" "$bsha" >> "$WORK/table.txt"

    CURRENT="$nxt"
    i=$((i + 1))
done

echo
echo "=================================================================================="
echo " GENERATION DECAY — every row compiled THE SAME TREE, only the compiler differs"
echo "=================================================================================="
cat "$WORK/table.txt"
echo
echo "HOW TO READ THIS:"
echo "  * the work is IDENTICAL on every row, so any change in checker/codegen/cc is the"
echo "    compiler getting slower (or faster), never the input getting bigger."
echo "  * once two consecutive rows share a 'binary sha256' they are THE SAME PROGRAM —"
echo "    their times MUST match. If they do not, the measurement is at fault, not the"
echo "    compiler."
echo "  * if 'binary sha256' never repeats, this tree has NO fixpoint, which is a larger"
echo "    finding than the timing question."
