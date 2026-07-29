#!/usr/bin/env bash
# scripts/check_elf.sh — host-tool well-formedness gate for an own-backend ELF64
# object (issue #386 B1-7/B1-8). The own-backend ELF writer
# (`src/backend/objfile_elf.tks::emit_elf` → the shared `emit_elf_object`) produces
# a relocatable `ET_REL` object; this script asserts the HOST toolchain accepts it
# — `readelf` parses the header/sections/symbols/relocations, `objdump -d`
# disassembles `.text`, and `ld -r` (a relocatable relink, NOT an executable link —
# that is the diff_c_own.sh executing lane) consumes it. The engine-agnostic byte
# layout is pinned by the golden tests in `src/backend/objfile_elf_test.tkt`; this
# script is the external cross-check, the ELF sibling of `scripts/check_macho.sh`.
#
# TWO DEFECTS CLOSED 2026-07-29 (owner: "check_elf.sh é defeito de CI, precisa corrigir"):
#
#   1. IT REFUSED AARCH64. The script gated on `uname -m == x86_64` and then asserted the
#      literal string `X86-64` in the ELF header, so on an aarch64 Linux host it printed
#      "skipped" and returned 0 — WHICH THE CI READS AS PASS. Both binutils and the ELF
#      format are perfectly fine on aarch64; only this script refused. The expected
#      `e_machine` is now DERIVED FROM THE HOST (`elf_machine_pattern`), so the gate
#      validates x86-64 and AArch64 objects alike and still catches a CROSS-format object
#      emitted by mistake — the exact bug (a linux host silently emitting arm64 bytes) that
#      target_host_default_test.sh exists to catch.
#
#   2. AN ABSENT OBJECT WAS A PASS. `no object provided`/`file missing` returned 0, so a
#      producer that failed to write the .o scored a green gate. Under the trunk bar — "só
#      vai ao tronco se passar pelo CI, sem erros, alertas ou mesmo erros escondidos (que
#      não disparam)" — a gate that passes when its input is absent is precisely a hidden
#      error. Every skip is now a HARD FAILURE by default.
#
# SKIPPING IS NOT THIS SCRIPT'S JOB — SCHEDULING IS THE CALLER'S. `não agendada ≠ saltada`:
# a row that never runs on a host belongs to another leg, and the caller
# (target_host_default_test.sh) already routes each host to its own format's checker. So
# reaching a wrong-host or missing-tool branch here means the CALLER routed wrong, and that
# is a defect to shout about, not to absorb. `OBJ_CHECK_ALLOW_SKIP=1` restores the old
# lenient behaviour for a local sandbox that genuinely lacks binutils; CI never sets it.
#
# usage: scripts/check_elf.sh <object.o>
#   OBJ_CHECK_ALLOW_SKIP=1   (default: unset) — downgrade a hard failure to an honest skip.

set -u

allow_skip="${OBJ_CHECK_ALLOW_SKIP:-0}"

fail() { echo "check_elf: FAIL — $1" >&2; exit 1; }

# Absent tools/hosts/objects are failures unless the caller explicitly opted out.
skip_or_fail() {
    if [[ "$allow_skip" == "1" ]]; then
        echo "check_elf: skipped (OBJ_CHECK_ALLOW_SKIP=1) — $1"
        exit 0
    fi
    fail "$1 — a gate that passes with nothing to check is a hidden error; set OBJ_CHECK_ALLOW_SKIP=1 only in a local sandbox"
}

# The `readelf -h` machine string this host's own objects must carry. Keeping the
# expectation host-derived is what lets the gate accept AArch64 while still rejecting an
# object emitted for the wrong architecture.
elf_machine_pattern() {
    case "$(uname -m)" in
        x86_64|amd64) echo "X86-64" ;;
        aarch64|arm64) echo "AArch64" ;;
        *) echo "" ;;
    esac
}

if [[ "$(uname -s)" != "Linux" ]]; then
    skip_or_fail "called on $(uname -s)-$(uname -m), which does not produce ELF objects (the caller routed the wrong checker)"
fi

MACHINE_PATTERN="$(elf_machine_pattern)"
if [[ -z "$MACHINE_PATTERN" ]]; then
    skip_or_fail "no expected ELF e_machine mapped for $(uname -s)-$(uname -m) — teach elf_machine_pattern this architecture"
fi

OBJ="${1:-}"
if [[ -z "$OBJ" ]]; then
    skip_or_fail "no object argument given (arg1 empty)"
fi
if [[ ! -f "$OBJ" ]]; then
    skip_or_fail "the object '$OBJ' does not exist — its producer did not write it"
fi

READELF="readelf"
OBJDUMP="objdump"
NM="nm"
LD="ld"

for tool in "$READELF" "$OBJDUMP" "$NM" "$LD"; do
    command -v "$tool" >/dev/null 2>&1 || skip_or_fail "the host binutils '$tool' is absent on $(uname -s)-$(uname -m)"
done

hdr="$("$READELF" -h "$OBJ" 2>/dev/null)" || fail "$READELF could not parse $OBJ"
echo "$hdr" | grep -q "ELF64"             || fail "not an ELF64 object"
echo "$hdr" | grep -qi "REL"              || fail "not an ET_REL (relocatable) object"
echo "$hdr" | grep -qi "$MACHINE_PATTERN"  || fail "not an $MACHINE_PATTERN object (this host is $(uname -m); a cross-format object reached the host checker)"

"$READELF" -S "$OBJ" 2>/dev/null | grep -q "\.text"      || fail "missing .text section"
"$READELF" -S "$OBJ" 2>/dev/null | grep -q "\.symtab"    || fail "missing .symtab section"

"$NM" "$OBJ" 2>/dev/null | grep -qE "^[0-9a-f]+ [Tt] " || fail "no defined text symbol in $OBJ"

"$OBJDUMP" -d "$OBJ" 2>/dev/null | grep -q "<.*>:" || fail "objdump -d found no disassembled function in $OBJ"

"$LD" -r "$OBJ" -o /dev/null 2>/dev/null || fail "ld -r rejected $OBJ (not linker-consumable)"

echo "check_elf: OK — $OBJ is a well-formed, linker-consumable $MACHINE_PATTERN ET_REL ELF64 object"
exit 0
