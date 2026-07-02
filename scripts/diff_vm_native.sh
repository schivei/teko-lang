#!/usr/bin/env bash
# scripts/diff_vm_native.sh — differential VM==native harness (issue #54).
#
# Every confirmed VM/native divergence in the chore/reboot audit existed because nothing
# cross-runs the same program through both engines, and because examples/regressions/*
# were never fed to a real `cc`. This harness closes that structural blind spot:
#
#   For every project under examples/regressions/ (any directory containing a *.tkp),
#     1. run it on the VM        : teko run   <proj>
#     2. build it natively       : teko build <proj> -o <tmp>   (front-end -> C -> host cc)
#        and execute the binary
#   and assert IDENTICAL exit codes and identical program stdout.
#
# `teko run` prints compiler diagnostics on stdout ("teko: ..." + the file-mapping lines);
# those are stripped before comparing so only PROGRAM output is diffed.
#
# EXPECTED_FAIL: fixtures that document a KNOWN engine divergence. They must KEEP
# diverging — the day both engines agree (someone closed the gap, or native regressed
# into the VM's behavior) the fixture turns XPASS and the harness fails LOUDLY so the
# list gets pruned and the fixture starts gating normally.
#
# usage: scripts/diff_vm_native.sh [fixture-dir ...]
#   TEKO=<path-to-teko-binary>   (default: ./build/teko)
#   FIXTURE_ROOT=<dir>           (default: examples/regressions)

set -u

TEKO="${TEKO:-./build/teko}"
FIXTURE_ROOT="${FIXTURE_ROOT:-examples/regressions}"

# ── EXPECTED-FAIL list ─────────────────────────────────────────────────────────────────
# class_destruct_effects — the VM fires NO class destructors: W10b.CLASS increment 4's
#   `destruct` injection lives ONLY in native codegen (src/codegen/codegen.c,
#   cg_find_destruct_ns / the exit-edge emission); src/vm has zero destructor logic.
#   Known divergence (TEKO_MASTER_PLAN.md W10b.CLASS increment 4 / issue #54 audit).
#   Native exits 7 from inside Tracker::destruct, the VM falls off the end with 0. When
#   the VM gains destructors (or native stops firing them) this becomes XPASS and MUST
#   be removed here.
EXPECTED_FAIL=(
    class_destruct_effects
)

# ── NATIVE-ONLY list ───────────────────────────────────────────────────────────────────
# Fixtures the VM refuses BY DESIGN (not a divergence bug): `extern` FFI cannot run on the
# VM (C7.1a — "use `teko build` to compile it natively"). These are built through the host
# cc and EXECUTED, asserting only that the binary neither fails to build nor crashes
# (exit < 126 rules out panic/abort/signal deaths); no VM comparison is possible.
# time_types — calls the extern host-clock FFI (teko::time).
NATIVE_ONLY=(
    time_types
)

is_expected_fail() {
    local name="$1" x
    for x in "${EXPECTED_FAIL[@]}"; do [ "$x" = "$name" ] && return 0; done
    return 1
}

is_native_only() {
    local name="$1" x
    for x in "${NATIVE_ONLY[@]}"; do [ "$x" = "$name" ] && return 0; done
    return 1
}

# Strip the compiler's own stdout diagnostics from a `teko run` capture, leaving only
# program output: "teko: ..." status lines and the indented "<file>\t-> <ns>" mapping lines.
filter_compiler_diag() {
    sed -e '/^teko: /d' -e $'/^  [^\t]*\t-> /d' "$1"
}

# Run a command with a timeout when `timeout` exists (Linux CI); raw otherwise (macOS dev).
run_limited() {
    if command -v timeout >/dev/null 2>&1; then
        timeout 120 "$@"
    else
        "$@"
    fi
}

if [ ! -x "$TEKO" ]; then
    echo "diff_vm_native: teko binary not found/executable at '$TEKO' (set TEKO=...)" >&2
    exit 2
fi

# Fixture set: explicit args, else every directory under FIXTURE_ROOT with a *.tkp.
fixtures=()
if [ "$#" -gt 0 ]; then
    fixtures=("$@")
else
    while IFS= read -r tkp; do
        fixtures+=("$(dirname "$tkp")")
    done < <(find "$FIXTURE_ROOT" -mindepth 2 -maxdepth 2 -name '*.tkp' | sort)
fi
if [ "${#fixtures[@]}" -eq 0 ]; then
    echo "diff_vm_native: no fixtures found under '$FIXTURE_ROOT'" >&2
    exit 2
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/teko-diff.XXXXXX")"
trap 'rm -rf "$work"' EXIT

pass=0 fail=0 xfail=0 xpass=0
failed_names=() xpass_names=()

echo "diff_vm_native: teko=$TEKO — ${#fixtures[@]} fixture(s)"
echo

for proj in "${fixtures[@]}"; do
    name="$(basename "$proj")"
    out="$work/$name"
    mkdir -p "$out"

    # ── engine 1: the VM (skipped for native-only fixtures) ────────────────────────
    if ! is_native_only "$name"; then
        run_limited "$TEKO" run "$proj" >"$out/vm.stdout" 2>"$out/vm.stderr"
        vm_exit=$?
        filter_compiler_diag "$out/vm.stdout" >"$out/vm.prog"
    fi

    # ── engine 2: native (front-end -> C -> host cc -> execute) ────────────────────
    if ! run_limited "$TEKO" build "$proj" -o "$out/bin" >"$out/build.stdout" 2>"$out/build.stderr"; then
        detail="native build failed (teko build/cc):"
        if is_expected_fail "$name"; then
            xfail=$((xfail + 1))
            printf 'XFAIL %-28s %s\n' "$name" "$detail known divergence, still diverging"
        else
            fail=$((fail + 1)); failed_names+=("$name")
            printf 'FAIL  %-28s %s\n' "$name" "$detail"
            sed 's/^/      | /' "$out/build.stderr" | tail -20
            tail -5 "$out/build.stdout" | sed 's/^/      | /'
        fi
        continue
    fi
    run_limited "$out/bin/$name" >"$out/native.stdout" 2>"$out/native.stderr"
    native_exit=$?

    # ── native-only: no VM to compare against; assert the binary did not crash ─────
    if is_native_only "$name"; then
        if [ "$native_exit" -lt 126 ]; then
            pass=$((pass + 1))
            printf 'PASS  %-28s native-only (extern FFI, no VM run) exit=%s\n' "$name" "$native_exit"
        else
            fail=$((fail + 1)); failed_names+=("$name")
            printf 'FAIL  %-28s native-only binary crashed (exit=%s)\n' "$name" "$native_exit"
            [ -s "$out/native.stderr" ] && tail -5 "$out/native.stderr" | sed 's/^/      | /'
        fi
        continue
    fi

    # ── compare ─────────────────────────────────────────────────────────────────────
    verdict=ok detail=""
    if [ "$vm_exit" -ne "$native_exit" ]; then
        verdict=diverge detail="exit codes differ: vm=$vm_exit native=$native_exit"
    elif ! cmp -s "$out/vm.prog" "$out/native.stdout"; then
        verdict=diverge detail="stdout differs (vm vs native):"
    fi

    if [ "$verdict" = ok ]; then
        if is_expected_fail "$name"; then
            xpass=$((xpass + 1)); xpass_names+=("$name")
            printf 'XPASS %-28s engines AGREE (exit %s) but fixture is on EXPECTED_FAIL — gap closed or native regressed; prune the list\n' "$name" "$vm_exit"
        else
            pass=$((pass + 1))
            printf 'PASS  %-28s exit=%s\n' "$name" "$vm_exit"
        fi
    else
        if is_expected_fail "$name"; then
            xfail=$((xfail + 1))
            printf 'XFAIL %-28s known divergence, still diverging (%s)\n' "$name" "$detail"
        else
            fail=$((fail + 1)); failed_names+=("$name")
            printf 'FAIL  %-28s %s\n' "$name" "$detail"
            if [ "$vm_exit" -eq "$native_exit" ]; then
                diff -u "$out/vm.prog" "$out/native.stdout" | sed 's/^/      | /' | head -30
            fi
            [ -s "$out/vm.stderr" ] && tail -3 "$out/vm.stderr" | sed 's/^/      | vm: /'
        fi
    fi
done

echo
echo "diff_vm_native: $pass passed, $fail failed, $xfail expected-fail (still diverging), $xpass unexpectedly passing"
[ "$fail" -gt 0 ] && echo "  FAILED: ${failed_names[*]}"
[ "$xpass" -gt 0 ] && echo "  XPASS (prune EXPECTED_FAIL or investigate a native regression): ${xpass_names[*]}"

if [ "$fail" -gt 0 ] || [ "$xpass" -gt 0 ]; then exit 1; fi
exit 0
