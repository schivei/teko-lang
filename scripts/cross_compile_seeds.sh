#!/usr/bin/env sh
# scripts/cross_compile_seeds.sh — cross-compile the emitted teko.c into the FIVE HOST seeds
# with `zig cc`, from ONE x86_64 Linux runner. Sibling of cross_compile_linux.sh, different
# question: that one builds what we PUBLISH (targets), this one builds what CI must EXECUTE
# (hosts).
#
# A seed is needed per HOST THAT RUNS THE COMPILER, not per published target. The six Linux
# release artifacts (glibc+musl x x86_64/arm64/riscv64) are cross-compiled outputs — no teko
# binary ever runs on musl or riscv64 in CI. GitHub Actions offers exactly five hosts:
#
#   ubuntu-latest / ubuntu-24.04-arm / macos-latest / windows-latest / windows-11-arm
#
# WHY THIS EXISTS (transitional — .31 ONLY, removed in the first .32 wagon): the merge train
# squash-merges its wagons, so the intermediate seed SHAs never become reachable from main.
# The org therefore has no ladder rung able to compile the .31 tip. These committed seeds are
# that rung. The permanent replacement is the nightly-per-build + promotion system.
#
# The two `zig cc` deviations from native cc are IDENTICAL to cross_compile_linux.sh's and are
# required for the same reasons — see that file's header for the full history:
#   -O2                        the seed-speed win; both x86_64 UBs that once miscompiled are
#                              closed (#283 uninitialized temp, #577 noinline self-recursion).
#   -fno-sanitize=undefined    zig cc traps UB by default; the checker's INT64_MIN-negation
#                              range-check would abort on any i64 literal near the low bound.
# Added here and NOT there: `-s`. A seed is a transient bootstrap tool, never debugged in place,
# and stripping is what makes the committed blob ~3.6 MB instead of ~15 MB.
#
# The Windows seeds come out mingw-flavoured (zig/clang), not MSVC. That is correct for a SEED:
# its only job is to run and emit the portable teko.c, and its own ABI never reaches a published
# artifact — release.yml keeps building those natively.
#
# Usage: cross_compile_seeds.sh <teko_c> <src_dir> <out_dir>
#   teko_c   path to the emitted teko.c
#   src_dir  the repo's src/ (teko_rt.*, assert.*, win32_compat.h)
#   out_dir  where teko-<label>.xz and SEEDS.sha256 land
#
# Requires `zig` and `xz` on PATH (the caller installs the pinned zig).
# POSIX sh only.
set -eu

TEKO_C="${1:?usage: cross_compile_seeds.sh <teko_c> <src_dir> <out_dir>}"
SRC="${2:?missing src_dir}"
OUT="${3:?missing out_dir}"

mkdir -p "$OUT"

# Same define as teko's own run_cc (src/build/project.tks) — without it `--version` reports the
# 0.0.0.0-dev fallback. BARE token: embedded quotes broke Windows arg re-parsing.
TEKO_VERSION_TAG="$(sh scripts/derive_version.sh 2>/dev/null || echo v0.0.0.0-dev)"
TEKO_VERSION_STRING="${TEKO_VERSION_TAG#v}"

# build_seed LABEL TRIPLE STATIC EXE_SUFFIX ARCH_KEYWORD
#   STATIC=1 -> -static. Both Linux seeds are musl-static ON PURPOSE: a committed blob sits in
#   the tree for a whole train, and a glibc-dynamic seed would carry a >= 2.28 requirement with
#   it. Static musl runs on any Linux.
build_seed() {
    label="$1"; triple="$2"; static="$3"; sfx="$4"; arch_kw="$5"
    echo "=== $label ($triple, static=$static) ==="
    bin="$OUT/teko-$label$sfx"
    extra=""
    [ "$static" = "1" ] && extra="-static"
    zig cc -target "$triple" -std=c2x -w -O2 -fno-sanitize=undefined -s \
        "-DTEKO_VERSION_STRING=$TEKO_VERSION_STRING" $extra \
        -I"$SRC/runtime" -I"$SRC/assert" \
        "$TEKO_C" "$SRC/runtime/teko_rt.c" "$SRC/assert/assert.c" -lm \
        -o "$bin"
    file "$bin"
    if ! file "$bin" | grep -q "$arch_kw"; then
        echo "cross_compile_seeds: $label produced the WRONG architecture (expected '$arch_kw')" >&2
        exit 1
    fi
    # zig emits a .pdb next to the Windows binaries; it is debug data, never committed.
    rm -f "$OUT/teko-$label.pdb"
    xz -9 -c "$bin" > "$bin.xz"
    rm -f "$bin"
}

build_seed linux-x86_64   x86_64-linux-musl    1 ""     "x86-64"
build_seed linux-arm64    aarch64-linux-musl   1 ""     "aarch64"
build_seed macos-arm64    aarch64-macos        0 ""     "arm64"
build_seed windows-x86_64 x86_64-windows-gnu   0 ".exe" "x86-64"
build_seed windows-arm64  aarch64-windows-gnu  0 ".exe" "Aarch64"

# The checksum manifest is the REVIEWABLE artifact. Nobody reviews 3.6 MB of binary; the PR
# criterion is "the sha256 matches what the green run produced", and this small file is what a
# human actually reads.
( cd "$OUT" && sha256sum teko-*.xz > SEEDS.sha256 )
echo "=== $OUT/SEEDS.sha256 ==="
cat "$OUT/SEEDS.sha256"
echo "cross_compile_seeds: all five host seeds OK"
