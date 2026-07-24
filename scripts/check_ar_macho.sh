#!/usr/bin/env bash
# scripts/check_ar_macho.sh — THEORY validation script (branch theory/kp16-ar-macho-coff,
# NEVER merged): host-tool well-formedness gate for the BSD-format `.a` static archive
# (`src/backend/objfile_ar_macho.tks::emit_static_archive_macho`) — the Mach-O sibling of
# `scripts/check_ar_elf.sh`. Asserts the REAL macOS toolchain accepts the archive: `ar t`
# lists the member, `nm -a` sees the `__.SYMDEF_64`-indexed symbols, and `ld -r` links the
# extracted member. The engine-agnostic byte layout is pinned by the golden tests in
# `src/backend/objfile_ar_macho_test.tkt`; this script is the external cross-check, run
# ONLY on the theory CI's macos-latest runner (real Apple `ar`/`nm`/`ld` — the ONE
# toolchain that can validate a BSD `__.SYMDEF_64` table).
#
# FAIL LOUD (M.3, owner ruling on run 30061110006): every check TRACES to stderr before it
# runs and prints the underlying tool's OWN stdout+stderr on failure — a silent "exit 1,
# no output" gate is never acceptable. `AR_CHECK_REQUIRE_TOOLS=1` turns an honest-skip
# (host/tool absent, or no archive given) into a HARD FAILURE instead of exit 0 — set this
# on a runner where the toolchain is guaranteed present (the theory CI's macos-latest job),
# mirroring `validate_wasm_own.sh`'s `REQUIRE_WASM_ENGINE` seam, so a provisioning gap can
# never masquerade as a silent pass.
#
# usage: scripts/check_ar_macho.sh <archive.a> [symbol_that_must_resolve]
#   AR_CHECK_REQUIRE_TOOLS=1   (default: unset) — an honest-skip becomes a hard failure.

set -u

trace() { echo "check_ar_macho: $*" >&2; }
fail() { echo "check_ar_macho: FAIL — $1" >&2; exit 1; }

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

if [[ "$(uname -s)" != "Darwin" ]]; then
    skip_or_fail "needs a macOS host (ar/nm/ld) on $(uname -s)"
fi

ARCHIVE="${1:-}"
if [[ -z "$ARCHIVE" || ! -f "$ARCHIVE" ]]; then
    skip_or_fail "no archive provided (arg1='${ARCHIVE:-<empty>}')"
fi

SYMBOL="${2:-}"
trace "checking archive=$ARCHIVE symbol=${SYMBOL:-<none>}"

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

trace "step: nm -a (symbol index)"
nm_out="$(nm -a "$ARCHIVE" 2>&1)"
nm_rc=$?
if [[ "$nm_rc" -ne 0 ]]; then
    trace "nm -a output:"; printf '%s\n' "$nm_out" | sed 's/^/      | /' >&2
    fail "nm -a could not read $ARCHIVE (rc=$nm_rc)"
fi

if [[ -n "$SYMBOL" ]]; then
    trace "step: symbol index lists '$SYMBOL'"
    if ! printf '%s\n' "$nm_out" | grep -q "$SYMBOL"; then
        trace "nm -a output:"; printf '%s\n' "$nm_out" | sed 's/^/      | /' >&2
        fail "the archive symbol index does not list '$SYMBOL'"
    fi
fi

trace "step: ar x (extract '$member') + ld -r (relocatable relink)"
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/check-ar-macho.XXXXXX")"
ar_x_out="$(cd "$tmp_dir" && ar x "$ARCHIVE" "$member" 2>&1)"
ar_x_rc=$?
extracted="$tmp_dir/$member"
if [[ "$ar_x_rc" -ne 0 || ! -f "$extracted" ]]; then
    trace "ar x output:"; printf '%s\n' "$ar_x_out" | sed 's/^/      | /' >&2
    fail "ar x could not extract member $member from $ARCHIVE (rc=$ar_x_rc)"
fi
ld_out="$(ld -r "$extracted" -o /dev/null 2>&1)"
ld_rc=$?
rm -rf "$tmp_dir"
if [[ "$ld_rc" -ne 0 ]]; then
    trace "ld -r output:"; printf '%s\n' "$ld_out" | sed 's/^/      | /' >&2
    fail "ld -r rejected the extracted member $member (rc=$ld_rc)"
fi

echo "check_ar_macho: OK — $ARCHIVE is a well-formed, ar/nm/ld-consumable BSD static archive"
exit 0
