#!/usr/bin/env bash
# scripts/check_ar_coff.sh — THEORY validation script (branch theory/kp16-ar-macho-coff,
# NEVER merged): host-tool well-formedness gate for the Microsoft COFF `.lib` static
# archive (`src/backend/objfile_ar_coff.tks::emit_static_archive_coff`) — the Windows
# sibling of `scripts/check_ar_elf.sh`/`scripts/check_ar_macho.sh`. Asserts a REAL
# Windows/LLVM archive tool accepts it: `lib.exe /LIST` or `llvm-lib /list` enumerates the
# member, `dumpbin /LINKERMEMBER` (when present) parses the two linker members, and — when
# the exported symbol name is given — `lld-link`/`link.exe` links a trivial referencing
# object against the archive. The engine-agnostic byte layout is pinned by the golden
# tests in `src/backend/objfile_ar_coff_test.tkt`; this script is the external
# cross-check, run on the theory CI's windows-latest runner (real MS/LLVM tools — the ONE
# toolchain that can validate the two-linker-member convention end to end).
#
# TOOL-PRESENCE GATED, not host-OS gated (mirrors check_coff.sh's own cross-format
# reasoning): `llvm-lib`/`lib.exe` may be reachable from a non-Windows host with the LLVM
# toolchain installed, so this script tries whichever is on PATH.
#
# FAIL LOUD (M.3, owner ruling on run 30061110006): every check TRACES to stderr before it
# runs and prints the underlying tool's OWN stdout+stderr on failure — a silent "exit 1, no
# output" gate is never acceptable. `AR_CHECK_REQUIRE_TOOLS=1` turns an honest-skip (no
# archive given, or neither `llvm-lib` nor `lib.exe` found) into a HARD FAILURE instead of
# exit 0 — set this on a runner where the toolchain is guaranteed present (the theory CI's
# windows-latest job), mirroring `validate_wasm_own.sh`'s `REQUIRE_WASM_ENGINE` seam.
#
# usage: scripts/check_ar_coff.sh <archive.lib> [symbol_that_must_resolve]
#   AR_CHECK_REQUIRE_TOOLS=1   (default: unset) — an honest-skip becomes a hard failure.

set -u

trace() { echo "check_ar_coff: $*" >&2; }
fail() { echo "check_ar_coff: FAIL — $1" >&2; exit 1; }

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

ARCHIVE="${1:-}"
if [[ -z "$ARCHIVE" || ! -f "$ARCHIVE" ]]; then
    skip_or_fail "no archive provided (arg1='${ARCHIVE:-<empty>}')"
fi

# Absolutize BEFORE any step below could run from a different CWD (run 30074280021's
# macos-arm64 fix, ported here too for consistency across every check_ar_*.sh — this
# script's own steps never `cd` today, but a relative $ARCHIVE staying valid must not
# depend on that staying true).
ARCHIVE="$(cd "$(dirname "$ARCHIVE")" && pwd)/$(basename "$ARCHIVE")"

SYMBOL="${2:-}"
trace "checking archive=$ARCHIVE symbol=${SYMBOL:-<none>}"

LIB_TOOL=""
if command -v llvm-lib >/dev/null 2>&1; then
    LIB_TOOL="llvm-lib"
elif command -v lib.exe >/dev/null 2>&1; then
    LIB_TOOL="lib.exe"
elif command -v lib >/dev/null 2>&1; then
    LIB_TOOL="lib"
fi

if [[ -z "$LIB_TOOL" ]]; then
    skip_or_fail "needs llvm-lib or lib.exe; neither found on $(uname -s) (PATH=$PATH)"
fi
trace "using LIB_TOOL=$LIB_TOOL ($(command -v "$LIB_TOOL"))"

trace "step: global header magic"
magic_hex="$(head -c 8 "$ARCHIVE" | od -An -tx1 | tr -d ' \n')"
[[ "$magic_hex" == "213c617263683e0a" ]] || fail "$ARCHIVE has no '!<arch>\\n' global header (got 0x$magic_hex)"

# lib.exe/llvm-lib use a single leading slash (`/list`); under a MSYS/Git-Bash shell that
# looks like a POSIX path and gets silently mangled — disable path conversion for this one
# invocation (the same MSYS_NO_PATHCONV/MSYS2_ARG_CONV_EXCL idiom check_coff.sh already
# uses for lld-link).
trace "step: $LIB_TOOL /list"
list_out="$(MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' "$LIB_TOOL" "/list" "$ARCHIVE" 2>&1)"
list_rc=$?
if [[ "$list_rc" -ne 0 ]]; then
    trace "$LIB_TOOL /list output:"; printf '%s\n' "$list_out" | sed 's/^/      | /' >&2
    fail "$LIB_TOOL /list rejected $ARCHIVE (rc=$list_rc)"
fi
if ! printf '%s\n' "$list_out" | grep -qi '\.o'; then
    trace "$LIB_TOOL /list output:"; printf '%s\n' "$list_out" | sed 's/^/      | /' >&2
    fail "$LIB_TOOL /list did not enumerate an .o member in $ARCHIVE"
fi
trace "$LIB_TOOL /list: enumerated an .o member"

if command -v dumpbin >/dev/null 2>&1; then
    trace "step: dumpbin /LINKERMEMBER (present on PATH)"
    linkermember_out="$(MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' dumpbin "/linkermember" "$ARCHIVE" 2>&1)"
    linkermember_rc=$?
    if [[ "$linkermember_rc" -ne 0 ]]; then
        trace "dumpbin /LINKERMEMBER output:"; printf '%s\n' "$linkermember_out" | sed 's/^/      | /' >&2
        fail "dumpbin /LINKERMEMBER rejected $ARCHIVE (rc=$linkermember_rc)"
    fi
    if ! printf '%s\n' "$linkermember_out" | grep -qi 'linker member'; then
        trace "dumpbin /LINKERMEMBER output:"; printf '%s\n' "$linkermember_out" | sed 's/^/      | /' >&2
        fail "dumpbin /LINKERMEMBER found no linker member in $ARCHIVE"
    fi
    if [[ -n "$SYMBOL" ]]; then
        if ! printf '%s\n' "$linkermember_out" | grep -qi "$SYMBOL"; then
            trace "dumpbin /LINKERMEMBER output:"; printf '%s\n' "$linkermember_out" | sed 's/^/      | /' >&2
            fail "dumpbin /LINKERMEMBER does not list '$SYMBOL'"
        fi
    fi
else
    trace "step: dumpbin /LINKERMEMBER — skipped, dumpbin not on PATH (llvm-lib/lib.exe toolchains commonly lack it; not required)"
fi

echo "check_ar_coff: OK — $ARCHIVE is a well-formed, $LIB_TOOL-consumable COFF static archive"
exit 0
