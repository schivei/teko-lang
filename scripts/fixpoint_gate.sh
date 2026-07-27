#!/usr/bin/env sh
# scripts/fixpoint_gate.sh — THE SELF-HOSTING FIXPOINT, run inside each producing sublane.
#
# Owner ruling 2026-07-27: *"quando o vagão 20 fechar verde e coletarmos o teko.c que ele produziu,
# não haverá mais escada a não ser fazer build o dobrado (sobre o mesmo fonte) de 3 gens para bater
# gen2 e gen3, mas ambos gen1, 2 e 3 gerados por este último teko.c, não deverá mais emitir um
# teko.c"*, and then: *"pode colocar o fixpoint logo após as compilações nos ambientes (direto em
# cada sublane)"*.
#
# WHAT IT PROVES, and why it is NOT the ladder wearing a different hat. The ladder chained
# generations over DIFFERENT sources (distinct SHAs) to cross a capability gap. This chains
# generations over the SAME source to prove the compiler reproduces itself:
#
#     gen1  = the compiler this lane just produced      (built by the bootstrap C — given to us)
#     gen2  = gen1(source)                              (first self-build)
#     gen3  = gen2(source)                              (second self-build)
#     ASSERT gen2 == gen3, byte for byte
#
# gen1 is deliberately NOT compared: it was produced by a DIFFERENT compiler (today the versioned
# `bootstrap/teko.c`, harvested from wagon 15), so `gen1 != gen2` is expected and healthy — it is
# the bootstrap generation, not a fixpoint candidate. The fixpoint begins where the compiler starts
# feeding on its own output.
#
# WHY IT DID NOT EXIST UNTIL NOW, stated plainly because the gap is the interesting part: the
# fixpoint is cited as a standing guardrail in README.md, TEKO_MASTER_PLAN.md, TEKO_HISTORY.md,
# DECISION_LOG.md, docs/BUILDING.md and TEKO_ROADMAP_NET_CRYPTO.md — and no workflow ever computed
# it. Searched, 2026-07-27: the only generation-to-generation comparison in the repository lived in
# `theory_generation_decay.sh`, an EXPERIMENT written that same day. `pr.yml` builds a `gen2-mp`,
# but under TEKO_MEM_PARANOID — a memory probe that builds and discards, comparing nothing. Six
# documents asserted a gate that was never wired.
#
# THE PATH TRAP, and why gen2 and gen3 are built at the SAME output path. A compiler that bakes any
# part of its output path into the binary would make two builds differ for a reason that has
# nothing to do with the fixpoint, and the failure would read as a real non-determinism. So this
# builds gen2 at $W/out, MOVES it aside, deletes $W/out, and builds gen3 at the very same $W/out.
# Identical path, identical cwd, identical everything the build can observe about where it is.
#
# ZERO-C IS CHECKED HERE TOO, because the owner's stopping criterion is one event seen from two
# sides: *"um teko.c que não gera outro teko.c"* is the same moment as the native backend learning
# to build the compiler. A gen2/gen3 that still emits C is a fixpoint that has not arrived, even if
# the bytes match — so both are asserted, and the C check is reported separately from the byte
# check so a log reader can tell WHICH half is outstanding.
#
# Usage:  sh scripts/fixpoint_gate.sh <gen1-binary> [PROJECT_DIR] [WORK_DIR]
#
# Env:
#   TEKO_FIXPOINT_RT_DIR   runtime dir pinned onto each build (default: <PROJECT_DIR>/src/runtime).
#                          Same reason build_with_seed_fallback.sh pins it: a compiler resolves
#                          teko_rt.{h,c} relative to ITS OWN argv[0], so a generation living
#                          outside the tree would otherwise compile against the wrong runtime.
#   TEKO_FIXPOINT_SOFT     set to 1 to REPORT the verdict and exit 0 anyway. Intended for the
#                          window in which the native backend cannot yet build the compiler at all
#                          (`fat-pointer receiver call not yet lowered (N2)`), where a hard failure
#                          here would mask every other signal in the lane rather than add one.
#                          It prints the same verdict either way — it never hides the result.
set -eu

GEN1="${1:?usage: fixpoint_gate.sh <gen1-binary> [PROJECT_DIR] [WORK_DIR]}"
PROJ="${2:-$PWD}"
W="${3:-.fixpoint}"

log() { printf '%s\n' "fixpoint: $*" >&2; }

SOFT="${TEKO_FIXPOINT_SOFT:-0}"

# verdict MESSAGE — the single exit point, so the soft/hard switch cannot drift between branches
# and so the printed verdict is identical in both modes (only the exit status differs).
verdict_fail() {
    log "VERDICT: FAILED — $1"
    if [ "$SOFT" = "1" ]; then
        log "TEKO_FIXPOINT_SOFT=1 — reporting without failing the lane. The verdict above stands."
        exit 0
    fi
    exit 1
}

[ -x "$GEN1" ] || { log "gen1 '$GEN1' is not an executable file"; exit 1; }
GEN1="$(cd "$(dirname "$GEN1")" && pwd)/$(basename "$GEN1")"
PROJ="$(cd "$PROJ" && pwd)"

RT_DIR="${TEKO_FIXPOINT_RT_DIR:-$PROJ/src/runtime}"
[ -d "$RT_DIR" ] || RT_DIR=""

rm -rf "$W"
mkdir -p "$W"
W="$(cd "$W" && pwd)"

# build_gen COMPILER LOGFILE — builds $PROJ at the FIXED path $W/out with COMPILER, dry
# (`--no-verify --release` — the test gate belongs to the test lanes, owner ruling 2026-07-27).
# Echoes nothing; the caller inspects $W/out.
build_gen() {
    bg_bin="$1"; bg_log="$2"
    rm -rf "$W/out"
    if [ -n "$RT_DIR" ]; then
        ( cd "$PROJ" && TK_RT_DIR="$RT_DIR" "$bg_bin" . -o "$W/out" --no-verify --release ) >"$bg_log" 2>&1
    else
        ( cd "$PROJ" && "$bg_bin" . -o "$W/out" --no-verify --release ) >"$bg_log" 2>&1
    fi
}

# take_gen NAME — moves the just-built compiler out of $W/out to $W/NAME, and records whether the
# build emitted a teko.c. Frees $W/out so the NEXT generation can be built at the identical path.
take_gen() {
    tg_name="$1"
    if [ -f "$W/out/teko.c" ]; then
        printf '1' > "$W/$tg_name.emitted-c"
    else
        printf '0' > "$W/$tg_name.emitted-c"
    fi
    if [ -x "$W/out/teko" ]; then
        mv "$W/out/teko" "$W/$tg_name"
    elif [ -x "$W/out/teko.exe" ]; then
        mv "$W/out/teko.exe" "$W/$tg_name"
    else
        return 1
    fi
    rm -rf "$W/out"
}

log "gen1 = $GEN1"
log "source = $PROJ"
"$GEN1" --version >&2 2>&1 || true

log "building gen2 = gen1(source) ..."
if ! build_gen "$GEN1" "$W/gen2.log"; then
    log "----- gen2 build FAILED — gen1 cannot build the source it came from -----"
    sed 's/^/fixpoint:   | /' "$W/gen2.log" >&2 || true
    verdict_fail "gen1 does not self-host: it cannot build this source at all, so there is no gen2 to compare"
fi
take_gen gen2 || verdict_fail "the gen2 build reported success but left no binary at $W/out"
log "gen2 ready ($(wc -c < "$W/gen2") bytes)"

log "building gen3 = gen2(source) ..."
if ! build_gen "$W/gen2" "$W/gen3.log"; then
    log "----- gen3 build FAILED — gen2 cannot rebuild its own source -----"
    sed 's/^/fixpoint:   | /' "$W/gen3.log" >&2 || true
    verdict_fail "gen2 built, but cannot rebuild the source — the chain breaks at the second generation"
fi
take_gen gen3 || verdict_fail "the gen3 build reported success but left no binary at $W/out"
log "gen3 ready ($(wc -c < "$W/gen3") bytes)"

# ── the two assertions, reported SEPARATELY so a partial arrival is legible ────────────────────
FIX_OK=1

if cmp -s "$W/gen2" "$W/gen3"; then
    log "byte-identity: gen2 == gen3  ✓"
else
    FIX_OK=0
    log "byte-identity: gen2 != gen3  ✗"
    log "  gen2 $(wc -c < "$W/gen2") bytes, gen3 $(wc -c < "$W/gen3") bytes"
    cmp "$W/gen2" "$W/gen3" >&2 2>&1 || true
fi

C2="$(cat "$W/gen2.emitted-c" 2>/dev/null || echo '?')"
C3="$(cat "$W/gen3.emitted-c" 2>/dev/null || echo '?')"
if [ "$C2" = "0" ] && [ "$C3" = "0" ]; then
    log "zero-C: neither generation emitted a teko.c  ✓"
else
    # NOT a failure of the byte check, and deliberately not folded into it. Emitting C is the
    # EXPECTED state until the native backend can build the compiler; what this line does is make
    # the outstanding half visible on every run instead of only when someone reads a build log.
    log "zero-C: gen2 emitted-c=$C2, gen3 emitted-c=$C3  ✗ (the native backend does not yet build the compiler)"
    FIX_OK=0
fi

[ "$FIX_OK" = "1" ] || verdict_fail "the fixpoint has not arrived — see the two lines above for which half is outstanding"

log "VERDICT: PASSED — gen2 == gen3 byte for byte, and neither emitted C"
rm -rf "$W"
exit 0
