#!/usr/bin/env sh
# scripts/known_stop_gate_test.sh — the REGRESSION for `scripts/known_stop_gate.sh`, the
# KNOWN-STOP envelope that must absorb ONLY the one diagnostic it pins.
#
# THE INVERSION PROOF THIS FILE EXISTS FOR (fixture 3 below): a log shaped EXACTLY like the one
# measured in run `30400712261` — one regression row failed, no unit test failed, the word
# `B1-args` present — but naming the SIBLING "parameter" stop (`select_param_x86`) instead of the
# pinned "call argument" one (`pin_args_x86`). The BUG this gate replaces read that shape as
# "KNOWN-STOP held" (a bare substring match on `B1-args`); this gate MUST answer RED. Without this
# fixture proving the inversion, a future edit could reintroduce the substring match and nothing
# here would notice.
#
# Every fixture is a throwaway log file in a temp dir — no compiler, no network, runs in the
# `CI gate` job alongside `scripts/no_c_in_tests_gate_test.sh`.
#
# Usage:  sh scripts/known_stop_gate_test.sh
# POSIX sh only.
set -eu

HERE="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
GATE="$HERE/known_stop_gate.sh"

FAILED=0
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT INT TERM

pass() { printf '  ok   %s\n' "$1"; }
fail() { printf 'ERROR: %s\n' "$1" >&2; FAILED=1; }

# gate_rc RC LOG_CONTENT — writes LOG_CONTENT to a fresh fixture file, runs the gate against it
# with the given RC, and echoes the gate's own exit code (never letting `set -e` end the whole
# test run on the first fixture that is supposed to fail).
gate_rc() {
    gr_rc_in="$1"
    printf '%s' "$2" > "$ROOT/log"
    gr_rc=0
    sh "$GATE" "$gr_rc_in" "$ROOT/log" >"$ROOT/out" 2>"$ROOT/err" || gr_rc=$?
    printf '%s' "$gr_rc"
}

PINNED_LINE="teko: regression FAIL examples/regressions/own_native (10/10) — own_arith_exit[0]: compile failed (exit 1); captured output tail:
isel x86-64: B1-args — an integer call argument past the ABI's argument-register window needs the stack-arg slot (0.3.1)"
SUMMARY_1_FAILED="teko: regressions 10 run, 0 skipped, 1 failed (9 ok, 1 failed)"

echo "--- known-stop gate ---"

# 1. the pinned diagnostic, alone, with the summary and RC != 0 — HELD (rc=0).
rc="$(gate_rc 1 "$PINNED_LINE
$SUMMARY_1_FAILED
")"
[ "$rc" = "0" ] && pass "the exact pinned diagnostic, one failure, no unit fail — held (rc=0)" \
    || fail "the pinned shape should be held: got rc=$rc, err: $(cat "$ROOT/err")"

# 2. RC == 0 (the suite passed) — LIFTED, never read as held.
rc="$(gate_rc 0 "teko: regressions 10 run, 0 skipped, 0 failed (10 ok)
")"
[ "$rc" = "1" ] && pass "a passing suite (RC=0) is LIFTED, not held (rc=1)" \
    || fail "RC=0 should never be held: got rc=$rc"
grep -qi 'LIFTED' "$ROOT/err" && pass "the LIFTED case says so on stderr" \
    || fail "the LIFTED case did not name itself: $(cat "$ROOT/err")"

# 3. THE INVERSION PROOF — the SIBLING "parameter" stop (select_param_x86), not the pinned one,
# with the SAME one-failure/no-unit-fail shape the bug's substring match would have accepted.
SIBLING_PARAM="teko: regression FAIL examples/regressions/own_native (10/10) — some_scenario[0]: compile failed (exit 1); captured output tail:
isel x86-64: B1-args — an integer parameter past the ABI's argument-register window needs the stack-arg slot (0.3.1)"
rc="$(gate_rc 1 "$SIBLING_PARAM
$SUMMARY_1_FAILED
")"
[ "$rc" = "1" ] && pass "INVERSION: the sibling PARAMETER stop, same shape otherwise, stays RED (rc=1)" \
    || fail "a sibling B1-args stop must never be swallowed: got rc=$rc, out: $(cat "$ROOT/err")"

# 3b. THE INVERSION PROOF, second sibling — the variadic-call stop.
SIBLING_VARIADIC="teko: regression FAIL examples/regressions/own_native (10/10) — some_scenario[0]: compile failed (exit 1); captured output tail:
isel x86-64: B1-args — a variadic call needs the register/stack split and the AL vector count; LCall carries no arity (0.3.1)"
rc="$(gate_rc 1 "$SIBLING_VARIADIC
$SUMMARY_1_FAILED
")"
[ "$rc" = "1" ] && pass "INVERSION: the sibling VARIADIC stop, same shape otherwise, stays RED (rc=1)" \
    || fail "the variadic sibling must never be swallowed either: got rc=$rc"

# 4. two regression rows failed — even the exact pinned diagnostic must not cover a SECOND break.
rc="$(gate_rc 1 "$PINNED_LINE
teko: regressions 10 run, 0 skipped, 2 failed (8 ok, 2 failed)
")"
[ "$rc" = "1" ] && pass "a second, unrelated failure alongside the pin stays RED (rc=1)" \
    || fail "more than one failed row must never be held: got rc=$rc"

# 5. a unit `#test` failed (no trailing 'ok') alongside the exact pinned diagnostic — still RED:
# this pin covers the regression tier only.
UNIT_FAIL_LINE="test panic_probe::probe_two_panics ... teko: deliberate panic: assertion failed: is_true"
rc="$(gate_rc 1 "test panic_probe::probe_one_ok ... ok
$UNIT_FAIL_LINE
$PINNED_LINE
$SUMMARY_1_FAILED
")"
[ "$rc" = "1" ] && pass "a failed unit test alongside the pin stays RED (rc=1)" \
    || fail "a unit-test failure must never be held: got rc=$rc"

# 6. no regression summary line at all (the unit gate crashed before the regression tier ran) —
# NOT the pinned stop, stays RED.
rc="$(gate_rc 1 "test panic_probe::probe_two_panics ... teko: deliberate panic: assertion failed: is_true
")"
[ "$rc" = "1" ] && pass "no regression summary at all (gate crashed first) stays RED (rc=1)" \
    || fail "an absent regression summary must never be held: got rc=$rc"

# 7. a log naming ONLY the family substring nowhere near a real failure line still fails once the
# summary shows zero rows failed and RC is still nonzero (an inconsistent/foreign failure shape).
rc="$(gate_rc 1 "some unrelated crash before any regression ran, mentions B1-args in passing
")"
[ "$rc" = "1" ] && pass "an unrelated mention of the family name with no summary stays RED (rc=1)" \
    || fail "a foreign mention must never be held: got rc=$rc"

# 8. FATAL inputs are never silently read as held or as a plain fail.
rc=0
sh "$GATE" 1 "$ROOT/does-not-exist" >"$ROOT/out8" 2>"$ROOT/err8" || rc=$?
[ "$rc" = "2" ] && pass "a missing log file is FATAL (rc=2)" \
    || fail "a missing log file should be FATAL (rc=2): got rc=$rc"

rc=0
printf 'x\n' > "$ROOT/log9"
sh "$GATE" "not-a-number" "$ROOT/log9" >"$ROOT/out9" 2>"$ROOT/err9" || rc=$?
[ "$rc" = "2" ] && pass "a non-numeric RC is FATAL (rc=2)" \
    || fail "a non-numeric RC should be FATAL (rc=2): got rc=$rc"

if [ "$FAILED" = "1" ]; then
    echo "known_stop_gate_test FAILED." >&2
    exit 1
fi
echo "known_stop_gate_test OK — the pinned diagnostic alone is held, both SIBLING B1-args stops"
echo "stay red (the inversion proof), a second failure or a failed unit test are never absorbed,"
echo "a lifted (RC=0) suite is never held, and every malformed input is FATAL rather than a guess."
