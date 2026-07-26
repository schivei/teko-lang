#!/usr/bin/env sh
# scripts/produce_assets.sh — THE SINGLE PRODUCTION PATH for a published teko asset.
#
# ── WHY IT IS A SCRIPT AND NOT WORKFLOW STEPS ────────────────────────────────────────────────
# Two things now mint the nine published assets: `pr.yml`'s `artifact` root (per PR, so every
# asset is gated) and `nightly.yml` (per push to main, so the release has something to promote).
# If those two carried their own copies of "provision the seed, walk the ladder, dry-build, build
# the native Linux assets, stage them", they would drift — and a drift between what CI proved and
# what the release shipped is exactly the class of defect this train has been paying for. One
# script, two callers, no second path.
#
# WHAT A CALLER STILL OWNS, and why it cannot live here:
#   * `actions/checkout` — this script runs inside the tree.
#   * binfmt registration for riscv64 — a privileged `docker run` that GitHub exposes as an
#     action; scripts/native_linux_asset.sh PROBES the registration and fails with a named error
#     if the caller forgot it, so the split cannot become a silent wrong-arch build.
#   * what to do with the result — pr.yml uploads `stage/` as a run artifact; nightly.yml packages
#     it with scripts/package_release.sh. Producing and publishing are different jobs.
#
# ── WHAT IT DOES, IN ORDER ───────────────────────────────────────────────────────────────────
#   1. Provision the host-runnable seed (scripts/ci_provision_teko.sh) and put it on PATH. The
#      seed label is NOT derived here: a producer standing on x86_64 hardware may be minting
#      riscv64 assets, so the caller states which released binary RUNS on this runner. A
#      derivation the caller can get wrong is a derivation that silently picks the wrong blob.
#   2. THE DRY BUILD + THE LADDER (scripts/build_with_seed_fallback.sh): the newest RELEASED seed
#      builds the tip directly; else the COMMITTED seed in bootstrap/seeds/; else the pinned SHA
#      ladder. `--no-verify --release` — DRY, no test gate (the gate is the test layer's whole
#      job) and `-O2` because every published asset is a release link.
#      Leaves out/teko[.exe] AND out/teko.c — every asset this producer mints comes from that one
#      C, so all of them are the same generation of the compiler by construction.
#   3. LINUX ONLY: one native container build per label (scripts/native_linux_asset.sh).
#      Non-Linux producers are already standing on the target platform, so the dry build IS the
#      asset.
#   4. STAGE: stage/<label>/teko[.exe] — one directory per asset label. That is THE CONTRACT a
#      consumer sees; a producer can change HOW it mints an asset without the consumer noticing.
#
# Usage:  produce_assets.sh <kind> <seed-label> <asset-label>…
#   kind         `linux`  — the assets are built by native_linux_asset.sh from out/teko.c
#                `native` — the runner IS the target; the dry build's own binary is the asset
#   seed-label   the released asset label whose binary RUNS on this runner
#   asset-label… the labels this producer promised to mint
#
# Env:  GH_TOKEN — optional, passed through to ci_provision_teko.sh for API rate-limit relief.
#
# POSIX sh only.
set -eu

KIND="${1:?usage: produce_assets.sh <kind> <seed-label> <asset-label>...}"
SEED="${2:?missing seed-label}"
if [ "$#" -lt 3 ]; then
    echo "produce_assets: a producer must promise at least one asset label" >&2
    exit 1
fi
shift 2
PRODUCES="$*"

log() { printf '%s\n' "produce_assets: $*" >&2; }

case "$KIND" in
    linux|native) ;;
    *) log "kind must be 'linux' or 'native', not '$KIND'"; exit 1 ;;
esac

log "kind=$KIND seed=$SEED produces='$PRODUCES'"

# ── 1. the seed ───────────────────────────────────────────────────────────────────────────────
# ci_provision_teko.sh appends `.seed` to $GITHUB_PATH, which only affects LATER STEPS — this
# script is one step, so it puts the seed on its own PATH as well. That also makes the script
# runnable by hand outside Actions, which is the only way anyone debugs a producer.
sh scripts/ci_provision_teko.sh "$SEED"
if [ -d .seed ]; then
    PATH="$PWD/.seed:$PATH"
    export PATH
fi
command -v teko >/dev/null 2>&1 || {
    log "provisioning reported success but no 'teko' is on PATH"
    exit 1
}
teko --version

# ── 2. the dry build + the ladder ─────────────────────────────────────────────────────────────
sh scripts/build_with_seed_fallback.sh out

[ -f out/teko.c ] || { log "the dry build produced no out/teko.c"; exit 1; }

# ── 3. the Linux assets, each with its target's own native toolchain ──────────────────────────
if [ "$KIND" = "linux" ]; then
    for label in $PRODUCES; do
        sh scripts/native_linux_asset.sh "$label" out/teko.c src
    done
fi

# ── 4. stage ──────────────────────────────────────────────────────────────────────────────────
# The loop FAILS if a promised asset is missing. A producer that stages less than it promised
# would otherwise surface as a confusing download error one job away from its cause.
rm -rf stage
for label in $PRODUCES; do
    mkdir -p "stage/$label"
    if [ "$KIND" = "linux" ]; then
        src="gd-$label/teko"
    else
        src="out/teko"
        [ -f out/teko.exe ] && src="out/teko.exe"
    fi
    if [ ! -f "$src" ]; then
        log "promised '$label' but $src does not exist"
        exit 1
    fi
    case "$src" in
        *.exe) cp "$src" "stage/$label/teko.exe" ;;
        *)     cp "$src" "stage/$label/teko" ;;
    esac
    echo "staged $label <- $src"
done
ls -lR stage

log "OK — staged: $PRODUCES"
