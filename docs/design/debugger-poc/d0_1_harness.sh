#!/usr/bin/env sh
# docs/design/debugger-poc/d0_1_harness.sh — the D0.1 debugger harness.
#
# The crumb D0.1 of docs/design/debugger-superficie-e-contramedida-0.3.1.md (Peca 4, section 9.1):
# a debugger harness that READS the debugger's own output and ASSERTS the TEXT it printed, never a
# bare process exit code. Every assertion below prints "PASS <name>" or "FAIL <name>" after matching
# a STRING in gdb's stdout, so the verdict is the text a human reads, not a hidden status.
#
# It is BORN WITH A NEGATIVE PROOF. The DWARF writer (src/backend/dwarf.tks, crumb D1.3) is complete
# and byte-exact against the golden of this directory, but it is NOT YET wired into the compiler's
# object emission (crumb D1.6, a separate lane). So a real Teko-compiled binary today carries NO
# .debug_* sections and gdb finds no Teko source in it. The NEGATIVE section asserts exactly that
# absence: the day D1.6 lands, that assertion flips and the harness reports the regression by name.
#
# The POSITIVE section is the section-5.2 defence: it asserts the backtrace depth THROUGH A FRAMELESS
# leaf (adv.s's `lvl4`), not a weak ">= N frames". A frameless function returns RSP exactly as it
# received it; if the unwind survives it with the CALLER frame (`lvl3`) recovered and named, the
# claim that unwinding needs CFI (red-flag 3) is refuted with no CFI in the object at all.
#
# Run from this directory. It is documentation that executes, not a CI gate: it needs `as`, `cc`,
# `gdb`, and — for the negative proof — the `teko` compiler on PATH (override with $TEKO).
set -e
cd "$(dirname "$0")"

TEKO="${TEKO:-teko}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass_if_contains() {
    name="$1"
    needle="$2"
    haystack="$3"
    case "$haystack" in
        *"$needle"*) echo "PASS $name" ;;
        *) echo "FAIL $name (expected to read: $needle)" ;;
    esac
}

echo "=== D0.1 POSITIVE: frameless five-frame unwind, NO CFI (adv.s) ==="
as -g -o "$WORK/adv.o" adv.s
cc -o "$WORK/adv" "$WORK/adv.o" 2>/dev/null
BT="$(gdb -batch -nx -ex 'break *lvl4' -ex run -ex bt "$WORK/adv" 2>&1)"
pass_if_contains frameless_leaf_named   '#0  lvl4 () at hello.tks:30' "$BT"
pass_if_contains caller_of_frameless    'in lvl3 () at hello.tks:42'  "$BT"
pass_if_contains frameless_caller_hole  'in lvl2 () at hello.tks:20'  "$BT"
pass_if_contains framed_by_alignment    'in lvl1 () at hello.tks:17'  "$BT"
pass_if_contains bottom_frame_main      'in main () at hello.tks:41'  "$BT"

echo "=== D0.1 NEGATIVE (born-negative): the compiler emits no DWARF yet (D1.6 open) ==="
mkdir -p "$WORK/neg/src"
cat > "$WORK/neg/teko.tkp" <<'TKP'
name = "neg"
source = "src"
version = "0.0.1"
suffix = "alpha"
description = "the born-negative fixture: a real Teko binary carries no DWARF until D1.6"
[artifact]
kind = "binary"
TKP
cat > "$WORK/neg/main.tks" <<'TKS'
teko::io::println("hi")
teko::exit(0)
TKS
"$TEKO" build "$WORK/neg" -o "$WORK/neg/out" --no-verify >/dev/null 2>&1
NEG="$(gdb -batch -nx -ex 'info line main' "$WORK/neg/out/neg" 2>&1)"
pass_if_contains no_teko_line_info 'No line number information available' "$NEG"
