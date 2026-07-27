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
# TRANSITIONAL SUBJECT (owner ruling — .31 self-hosting sequence): `teko.c`/`teko.h` exist ONLY
# for the seed→gen1 step; gen2 onward is produced by the NATIVE backend, no C at all. Everything
# this file does — pick a libc, run a C compiler over `teko.c` — is therefore scaffolding for
# gen1, not a permanent shape. The day the native backend emits gen1's own object directly, this
# file's job collapses from "which C compiler, in which container" to "which libc the LINKER
# targets" (owner, 2026-07-26: "não é .c que importa, importa o linker") — a flag, not a
# toolchain selection. Nothing here should be built with more permanence than that future implies.
#
# ── WHAT "NATIVE" MEANS FOR EACH OF THE FOUR LABELS ───────────────────────────────────────────
# The axis is (ARCH x LIBC), and it SPLITS by libc as of this wagon (owner, 2026-07-27: "Se tem
# esse musl-tools, separaria em lanes diferentes, assim podemos executar em paralelo e não usar
# container, tanto para arm quanto x64").
#
#   linux-x86_64-glibc   container quay.io/pypa/manylinux_2_28_x86_64   host CPU (x86_64)
#   linux-arm64-glibc    container quay.io/pypa/manylinux_2_28_aarch64  host CPU (arm64 runner)
#   linux-x86_64-musl    the RUNNER'S OWN musl-gcc (musl-tools), no container, host CPU (x86_64)
#   linux-arm64-musl     the RUNNER'S OWN musl-gcc (musl-tools), no container, host CPU (arm64)
#
# ── WHY THE glibc ASSETS STILL GO THROUGH manylinux_2_28 AND NOT THE RUNNER'S OWN cc ──────────
# A dynamically linked glibc binary carries a FLOOR: it will not start on a system whose glibc is
# older than the one it was linked against. `zig cc -target x86_64-linux-gnu.2.28` pinned that
# floor at 2.28, and the release notes PROMISE it ("Linux ships glibc (dynamic; needs glibc >=
# 2.28)"). Building with ubuntu-24.04's own cc would silently raise the floor to 2.39 and break
# every user on an older distro while every CI lane stayed green — the runner has 2.39, so
# nothing in CI would ever notice. The manylinux_2_28 images are the standard answer to exactly
# this problem: a glibc-2.28 sysroot with a modern gcc, same architecture, no cross-compilation.
# The published promise is therefore kept BY CONSTRUCTION rather than by hope. This is NOT
# negotiable by the "musl leaves the container" ruling above — that ruling names musl, and a
# dynamically linked asset's version floor is exactly the kind of promise a container exists to
# keep.
#
# ── WHY musl LEAVES THE CONTAINER ─────────────────────────────────────────────────────────────
# musl's whole published promise is "fully static" — there is no floor to protect, because the
# binary carries its own libc. `musl-tools` (musl-gcc) on the runner's OWN package repository
# (Ubuntu noble/universe, version 1.2.4-2 at the time this was written — see PROVENANCE.txt's
# `toolchain_libc` line for what actually built a given asset) is therefore just as "native" as
# the Alpine container was, at a fraction of the wall clock: no image pull, no `apk add`, and it
# runs CONCURRENTLY with the glibc container build in the SAME job instead of serially after it
# (see scripts/produce_assets.sh). `musl-gcc` and Alpine's gcc are both, ultimately, gcc linked
# against musl; the difference this file cares about is which ONE actually built the shipped
# bytes, and that is exactly what gets recorded, never assumed.
#
# musl-tools' AVAILABILITY ON THE arm64 RUNNER IS AN ASSERTION THIS FILE MAKES, NOT A FACT ANYONE
# HAD CONFIRMED WHEN IT WAS WRITTEN — it was verified only on x86_64. If it is absent there, this
# file FAILS THE JOB LOUD (ensure_musl_toolchain below) rather than silently falling back to the
# Alpine container: a silent fallback would hide exactly the fact that needs reporting.
#
# ── REQUIREMENTS ──────────────────────────────────────────────────────────────────────────────
#   * glibc labels: `docker` on PATH (present on every GitHub-hosted Linux runner), and the CWD
#     must be the repo root with <teko_c>/<src_dir> inside it — the container mounts the CWD at
#     /w, so a path outside it cannot be reached and is refused up front.
#   * musl labels: `musl-gcc` on PATH (installed by the caller's workflow step — see pr.yml /
#     nightly.yml's "Install musl-tools" step — or already present on a hand-run host).
#
# Usage: native_linux_asset.sh <label> <teko_c> <src_dir>
#   label     one of the four labels above
#   teko_c    path to the emitted teko.c (repo-relative, or absolute under the CWD)
#   src_dir   the repo's src/ (teko_rt.*, assert.*)
#
# The asset lands at ./gd-<label>/teko — the SAME layout cross_compile_linux.sh used, so every
# consumer (the staging step, package_release.sh) is unchanged by the toolchain swap.
#
# Image overrides (all optional — for pinning or for a mirror; never to reintroduce a cross
# toolchain): TEKO_IMG_GLIBC_X86_64, TEKO_IMG_GLIBC_ARM64.
#
# POSIX sh only.
set -eu

LABEL="${1:?usage: native_linux_asset.sh <label> <teko_c> <src_dir>}"
TEKO_C="${2:?missing teko_c}"
SRC="${3:?missing src_dir}"

IMG_GLIBC_X86_64="${TEKO_IMG_GLIBC_X86_64:-quay.io/pypa/manylinux_2_28_x86_64}"
IMG_GLIBC_ARM64="${TEKO_IMG_GLIBC_ARM64:-quay.io/pypa/manylinux_2_28_aarch64}"

log() { printf '%s\n' "native_linux_asset: $*" >&2; }

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

case "$LABEL" in
    linux-x86_64-glibc|linux-x86_64-musl) ARCH_KW="x86-64" ;;
    linux-arm64-glibc|linux-arm64-musl)   ARCH_KW="aarch64" ;;
    *)
        log "'$LABEL' is not one of the four Linux labels"
        log "  linux-{x86_64,arm64}-{glibc,musl}"
        exit 1 ;;
esac

GD="gd-$LABEL"
rm -rf "$GD"; mkdir -p "$GD"

# sha256_of FILE — same portable digest helper scripts/produce_assets.sh uses, duplicated here
# rather than sourced: this file has no dependency on that one and should not gain one just to
# share four lines.
sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        printf '%s' "no-digest-tool"
    fi
}

# heartbeat_start LABEL — begins a background "still working" pulse every 15s, and echoes its
# PID. THE DEFECT THIS CLOSES: `gcc`/`musl-gcc` run with `-w` and print NOTHING on success, so a
# 68-77s compile of the emitted teko.c produced zero log lines end to end — indistinguishable
# from a hung build to anyone who had not already memorised that silence is normal here. The
# watcher makes the silence itself a reported fact instead of an absence of facts.
heartbeat_start() {
    hb_label="$1"
    # `>/dev/null </dev/null` on the backgrounded subshell is NOT decoration: `heartbeat_start` is
    # always called as `x="$(heartbeat_start …)"`, and a background job that inherits the
    # command substitution's pipe on its OWN stdout keeps that pipe's write end open for as long
    # as the job runs — which is deliberately past the `$(...)` returning. Without this, the
    # substitution never sees EOF and the caller hangs forever waiting for a PID it already has.
    # stderr is left alone on purpose: it is not part of the pipe the command substitution reads.
    (
        hb_elapsed=0
        while :; do
            sleep 15
            hb_elapsed=$((hb_elapsed + 15))
            printf 'native_linux_asset: %s — still compiling (elapsed %ds; silence is expected, -w suppresses warnings)\n' "$hb_label" "$hb_elapsed" >&2
        done
    ) </dev/null >/dev/null &
    printf '%s' "$!"
}

# heartbeat_stop PID — ends the watcher started above, INCLUDING its current `sleep`. A non-
# interactive `sh script.sh` runs with job control off, so the subshell and every `sleep` it
# forks share this SCRIPT's OWN process group (measured: killing only the subshell PID leaves
# `sleep` running, reparented to init, for the REST of its 15s — still holding this script's
# stdout/stderr open the whole time, which is exactly the fd a log reader or `tail`-style
# consumer blocks on waiting for EOF). Killing the subshell's own children first, THEN the
# subshell itself, closes every fd the watcher held. Errors are swallowed throughout: the watcher
# may already be gone (killed by job cancellation, or reaped by a shell that got there first),
# and that is not a build failure.
heartbeat_stop() {
    for hs_child in $(pgrep -P "$1" 2>/dev/null || true); do
        kill "$hs_child" >/dev/null 2>&1 || true
    done
    kill "$1" >/dev/null 2>&1 || true
    wait "$1" 2>/dev/null || true
}

# ── glibc: containerized, exactly as before ───────────────────────────────────────────────────
build_glibc() {
    bg_image="$1"

    command -v docker >/dev/null 2>&1 || {
        log "docker is not on PATH — the glibc asset is built inside a manylinux_2_28 container"
        log "so there is no path forward without it (this ruling is unchanged for glibc)."
        exit 1
    }

    echo "=== $LABEL — native build in $bg_image ==="

    # THE PULL IS SEPARATE FROM THE RUN, AND THAT SEPARATION IS THE OBSERVABILITY FIX. `docker
    # run` on a cold runner auto-pulls and prints "Unable to find image '$bg_image' locally" —
    # ordinary cache-miss noise that reads exactly like an alarm to anyone who has not already
    # learned to ignore it. Naming the pull as its own timed, explicitly-normal step means the
    # log says what is happening BEFORE docker says anything that could be misread.
    log "pulling $bg_image (a cache-miss here is NORMAL on a fresh runner, not a failure)"
    bg_pull_start="$(date +%s)"
    if ! docker pull "$bg_image" >"$GD/docker-pull.log" 2>&1; then
        log "FATAL: could not pull $bg_image"
        sed 's/^/    docker pull: /' "$GD/docker-pull.log" >&2
        exit 1
    fi
    sed 's/^/    docker pull: /' "$GD/docker-pull.log" >&2
    log "$bg_image pulled in $(($(date +%s) - bg_pull_start))s"

    # ONE `sh -c` inside the container: a toolchain diagnostic that names the compiler and libc
    # actually present (so a wrong or drifted image is legible at the TOP of the log instead of
    # inferred from a compile error), then the compile.
    #
    # `-std=c2x` not `-std=c23`: gcc only learned the `c23` spelling in 14, and this set spans gcc
    # 10 (older stable images) through 14. `-w` because the emitted C is machine-generated and its
    # warnings are not a signal any human acts on. `-O2` because every PUBLISHED asset is a release
    # link (teko's own `--release` passes it), so an asset built without it would not be the asset.
    #
    # `-ldl` is REQUIRED, and it is the glibc floor that requires it. `teko_rt.c`'s
    # `tk_obs_dump_table` calls `dladdr`, which lives in `libdl` up to glibc 2.33 and was folded
    # INTO `libc` at 2.34. Building against the floor (manylinux_2_28 → glibc 2.28) therefore
    # needs the explicit link that the runner's own glibc 2.39 made invisible.
    bg_cc_line="gcc -std=c2x -w -O2 -DTEKO_VERSION_STRING=$TEKO_VERSION_STRING \
        -I$R_SRC/runtime -I$R_SRC/assert \
        $R_TEKO_C $R_SRC/runtime/teko_rt.c $R_SRC/assert/assert.c -lm -ldl \
        -o $GD/teko"

    bg_hb="$(heartbeat_start "$LABEL")"
    bg_build_start="$(date +%s)"
    # The exit code of `docker run` must survive the pipe that prefixes its output, and POSIX sh
    # has no `PIPESTATUS`/`pipefail` — so it is written to a file inside the `{ }` group that
    # produces the piped stdout, and read back once the pipe has drained.
    {
        docker run --rm -v "$PWD:/w" -w /w "$bg_image" sh -c "set -eu
echo '--- container toolchain ---'
uname -m
gcc --version | head -n1
(ldd --version 2>/dev/null | head -n1) || echo 'ldd: absent'
{ echo \"image=$bg_image\"
  echo \"arch=\$(uname -m)\"
  echo \"cc=\$(gcc --version | head -n1)\"
  echo \"libc=\$( (ldd --version 2>/dev/null | head -n1) || echo unknown )\"
} > $GD/TOOLCHAIN.txt
echo '--- compiling ---'
$bg_cc_line"
        echo "$?" > "$GD/.run-rc"
    } 2>&1 | sed "s/^/    [$LABEL container] /"
    heartbeat_stop "$bg_hb"
    bg_rc="$(cat "$GD/.run-rc" 2>/dev/null || echo 1)"
    rm -f "$GD/.run-rc"
    log "$LABEL compiled in $(($(date +%s) - bg_build_start))s"
    [ "$bg_rc" = "0" ] || { log "FATAL: $bg_image build failed (rc=$bg_rc) — see the prefixed log above"; exit 1; }

    [ -f "$GD/teko" ] || { log "$LABEL: the container reported success but wrote no $GD/teko"; exit 1; }
    [ -f "$GD/TOOLCHAIN.txt" ] || { log "$LABEL: the container wrote no TOOLCHAIN.txt"; exit 1; }
}

# ── musl: the runner's OWN musl-gcc, no container ─────────────────────────────────────────────

# ensure_musl_toolchain — refuses to proceed silently. `musl-gcc` is expected to already be on
# PATH (the caller's workflow installs `musl-tools` as its own step, exactly like the existing
# `clang`/`qemu-user-static` installs in pr.yml/nightly.yml) — this function's ONLY job is to say
# so loudly and stop if it is not, rather than reach for the container this file no longer owns
# a path back to.
ensure_musl_toolchain() {
    command -v musl-gcc >/dev/null 2>&1 && return 0
    log "FATAL: musl-gcc is not on PATH."
    log "musl-tools' availability on this runner was an ASSERTION (confirmed on x86_64 only) when"
    log "this lane was written — see this file's header. This is the FAIL-LOUD this restriction"
    log "demands: no silent fallback to a container exists here. If this is the arm64 runner,"
    log "report it; the fix is routing linux-arm64-musl back to the Alpine container path, by a"
    log "human decision, not by this script guessing."
    exit 1
}

# musl_libc_version — the musl runtime's OWN version string, read from its dynamic loader.
# musl's `ldd` has no `--version` flag; the loader prints "musl libc (ARCH)\nVersion X.Y.Z\n…" to
# STDERR when invoked with no arguments (and exits 1 doing it) — THE DEFECT THIS FIXES:
# `2>/dev/null` around that exact invocation is why PROVENANCE.txt's `toolchain_libc` field came
# out empty for the old Alpine path (measured, not assumed — see this file's git history). Merging
# stderr into the pipe this reads, rather than discarding it, is the whole fix.
musl_libc_version() {
    ml_loader="/lib/ld-musl-$(uname -m).so.1"
    [ -e "$ml_loader" ] || { printf '%s' "unknown"; return 0; }
    "$ml_loader" 2>&1 | sed -n 's/^Version //p' | head -n1
}

build_musl() {
    ensure_musl_toolchain

    echo "=== $LABEL — native build with musl-gcc (no container) ==="

    bm_static="-static"
    bm_cc_line="musl-gcc -std=c2x -w -O2 -DTEKO_VERSION_STRING=$TEKO_VERSION_STRING $bm_static \
        -I$R_SRC/runtime -I$R_SRC/assert \
        $R_TEKO_C $R_SRC/runtime/teko_rt.c $R_SRC/assert/assert.c -lm -ldl \
        -o $GD/teko"

    {
        echo "image=musl-tools (host, no container)"
        echo "arch=$(uname -m)"
        echo "cc=$(musl-gcc --version | head -n1)"
        echo "libc=musl $(musl_libc_version)"
    } > "$GD/TOOLCHAIN.txt"
    echo '--- host toolchain ---'
    sed 's/^/    /' "$GD/TOOLCHAIN.txt"

    bm_hb="$(heartbeat_start "$LABEL")"
    bm_build_start="$(date +%s)"
    set +e
    sh -c "$bm_cc_line"
    bm_rc=$?
    set -e
    heartbeat_stop "$bm_hb"
    log "$LABEL compiled in $(($(date +%s) - bm_build_start))s"
    [ "$bm_rc" = "0" ] || { log "FATAL: musl-gcc build failed (rc=$bm_rc)"; exit 1; }

    [ -f "$GD/teko" ] || { log "$LABEL: musl-gcc reported success but wrote no $GD/teko"; exit 1; }
}

case "$LABEL" in
    linux-x86_64-glibc) build_glibc "$IMG_GLIBC_X86_64" ;;
    linux-arm64-glibc)  build_glibc "$IMG_GLIBC_ARM64" ;;
    linux-x86_64-musl)  build_musl ;;
    linux-arm64-musl)   build_musl ;;
esac

# The architecture assertion survives the toolchain swap unchanged: it is what turns "the build
# ran" into "the build produced the thing it was asked for". A wrong-arch result here means the
# image/toolchain or the platform routing is wrong, and saying so beats shipping it.
file "$GD/teko"
if ! file "$GD/teko" | grep -q "$ARCH_KW"; then
    log "$LABEL produced the WRONG architecture (expected '$ARCH_KW')"
    exit 1
fi

log "$LABEL OK — $GD/teko built ($(sha256_of "$GD/teko"), $(wc -c < "$GD/teko" | tr -d ' ') bytes)"
