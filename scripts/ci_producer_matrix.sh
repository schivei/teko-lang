#!/usr/bin/env sh
# scripts/ci_producer_matrix.sh — THE PRODUCER LEG TABLE, in one place.
#
# Prints the `strategy.matrix` JSON for the artifact-producing layer. Two callers, one table:
#   pr.yml     `plan` → the `artifact` root's matrix (light or full)
#   nightly.yml `plan` → always full; a nightly that minted fewer than seven assets would leave the
#                        release unable to promote a complete set
#
# WHY THE TABLE IS HERE AND NOT INLINE IN EACH WORKFLOW: scripts/produce_assets.sh already made
# the STEPS single-sourced; the leg table is the other half of the same definition. A producer
# added to one workflow and forgotten in the other is a nightly that publishes six assets, or a
# PR that gates six and ships seven — and both failures surface at the release, which is the
# worst possible place to discover them.
#
# ── THE FIELDS ────────────────────────────────────────────────────────────────────────────────
#   producer  names the leg and its uploaded artifact (`teko-assets-<producer>`)
#   os        the runner
#   timeout   job budget, minutes
#   kind      `linux` (assets built by scripts/native_linux_asset.sh from the emitted teko.c —
#             when `produces` names both a glibc and a musl label, produce_assets.sh builds them
#             IN PARALLEL on this one runner, not serially; see that script) or `native` (the
#             runner IS the target; the dry build's own binary is the asset)
#   seed      the released asset label whose binary RUNS on this runner — NOT always what the leg
#             produces, so it is a field rather than a derivation the caller can get wrong
#   produces  the CONTRACT: the asset labels this leg must mint
#   emit_c    present on the leg that additionally publishes out/teko.c — the sanitizer lanes
#             consume it because they need a DIFFERENTLY COMPILED compiler, the one thing a
#             downloaded binary cannot be
#
# LEG ORDER IS SLOWEST-FIRST. GitHub starts matrix legs in declaration order, so the slowest
# producer must not queue behind cheap ones.
#
# Usage:  ci_producer_matrix.sh <light|full>
#
# POSIX sh only.
set -eu

MODE="${1:?usage: ci_producer_matrix.sh <light|full>}"

A_LA='{"producer":"linux-arm64","os":"ubuntu-24.04-arm","timeout":90,"kind":"linux","seed":"linux-arm64-glibc","produces":"linux-arm64-glibc linux-arm64-musl"}'
A_WX='{"producer":"windows-x86_64","os":"windows-latest","timeout":90,"kind":"native","seed":"windows-x86_64","produces":"windows-x86_64"}'
A_MAC='{"producer":"macos-arm64","os":"macos-latest","timeout":60,"kind":"native","seed":"macos-arm64","produces":"macos-arm64"}'

case "$MODE" in
    full)
        A_LX='{"producer":"linux-x86_64","os":"ubuntu-latest","timeout":90,"kind":"linux","seed":"linux-x86_64-glibc","emit_c":true,"produces":"linux-x86_64-glibc linux-x86_64-musl"}'
        printf '{"include":[%s,%s,%s,%s]}\n' "$A_LX" "$A_LA" "$A_WX" "$A_MAC"
        ;;
    light)
        # The light tier mints ONE Linux asset instead of four — the label filter is on `produces`,
        # so the leg pays for one native container build rather than building four and discarding
        # three. windows-x86_64 rides the light tier for the empirical reason in pr.yml's header:
        # this train's wagon-unique failures happened on Windows and only on Windows.
        A_LX='{"producer":"linux-x86_64","os":"ubuntu-latest","timeout":90,"kind":"linux","seed":"linux-x86_64-glibc","emit_c":true,"produces":"linux-x86_64-glibc"}'
        printf '{"include":[%s,%s]}\n' "$A_LX" "$A_WX"
        ;;
    *)
        echo "ci_producer_matrix: mode must be 'light' or 'full', not '$MODE'" >&2
        exit 1
        ;;
esac
