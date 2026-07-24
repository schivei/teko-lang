#!/usr/bin/env bash
# scripts/target_host_default_test.sh — proves the R1 host-target default (owner
# ruling 2026-07-24, teko-target-crosslink-0.3.1.md §0/§2.1): a build with
# TEKO_BACKEND=native and TEKO_TARGET UNSET must emit an object in THIS HOST's own
# format. The bug this replaces (teko-target-crosslink §1): `native_target()` used
# to fall back to a hardcoded `Arm64Macho` whenever `TEKO_TARGET` was absent,
# regardless of the real host — a linux x86_64 host silently emitted an arm64
# Mach-O object that `cc`/`ld` then rejected with an opaque cross-format error.
#
# scripts/diff_c_own.sh ALWAYS pins TEKO_TARGET explicitly for its differential
# lanes (so a host-arch mismatch can never mask a regression there); this script
# deliberately LEAVES TEKO_TARGET unset, so it is the one harness that actually
# exercises native_target()'s R1 default path end-to-end: build, well-formedness
# check via the host-appropriate scripts/check_{elf,macho,coff}.sh, and run.
#
# usage: scripts/target_host_default_test.sh
#   TEKO=<self-hosted-teko>   (default: ./bin/teko — MUST carry emit_native)
#   FIXTURE=<project-dir>     (default: examples/regressions/own_exit_code, exit 42)

set -u

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEKO="${TEKO:-$script_dir/bin/teko}"
FIXTURE="${FIXTURE:-$script_dir/examples/regressions/own_exit_code}"
name="$(basename "$FIXTURE")"

if [[ ! -x "$TEKO" ]]; then
    echo "target_host_default_test: no self-hosted teko at '$TEKO' (build it: teko . -o bin) — cannot run" >&2
    exit 2
fi

host_os="$(uname -s)"
host_arch="$(uname -m)"

work="$(mktemp -d "${TMPDIR:-/tmp}/teko-target-default.XXXXXX")"
trap 'rm -rf "$work"' EXIT

out="$work/out"
( unset TEKO_TARGET; env TEKO_BACKEND=native "$TEKO" build "$FIXTURE" -o "$out" --no-verify --release ) >"$work/build.log" 2>&1
build_rc=$?

if [[ "$build_rc" -ne 0 ]]; then
    echo "target_host_default_test: FAIL — build with TEKO_BACKEND=native and TEKO_TARGET unset failed on $host_os-$host_arch"
    tail -20 "$work/build.log" | sed 's/^/      | /'
    exit 1
fi

if ! grep -qF "(own backend)" "$work/build.log"; then
    echo "target_host_default_test: FAIL — TEKO_BACKEND=native did not report the own-backend marker (silent no-op — wrong/stale teko binary?)"
    exit 1
fi

objp="$out/$name.o"
if [[ ! -f "$objp" ]]; then
    echo "target_host_default_test: FAIL — build reported success but no $name.o was written"
    exit 1
fi

if [[ "$host_os" == "Linux" && "$host_arch" == "x86_64" ]]; then
    "$script_dir/scripts/check_elf.sh" "$objp" || exit 1
elif [[ "$host_os" == "Darwin" && "$host_arch" == "arm64" ]]; then
    "$script_dir/scripts/check_macho.sh" "$objp" || exit 1
elif [[ ( "$host_os" == MINGW* || "$host_os" == MSYS* || "$host_os" == CYGWIN* ) && "$host_arch" == "x86_64" ]]; then
    "$script_dir/scripts/check_coff.sh" "$objp" || exit 1
else
    echo "target_host_default_test: no well-formedness checker for $host_os-$host_arch — the build itself already proved the TEKO_TARGET-unset default succeeds"
fi

"$out/$name" >"$work/run.log" 2>&1
run_rc=$?
if [[ "$run_rc" -ne 42 ]]; then
    echo "target_host_default_test: FAIL — the host-default own-native binary exited $run_rc (want 42)"
    cat "$work/run.log" | sed 's/^/      | /'
    exit 1
fi

echo "target_host_default_test: PASS — TEKO_BACKEND=native with TEKO_TARGET unset emits the correct $host_os-$host_arch object format and runs (exit 42)"
exit 0
