#!/usr/bin/env sh
# scripts/native_linux_asset.sh — build ONE published Linux asset from the emitted teko.c with
# the TARGET'S OWN NATIVE TOOLCHAIN. No cross-compiler. Ever.
#
# THIS FILE REPLACES scripts/cross_compile_linux.sh, WHICH IS DELETED (owner order 2026-07-26:
# "parar de cross compilar em zig, usar compilação nativa na esteira que irá gerar os artefatos"
# and, the same day, "O zig deve morrer agora, imediatamente, sem espera"). Every caller that
# used to reach for `zig cc -target <triple>` now names a LABEL here and gets a binary produced
# by the compiler that actually ships on that platform.
#
# ── WHAT "NATIVE" MEANS FOR EACH OF THE SIX LABELS ────────────────────────────────────────────
# The axis is (ARCH x LIBC). A toolchain is NATIVE when it is the target platform's own gcc,
# running against the target platform's own libc headers and objects. Which CPU executes that gcc
# is a separate question, and for riscv64 the answer is "an emulated one" — that is emulation of
# the RUNNER, not cross-compilation of the ARTIFACT.
#
#   linux-x86_64-glibc   container quay.io/pypa/manylinux_2_28_x86_64   host CPU (x86_64)
#   linux-x86_64-musl    container alpine (x86_64)                      host CPU (x86_64)
#   linux-arm64-glibc    container quay.io/pypa/manylinux_2_28_aarch64  host CPU (arm64 runner)
#   linux-arm64-musl     container alpine (arm64)                       host CPU (arm64 runner)
#   linux-riscv64-glibc  container riscv64/debian:unstable              qemu/binfmt (emulated)
#   linux-riscv64-musl   container alpine --platform linux/riscv64      qemu/binfmt (emulated)
#
# ── WHY THE glibc ASSETS GO THROUGH manylinux_2_28 AND NOT THE RUNNER'S OWN cc ────────────────
# A dynamically linked glibc binary carries a FLOOR: it will not start on a system whose glibc is
# older than the one it was linked against. `zig cc -target x86_64-linux-gnu.2.28` pinned that
# floor at 2.28, and the release notes PROMISE it ("Linux ships glibc (dynamic; needs glibc >=
# 2.28)"). Building with ubuntu-24.04's own cc would silently raise the floor to 2.39 and break
# every user on an older distro while every CI lane stayed green — the runner has 2.39, so
# nothing in CI would ever notice. The manylinux_2_28 images are the standard answer to exactly
# this problem: a glibc-2.28 sysroot with a modern gcc, same architecture, no cross-compilation.
# The published promise is therefore kept BY CONSTRUCTION rather than by hope.
#
# THE riscv64 glibc FLOOR IS A KNOWN DELTA and is not hidden: there is no glibc-2.28-era riscv64
# base image (riscv64 support landed in glibc 2.27 and no LTS distro ships an image that old for
# it), so that asset links against the container's glibc. riscv64 hardware and distros are all
# recent, so the practical exposure is nil — but it IS a change from the zig pin and is stated
# here rather than discovered later.
#
# ── WHY CONTAINERS AND NOT THE RUNNER, EVEN WHERE THE ARCH MATCHES ────────────────────────────
# Uniformity is the point. One code path builds all six, so the PR artifact lane and the release
# mint their assets with byte-identical commands; a divergence between "what CI proved" and "what
# the release shipped" is precisely the class of break `release-cross-smoke` existed to catch,
# and it is closed here by removing the divergence instead of by adding a lane to watch it.
#
# ── REQUIREMENTS ──────────────────────────────────────────────────────────────────────────────
#   * `docker` on PATH (present on every GitHub-hosted Linux runner).
#   * for the riscv64 labels: binfmt registered for riscv64 — the caller does this
#     (`docker/setup-qemu-action`, or `docker run --privileged tonistiigi/binfmt --install`).
#     A missing registration fails here with a named error, never with a silent wrong-arch build.
#   * the CWD must be the repo root, and <teko_c>/<src_dir> must live inside it: the container
#     sees the CWD mounted at /w, so a path outside it cannot be reached and is refused up front.
#
# Usage: native_linux_asset.sh <label> <teko_c> <src_dir>
#   label     one of the six labels above
#   teko_c    path to the emitted teko.c (repo-relative, or absolute under the CWD)
#   src_dir   the repo's src/ (teko_rt.*, assert.*)
#
# The asset lands at ./gd-<label>/teko — the SAME layout cross_compile_linux.sh used, so every
# consumer (the staging step, package_release.sh) is unchanged by the toolchain swap.
#
# Image overrides (all optional — for pinning or for a mirror; never to reintroduce a cross
# toolchain): TEKO_IMG_GLIBC_X86_64, TEKO_IMG_GLIBC_ARM64, TEKO_IMG_MUSL, TEKO_IMG_RISCV_GLIBC,
# TEKO_IMG_RISCV_MUSL.
#
# POSIX sh only.
set -eu

LABEL="${1:?usage: native_linux_asset.sh <label> <teko_c> <src_dir>}"
TEKO_C="${2:?missing teko_c}"
SRC="${3:?missing src_dir}"

IMG_GLIBC_X86_64="${TEKO_IMG_GLIBC_X86_64:-quay.io/pypa/manylinux_2_28_x86_64}"
IMG_GLIBC_ARM64="${TEKO_IMG_GLIBC_ARM64:-quay.io/pypa/manylinux_2_28_aarch64}"
IMG_MUSL="${TEKO_IMG_MUSL:-alpine:3.21}"
IMG_RISCV_GLIBC="${TEKO_IMG_RISCV_GLIBC:-riscv64/debian:unstable}"
IMG_RISCV_MUSL="${TEKO_IMG_RISCV_MUSL:-alpine:3.21}"

log() { printf '%s\n' "native_linux_asset: $*" >&2; }

command -v docker >/dev/null 2>&1 || {
    log "docker is not on PATH — every native Linux asset is built inside the target's own"
    log "container image, so there is no path forward without it."
    exit 1
}

# rel PATH — echo PATH made relative to the CWD, refusing anything outside it. The container only
# has /w (= the CWD) mounted; a path it cannot see would otherwise fail deep inside gcc with a
# missing-file error that names the container's view, not the caller's mistake.
rel() {
    case "$1" in
        /*)
            case "$1" in
                "$PWD"/*) printf '%s' "${1#"$PWD"/}" ;;
                *) log "'$1' is outside the working directory '$PWD' and cannot be mounted"; exit 1 ;;
            esac ;;
        *) printf '%s' "$1" ;;
    esac
}

R_TEKO_C="$(rel "$TEKO_C")"
R_SRC="$(rel "$SRC")"
[ -f "$R_TEKO_C" ] || { log "no emitted C at '$R_TEKO_C'"; exit 1; }
[ -d "$R_SRC/runtime" ] || { log "'$R_SRC' does not look like the repo's src/ (no runtime/)"; exit 1; }

# The embedded `teko --version` reads -DTEKO_VERSION_STRING at build time (teko_rt.c stringizes
# it), exactly as teko's own run_cc does (src/build/project.tks). A hand-rolled cc line must pass
# the SAME define or `--version` reports the 0.0.0.0-dev fallback instead of the manifest version
# — and scripts/cli_flags_test.sh, which runs against the PUBLISHED asset, asserts the manifest
# string exactly. derive_version.sh yields the git TAG `v<version>[-<suffix>]`; the binary reports
# the tag MINUS the leading `v`. The value is a BARE token — embedded quotes broke Windows arg
# re-parsing (project.tks).
TEKO_VERSION_TAG="$(sh scripts/derive_version.sh 2>/dev/null || echo v0.0.0.0-dev)"
TEKO_VERSION_STRING="${TEKO_VERSION_TAG#v}"

# ── per-label toolchain selection ─────────────────────────────────────────────────────────────
# PLATFORM is empty when the image runs on the host CPU; `--platform linux/riscv64` is what routes
# an image through binfmt. SETUP is the in-container package install (empty where the image
# already ships a compiler). ARCH_KW is grepped in host-side `file` output to assert the result.
PLATFORM=""
STATIC=""
case "$LABEL" in
    linux-x86_64-glibc)
        IMAGE="$IMG_GLIBC_X86_64"; SETUP=""; ARCH_KW="x86-64" ;;
    linux-x86_64-musl)
        IMAGE="$IMG_MUSL"; SETUP="apk add --no-cache build-base >/dev/null"; STATIC="-static"; ARCH_KW="x86-64" ;;
    linux-arm64-glibc)
        IMAGE="$IMG_GLIBC_ARM64"; SETUP=""; ARCH_KW="aarch64" ;;
    linux-arm64-musl)
        IMAGE="$IMG_MUSL"; SETUP="apk add --no-cache build-base >/dev/null"; STATIC="-static"; ARCH_KW="aarch64" ;;
    linux-riscv64-glibc)
        IMAGE="$IMG_RISCV_GLIBC"; PLATFORM="linux/riscv64"
        SETUP="apt-get update -qq >/dev/null && apt-get install -y -qq --no-install-recommends gcc libc6-dev >/dev/null"
        ARCH_KW="RISC-V" ;;
    linux-riscv64-musl)
        IMAGE="$IMG_RISCV_MUSL"; PLATFORM="linux/riscv64"
        SETUP="apk add --no-cache build-base >/dev/null"; STATIC="-static"; ARCH_KW="RISC-V" ;;
    *)
        log "'$LABEL' is not one of the six Linux labels"
        log "  linux-{x86_64,arm64,riscv64}-{glibc,musl}"
        exit 1 ;;
esac

# The host must be able to RUN the image. For a host-CPU image that is trivially true; for the
# emulated pair it depends on binfmt, and a missing registration is the one environmental failure
# that would otherwise surface as an unreadable "exec format error" three layers down.
if [ -n "$PLATFORM" ]; then
    if ! docker run --rm --platform "$PLATFORM" "$IMAGE" uname -m >/dev/null 2>&1; then
        log "cannot execute '$IMAGE' as $PLATFORM on this host."
        log "riscv64 has no GitHub runner, so its NATIVE gcc runs on an EMULATED CPU: the caller"
        log "must register binfmt first (docker/setup-qemu-action, or"
        log "  docker run --privileged --rm tonistiigi/binfmt --install riscv64)."
        log "Reintroducing a cross-compiler is NOT the fallback — there is none."
        exit 1
    fi
fi

GD="gd-$LABEL"
rm -rf "$GD"; mkdir -p "$GD"

echo "=== $LABEL — native build in $IMAGE${PLATFORM:+ (platform $PLATFORM)} ==="

# ONE `sh -c` inside the container: the package install, a toolchain diagnostic that names the
# compiler and libc actually present (so a wrong or drifted image is legible at the TOP of the
# log instead of inferred from a compile error), then the compile.
#
# `-std=c2x` not `-std=c23`: gcc only learned the `c23` spelling in 14, and this set spans gcc 10
# (older stable images) through 14. `-w` because the emitted C is machine-generated and its
# warnings are not a signal any human acts on. `-O2` because every PUBLISHED asset is a release
# link (teko's own `--release` passes it), so an asset built without it would not be the asset.
#
# There is deliberately NO `-fno-sanitize=undefined` here. That flag existed ONLY because zig cc
# turns UBSan traps on by default and the checker's boundary INT64_MIN negation then aborted the
# process; stock gcc does not trap, so the workaround dies with the toolchain that needed it.
CC_LINE="gcc -std=c2x -w -O2 -DTEKO_VERSION_STRING=$TEKO_VERSION_STRING $STATIC \
    -I$R_SRC/runtime -I$R_SRC/assert \
    $R_TEKO_C $R_SRC/runtime/teko_rt.c $R_SRC/assert/assert.c -lm \
    -o $GD/teko"

docker run --rm ${PLATFORM:+--platform "$PLATFORM"} \
    -v "$PWD:/w" -w /w "$IMAGE" \
    sh -c "set -eu
${SETUP:-true}
echo '--- container toolchain ---'
uname -m
gcc --version | head -n1
(ldd --version 2>/dev/null | head -n1) || echo 'ldd: absent (musl image)'
echo '--- compiling ---'
$CC_LINE"

[ -f "$GD/teko" ] || { log "$LABEL: the container reported success but wrote no $GD/teko"; exit 1; }

# The architecture assertion survives the toolchain swap unchanged: it is what turns "the build
# ran" into "the build produced the thing it was asked for". A wrong-arch result here means the
# image or the platform routing is wrong, and saying so beats shipping it.
file "$GD/teko"
if ! file "$GD/teko" | grep -q "$ARCH_KW"; then
    log "$LABEL produced the WRONG architecture (expected '$ARCH_KW')"
    exit 1
fi

log "$LABEL OK — $GD/teko built natively in $IMAGE"
