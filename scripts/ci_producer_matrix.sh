#!/usr/bin/env sh
# scripts/ci_producer_matrix.sh — THE PRODUCER LEG TABLE, in one place.
#
# Prints the `strategy.matrix` JSON for the artifact-producing layer. Two callers, one table:
#   pr.yml     `plan` → the `artifact` root's matrix (light or full)
#   nightly.yml `plan` → always full; a nightly that minted fewer than nine assets would leave the
#                        release unable to promote a complete set
#
# WHY THE TABLE IS HERE AND NOT INLINE IN EACH WORKFLOW: scripts/produce_assets.sh already made
# the STEPS single-sourced; the leg table is the other half of the same definition. A producer
# added to one workflow and forgotten in the other is a nightly that publishes eight assets, or a
# PR that gates eight and ships nine — and both failures surface at the release, which is the
# worst possible place to discover them.
#
# ── THE FIELDS ────────────────────────────────────────────────────────────────────────────────
#   producer  names the leg and its uploaded artifact (`teko-assets-<producer>`)
#   os        the runner
#   timeout   job budget, minutes
#   kind      `linux` (assets built by scripts/native_linux_asset.sh from the emitted teko.c) or
#             `native` (the runner IS the target; the dry build's own binary is the asset)
#   seed      the released asset label whose binary RUNS on this runner — NOT always what the leg
#             produces: the two riscv64 legs stand on x86_64 hardware because no riscv64 runner
#             exists, and only the gcc they drive is riscv64
#   produces  the CONTRACT: the asset labels this leg must mint
#   binfmt    present when the leg needs qemu/binfmt registered before it can run its container
#   emit_c    present on the leg that additionally publishes out/teko.c — the sanitizer lanes
#             consume it because they need a DIFFERENTLY COMPILED compiler, the one thing a
#             downloaded binary cannot be
#
# ── WHY riscv64 IS TWO LEGS ───────────────────────────────────────────────────────────────────
# Each riscv64 leg's expensive half is a full gcc run under CPU emulation. Two legs run them in
# parallel; one leg would serialise them. The emulation cost is accepted by the owner and is
# deliberately not optimised — the split is about wall clock, not about paying less.
#
# LEG ORDER IS SLOWEST-FIRST. GitHub starts matrix legs in declaration order, so the emulated
# riscv64 producers and the ~11-minute windows-arm64 one must not queue behind cheap ones.
#
# Usage:  ci_producer_matrix.sh <light|full>
#
# POSIX sh only.
set -eu

MODE="${1:?usage: ci_producer_matrix.sh <light|full>}"

A_RG='{"producer":"linux-riscv64-glibc","os":"ubuntu-latest","timeout":300,"kind":"linux","seed":"linux-x86_64-glibc","binfmt":true,"produces":"linux-riscv64-glibc"}'
A_RM='{"producer":"linux-riscv64-musl","os":"ubuntu-latest","timeout":300,"kind":"linux","seed":"linux-x86_64-glibc","binfmt":true,"produces":"linux-riscv64-musl"}'
A_WA='{"producer":"windows-arm64","os":"windows-11-arm","timeout":150,"kind":"native","seed":"windows-arm64","produces":"windows-arm64"}'
A_LA='{"producer":"linux-arm64","os":"ubuntu-24.04-arm","timeout":90,"kind":"linux","seed":"linux-arm64-glibc","produces":"linux-arm64-glibc linux-arm64-musl"}'
A_WX='{"producer":"windows-x86_64","os":"windows-latest","timeout":90,"kind":"native","seed":"windows-x86_64","produces":"windows-x86_64"}'
A_MAC='{"producer":"macos-arm64","os":"macos-latest","timeout":60,"kind":"native","seed":"macos-arm64","produces":"macos-arm64"}'

case "$MODE" in
    full)
        A_LX='{"producer":"linux-x86_64","os":"ubuntu-latest","timeout":90,"kind":"linux","seed":"linux-x86_64-glibc","emit_c":true,"produces":"linux-x86_64-glibc linux-x86_64-musl"}'
        printf '{"include":[%s,%s,%s,%s,%s,%s,%s]}\n' "$A_RG" "$A_RM" "$A_WA" "$A_LX" "$A_LA" "$A_WX" "$A_MAC"
        ;;
    light)
        # The light tier mints ONE Linux asset instead of six — the label filter is on `produces`,
        # so the leg pays for one native container build rather than building six and discarding
        # five. windows-x86_64 rides the light tier for the empirical reason in pr.yml's header:
        # this train's wagon-unique failures happened on Windows and only on Windows.
        A_LX='{"producer":"linux-x86_64","os":"ubuntu-latest","timeout":90,"kind":"linux","seed":"linux-x86_64-glibc","emit_c":true,"produces":"linux-x86_64-glibc"}'
        printf '{"include":[%s,%s]}\n' "$A_LX" "$A_WX"
        ;;
    *)
        echo "ci_producer_matrix: mode must be 'light' or 'full', not '$MODE'" >&2
        exit 1
        ;;
esac
