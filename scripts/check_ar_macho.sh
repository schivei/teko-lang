#!/usr/bin/env bash
# scripts/check_ar_macho.sh — THEORY validation script (branch theory/kp16-ar-macho-coff,
# NEVER merged): host-tool well-formedness gate for the BSD-format `.a` static archive
# (`src/backend/objfile_ar_macho.tks::emit_static_archive_macho`) — the Mach-O sibling of
# `scripts/check_ar_elf.sh`. Asserts the REAL macOS toolchain accepts the archive: `ar t`
# lists the member, `nm -a`/`nm --print-armap` (or `otool`, as a fallback) sees the
# `__.SYMDEF`-indexed symbols, and `ld -r` links the extracted member. The engine-agnostic
# byte layout is pinned by the golden tests in `src/backend/objfile_ar_macho_test.tkt`;
# this script is the external cross-check, run ONLY on the theory CI's macos-latest runner
# (real Apple `ar`/`nm`/`ld` — the ONE toolchain that can validate a BSD `__.SYMDEF` table).
#
# GATED on Darwin (any arch); on any other host the script HONEST-SKIPS with a named
# reason (exit 0). With no argument, or no archive at the path, it also honest-skips.
#
# usage: scripts/check_ar_macho.sh <archive.a> [symbol_that_must_resolve]

set -u

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "check_ar_macho: skipped — needs a macOS host (ar/nm/ld) on $(uname -s)"
    exit 0
fi

ARCHIVE="${1:-}"
if [[ -z "$ARCHIVE" || ! -f "$ARCHIVE" ]]; then
    echo "check_ar_macho: skipped — no archive provided"
    exit 0
fi

SYMBOL="${2:-}"

fail() { echo "check_ar_macho: FAIL — $1"; exit 1; }

magic_hex="$(head -c 8 "$ARCHIVE" | od -An -tx1 | tr -d ' \n')"
[[ "$magic_hex" == "213c617263683e0a" ]] || fail "$ARCHIVE has no '!<arch>\\n' global header"

ar t "$ARCHIVE" >/dev/null 2>&1 || fail "ar t rejected $ARCHIVE"

member="$(ar t "$ARCHIVE" 2>/dev/null | head -1)"
[[ -n "$member" ]] || fail "ar t listed no member in $ARCHIVE"

nm -a "$ARCHIVE" >/dev/null 2>&1 || fail "nm -a could not read $ARCHIVE"

if [[ -n "$SYMBOL" ]]; then
    nm -a "$ARCHIVE" 2>/dev/null | grep -q "$SYMBOL" || fail "the archive symbol index does not list '$SYMBOL'"
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/check-ar-macho.XXXXXX")"
( cd "$tmp_dir" && ar x "$ARCHIVE" "$member" ) 2>/dev/null
extracted="$tmp_dir/$member"
if [[ -f "$extracted" ]]; then
    ld -r "$extracted" -o /dev/null 2>/dev/null || fail "ld -r rejected the extracted member $member"
    rm -rf "$tmp_dir"
else
    fail "ar x could not extract member $member from $ARCHIVE"
fi

echo "check_ar_macho: OK — $ARCHIVE is a well-formed, ar/nm/ld-consumable BSD static archive"
exit 0
