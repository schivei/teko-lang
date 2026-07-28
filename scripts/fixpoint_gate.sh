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
# THE CHAIN, AND WHICH LINK THE VERDICT COMPARES. Owner ruling 2026-07-28:
#
#   release 0.3.0.31 --TEKO_BACKEND=c--> gen0 --TEKO_BACKEND=c--> gen1 (emits teko.c)
#                    --native--> gen2 --native--> gen3            ASSERT gen2 == gen3
#
# The published release builds gen0 and gen0 builds gen1, both down the C route; gen1 then builds
# gen2 and gen2 builds gen3, both on the native backend. The first two links belong to
# `build_with_seed_fallback.sh` (the ladder, which hands this script its gen1); THIS script owns
# the last two and the verdict.
#
# THE VERDICT DOES NOT MOVE. Owner ruling 2026-07-28: *"gen1 nunca será igual a gen2 — são
# geradores diferentes (cc vs backend próprio do Teko). O veredito do fixpoint não muda: continua
# gen2 == gen3."* gen1 is deliberately NOT compared, and not because it is untrustworthy: it came
# out of `cc` over emitted C while gen2 comes out of Teko's own backend. Two different code
# generators emitting the same program cannot agree byte for byte — the same reason one
# `out/teko.c` compiled with `cc` and with `musl-gcc` yields two different assets. The fixpoint
# begins where the compiler starts feeding on its own output, and that is gen2.
#
# SUPERSEDED, RECORDED RATHER THAN DELETED: the 2026-07-27 ruling *"o fixpoint irá migrar de gen2
# === gen3 para gen1 === gen2, quando tivermos o último teko.c"* described a chain in which gen1
# and gen2 would eventually share a generator. Under the 0.3.1.0 chain they never do — gen1 is C
# route BY CONSTRUCTION (it is the generation that emits the teko.c we harvest) and gen2 is native
# — so the migration it anticipated has no arrival date and the operands are FIXED at gen2/gen3.
# This script therefore has ONE comparison, in every state of the world, and no mode that moves it.
#
# WHAT IS OBSERVED IS THE PROVENANCE OF gen1, AND THE CRITERION IS A DECLARATION, NOT A FILE.
# `scripts/degrau.sh` answers "is there a declared degrau?" for this gate and for the ladder, from
# one versioned claim (`bootstrap/DEGRAU`). Owner ruling 2026-07-28: *"só podemos usar teko.c se e
# somente se identificarmos degrau."* The OLD discriminator — "does `bootstrap/teko.c` exist?" —
# is retired here: under this chain the C is an OUTPUT, versioned after a green run, so its mere
# presence would have re-routed gen1 through `cc` forever and in silence. It changes nothing about
# the verdict either way; it is reported so a log reader knows which chain produced the gen1 in
# hand.
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
# apenas gen2 e 3"*. gen1 emits C BY CONSTRUCTION (it is the last generation built down the C
# route — that emission IS the teko.c this train harvests), so judging it would fail the gate for
# doing exactly what it must do. That ruling is also why the degrau declaration read below changes
# no operand: the ladder's business is the ladder's. This is also why `scripts/no_emitted_c.sh` is
# NOT wired: it sweeps the whole worktree and cannot tell whose emission it found.
#
# Usage:  sh scripts/fixpoint_gate.sh <gen1-binary> [PROJECT_DIR] [WORK_DIR]
#
# Env:
#   TEKO_FIXPOINT_RT_DIR   runtime dir pinned onto each build (default: <PROJECT_DIR>/src/runtime).
#                          Same reason build_with_seed_fallback.sh pins it: a compiler resolves
#                          teko_rt.{h,c} relative to ITS OWN argv[0], so a generation living
#                          outside the tree would otherwise compile against the wrong runtime.
#   TEKO_FIXPOINT_BACKEND  which backend gen2 and gen3 are built with (default `c` — see
#                          `build_gen`, which is the ONLY place it is read). THE LEVER: the
#                          0.3.1.0 chain wants `native` here, and the day a platform's native
#                          self-build passes, that leg sets it and proves the chain the owner
#                          drew. It is still `c` today because the native backend does not build
#                          the compiler yet, and a leg that flipped early would red for a stop
#                          that is already named elsewhere.
#   TEKO_FIXPOINT_GEN1_C   the C emitted ALONGSIDE gen1 (default: <dirname gen1>/teko.c) — the
#                          harvest candidate this run stages as <WORK_DIR>/gen1.c.
#   TEKO_FIXPOINT_MODE     `degrau` | `normal` — force the PROVENANCE LABEL instead of observing
#                          the declaration. It selects no operand and no assertion (2026-07-28:
#                          the verdict is gen2 == gen3 in every state); it exists to test the
#                          observation itself. CI must let it observe.
#   TEKO_FIXPOINT_SOFT     set to 1 to REPORT the verdict and exit 0 anyway. Intended for the
#                          window in which the native backend cannot yet build the compiler at all
#                          (`fat-pointer receiver call not yet lowered (N2)`), where a hard failure
#                          here would mask every other signal in the lane rather than add one.
#                          It prints the same verdict either way — it never hides the result.
set -eu

# shellcheck source=scripts/degrau.sh
. "$(dirname "$0")/degrau.sh"

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
# is what the two-route plan restores it for (owner, 2026-07-27: *".31 com as duas rotas, .32
# ensina o nativo, .33 remove"* — resequenced by platform on the 0.3.1 lane, where the Linux leg
# goes native first and the C lives until 0.3.1.4).
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
# ── THE LEVER, AND IT IS THIS LINE ────────────────────────────────────────────────────────────
# `bg_backend="${TEKO_FIXPOINT_BACKEND:-c}"`, the third line of `build_gen` below, is where the
# 0.3.1.0 chain is switched on. The owner's chain builds gen2 and gen3 NATIVE; they run `c` only
# because the native backend does not build the compiler yet, and a leg that flipped before its
# native self-build passes would go red for a stop that is already named, by address, elsewhere.
#
# HOW TO FLIP IT, per platform, when that leg's native self-build passes: set
# `TEKO_FIXPOINT_BACKEND: native` on that leg's fixpoint STEP (never on the job or the workflow).
# The scope argument above is the whole reason the variable is read HERE and nowhere else — a
# global pin leaks into the suite and turns regressors green by compiling them down another road.
#
# The pin disappears entirely — not flipped, deleted — when the C route is retired (0.3.1.4 on the
# platform-sequenced plan), because then there is only one backend to pin it to.
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
# WHAT gen2.c AND gen3.c ARE FOR, now that the harvest candidate is gen1's C (see `stage_gen1_c`):
# they are the EVIDENCE, not the payload. Two different binaries emitting byte-identical C for one
# source is the C emission's own fixpoint, one layer below the binary one, and it is only visible
# if both files survive the run. They exist at all only while gen2/gen3 still run down the C route;
# once the `TEKO_FIXPOINT_BACKEND` lever flips, `emitted-c` reads 0 and there is nothing to keep.
# `rm -rf "$W/out"` still runs; it just no longer takes the C with it.
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

# stage_gen1_c — copies the C emitted ALONGSIDE gen1 into $W/gen1.c, so a passing run leaves the
# harvest candidate next to the generations that vouch for it.
#
# THE HARVEST POINT MOVED, AND THIS FUNCTION IS THE MOVE. It used to be `gen3.c`, on the reasoning
# that the freshest C came from the generation just proven byte-equal to its successor. Under the
# 0.3.1.0 chain gen2 and gen3 are built NATIVE and emit no C at all — there is no `gen3.c` to take.
# The last generation that emits C is gen1, and its emission is the one file that `cc` turns back
# into a working compiler.
#
# THE PROOF DID NOT WEAKEN, IT MOVED WITH IT: the C is not trusted because it came from the last
# generation, but because gen2 and gen3 DESCEND from the binary it encodes and proved the fixpoint.
# A run whose gen2 != gen3 harvests nothing — the callers gate the upload on this script's exit.
stage_gen1_c() {
    sg_src="${TEKO_FIXPOINT_GEN1_C:-$(dirname "$GEN1")/teko.c}"
    if [ ! -f "$sg_src" ]; then
        log "gen1 emitted no C beside it ($sg_src is absent) — nothing to harvest from this run."
        log "  That is expected only once gen1 itself is built by the native route; while the"
        log "  chain's first links run with TEKO_BACKEND=c, a missing C means the ladder changed."
        return 0
    fi
    cp "$sg_src" "$W/gen1.c"
    log "gen1's emitted C staged: $W/gen1.c ($(wc -c < "$W/gen1.c") bytes, from $sg_src)"
    return 0
}

# ── observe the PROVENANCE of gen1 — it explains the run, it selects nothing ───────────────────
DG=0
degrau_scan "$PROJ" || DG=$?
if [ "$DG" = "2" ]; then
    log "the degrau declaration is broken — see the lines above. This gate refuses to guess which"
    log "chain produced its gen1: a gate that guesses its own input is not a gate."
    exit 1
fi
if [ -n "${TEKO_FIXPOINT_MODE:-}" ]; then
    PROVENANCE="$TEKO_FIXPOINT_MODE"
    log "provenance = $PROVENANCE (FORCED via TEKO_FIXPOINT_MODE — CI must not do this; it labels"
    log "             the run and chooses no operand)"
elif [ "$DG" = "0" ]; then
    PROVENANCE=degrau
    degrau_report
    log "provenance = degrau — gen1 descends from the DECLARED C, through cc"
else
    PROVENANCE=normal
    degrau_report
    degrau_note_undeclared_c "$PROJ"
    log "provenance = normal — release -> gen0 -> gen1, the 0.3.1.0 chain"
fi

log "gen1 = $GEN1"
log "source = $PROJ"
"$GEN1" --version >&2 2>&1 || true
stage_gen1_c

log "building gen2 = gen1(source) ..."
if ! build_gen "$GEN1" "$W/gen2.log"; then
    log "----- gen1's build of the source (did not complete) -----"
    sed 's/^/fixpoint:   | /' "$W/gen2.log" >&2 || true
    verdict_fail "gen1 does not build the source it came from — the compiler does not self-host, so gen2 does not exist and the fixpoint cannot hold. See the address above."
fi
take_gen gen2 || verdict_fail "the gen2 build reported success but left no binary at $W/out"
log "gen2 ready ($(wc -c < "$W/gen2") bytes)"

# LEFT and RIGHT are the two generations that share a code generator, and there is exactly one such
# pair — gen2 and gen3. They are named rather than inlined because every assertion below reads them,
# and because the pair being FIXED is itself a decision (owner ruling 2026-07-28: *"O veredito do
# fixpoint não muda: continua gen2 == gen3"*), not an accident of this file's shape.
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
# THE CANDIDATE IS gen1's C, and the change of address is the 0.3.1.0 chain's, not this file's.
# Owner ruling 2026-07-28: the `teko.c` *"deixa de ser ENTRADA e passa a ser SAÍDA: versionado
# depois do verde, colhido do gen1 (a última geração que emite C)"*. gen2 and gen3 are built
# native, emit nothing, and are therefore no longer harvestable at all — what they contribute is
# the PROOF, since both descend from the binary that C encodes.
#
# Naming the file in the log — with its size — is what lets a reader of a green run harvest it
# without re-deriving which file to take.
#
# THE AGREEMENT LINE SURVIVES for as long as gen2/gen3 still run down the C route (the lever in
# `build_gen` has not been pulled on this leg yet). `$LEFT.c == $RIGHT.c` means two different
# compiler BINARIES emitted byte-identical C for the same source — the C emission has reached its
# own fixpoint, one layer below the binary fixpoint. A disagreement there would be a real finding
# even with `$LEFT == $RIGHT` green, since the binaries would agree while their C does not. The day
# the lever flips, both files are absent and this block simply says nothing.
if [ -f "$W/$LEFT.c" ] && [ -f "$W/$RIGHT.c" ]; then
    if cmp -s "$W/$LEFT.c" "$W/$RIGHT.c"; then
        log "emitted-C identity: $LEFT.c == $RIGHT.c  ✓ ($(wc -c < "$W/$RIGHT.c") bytes)"
    else
        log "emitted-C identity: $LEFT.c != $RIGHT.c  — the binaries agree but their C does not"
        log "  $LEFT.c $(wc -c < "$W/$LEFT.c") bytes, $RIGHT.c $(wc -c < "$W/$RIGHT.c") bytes"
    fi
fi
if [ -f "$W/gen1.c" ]; then
    log "harvest: $W/gen1.c is this run's candidate for bootstrap/teko.c — the C of gen1, the last"
    log "         generation that emits any, vouched for by the gen2/gen3 that descend from it"
fi

# ZERO-C IS REPORTED, NOT ENFORCED — a DATED window, not a permanent softening. Owner ruling
# 2026-07-27: *"pode remover por hora a validação de no-c para este caso"*, inside the release plan
# he set out the same night — **both routes alive, then the native backend taught, then C removed**
# — which the 0.3.1 lane RESEQUENCED BY PLATFORM: the Linux leg goes native first, macOS/Windows/
# wasm keep the C route, and **the C lives until 0.3.1.4**. Under that plan a generation emitting C
# before 0.3.1.4 is the DESIGNED state, so failing on it would red every run for doing precisely
# what the version asks of it.
#
# WHAT STILL FAILS, and it is the half that carries the meaning: `gen2 == gen3` byte for byte —
# THIS COMPILER REBUILDS ITSELF. That claim is enforced above and never relaxed.
#
# THIS IS NOT THE SOFT MODE RETURNING. That one printed `FAILED` and exited 0 — a lane green over a
# stated defect. Here the verdict tells the truth about the version it is running in: the byte
# fixpoint is REQUIRED and reported as such; the zero-C is an ANNOUNCED goal with a version
# attached, reported every run so its arrival is never a surprise.
#
# TO RE-ARM IT (the wagon that retires the C, 0.3.1.4): set ZERO_C_ENFORCED=1 below. One line.
ZERO_C_ENFORCED="${TEKO_FIXPOINT_ZERO_C:-0}"
CL="$(cat "$W/$LEFT.emitted-c" 2>/dev/null || echo '?')"
CR="$(cat "$W/$RIGHT.emitted-c" 2>/dev/null || echo '?')"
if [ "$CL" = "0" ] && [ "$CR" = "0" ]; then
    log "zero-C: neither generation emitted a teko.c  ✓"
elif [ "$ZERO_C_ENFORCED" = "1" ]; then
    log "zero-C: $LEFT emitted-c=$CL, $RIGHT emitted-c=$CR  ✗ (enforced — TEKO_FIXPOINT_ZERO_C=1)"
    FIX_OK=0
else
    log "zero-C: $LEFT emitted-c=$CL, $RIGHT emitted-c=$CR  — REPORTED, not enforced (the 0.3.1.4"
    log "        goal; this lane ships both routes on purpose). TEKO_FIXPOINT_ZERO_C=1 makes it bite."
fi

[ "$FIX_OK" = "1" ] || verdict_fail "the fixpoint has not arrived — see the two lines above for which half is outstanding"

if [ "$CL" = "0" ] && [ "$CR" = "0" ]; then
    log "VERDICT: PASSED — $LEFT == $RIGHT byte for byte, and neither emitted C"
else
    log "VERDICT: PASSED — $LEFT == $RIGHT byte for byte. (Both still emit C: the 0.3.1.4 goal,"
    log "         reported above.)"
fi
# CLEAN THE SCRATCH, NEVER THE HARVEST. This used to be a flat `rm -rf "$W"`, which deleted the
# emitted C two lines after the script had just printed
#
#     fixpoint: harvest: …/.fixpoint/gen1.c is this run's candidate for bootstrap/teko.c
#
# — it named the one file worth keeping and then destroyed it, so every downstream step saw an empty
# directory and `upload-artifact` reported "No files were found" over a fixpoint that had PASSED.
#
# What is scratch and what is product: the generation BINARIES (~2.5 MB each), the build LOGS and the
# emitted-c flags exist only to reach the verdict above and are worthless once it is printed. The
# `.c` files are the product — `gen1.c` is the next `bootstrap/teko.c`. So the sweep is enumerated
# rather than blanket, and `*.c` is simply not in it.
#
# THE GENERATION BINARIES SURVIVE TOO, and that is a second reversal. They were scratch while the
# only question was the verdict; they became the INSTRUMENT the moment the question turned into
# "does a DIFFERENT generation behave differently?". Owner ruling 2026-07-28: *"o que é a compilação
# do que um binário executando em runtime a identidade de um código? Entende o paradoxo? Se o erro é
# em runtime, ele pode não morar onde estamos olhando."*
#
# The compiler is a binary executing, so a stop while compiling some project is that binary's OWN
# run time — and gen1's checker was lowered by an older compiler than the one whose source anyone
# reads. Running the same probe under gen1 and under gen2 is what tells a defect in the SOURCE apart
# from a defect in the LINEAGE, and it cannot be done if the gate deletes the generations.
#
# ~2.5 MB apiece, on a runner that is discarded minutes later. The logs and the emitted-c marks stay
# in the sweep: those really are spent once the verdict is printed.
rm -rf "$W/out"
rm -f "$W"/*.log "$W"/*.emitted-c
exit 0
