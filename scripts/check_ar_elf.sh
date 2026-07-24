#!/usr/bin/env bash
# scripts/check_ar_elf.sh — host-tool well-formedness gate for an own-backend GNU-format
# `.a` static archive (KP16, `src/backend/objfile_ar.tks::emit_static_archive`, the ELF
# lane of the kill-C static-archive substrate, `docs/design/kill-c-pull-forward-0.3.0.30.md`
# §2(b)). The own-backend archive writer wraps ONE whole-program `ET_REL` ELF64 object
# into a single-member GNU ar archive with a GNU symbol-table member; this script asserts
# the HOST toolchain accepts it — `ar t` lists the member, `nm` (via `ar`'s `nm --print-armap`
# or a plain `nm` on the archive) sees the indexed symbols, and the system `ld` links a
# trivial referencing object against it. The engine-agnostic byte layout is pinned by the
# golden tests in `src/backend/objfile_ar_test.tkt`; this script is the external
# cross-check, the archive sibling of `scripts/check_elf.sh`.
#
# GATED on linux-x86_64 (needs the host `ar`/`nm`/`ld` binutils reachable).
#
# FAIL LOUD (M.3, the THEORY branch's owner ruling on run 30061110006, ported here too for
# consistency across every check_ar_*.sh — the KP16 port-back should carry this hardening):
# every check TRACES to stderr before it runs and prints the underlying tool's OWN
# stdout+stderr on failure — a silent "exit 1, no output" gate is never acceptable.
# `AR_CHECK_REQUIRE_TOOLS=1` turns an honest-skip (wrong host, or no archive given) into a
# HARD FAILURE instead of exit 0 — set this on a runner where the toolchain is guaranteed
# present, mirroring `validate_wasm_own.sh`'s `REQUIRE_WASM_ENGINE` seam.
#
# usage: scripts/check_ar_elf.sh <archive.a> [symbol_that_must_resolve]
#   AR_CHECK_REQUIRE_TOOLS=1   (default: unset) — an honest-skip becomes a hard failure.

set -u

trace() { echo "check_ar_elf: $*" >&2; }
fail() { echo "check_ar_elf: FAIL — $1" >&2; exit 1; }

require="${AR_CHECK_REQUIRE_TOOLS:-0}"

skip_or_fail() {
    local reason="$1"
    if [[ "$require" == "1" ]]; then
        fail "AR_CHECK_REQUIRE_TOOLS=1 but $reason — failing closed (this mode never honest-skips)"
    fi
    trace "skipped — $reason"
    exit 0
}

trace "starting on $(uname -s)-$(uname -m), AR_CHECK_REQUIRE_TOOLS=$require"

if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    skip_or_fail "needs a linux-x86_64 host (ar/nm/ld) on $(uname -s)-$(uname -m)"
fi

ARCHIVE="${1:-}"
if [[ -z "$ARCHIVE" || ! -f "$ARCHIVE" ]]; then
    skip_or_fail "no archive provided (arg1='${ARCHIVE:-<empty>}')"
fi

# Absolutize BEFORE any step below could run from a different CWD (run 30074280021's
# macos-arm64 fix, ported here too for consistency across every check_ar_*.sh — this
# script's own `ar x --output=DIR` step never `cd`s today, but a relative $ARCHIVE
# staying valid must not depend on that staying true).
ARCHIVE="$(cd "$(dirname "$ARCHIVE")" && pwd)/$(basename "$ARCHIVE")"

SYMBOL="${2:-}"
trace "checking archive=$ARCHIVE symbol=${SYMBOL:-<none>}"

# Compared byte-for-byte via `od` (not a plain `"$(head -c 8 ...)"` == string compare):
# command substitution strips trailing newlines, which would silently eat the magic's
# trailing `\n` and desync every subsequent byte check.
trace "step: global header magic"
magic_hex="$(head -c 8 "$ARCHIVE" | od -An -tx1 | tr -d ' \n')"
[[ "$magic_hex" == "213c617263683e0a" ]] || fail "$ARCHIVE has no '!<arch>\\n' global header (got 0x$magic_hex)"

trace "step: ar t (list members)"
ar_t_out="$(ar t "$ARCHIVE" 2>&1)"
ar_t_rc=$?
if [[ "$ar_t_rc" -ne 0 ]]; then
    trace "ar t output:"; printf '%s\n' "$ar_t_out" | sed 's/^/      | /' >&2
    fail "ar t rejected $ARCHIVE (rc=$ar_t_rc)"
fi
member="$(printf '%s\n' "$ar_t_out" | head -1)"
[[ -n "$member" ]] || fail "ar t listed no member in $ARCHIVE"
trace "ar t: first member = '$member'"

trace "step: nm --print-armap (symbol index)"
nm_out="$(nm --print-armap "$ARCHIVE" 2>&1)"
nm_rc=$?
if [[ "$nm_rc" -ne 0 ]]; then
    trace "nm --print-armap output:"; printf '%s\n' "$nm_out" | sed 's/^/      | /' >&2
    fail "nm --print-armap could not read $ARCHIVE (rc=$nm_rc)"
fi

if [[ -n "$SYMBOL" ]]; then
    trace "step: symbol index lists '$SYMBOL'"
    if ! printf '%s\n' "$nm_out" | grep -q "$SYMBOL"; then
        trace "nm --print-armap output:"; printf '%s\n' "$nm_out" | sed 's/^/      | /' >&2
        fail "the archive symbol index does not list '$SYMBOL'"
    fi
fi

trace "step: ar x (extract '$member') + ld -r (relocatable relink)"
tmp_rel="$(mktemp -u "${TMPDIR:-/tmp}/check-ar-elf.XXXXXX").o"
ar_x_out="$(ar x --output="$(dirname "$tmp_rel")" "$ARCHIVE" "$member" 2>&1)"
ar_x_rc=$?
extracted="$(dirname "$tmp_rel")/$member"
if [[ "$ar_x_rc" -ne 0 || ! -f "$extracted" ]]; then
    trace "ar x output:"; printf '%s\n' "$ar_x_out" | sed 's/^/      | /' >&2
    fail "ar x could not extract member $member from $ARCHIVE (rc=$ar_x_rc)"
fi
ld_out="$(ld -r "$extracted" -o /dev/null 2>&1)"
ld_rc=$?
rm -f "$extracted"
if [[ "$ld_rc" -ne 0 ]]; then
    trace "ld -r output:"; printf '%s\n' "$ld_out" | sed 's/^/      | /' >&2
    fail "ld -r rejected the extracted member $member (rc=$ld_rc)"
fi

echo "check_ar_elf: OK — $ARCHIVE is a well-formed, ar/nm/ld-consumable GNU static archive"
exit 0
