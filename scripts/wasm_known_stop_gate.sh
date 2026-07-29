#!/usr/bin/env bash
# scripts/wasm_known_stop_gate.sh — the KNOWN-STOP pin for wasm32-wasi's missing executor.
#
# OWNER RULING 2026-07-29, verbatim: *"KNOWN-STOP, wasm terá a própria versão para refinar"*.
#
# WHAT IS BROKEN, MEASURED. `examples/regressions/own_native/own_native.tkr` carries a
# `Given target = "wasm32-wasi"` row whose claim is `When built and run` / `Then exit = 7`, so it
# needs a wasm ENGINE to keep that claim. Exactly one CI job provisions one —
# `regressor-full`, installing wasmtime — and that job is gated on `full == 'true'`. On a
# light-tier PR no leg has an engine, so on a normal PR the row is exercised NOWHERE. Its own
# scenario name even ends "(or honest-skips without the toolchain)", which legitimises the very
# skip the owner's law refuses: *"testes com skip ou warnings do compilador não serão aceitos"*.
#
# WHY A KNOWN-STOP AND NOT A FIX. Refining wasm is its own version's work by owner ruling, so the
# honest move is to PIN the gap where everyone can see it rather than quietly tolerate it. His
# fail-closed principle is the reason this file exists at all: *"um row que ninguém correu é
# inconclusivo, e reportar verde sobre ele é a mesma classe de mentira"*. A pin makes the
# inconclusive row VISIBLE on every leg instead of invisible on all of them.
#
# THIS PIN INVERTS, which is what makes it a KNOWN-STOP and not a comment. It passes GREEN while the
# gap is open and turns RED the moment the gap closes — either because a both-tier leg gains an
# engine, or because the row stops needing one. Red here does NOT mean something broke; it means the
# pin has done its job and the row must be PROMOTED. The owner's own warning applies: *"um
# KNOWN-STOP que falha (vermelho) não necessariamente significa que o erro que existia foi
# corrigido"* — it may equally mean the direction changed.
#
# THE PROMOTION CRUMB, for the wasm version, so it arrives ready:
#   1. SPLIT THE ROW'S CLAIM. `When built` + `Then object well-formed` needs NO engine — the same
#      insight that removed mingw from the COFF cross row (docs/memory/teko-laws-digest.md, "cross
#      compiling SIM, mingw NUNCA"): a claim about the emitted artefact's FORMAT is checkable
#      anywhere. Only `and run` / `Then exit = 7` wants an engine. Split, and the format half runs
#      on all six legs immediately.
#   2. Then give one both-tier leg an engine, so the executing half has an owner every PR.
#   3. Drop the "(or honest-skips without the toolchain)" tail from the scenario name — a row must
#      not carry its own excuse.
#   4. Retire this file. A pin outliving its gap is a lie in the other direction.
# Known wasm constraints already recorded, so step 2 is not a surprise: WASI and Browser are 64-bit
# only (owner), and `src/backend/stackify.tks`'s `C1-wasm64-scope` still refuses `LAlloca`, rodata
# and `Ptr` signatures on wasm64.
#
# NO YAML PARSER, NO GNU-ONLY TOOL. It walks the workflow with awk, because this gate runs on all
# six hosts and pyyaml is not guaranteed on any of them — and `sed -E`/`grep -P` are absent from the
# Windows runner's Git-Bash, which once cost this lane an opaque exit 127.
#
# usage: bash scripts/wasm_known_stop_gate.sh

set -u

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$script_dir"

WORKFLOW=".github/workflows/pr.yml"
ROW_FILE="examples/regressions/own_native/own_native.tkr"

pass_count=0
fail_count=0
ok() { echo "  ok   — $1"; pass_count=$((pass_count + 1)); }
no() { echo "  FAIL — $1" >&2; fail_count=$((fail_count + 1)); }

for f in "$WORKFLOW" "$ROW_FILE"; do
    [ -f "$f" ] || { echo "wasm_known_stop_gate: FATAL — $f is missing; the pin cannot judge what it cannot read" >&2; exit 2; }
done

echo "wasm_known_stop_gate: KNOWN-STOP — wasm32-wasi has no executor on a both-tier leg"

# Every job whose steps mention wasmtime, one per line, attributed by walking the job headers.
#
# COMMENT LINES ARE SKIPPED FIRST, and that is not tidiness — it is a bug this gate hit on its own
# first run. A job's header comment sits ABOVE its `jobname:` line, so a comment that merely NAMES
# wasmtime gets attributed to the PREVIOUS job: the pin reported `cli-surface-macos-arm64` as an
# engine provider purely because two explanatory lines above `regressor-full:` mention the word.
# Prose is not provisioning. (Third instance of this exact confusion in one day — the sed/grep scan
# in objfile_gate_test.sh flagged prose in a comment AND prose in a string before it was tight.)
engine_jobs="$(awk '
    /^[[:space:]]*#/                  { next }
    /^  [a-z0-9_-]+:[[:space:]]*$/    { job = $1; sub(/:$/, "", job) }
    /wasmtime/                        { if (job != "" && !(job in seen)) { seen[job] = 1; print job } }
' "$WORKFLOW")"

echo "1) exactly one job provisions a wasm engine, and it is the full-tier one"
engine_count="$(printf '%s\n' "$engine_jobs" | grep -c '[^[:space:]]' || true)"
if [ "$engine_count" -eq 1 ] && [ "$engine_jobs" = "regressor-full" ]; then
    ok "regressor-full is the sole wasm-engine provider"
elif [ "$engine_count" -eq 0 ]; then
    no "NO job provisions a wasm engine at all — the row is unrunnable even on the full tier, which is a wider gap than this pin describes"
else
    no "the wasm engine is now provisioned by [$(printf '%s' "$engine_jobs" | tr '\n' ' ')] — if any of those is a both-tier leg, PROMOTE the row (see the promotion crumb in this file's header) and retire this pin"
fi

echo "2) that job is gated on the FULL tier, so a light-tier PR has no engine"
# The `if:` guarding regressor-full must still require full == 'true'. If that gate is relaxed, the
# engine reaches every PR and the pin is obsolete.
full_gated="$(awk '
    /^[[:space:]]*#/ { next }
    /^  regressor-full:[[:space:]]*$/ { injob = 1; next }
    injob && /^  [a-z0-9_-]+:[[:space:]]*$/ { injob = 0 }
    injob && /if:/ && /full/ && /true/ { found = 1 }
    END { print (found ? "yes" : "no") }
' "$WORKFLOW")"
if [ "$full_gated" = "yes" ]; then
    ok "regressor-full still requires full == 'true'"
else
    no "regressor-full is no longer full-tier-gated — the wasm engine now reaches every PR, so PROMOTE the row and retire this pin"
fi

echo "3) the pinned row still needs an engine (it claims 'built and run')"
# Read the wasm32-wasi scenario's own body: the pin describes a row that EXECUTES. The day the row
# claims only the emitted object's format, it needs no engine and this pin is describing a gap that
# no longer exists.
row_runs="$(awk '
    /Given target = "wasm32-wasi"/ { inrow = 1; next }
    inrow && /^[[:space:]]*Scenario:/ { inrow = 0 }
    inrow && /When built and run/ { found = 1 }
    END { print (found ? "yes" : "no") }
' "$ROW_FILE")"
if [ "$row_runs" = "yes" ]; then
    ok "the wasm32-wasi row still claims 'When built and run', so it still wants an engine"
else
    no "the wasm32-wasi row no longer claims 'When built and run' — someone split or retired it, so PROMOTE and retire this pin"
fi

echo "wasm_known_stop_gate: $pass_count passed, $fail_count failed"
if [ "$fail_count" -ne 0 ]; then
    echo "wasm_known_stop_gate: RED — the pinned gap moved. Read the promotion crumb in this file's header before assuming a regression: a KNOWN-STOP going red is the pin working, not a defect appearing." >&2
    exit 1
fi
echo "wasm_known_stop_gate: GREEN — the gap is still open, exactly as pinned (wasm refines in its own version)"
exit 0
