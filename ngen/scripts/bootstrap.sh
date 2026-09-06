#!/bin/sh
# bootstrap.sh -- S4.2 (docs/design/plano-ngen-entrega4.md §64(e), "Rito do
# fixpoint"): the fixed point of the SELF-HOSTED teko, in the protocol of the
# `mc` repository's own `scripts/bootstrap.sh`.
#
#   teko0 = mc build ngen --config <cfg> --compiler-only     (the stock mc)
#   teko1 = teko0 build ngen --config <cfg1> --entry-only     over mc_teko.tk
#   teko2 = teko1 ...                                         over mc_teko.tk
#   teko3 = teko2 ...                                         over mc_teko.tk
#   cmp build/teko2.o build/teko3.o          <- the criterion, on the OBJECTS
#   diff of `--dump-asm` between teko2 and teko3   <- the same, in readable form
#   the 45 fixtures compiled by teko1               <- and it is a compiler
#
# `teko1.o` vs `teko2.o` is NOT the criterion (they come from two different
# compilers -- the stock mc's codegen and teko1's own), exactly as `mc1.o` vs
# `mc2.o` is not the criterion there.
#
# No `set -e`: every step checks its own exit code and says which one failed,
# so a failure in the middle never passes silently. Times are printed per
# stage, sizes for every object and binary.
#
# Usage, from the REPOSITORY ROOT (the config path has to stay relative --
# with an absolute one the module treats every file as "outside the project"
# and the `internal` check goes blind, D224):
#
#   sh ngen/scripts/bootstrap.sh                 # host from `mc --host`
#   sh ngen/scripts/bootstrap.sh --os linux --arch x86_64
#
# KNOWN BLOCKER, mc side (docs/design/plano-ngen-entrega4.md §70(f)): stage 1
# stops at `mc/objmodel:212: expected ; after expression`. `word_add` marks the
# token ENTRY, and the entry is shared with the `#rule` road, so teaching
# `while`/`for` (teko does: its handlers do scope/RC and `break N` rewriting)
# takes the prelude's own `while` away from every source the dialect does not
# claim -- the core's, which use it ~150 times. With that one item fixed the
# ladder closes: measured with the two registrations commented out, teko2.o ==
# teko3.o (and teko1.o == teko2.o).
#
# The derived configs are written next to `ngen/mc.toml` (an entry path is
# resolved against the CONFIG's own directory, so a config in a scratch
# directory cannot find `mc_teko.tk`) and removed on exit; `ngen/mc.toml`
# itself is never touched. They keep the `[linker]` block on purpose: with a
# linker `mc` writes `<out>.o` and hands it over, which is what leaves the
# object on disk for the `cmp` -- without one the built-in executable backend
# writes the binary and no object at all.

os=""
arch=""
while [ $# -gt 0 ]; do
    case "$1" in
        --os)   os="$2";   shift 2 ;;
        --arch) arch="$2"; shift 2 ;;
        *) echo "usage: bootstrap.sh [--os OS] [--arch ARCH]" >&2; exit 1 ;;
    esac
done

if [ ! -f ngen/mc.toml ]; then
    echo "FAIL: run from the repository root (ngen/mc.toml not found)" >&2
    exit 1
fi
if [ ! -f ngen/mc_teko.tk ]; then
    echo "FAIL: ngen/mc_teko.tk not found" >&2
    exit 1
fi
if ! command -v mc >/dev/null 2>&1; then
    echo "FAIL: no 'mc' on PATH (ngen/HANDOFF.md §4 installs it from the release)" >&2
    exit 1
fi

if [ -z "$os" ];   then os=$(mc --host   | awk '$1 == "os"   { print $2 }'); fi
if [ -z "$arch" ]; then arch=$(mc --host | awk '$1 == "arch" { print $2 }'); fi
if [ -z "$os" ] || [ -z "$arch" ]; then
    echo "FAIL: could not resolve the host pair (mc --host)" >&2
    exit 1
fi

cfg0="ngen/mc.boot0.toml"
cfg1="ngen/mc.boot1.toml"
cfg2="ngen/mc.boot2.toml"
cfg3="ngen/mc.boot3.toml"
cfgf="ngen/mc.bootfix.toml"
asm2="${TMPDIR:-/tmp}/teko2.$$.asm"
asm3="${TMPDIR:-/tmp}/teko3.$$.asm"
out="${TMPDIR:-/tmp}/bootstrap.$$.out"
err="${TMPDIR:-/tmp}/bootstrap.$$.err"
trap 'rm -f "$cfg0" "$cfg1" "$cfg2" "$cfg3" "$cfgf" "$asm2" "$asm3" "$out" "$err"' EXIT

# derive CONFIG ENTRY OUT -- ngen/mc.toml with the host's own target and one
# stage's entry/output, the same `sed` shape HANDOFF.md §4 uses for a fixture
derive() {
    sed -e "s#^os   = .*#os   = \"$os\"#" \
        -e "s#^arch = .*#arch = \"$arch\"#" \
        -e "s#^entry = .*#entry = \"$2\"#" \
        -e "s#^out   = .*#out   = \"$3\"#" \
        ngen/mc.toml > "$1"
}

now() {
    perl -MTime::HiRes=time -e 'printf "%.3f\n", time'
}

dt() {
    perl -e 'printf "%.3f", '"$2"' - '"$1"''
}

size_of() {
    wc -c < "$1" | tr -d ' '
}

# step DESCRIPTION CMD... -- runs CMD, times it, and stops the whole script
# with the command's own output when it fails
step() {
    desc="$1"; shift
    t0=$(now)
    "$@" >"$out" 2>"$err"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FAIL: $desc (exit $rc)" >&2
        echo "--- command: $* ---" >&2
        cat "$out" "$err" >&2
        exit 1
    fi
    t1=$(now)
    echo "  $desc: $(dt "$t0" "$t1")s"
    cat "$out"
}

echo "=== S4.2 -- fixed point of the self-hosted teko: teko0 -> teko1 -> teko2 -> teko3 ==="
echo "-- target $os/$arch, entry ngen/mc_teko.tk --"

t_total0=$(now)

echo "-- stage 0: mc build ngen --compiler-only -> ngen/build/teko --"
derive "$cfg0" "tests/hello.tk" "build/teko-hello"
step "mc builds teko0" mc build ngen --config "$cfg0" --compiler-only
echo "  size ngen/build/teko: $(size_of ngen/build/teko) bytes"

echo "-- stage 1: teko0 mc_teko.tk -> ngen/build/teko1 --"
derive "$cfg1" "mc_teko.tk" "build/teko1"
step "teko0 compiles mc_teko.tk" ngen/build/teko build ngen --config "$cfg1" --entry-only
echo "  size ngen/build/teko1.o: $(size_of ngen/build/teko1.o) bytes"
echo "  size ngen/build/teko1:   $(size_of ngen/build/teko1) bytes"

echo "-- stage 2: teko1 mc_teko.tk -> ngen/build/teko2 --"
derive "$cfg2" "mc_teko.tk" "build/teko2"
step "teko1 compiles mc_teko.tk" ngen/build/teko1 build ngen --config "$cfg2" --entry-only
echo "  size ngen/build/teko2.o: $(size_of ngen/build/teko2.o) bytes"

echo "-- stage 3: teko2 mc_teko.tk -> ngen/build/teko3 --"
derive "$cfg3" "mc_teko.tk" "build/teko3"
step "teko2 compiles mc_teko.tk" ngen/build/teko2 build ngen --config "$cfg3" --entry-only
echo "  size ngen/build/teko3.o: $(size_of ngen/build/teko3.o) bytes"

echo "-- criterion 1: cmp ngen/build/teko2.o ngen/build/teko3.o --"
if ! cmp ngen/build/teko2.o ngen/build/teko3.o; then
    echo "FAIL: teko2.o != teko3.o -- no fixed point" >&2
    echo "diagnosis: diff <(ngen/build/teko2 --dump-asm ngen/mc_teko.tk) <(ngen/build/teko3 --dump-asm ngen/mc_teko.tk)" >&2
    exit 1
fi
echo "  ok: teko2.o == teko3.o"

echo "-- criterion 2: --dump-asm of teko2 vs teko3 --"
ngen/build/teko2 --dump-asm ngen/mc_teko.tk > "$asm2" 2>&1
ngen/build/teko3 --dump-asm ngen/mc_teko.tk > "$asm3" 2>&1
if ! diff "$asm2" "$asm3" > "$out"; then
    echo "FAIL: the two dumps differ" >&2
    head -40 "$out" >&2
    exit 1
fi
echo "  ok: $(wc -l < "$asm2" | tr -d ' ') lines, diff empty"

echo "-- criterion 3: teko1 compiles the fixtures --"
pass=0
fail=0
for src in ngen/tests/*.tk; do
    n=$(basename "$src" .tk)
    want=$(grep -m1 '// expect-exit:' "$src" | sed 's/.*expect-exit: *//')
    derive "$cfgf" "tests/$n.tk" "build/$n"
    if ngen/build/teko1 build ngen --config "$cfgf" --entry-only >"$out" 2>"$err"; then
        "ngen/build/$n"
        got=$?
    else
        got="build-fail"
    fi
    if [ "$got" = "$want" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "  FAIL $n: exit $got, want $want" >&2
        head -3 "$out" "$err" >&2
    fi
done
echo "  fixtures: $pass passed, $fail failed"

t_total1=$(now)
echo "=== total: $(dt "$t_total0" "$t_total1")s ==="

if [ "$fail" -ne 0 ]; then
    echo "FAIL: teko1 does not compile every fixture" >&2
    exit 1
fi
echo "FIXPOINT OK"
