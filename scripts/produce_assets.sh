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

# sha256_of FILE — portable digest (sha256sum on Linux, shasum -a 256 on macOS), the same shape
# scripts/ci_provision_teko.sh uses. Prints `absent` rather than an empty string so a missing file
# can never be mistaken for a matching digest downstream.
sha256_of() {
    [ -f "$1" ] || { printf '%s' "absent"; return 0; }
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        printf '%s' "no-digest-tool"
    fi
}

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

# THE SEED VERSION IS RECORDED, and that is not decoration. gen1 is `seed(tree)`: the emitted
# teko.c is produced BY the seed, so two runs of the SAME tree emit the same C only if they stood
# on the SAME seed. ci_provision_teko.sh deliberately takes the NEWEST released seed ("SEED = the
# NEWEST usable released seed, ALWAYS"), which is a function of WHEN the lane ran, not of the tree.
# On a train that cuts a release per bump, a seed change between a PR's run and the merge push is
# ordinary — and it is the single most likely honest explanation for two builds of one tree
# disagreeing. nightly.yml's reproducibility gate reads this to tell that apart from a real defect.
SEED_VERSION="$(teko --version 2>/dev/null || echo 'unknown')"

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

# ── 5. PROVENANCE, ONE FILE PER ASSET ─────────────────────────────────────────────────────────
# The three ANTECEDENTS of the reproducibility law, recorded so the law can be CHECKED instead of
# assumed:
#
#     same tree  +  same seed  +  same toolchain   ⇒   same bytes
#
# The tree is the checkout. The seed is recorded above. The toolchain is per-label: for `linux`
# producers scripts/native_linux_asset.sh captured the container's own image and gcc; for `native`
# producers the runner's own cc IS the toolchain — and that one is pinned by nothing, it drifts
# with the runner image.
#
# `teko_c_sha256` is the discriminator that makes a failure DIAGNOSABLE rather than merely red.
# teko.c is what the SEED emitted; each asset is that C compiled. So when two runs disagree:
#     teko.c differs                → the FRONT half moved (a different seed, or a different rung)
#     teko.c matches, bytes differ  → the BACK half moved (the C toolchain)
# One comparison and the cause is already bisected.
#
# ONE FILE PER LABEL, not one per producer, and that is load-bearing: nightly.yml's gate merges
# every producer's upload into one tree, and a `stage/PROVENANCE.txt` at the root would collide
# across producers and silently keep only the last. Labels are unique, so `stage/<label>/` is not
# just tidier — it is the only layout that survives the merge.
for label in $PRODUCES; do
    bin="stage/$label/teko"
    [ -f "$bin" ] || bin="stage/$label/teko.exe"
    {
        echo "label=$label"
        echo "producer_kind=$KIND"
        echo "seed_label=$SEED"
        echo "seed_version=$SEED_VERSION"
        echo "manifest_tag=$(sh scripts/derive_version.sh 2>/dev/null || echo unknown)"
        echo "host_uname=$(uname -srm 2>/dev/null || echo unknown)"
        echo "teko_c_sha256=$(sha256_of out/teko.c)"
        echo "asset_sha256=$(sha256_of "$bin")"
        if [ -f "gd-$label/TOOLCHAIN.txt" ]; then
            sed 's/^/toolchain_/' "gd-$label/TOOLCHAIN.txt"
        else
            echo "toolchain_image=runner"
            echo "toolchain_cc=$( (cc --version 2>/dev/null || clang --version 2>/dev/null || gcc --version 2>/dev/null) | head -n1 || echo unknown)"
        fi
    } > "stage/$label/PROVENANCE.txt"
    echo "--- stage/$label/PROVENANCE.txt ---"
    cat "stage/$label/PROVENANCE.txt"
done

ls -lR stage

log "OK — staged: $PRODUCES"
