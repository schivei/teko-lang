#!/usr/bin/env sh
# scripts/provenance_gate.sh — THE PROVENANCE GATE: refuses `bootstrap/teko.c` unless a matching
# declaration proves WHICH CI run produced it, and the file on disk still hashes to what that run
# measured.
#
# Owner ruling 2026-07-29, literal: "arquivos C de gerações ou teko C precisam ser construídos via
# CI, não pelos agentes, e somente quando o CI passar verde. Assim que se cria degrau." — and,
# naming the gap directly: "nada no repositório diz de que corrida de CI veio o bootstrap/teko.c
# que lá está, nem impede alguém de commitar um C construído à mão que pareça igual."
#
# ── WHAT bootstrap/DEGRAU ALREADY PROVES, AND WHAT IT DOES NOT ─────────────────────────────────
# `scripts/degrau.sh` answers "is `bootstrap/teko.c` DECLARED AS A RUNG?" — a claim about USE. A
# tree with no `bootstrap/DEGRAU` at all is the NORMAL, EXPECTED state even while `bootstrap/teko.c`
# sits committed: the C is harvested OUTPUT (`scripts/fixpoint_gate.sh`'s `stage_gen1_c`), versioned
# after a green fixpoint, and ignored as input until a human declares a bridge. That design is
# correct and this gate does not touch it — see `scripts/degrau.sh`'s own header for why the C's
# mere presence can never again mean "use me".
#
# What NEITHER `bootstrap/DEGRAU` NOR `degrau_scan` ever asked is "where did THIS FILE come from?".
# A `why` sentence explains a bootstrap emergency; it says nothing about which CI run produced the
# bytes now sitting at `bootstrap/teko.c`, and nothing stops a hand-built C — however produced —
# from replacing it undetected. That is this gate's entire job.
#
# ── THE RECORD: `bootstrap/PROVENANCE`, A SIBLING FILE, NOT A DEGRAU FIELD ─────────────────────
# The SAME `key: value`, `#`-comment format `bootstrap/DEGRAU` already uses — parsed by the SAME
# kernel (`kv_field`, `scripts/degrau.sh`) — but a DIFFERENT file, deliberately not new fields
# bolted onto `bootstrap/DEGRAU`. Two lifecycles that do not agree:
#   * `bootstrap/DEGRAU` is DELETED "the day the degrau closes" (`scripts/degrau.sh`'s own
#     `degrau_recipe`) — a ceremony about an ACTIVE bootstrap emergency ending.
#   * `bootstrap/teko.c` keeps being re-harvested from every green fixpoint whether or not any
#     degrau is ever open; its provenance must SURVIVE a degrau's closing, or the very next harvest
#     would silently lose the one thing this gate exists to keep.
# Folding the two into one file would make `run`/`commit`/`sha256` mandatory on every harvest that
# never opens a bootstrap emergency, or would delete the provenance record the moment a degrau
# closes — either wrong. A sibling file with the IDENTICAL parser closes the format without
# duplicating it.
#
#     c:       bootstrap/teko.c    # REQUIRED — the file this record describes, relative to root
#     run:     123456789           # REQUIRED — the CI run id that harvested it
#     commit:  <sha>                # REQUIRED — the git SHA that run measured (built gen1 from)
#     sha256:  <hex digest>         # REQUIRED — sha256 of the harvested file; MUST match it now
#     since:   0.3.1.0              # optional — the version the harvest landed in
#
# ── EVERY MALFORMED DECLARATION IS FATAL, NEVER "no record" ────────────────────────────────────
# Same law as `scripts/degrau.sh`, same reason: a `bootstrap/PROVENANCE` that exists but does not
# parse, or names a file that is not there, means a human started this ceremony and did not finish
# it — reporting that as "no provenance" would read as the ORDINARY unrecorded-harvest case this
# gate flags a different way, and would hide the exact state a reviewer most needs to see.
#
# ── A RECORD PROVES NOTHING ABOUT A FILE GIT DOES NOT KNOW ─────────────────────────────────────
# A `sha256:` that matches bytes sitting on disk only proves the record and the WORKING TREE agree
# — not that the working tree file is the one actually committed. `scripts/no_c_in_tests_gate.sh`
# (a sibling gate, `cargo/0.3.1.0-sem-c-nos-testes`) already answers "is this .c GENERATED or
# VERSIONED?" for a project directory by asking `git ls-files`, which distinguishing generated C
# from committed C, is the exact technique reused here rather than re-derived: before trusting any
# record this gate first asks git whether `bootstrap/teko.c` is TRACKED at all. An untracked file
# at that path — however convincing its bytes — is refused before its hash is even read.
#
# ── THE VERDICT ──────────────────────────────────────────────────────────────────────────────────
#   PASS  (exit 0)  no C at the target path — nothing to prove; or a complete record whose `c:`
#                    names the target and whose `sha256` matches it exactly as it sits on disk.
#   RED   (exit 1)  the C exists with no record at all, or exists but is NOT tracked by git, or a
#                    record exists whose digest does not match the file — it changed (or was
#                    replaced) after the record was written and nobody updated it. This is "C
#                    changed (or generated out of place) without matching provenance".
#   FATAL (exit 2)  the target is not inside a git working tree at all (tracked-ness cannot be
#                    decided), or `bootstrap/PROVENANCE` exists but is broken — a required key
#                    missing, its `c:` names a file other than the one being checked or one that is
#                    not there, or no digest tool exists to verify it at all. Never degrades to RED
#                    or PASS.
#
# Usage:  sh scripts/provenance_gate.sh [ROOT]
#         ROOT defaults to the current directory. Must be inside a git working tree once a C sits
#         at the target path — any checkout of this repository qualifies.
#
# Env:
#   TEKO_PROVENANCE_FILE  the declaration's path (default: <ROOT>/bootstrap/PROVENANCE). For
#                         testing the gate itself; CI must let it read the versioned file.
#   TEKO_PROVENANCE_C     the C this gate checks (default: <ROOT>/bootstrap/teko.c). Same override
#                         reason as TEKO_PROVENANCE_FILE.
set -eu

HERE="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=scripts/degrau.sh
. "$HERE/degrau.sh"

ROOT="${1:-$PWD}"
PROVENANCE_FILE="${TEKO_PROVENANCE_FILE:-$ROOT/bootstrap/PROVENANCE}"
TARGET_C="${TEKO_PROVENANCE_C:-$ROOT/bootstrap/teko.c}"

log() { printf '%s\n' "provenance: $*" >&2; }

# sha256_of FILE — portable digest (sha256sum on Linux, shasum -a 256 elsewhere), the SAME idiom
# `scripts/ci_provision_teko.sh` already uses to verify a downloaded seed. Empty when neither tool
# is on PATH, which the caller must treat as "cannot verify", never as "matches".
sha256_of() {
    sc_f="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$sc_f" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$sc_f" | awk '{print $1}'
    else
        printf '%s' ""
    fi
}

# provenance_recipe — the exact act that turns an unrecorded harvest into a declared one, printed
# by the branch that fails because a committed C has no record at all. A failure that names the
# remedy is the difference between a gate and a wall (the same law `degrau_recipe` follows).
provenance_recipe() {
    log "To record it, after a fixpoint that HELD (see scripts/fixpoint_gate.sh's harvest):"
    log "  1. take the harvested C from that green CI run's artifact, as bootstrap/teko.c;"
    log "  2. compute its digest: sha256sum bootstrap/teko.c;"
    log "  3. write bootstrap/PROVENANCE:"
    log "        c:       bootstrap/teko.c"
    log "        run:     <the CI run id that harvested it>"
    log "        commit:  <the git SHA that run measured>"
    log "        sha256:  <the digest from step 2>"
    log "  4. commit the C and the record TOGETHER — never one without the other."
    return 0
}

if [ ! -f "$TARGET_C" ]; then
    log "PASS — no '$TARGET_C' present, nothing to prove provenance for."
    exit 0
fi

GIT_ROOT="$(cd "$ROOT" 2>/dev/null && git rev-parse --show-toplevel 2>/dev/null)" || GIT_ROOT=""
if [ -z "$GIT_ROOT" ]; then
    log "FATAL: '$ROOT' is not inside a git working tree."
    log "This gate tells a COMMITTED C apart from one merely sitting on disk by asking git which"
    log "paths are tracked; with no repository there is no way to make that distinction, and"
    log "guessing 'probably fine' is exactly the silent pass this gate exists to end."
    exit 2
fi

if ! git -C "$GIT_ROOT" ls-files --error-unmatch -- "$TARGET_C" >/dev/null 2>&1; then
    log "FAIL — '$TARGET_C' is present but NOT tracked by git."
    log "The one legitimate C at this path is a deliberate commit, harvested from a green CI run"
    log "and versioned together with its provenance record — never a file merely left on disk."
    log "(scripts/no_c_in_tests_gate.sh catches this same shape, generated vs. versioned C, after"
    log "a test session; this gate catches it here, at the one path a committed C may occupy.)"
    exit 1
fi

if [ ! -f "$PROVENANCE_FILE" ]; then
    log "FAIL — '$TARGET_C' is present but '$PROVENANCE_FILE' does not exist."
    log "Every committed C needs a record of WHICH CI run produced it."
    provenance_recipe
    exit 1
fi

PV_C="$(kv_field "$PROVENANCE_FILE" c)"
PV_RUN="$(kv_field "$PROVENANCE_FILE" run)"
PV_COMMIT="$(kv_field "$PROVENANCE_FILE" commit)"
PV_SHA256="$(kv_field "$PROVENANCE_FILE" sha256)"

if [ -z "$PV_C" ] || [ -z "$PV_RUN" ] || [ -z "$PV_COMMIT" ] || [ -z "$PV_SHA256" ]; then
    log "FATAL: '$PROVENANCE_FILE' exists but is not a complete record. Required keys:"
    log "  c:       bootstrap/teko.c   the file this record describes"
    log "  run:     <CI run id>        the run that harvested it"
    log "  commit:  <sha>              the git SHA that run measured"
    log "  sha256:  <hex digest>       the digest of the harvested file"
    log "Delete the file if there is no record to make — an incomplete claim is worse than none."
    exit 2
fi

case "$PV_C" in
    /*) PV_C_RESOLVED="$PV_C" ;;
    *) PV_C_RESOLVED="$ROOT/$PV_C" ;;
esac

if [ "$PV_C_RESOLVED" != "$TARGET_C" ]; then
    log "FATAL: '$PROVENANCE_FILE' names '$PV_C_RESOLVED', not the file being checked ('$TARGET_C')."
    log "A record only proves provenance for the file it names — point it at the right one."
    exit 2
fi

ACTUAL_SHA256="$(sha256_of "$TARGET_C")"
if [ -z "$ACTUAL_SHA256" ]; then
    log "FATAL: no sha256sum/shasum on PATH — this gate cannot verify '$TARGET_C' at all."
    log "Refusing to report a pass it cannot back up."
    exit 2
fi

if [ "$ACTUAL_SHA256" != "$PV_SHA256" ]; then
    log "FAIL — '$TARGET_C' does not match the recorded provenance."
    log "  recorded sha256: $PV_SHA256"
    log "  actual   sha256: $ACTUAL_SHA256"
    log "'$TARGET_C' changed after '$PROVENANCE_FILE' was written and nobody updated the record."
    exit 1
fi

log "PASS — '$TARGET_C' matches CI run $PV_RUN (commit $PV_COMMIT), recorded in '$PROVENANCE_FILE'."
exit 0
