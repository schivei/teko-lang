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
# generations over the SAME source to prove the compiler reproduces itself.
#
# THERE ARE TWO FIXPOINTS, AND WHICH ONE IS CORRECT DEPENDS ON WHERE gen1 CAME FROM. The rule is
# one line: compare the first two generations that share a CODE GENERATOR. Owner ruling
# 2026-07-27: *"o fixpoint irá migrar de gen2 === gen3 para gen1 === gen2, quando tivermos o
# último teko.c"*.
#
#   BOOTSTRAP MODE — `bootstrap/teko.c` is present (today)
#     gen1 = cc(bootstrap/teko.c)     <- built by the C COMPILER, not by the native backend
#     gen2 = gen1(source)             <- native backend
#     gen3 = gen2(source)             <- native backend
#     ASSERT gen2 == gen3
#   gen1 is deliberately NOT compared here, and not because it is untrustworthy: it came out of
#   gcc/clang while gen2 came out of Teko's own backend. Two different code generators emitting
#   the same program cannot agree byte for byte — the same reason one `out/teko.c` compiled with
#   `cc` and with `musl-gcc` yields two different assets. The fixpoint begins where the compiler
#   starts feeding on its own output.
#
#   NATIVE MODE — no `bootstrap/teko.c` (the end state)
#     gen1 = seed(source)             <- the PUBLISHED release binary, native backend
#     gen2 = gen1(source)             <- native backend
#     ASSERT gen1 == gen2
#   With the C gone from the chain entirely — none emitted, none consumed — gen1 already shares
#   its generator with gen2, so the third generation buys nothing and one build per lane is saved.
#
# THE MODE IS NOT CONFIGURED, IT IS OBSERVED. The discriminator is the presence of the very file
# whose existence means "the seed cannot build this tree" — `bootstrap/teko.c`, the same condition
# `build_with_seed_fallback.sh`'s rung -1 keys off. So the day that file is deleted (owner:
# *"podemos apagar o teko.c e voltar a construção normal pegando a última versão publicada"*) this
# gate migrates by itself, in the same commit, with nobody remembering to flip it.
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
# It is scoped to gen2 and gen3 ONLY — never the ladder, never gen1. Owner ruling 2026-07-27: *"ele
# tem que saber quem está testando, logo, ele não pode nem deve avaliar a escada e nem a gen1,
# apenas gen2 e 3"*. gen1 emits C BY CONSTRUCTION today (it is built by the committed C's compiler,
# which still has the C backend), so judging it would fail the gate for doing exactly what it must
# do. This is also why `scripts/no_emitted_c.sh` is NOT wired: it sweeps the whole worktree and
# cannot tell whose emission it found.
#
# Usage:  sh scripts/fixpoint_gate.sh <gen1-binary> [PROJECT_DIR] [WORK_DIR]
#
# Env:
#   TEKO_FIXPOINT_RT_DIR   runtime dir pinned onto each build (default: <PROJECT_DIR>/src/runtime).
#                          Same reason build_with_seed_fallback.sh pins it: a compiler resolves
#                          teko_rt.{h,c} relative to ITS OWN argv[0], so a generation living
#                          outside the tree would otherwise compile against the wrong runtime.
#   TEKO_FIXPOINT_BACKEND  which backend the two generations are built with (default `c` — see
#                          `build_gen`). Set to `native` once the native route self-hosts; that is
#                          the .32/.33 switch, not a knob for day-to-day use.
#   TEKO_FIXPOINT_MODE     `bootstrap` | `native` — force the mode instead of observing it. Only
#                          for testing the gate itself; CI must let it observe, or the automatic
#                          migration above stops being automatic.
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

# verdict_fail MESSAGE — the fixpoint does not hold. Fails the lane. THERE IS NO OTHER OUTCOME
# BESIDES PASSED, and that is a correction of my own design, not the original one.
#
# This script briefly carried a third verdict, NOT APPLICABLE, for the case where the native
# backend honest-stops before gen2 exists — on the reasoning that a fixpoint is a claim about two
# generations, so with no pair there is no answer, true or false. Owner ruling 2026-07-27:
# *"discordo veementemente, só há uma saída que pode ser verdadeira"* and *"se nem consegue gerar
# gen2, nem tem que perder tempo com testes, não vai passar"*.
#
# The reasoning was wrong because it measured the wrong thing. The fixpoint is not a curiosity
# about two binaries; it is the statement THIS COMPILER BUILDS ITSELF. A compiler that cannot
# produce gen2 has already failed that statement outright — the missing pair IS the failure, not
# an obstacle to observing one. Dressing it as "not applicable" turned the loudest possible defect
# into a neutral note, and downstream lanes went on spending time testing a compiler that cannot
# reproduce itself.
#
# The cost is real and accepted: a red `artifact` job turns every downstream lane into a SKIP. That
# is the correct shape. There is nothing to learn from testing a compiler that does not self-host.
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
# THE BACKEND IS PINNED HERE, INSIDE THE SUBSHELL, AND THAT SCOPE IS THE WHOLE POINT.
#
# `gen1` (the wagon's own compiler) defaults to the NATIVE backend, which cannot yet build the
# compiler — so without asking for the C route there is no gen2 and no fixpoint. `TEKO_BACKEND=c`
# is what the .31 plan restores it for (owner, 2026-07-27: *".31 com as duas rotas, .32 ensina o
# nativo, .33 remove"*).
#
# WHY NOT EXPORT IT IN THE WORKFLOW STEP, which is the obvious place: because a global
# `TEKO_BACKEND=c` LEAKS INTO EVERYTHING THAT RUNS BELOW IT, and that is not a hypothetical — it
# was measured. Running the suite with it set turned `diagnostics.tkr` red: the scenario
# *"an unsupported TEKO_TARGET is an honest compile error"* got exit 0, because `TEKO_TARGET` is
# only consulted while the NATIVE backend is active. The regressors exist to exercise the native
# route; handing them the C one makes them assert something else entirely, and `cwd_build` went
# "green" the same way — not fixed, just compiled by another road.
#
# So the variable lives in THIS subshell, around THESE two builds, and nowhere else. The suite
# keeps running on the native default, which is what it is for.
#
# REMOVE IT when the native backend self-hosts (.32/.33): at that point gen2 comes from the native
# route and pinning C here would be measuring the road we are retiring.
build_gen() {
    bg_bin="$1"; bg_log="$2"
    rm -rf "$W/out"
    bg_backend="${TEKO_FIXPOINT_BACKEND:-c}"
    if [ -n "$RT_DIR" ]; then
        ( cd "$PROJ" && TK_RT_DIR="$RT_DIR" TEKO_BACKEND="$bg_backend" "$bg_bin" . -o "$W/out" --no-verify --release ) >"$bg_log" 2>&1
    else
        ( cd "$PROJ" && TEKO_BACKEND="$bg_backend" "$bg_bin" . -o "$W/out" --no-verify --release ) >"$bg_log" 2>&1
    fi
}

# take_gen NAME — moves the just-built compiler out of $W/out to $W/NAME, PRESERVES the teko.c that
# build emitted (as $W/NAME.c), and records whether there was one. Frees $W/out so the NEXT
# generation can be built at the identical path.
#
# THE EMITTED C IS KEPT, NOT JUST COUNTED, and that is the point of this function beyond moving a
# binary. Owner ruling 2026-07-28: *"fechando tudo verde, coletou o teko.c emitido no vagão 20?
# Acredito que seja importante a coleta, enviar o último teko.c estável da escada."*
#
# That C is the NEXT `bootstrap/teko.c`. Today's committed one came from wagon 15 and is old enough
# that the tree has moved under it; the one emitted by a generation that just PROVED gen2 == gen3 is
# the freshest C known to rebuild this exact tree. It used to be deleted three lines below, so the
# only artifact of a passing fixpoint was the word "PASSED" — the run threw away the one file worth
# keeping. `rm -rf "$W/out"` still runs; it just no longer takes the C with it.
take_gen() {
    tg_name="$1"
    if [ -f "$W/out/teko.c" ]; then
        printf '1' > "$W/$tg_name.emitted-c"
        cp "$W/out/teko.c" "$W/$tg_name.c"
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

# ── observe the mode ──────────────────────────────────────────────────────────────────────────
BOOTSTRAP_C="${TEKO_BOOTSTRAP_C:-$PROJ/bootstrap/teko.c}"
if [ -n "${TEKO_FIXPOINT_MODE:-}" ]; then
    MODE="$TEKO_FIXPOINT_MODE"
    log "mode = $MODE (FORCED via TEKO_FIXPOINT_MODE — CI must not do this)"
elif [ -f "$BOOTSTRAP_C" ]; then
    MODE=bootstrap
    log "mode = bootstrap ($BOOTSTRAP_C is present, so gen1 came out of cc, not the native backend)"
else
    MODE=native
    log "mode = native (no committed C — gen1 already shares its generator with gen2)"
fi

log "gen1 = $GEN1"
log "source = $PROJ"
"$GEN1" --version >&2 2>&1 || true

log "building gen2 = gen1(source) ..."
if ! build_gen "$GEN1" "$W/gen2.log"; then
    log "----- gen1's build of the source (did not complete) -----"
    sed 's/^/fixpoint:   | /' "$W/gen2.log" >&2 || true
    verdict_fail "gen1 does not build the source it came from — the compiler does not self-host, so gen2 does not exist and the fixpoint cannot hold. See the address above."
fi
take_gen gen2 || verdict_fail "the gen2 build reported success but left no binary at $W/out"
log "gen2 ready ($(wc -c < "$W/gen2") bytes)"

# LEFT and RIGHT are the two generations the mode says share a generator. Naming them once keeps
# the assertions below identical in both modes — the mode chooses the OPERANDS, never the rule.
if [ "$MODE" = "native" ]; then
    cp "$GEN1" "$W/gen1"
    LEFT=gen1
    RIGHT=gen2
    # gen1's own emitted-C is unknown to us here (it was built by the lane, not by this script),
    # so the zero-C assertion in native mode rests on gen2 — the generation this script watched.
    printf '%s' "$(cat "$W/gen2.emitted-c")" > "$W/gen1.emitted-c"
else
    log "building gen3 = gen2(source) ..."
    if ! build_gen "$W/gen2" "$W/gen3.log"; then
        log "----- gen2's build of the source (did not complete) -----"
        sed 's/^/fixpoint:   | /' "$W/gen3.log" >&2 || true
        verdict_fail "gen2 built but does not rebuild the source — the chain breaks at the second generation. See the address above."
    fi
    take_gen gen3 || verdict_fail "the gen3 build reported success but left no binary at $W/out"
    log "gen3 ready ($(wc -c < "$W/gen3") bytes)"
    LEFT=gen2
    RIGHT=gen3
fi

# ── the two assertions, reported SEPARATELY so a partial arrival is legible ────────────────────
FIX_OK=1

if cmp -s "$W/$LEFT" "$W/$RIGHT"; then
    log "byte-identity: $LEFT == $RIGHT  ✓"
else
    FIX_OK=0
    log "byte-identity: $LEFT != $RIGHT  ✗"
    log "  $LEFT $(wc -c < "$W/$LEFT") bytes, $RIGHT $(wc -c < "$W/$RIGHT") bytes"
    cmp "$W/$LEFT" "$W/$RIGHT" >&2 2>&1 || true
fi

# ── the harvest, and why it is REPORTED here rather than left to the caller ────────────────────
#
# A passing fixpoint leaves behind the freshest C known to rebuild this exact tree, and that file is
# the next `bootstrap/teko.c`. Naming it in the log — with its size and its agreement — is what lets
# a reader of the run decide to harvest WITHOUT re-deriving which of the two files to take.
#
# WHY $RIGHT.c AND NOT $LEFT.c, when the two are almost always identical: $RIGHT is the generation
# emitted by a compiler this script watched build successfully from source, and in bootstrap mode
# that is gen2 — the generation just proven byte-equal to its own successor. $LEFT.c in bootstrap
# mode came out of gen1, which the LANE built, not this script.
#
# THE AGREEMENT LINE IS THE INTERESTING ONE. `$LEFT.c == $RIGHT.c` means two different compiler
# BINARIES emitted byte-identical C for the same source — the C emission has reached its own
# fixpoint, one layer below the binary fixpoint. A disagreement there would be a real finding even
# with `$LEFT == $RIGHT` green, since it would mean the binaries agree while their C does not.
if [ -f "$W/$LEFT.c" ] && [ -f "$W/$RIGHT.c" ]; then
    if cmp -s "$W/$LEFT.c" "$W/$RIGHT.c"; then
        log "emitted-C identity: $LEFT.c == $RIGHT.c  ✓ ($(wc -c < "$W/$RIGHT.c") bytes)"
    else
        log "emitted-C identity: $LEFT.c != $RIGHT.c  — the binaries agree but their C does not"
        log "  $LEFT.c $(wc -c < "$W/$LEFT.c") bytes, $RIGHT.c $(wc -c < "$W/$RIGHT.c") bytes"
    fi
fi
if [ -f "$W/$RIGHT.c" ]; then
    log "harvest: $W/$RIGHT.c is this run's candidate for bootstrap/teko.c"
fi

# ZERO-C IS REPORTED, NOT ENFORCED — a DATED window, not a permanent softening. Owner ruling
# 2026-07-27: *"pode remover por hora a validação de no-c para este caso"*, inside the release plan
# he set out the same night: **.31 ships with BOTH routes alive** (the C emitter restored, the
# native backend still exercised by default), **.32 teaches the native backend**, **.33 removes C
# for good**. Under that plan a generation emitting C in .31/.32 is the DESIGNED state, so failing
# on it would red every run for doing precisely what the version asks of it.
#
# WHAT STILL FAILS, and it is the half that carries the meaning: `gen2 == gen3` byte for byte —
# THIS COMPILER REBUILDS ITSELF. That claim is enforced above and never relaxed.
#
# THIS IS NOT THE SOFT MODE RETURNING. That one printed `FAILED` and exited 0 — a lane green over a
# stated defect. Here the verdict tells the truth about the version it is running in: the byte
# fixpoint is REQUIRED and reported as such; the zero-C is an ANNOUNCED goal with a version
# attached, reported every run so its arrival is never a surprise.
#
# TO RE-ARM IT (the .33 wagon): set ZERO_C_ENFORCED=1 below. One line.
ZERO_C_ENFORCED="${TEKO_FIXPOINT_ZERO_C:-0}"
CL="$(cat "$W/$LEFT.emitted-c" 2>/dev/null || echo '?')"
CR="$(cat "$W/$RIGHT.emitted-c" 2>/dev/null || echo '?')"
if [ "$CL" = "0" ] && [ "$CR" = "0" ]; then
    log "zero-C: neither generation emitted a teko.c  ✓"
elif [ "$ZERO_C_ENFORCED" = "1" ]; then
    log "zero-C: $LEFT emitted-c=$CL, $RIGHT emitted-c=$CR  ✗ (enforced — TEKO_FIXPOINT_ZERO_C=1)"
    FIX_OK=0
else
    log "zero-C: $LEFT emitted-c=$CL, $RIGHT emitted-c=$CR  — REPORTED, not enforced (the .33 goal;"
    log "        .31 ships both routes on purpose). Set TEKO_FIXPOINT_ZERO_C=1 to make it bite."
fi

[ "$FIX_OK" = "1" ] || verdict_fail "the fixpoint has not arrived — see the two lines above for which half is outstanding"

if [ "$CL" = "0" ] && [ "$CR" = "0" ]; then
    log "VERDICT: PASSED — $LEFT == $RIGHT byte for byte, and neither emitted C"
else
    log "VERDICT: PASSED — $LEFT == $RIGHT byte for byte. (Both still emit C: the .33 goal, reported above.)"
fi
# CLEAN THE SCRATCH, NEVER THE HARVEST. This used to be a flat `rm -rf "$W"`, which deleted the
# emitted C two lines after the script had just printed
#
#     fixpoint: harvest: …/.fixpoint/gen3.c is this run's candidate for bootstrap/teko.c
#
# — it named the one file worth keeping and then destroyed it, so every downstream step saw an empty
# directory and `upload-artifact` reported "No files were found" over a fixpoint that had PASSED.
#
# What is scratch and what is product: the generation BINARIES (~2.5 MB each), the build LOGS and the
# emitted-c flags exist only to reach the verdict above and are worthless once it is printed. The
# `.c` files are the product — `gen3.c` is the next `bootstrap/teko.c`. So the sweep is enumerated
# rather than blanket, and `*.c` is simply not in it.
rm -rf "$W/out"
rm -f "$W/gen1" "$W/gen2" "$W/gen3" "$W"/*.log "$W"/*.emitted-c
exit 0
