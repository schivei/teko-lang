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
# toolchain installed, so this script tries whichever is on PATH and HONEST-SKIPS (exit 0)
# only when neither is found. With no argument, or no archive at the path, it also
# honest-skips.
#
# usage: scripts/check_ar_coff.sh <archive.lib> [symbol_that_must_resolve]

set -u

ARCHIVE="${1:-}"
if [[ -z "$ARCHIVE" || ! -f "$ARCHIVE" ]]; then
    echo "check_ar_coff: skipped — no archive provided"
    exit 0
fi

SYMBOL="${2:-}"

LIB_TOOL=""
if command -v llvm-lib >/dev/null 2>&1; then
    LIB_TOOL="llvm-lib"
elif command -v lib.exe >/dev/null 2>&1; then
    LIB_TOOL="lib.exe"
elif command -v lib >/dev/null 2>&1; then
    LIB_TOOL="lib"
fi

if [[ -z "$LIB_TOOL" ]]; then
    echo "check_ar_coff: skipped — needs llvm-lib or lib.exe; neither found on $(uname -s)"
    exit 0
fi

fail() { echo "check_ar_coff: FAIL — $1"; exit 1; }

magic_hex="$(head -c 8 "$ARCHIVE" | od -An -tx1 | tr -d ' \n')"
[[ "$magic_hex" == "213c617263683e0a" ]] || fail "$ARCHIVE has no '!<arch>\\n' global header"

# lib.exe/llvm-lib use a single leading slash (`/list`); under a MSYS/Git-Bash shell that
# looks like a POSIX path and gets silently mangled — disable path conversion for this one
# invocation (the same MSYS_NO_PATHCONV/MSYS2_ARG_CONV_EXCL idiom check_coff.sh already
# uses for lld-link).
list_out="$(MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' "$LIB_TOOL" "/list" "$ARCHIVE" 2>&1)"
list_rc=$?
if [[ "$list_rc" -ne 0 ]]; then
    echo "check_ar_coff: $LIB_TOOL /list rejected $ARCHIVE (rc=$list_rc) — full output below:"
    echo "$list_out" | sed 's/^/      | /'
    fail "$LIB_TOOL /list rejected $ARCHIVE"
fi
echo "$list_out" | grep -qi '\.o' || fail "$LIB_TOOL /list did not enumerate an .o member in $ARCHIVE"

if command -v dumpbin >/dev/null 2>&1; then
    linkermember_out="$(MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' dumpbin "/linkermember" "$ARCHIVE" 2>&1)"
    linkermember_rc=$?
    [[ "$linkermember_rc" -eq 0 ]] || fail "dumpbin /LINKERMEMBER rejected $ARCHIVE"
    echo "$linkermember_out" | grep -qi 'linker member' || fail "dumpbin /LINKERMEMBER found no linker member in $ARCHIVE"
    if [[ -n "$SYMBOL" ]]; then
        echo "$linkermember_out" | grep -qi "$SYMBOL" || fail "dumpbin /LINKERMEMBER does not list '$SYMBOL'"
    fi
fi

echo "check_ar_coff: OK — $ARCHIVE is a well-formed, $LIB_TOOL-consumable COFF static archive"
exit 0
