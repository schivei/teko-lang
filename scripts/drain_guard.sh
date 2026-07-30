#!/usr/bin/env bash
# scripts/drain_guard.sh — refuses a drain that carries CI into the wagon.
#
# OWNER RULING 2026-07-30, and it closes a hole that HAS ALREADY BEEN THROUGH:
#
#   integrator: "posso pôr no meu dreno uma conferência que grita quando um merge de `cargo/**` traz
#                ficheiro de workflow [...] ao contrário da regra não depende de o agente ter sido
#                informado — pega também o agente de uma sessão futura que nunca leu o brief"
#   owner:      "sim, adicione"
#
# THE INCIDENT IT DEFENDS AGAINST IS REAL, NOT HYPOTHETICAL. The owner, on the rule that agents must
# never author workflow files: *"foi a falta dessa guarda que fez a fast-lane ir para a main na outra
# sessão"*. `agent-fast-lane.yml` reached `main` by exactly this path — a workflow authored on a
# `theory/**` branch, cherry-picked back into the working branch, and drained without anyone noticing.
#
# WHY A SCRIPT AND NOT JUST THE RULE. The rule ("an agent never authors `.github/workflows/**`") closes
# the hole AT THE ORIGIN, which is stronger — nothing exists to travel. But it depends on the agent
# having been told. This check depends on nothing: it catches the agent of a future session who never
# read a brief, and it catches ME on a tired drain. Belt to the rule's braces.
#
# AND IT FILLS A REAL GAP IN THE DRAIN. The integrator's pre-merge ritual already checks brace and
# comment balance, duplicate exit codes, and calls with no definition. It checked NOTHING about which
# FILES a merge brings. That asymmetry is why the fast-lane slipped through: no eye was on it.
#
# THE INTEGRATOR IS STILL ALLOWED TO CHANGE CI — from the wagon itself, or with `--allow` when a
# branch is deliberately a CI change. What is refused is CI arriving as a PASSENGER on product work.
#
# usage: scripts/drain_guard.sh <ref-to-drain> [<wagon-ref>]
#        scripts/drain_guard.sh <ref-to-drain> --allow      # the branch IS a deliberate CI change
#
#   ref-to-drain  the branch/SHA about to be merged
#   wagon-ref     what to compare against (default: HEAD)

set -u

REF="${1:-}"
ARG2="${2:-}"

if [ -z "$REF" ]; then
    echo "drain_guard: FATAL — usage: $0 <ref-to-drain> [<wagon-ref>|--allow]" >&2
    exit 2
fi

ALLOW=0
WAGON="HEAD"
if [ "$ARG2" = "--allow" ]; then
    ALLOW=1
elif [ -n "$ARG2" ]; then
    WAGON="$ARG2"
fi

git rev-parse --verify "$REF" >/dev/null 2>&1 || {
    echo "drain_guard: FATAL — '$REF' is not a ref this repo knows" >&2
    exit 2
}

# The three-dot form is deliberate: it asks what the BRANCH changed since diverging, not what differs
# between the two tips. A two-dot diff would blame the branch for every CI change the wagon made
# meanwhile, and the guard would cry wolf on every honest drain.
changed="$(git diff --name-only "$WAGON...$REF" -- '.github/workflows/' 2>/dev/null)"

hits="$(printf '%s\n' "$changed" | grep -c '[^[:space:]]' || true)"

if [ "$hits" -eq 0 ]; then
    echo "drain_guard: OK — '$REF' brings no .github/workflows/ change"
    exit 0
fi

if [ "$ALLOW" -eq 1 ]; then
    echo "drain_guard: ALLOWED (--allow) — '$REF' changes CI deliberately:"
    printf '%s\n' "$changed" | sed 's/^/    /'
    exit 0
fi

echo "drain_guard: REFUSED — '$REF' carries CI into the wagon:" >&2
printf '%s\n' "$changed" | sed 's/^/    /' >&2
cat >&2 <<'WHY'

An agent must never author a file under .github/workflows/ — not even on a theory/** branch, because
a cherry-pick back into its working branch drags the restricted CI along. That is not a hypothesis:
it is how agent-fast-lane.yml reached main in an earlier session.

What to do:
  · If this is an AGENT branch: drop the workflow change from it and drain the rest. Whatever CI it
    wanted is the integrator's to write, on the wagon or on the integrator's own theory branch.
  · If YOU authored this CI change deliberately: re-run with --allow.
WHY
exit 1
