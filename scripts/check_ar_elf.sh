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
# GATED on linux-x86_64 (needs the host `ar`/`nm`/`ld` binutils reachable); on any other
# host the script HONEST-SKIPS with a named reason (exit 0). With no argument, or no
# archive at the path, it also honest-skips.
#
# usage: scripts/check_ar_elf.sh <archive.a> [symbol_that_must_resolve]

set -u

if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "check_ar_elf: skipped — needs a linux-x86_64 host (ar/nm/ld) on $(uname -s)-$(uname -m)"
    exit 0
fi

ARCHIVE="${1:-}"
if [[ -z "$ARCHIVE" || ! -f "$ARCHIVE" ]]; then
    echo "check_ar_elf: skipped — no archive provided"
    exit 0
fi

SYMBOL="${2:-}"

fail() { echo "check_ar_elf: FAIL — $1"; exit 1; }

# Compared byte-for-byte via `od` (not a plain `"$(head -c 8 ...)"` == string compare):
# command substitution strips trailing newlines, which would silently eat the magic's
# trailing `\n` and desync every subsequent byte check.
magic_hex="$(head -c 8 "$ARCHIVE" | od -An -tx1 | tr -d ' \n')"
[[ "$magic_hex" == "213c617263683e0a" ]] || fail "$ARCHIVE has no '!<arch>\\n' global header"

ar t "$ARCHIVE" >/dev/null 2>&1 || fail "ar t rejected $ARCHIVE"

member="$(ar t "$ARCHIVE" 2>/dev/null | head -1)"
[[ -n "$member" ]] || fail "ar t listed no member in $ARCHIVE"

nm --print-armap "$ARCHIVE" >/dev/null 2>&1 || fail "nm --print-armap could not read $ARCHIVE"

if [[ -n "$SYMBOL" ]]; then
    nm --print-armap "$ARCHIVE" 2>/dev/null | grep -q "$SYMBOL" || fail "the archive symbol index does not list '$SYMBOL'"
fi

tmp_rel="$(mktemp -u "${TMPDIR:-/tmp}/check-ar-elf.XXXXXX").o"
ar x --output="$(dirname "$tmp_rel")" "$ARCHIVE" "$member" 2>/dev/null
extracted="$(dirname "$tmp_rel")/$member"
if [[ -f "$extracted" ]]; then
    ld -r "$extracted" -o /dev/null 2>/dev/null || fail "ld -r rejected the extracted member $member"
    rm -f "$extracted"
else
    fail "ar x could not extract member $member from $ARCHIVE"
fi

echo "check_ar_elf: OK — $ARCHIVE is a well-formed, ar/nm/ld-consumable GNU static archive"
exit 0
