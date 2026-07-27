#!/usr/bin/env bash
# scripts/cli_flags_test.sh — assert the `teko` CLI version/help flags (feat/cli-version-flag).
#
# The version SINGLE SOURCE OF TRUTH is teko.tkp (`version` + `suffix`). This harness proves the
# built binary embeds and prints exactly the RAW manifest string (no "4th field = published gen"
# release substitution) and that the flags short-circuit correctly:
#
#   teko --version | -v   → stdout "teko <version>", exit 0
#   teko --help    | -h   → usage banner incl. "teko <version>", exit 0
#   teko                  → usage (stderr), exit 2  (UNCHANGED no-args path)
#
# Usage:  scripts/cli_flags_test.sh <path/to/teko-binary> [path/to/teko.tkp]
#         (default manifest: the teko.tkp at the repo root, relative to this script)
set -eu

BIN="${1:?usage: cli_flags_test.sh <teko-binary> [teko.tkp]}"
if [ "$#" -ge 2 ] && [ -n "$2" ]; then
  MANIFEST="$2"
else
  SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
  MANIFEST="$SCRIPT_DIR/../teko.tkp"
fi

fail() { echo "cli_flags_test: FAIL: $*" >&2; exit 1; }

# --- derive the EXPECTED version string RAW from teko.tkp (version + -suffix) -----------------
read_toml_string() {
  awk -v k="$1" '
    /^[[:space:]]*#/ { next }
    {
      if (match($0, "^[[:space:]]*" k "[[:space:]]*=")) {
        rest = substr($0, RSTART + RLENGTH)
        if (match(rest, /"/)) {
          rest = substr(rest, RSTART + 1)
          if (match(rest, /"/)) { print substr(rest, 1, RSTART - 1) } else { print "" }
          exit
        }
      }
    }' "$MANIFEST"
}
VERSION="$(read_toml_string version)"
[ -n "$VERSION" ] || fail "no 'version' in $MANIFEST"
SUFFIX="$(read_toml_string suffix || true)"
if [ -n "$SUFFIX" ]; then EXPECTED="teko $VERSION-$SUFFIX"; else EXPECTED="teko $VERSION"; fi
echo "cli_flags_test: expecting version line: '$EXPECTED' (binary: $BIN)"

# --- helper: run the binary with args, capture stdout, stderr, exit code ---------------------
run_flag() {   # $1..: args → sets OUT, ERR, RC
  set +e
  OUT="$("$BIN" "$@" 2>/tmp/cli_flags_err)"; RC=$?
  ERR="$(cat /tmp/cli_flags_err)"
  set -e
}

# --- --version / -v : stdout is exactly EXPECTED, exit 0 -------------------------------------
for f in --version -v; do
  run_flag "$f"
  [ "$RC" -eq 0 ]        || fail "'$f' exit $RC (want 0)"
  [ "$OUT" = "$EXPECTED" ] || fail "'$f' stdout '$OUT' (want '$EXPECTED')"
done

# --- --help / -h : exit 0, banner contains the version line AND the usage: line --------------
for f in --help -h; do
  run_flag "$f"
  [ "$RC" -eq 0 ] || fail "'$f' exit $RC (want 0)"
  # the banner goes to stderr (leads with the version line — main.tks's usage banner).
  printf '%s\n' "$ERR" | grep -qF "$EXPECTED" || fail "'$f' banner missing version line '$EXPECTED'"
  printf '%s\n' "$ERR" | grep -q "usage: teko"   || fail "'$f' banner missing 'usage: teko'"
done

# --- bare `teko` (no args) : UNCHANGED → usage to stderr, exit 2 -----------------------------
run_flag
[ "$RC" -eq 2 ] || fail "bare teko exit $RC (want 2)"

# --- --backend : REMOVED, AND SO IS THE ENV VAR IT USED TO POINT AT ---------------------------
# Two rulings, one lane. The 2026-07-24 ruling removed the FLAG and had the rejection point the
# user at `TEKO_BACKEND` — the env var was still the way to pick a backend. The 2026-07-27 ruling
# removed the env var too ("TEKO_BACKEND é outro que não deve mais existir"), because there is
# only one backend now. This lane had been asserting the FIRST ruling's prose and went red the
# moment the second landed — correctly: it was pinning a message that names a thing that must no
# longer exist.
#
# So the assertion is rewritten to guard the RULING instead of echoing the prose:
#
#   1. exit 2 — rejected at CLI dispatch, never silently ignored nor mistaken for the project
#      positional (the original point of this block, unchanged);
#   2. the message says the flag is gone AND why (`only one backend`) — a user who typed
#      `--backend` gets told the concept died, not just that the spelling is wrong;
#   3. the message MUST NOT mention `TEKO_BACKEND`. This is the part that earns its keep: it
#      turns the lane into an active guard that the env var stays dead. Pointing a user at an
#      env var the compiler no longer reads is worse than saying nothing — it is an honest-looking
#      instruction that cannot work (M.3).
#
# The rejection fires BEFORE any project directory is resolved, so the positional need not be a
# real buildable project — the repo root itself (always present) is enough.
BACKEND_FIXTURE_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"

# assert_backend_rejected SHAPE — the three assertions above over the last run_flag's RC/ERR.
#
# The NEGATIVE assertion is written as an `if`, not as `grep -q … && fail`. Under this script's
# `set -eu` the `&&` form is a trap: the PASSING case is grep finding nothing, which makes the
# whole list return 1 and aborts the run at the exact moment the code is correct. An `if`
# condition is exempt from `set -e` by definition, so the intent survives the shell.
assert_backend_rejected() {
    shape="$1"
    [ "$RC" -eq 2 ] || fail "'$shape' exit $RC (want 2)"
    printf '%s\n' "$ERR" | grep -q "only one backend" \
        || fail "'$shape' stderr does not say the backend concept is gone: $ERR"
    if printf '%s\n' "$ERR" | grep -q "TEKO_BACKEND"; then
        fail "'$shape' stderr still points at TEKO_BACKEND, which was removed: $ERR"
    fi
}

run_flag build --backend=native "$BACKEND_FIXTURE_DIR"
assert_backend_rejected 'build --backend=native'

run_flag run --backend native "$BACKEND_FIXTURE_DIR"
assert_backend_rejected 'run --backend native'

run_flag "$BACKEND_FIXTURE_DIR" --backend=c
assert_backend_rejected '<bare-project> --backend=c'

echo "cli_flags_test: PASS ($BIN)"
