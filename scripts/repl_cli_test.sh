#!/usr/bin/env bash
# scripts/repl_cli_test.sh — assert the `teko repl` CLI contract (DT3).
#
# `teko repl` is implemented pure-Teko (src/repl/repl.tks) and drives the OWN NATIVE backend
# exclusively (`force_native_backend` — never the retired VM, never the C emitter) by
# spawning `teko run` as a child process for every line — there is no in-process interpreter
# to script through a `.tkt` test, so this is a dedicated shell-level smoke test, run against
# a REAL built `teko` binary, mirroring `fmt_cli_test.sh`'s shape and the roadmap's own DT3
# verify clause ("native session smoke: scripted input → expected output").
#
# NATIVE-BACKEND DEPENDENCY (read before treating a failure here as this feature's bug). Every
# line below round-trips through the own native backend (a separate, parallel effort was
# repairing it at the time this REPL was written — see src/repl/repl.tks's module doc, "STATUS
# OF THAT ENGINE"). A failure in this script can mean the REPL's own logic is wrong, OR it can
# mean the native backend cannot yet build/run the tiny scratch program this script feeds it —
# this script cannot tell those apart by itself; check the scratch project's own build output
# (bin/teko run target/repl-session, then re-run with -v cc flags) before assuming the former.
#
#   teko repl                let/mut persist, a bare expression auto-displays its value
#   teko repl                an assignment persists but is NOT auto-displayed
#   teko repl                println(...) shows its own output, once, and is not replayed
#   teko repl                :quit ends the session, exit 0
#
# Usage:  scripts/repl_cli_test.sh <path/to/teko-binary>
set -eu

BIN="${1:?usage: repl_cli_test.sh <teko-binary>}"

fail() { echo "repl_cli_test: FAIL: $*" >&2; exit 1; }

work="$(mktemp -d "${TMPDIR:-/tmp}/teko-repl-cli.XXXXXX")"
trap 'rm -rf "$work"' EXIT
cd "$work"

SESSION=$'let x = 5\nx\nmut y = 10\ny = y + 1\ny\nprintln("hello")\n:quit\n'

set +e
out="$(printf '%s' "$SESSION" | "$BIN" repl 2>"$work/err")"; rc=$?
set -e

[ "$rc" -eq 0 ] || {
    echo "--- stdout ---" >&2
    printf '%s\n' "$out" >&2
    echo "--- stderr ---" >&2
    cat "$work/err" >&2
    fail "teko repl (scripted session) exit $rc (want 0)"
}

printf '%s\n' "$out" | grep -qx "5" || fail "auto-display of a bound variable ('x' -> 5) did not appear in the transcript"
printf '%s\n' "$out" | grep -qx "11" || fail "auto-display after a mutation ('y' -> 11, from y = y + 1) did not appear"
printf '%s\n' "$out" | grep -q "hello" || fail "println(\"hello\") did not appear in the transcript"

hello_count="$(printf '%s\n' "$out" | grep -c "hello")"
[ "$hello_count" -eq 1 ] || fail "println(\"hello\") appeared $hello_count times (want exactly 1 — a one-shot action must not replay)"

echo "repl_cli_test: PASS ($BIN)"
