#!/usr/bin/env sh
# examples/probes/match_shapes/run.sh — build every `match` shape SEPARATELY and print a table.
#
# TEMPORARY, like the directory it lives in. Owner ruling 2026-07-28: *"adicione saídas de log
# (temporárias) onde quer experimentar (já que ainda não temos debugger), monte um projeto de teste
# com diversas variações e possibilidade com match e aplique a teoria."*
#
# WHY ONE PROJECT PER CASE, and it is the whole design. A project build stops at the FIRST error, so
# the `bulk` regressor — 217 fixtures in one build — reports exactly one stop and hides every other.
# That is how two wrong hypotheses survived for hours. Ten separate builds means ten independent
# verdicts in one run, and the SHAPE of the table is the finding: which axis flips a case from green
# to stopped is the answer no amount of code-reading produced.
#
# THIS SCRIPT NEVER FAILS. It is an instrument, not a gate — a non-zero exit here would just be a
# second thing to debug. Every case is reported, including the ones that pass.
#
# TWO COMPILERS, NOT ONE. Owner ruling 2026-07-28: *"o que é a compilação do que um binário
# executando em runtime a identidade de um código? Entende o paradoxo? Se o erro é em runtime, ele
# pode não morar onde estamos olhando."*
#
# The compiler is a BINARY EXECUTING. Compiling `bulk` is `teko`'s own run time, and the binary that
# runs is NOT the checker source anyone reads — gen1 came out of `cc` over wagon 15's `teko.c`, so
# the checker it contains was lowered by an OLDER compiler. If that older compiler mislowered a
# `Type | null` anywhere in the checker, then `check_pattern` and `type_join` behave at run time
# differently from what their source says — and this wagon has already caught two bugs of exactly
# that family (the `byte` carrier width, `push` returning the wrong length).
#
# So `<label>` exists to run this matrix with MORE THAN ONE GENERATION. A case that stops under gen1
# and lowers under gen2 (or the reverse) proves the defect lives in the compiler LINEAGE, not in the
# source being compiled — which no amount of reading the checker could ever show.
#
# Usage:  sh examples/probes/match_shapes/run.sh <path-to-teko> [label]
set -u

TEKO="${1:?usage: run.sh <path-to-teko> [label]}"
LABEL="${2:-$(basename "$TEKO")}"
[ -x "$TEKO" ] || { printf '%s\n' "match_shapes: '$TEKO' is not executable" >&2; exit 0; }
# ABSOLUTIZE BEFORE ANY `cd`. Every caller passes a RELATIVE path (`out/teko`, `.rung-c/teko`,
# `.seed/teko`) and the loop below `cd`s into each case directory — after which that path resolves
# to nothing. The first run of this matrix produced 64 rows of "STOPPED  …: No such file or
# directory", i.e. a SHELL failure wearing a compiler verdict. `fixpoint_gate.sh` absolutizes its
# gen1 argument for exactly this reason; this script now does the same.
TEKO="$(cd "$(dirname "$TEKO")" && pwd)/$(basename "$TEKO")"
HERE="$(cd "$(dirname "$0")" && pwd)"

printf '\n===== match_shapes [%s]: which shapes can the NATIVE backend lower? =====\n' "$LABEL"
printf '%-26s %-8s %s\n' "CASE" "VERDICT" "WHAT THE COMPILER SAID"
printf '%s\n' "----------------------------------------------------------------------"

for dir in "$HERE"/[mt]*/; do
    case_name="$(basename "$dir")"
    log="$dir/.probe-$LABEL.log"
    ( cd "$dir" && TEKO_BACKEND=native "$TEKO" . -o out --no-verify ) >"$log" 2>&1
    rc=$?

    if [ "$rc" = "0" ]; then
        printf '%-26s %-8s %s\n' "$case_name" "LOWERED" "-"
        continue
    fi

    # The stop the backend named, else the last line — a build that died for any OTHER reason must
    # not be silently reported as a lowering stop.
    said="$(grep -m1 'has no single PrimKind' "$log" 2>/dev/null || true)"
    [ -n "$said" ] || said="$(grep -m1 'native backend N1' "$log" 2>/dev/null || true)"

    # A THIRD VERDICT, because two were not enough. A row that never reached the compiler — no such
    # file, not executable, a shell error — is NOT the backend refusing to lower something, and
    # printing it as STOPPED is how this matrix's first run reported 64 confident verdicts about
    # nothing. NORAN says the case was never measured, which is the honest thing to say.
    if [ -z "$said" ]; then
        tail_line="$(tail -n1 "$log" 2>/dev/null || true)"
        case "$tail_line" in
            *"No such file"*|*"not found"*|*"Permission denied"*|*"cannot execute"*)
                printf '%-26s %-8s %s\n' "$case_name" "NORAN" "$tail_line"
                continue ;;
        esac
        said="$tail_line"
    fi
    printf '%-26s %-8s %s\n' "$case_name" "STOPPED" "$(printf '%s' "$said" | sed 's/^teko: *//')"
done

printf '%s\n' "----------------------------------------------------------------------"
printf 'Read the table by AXIS, not by case. The mNN/tNN pair differs ONLY by whether the\n'
printf 'binding is inferred (mNN) or ANNOTATED (tNN) — the control group. Any other pair\n'
printf 'differs by exactly one property (value vs statement, cast vs literal, scalar vs struct).\n\n'

# The traces, in full, after the table — every stop the run produced, not just the first per case.
printf '===== every prim_kind_of stop trace, all cases =====\n'
grep -h 'TRACE prim_kind_of stop' "$HERE"/[mt]*/.probe-"$LABEL".log 2>/dev/null | sort | uniq -c | sort -rn || true
printf '\n'
exit 0
