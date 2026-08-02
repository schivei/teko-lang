#!/usr/bin/env bash
# theory/win-tests/run-staged.sh — roda `teko test` em ESTAGIOS com marcadores + watchdog por
# estagio, para LOCALIZAR onde o Windows trava. Cada estagio imprime START/END pelo bash (nao pelo
# teko), entao o ultimo marcador impresso nomeia o estagio que travou, mesmo com o teko bufferizando.
#
# Uso: sh theory/win-tests/run-staged.sh <caminho-do-teko>
set -u
TEKO="${1:-out/teko.exe}"
[ -x "$TEKO" ] || TEKO="out/teko"
if [ ! -x "$TEKO" ]; then echo "ERRO: teko nao encontrado (tentei out/teko.exe e out/teko)"; ls -lR out dl 2>/dev/null | head -40; exit 2; fi
echo "teko = $TEKO"; "$TEKO" --version || { echo "ERRO: --version falhou"; exit 3; }

# run_stage NOME SEGUNDOS CMD...
run_stage() {
  name="$1"; secs="$2"; shift 2
  echo ">>> STAGE START: $name (limite ${secs}s) $(date -u +%H:%M:%S)"
  "$@" & pid=$!
  ( sleep "$secs"; kill -9 "$pid" 2>/dev/null && echo "!!! STAGE TIMEOUT: $name apos ${secs}s" ) & wpid=$!
  wait "$pid"; rc=$?
  kill "$wpid" 2>/dev/null
  echo "<<< STAGE END: $name rc=$rc $(date -u +%H:%M:%S)"
  return $rc
}

# HIPOTESE 1: deadlock de orquestracao de processos (sharding/pool) no Windows.
# Se com jobs=1 destrava, o bug e na orquestracao, nao num teste.
echo "===== EXPERIMENTO A: jobs=1 (serial) ====="
TEKO_TEST_JOBS=1 TEKO_REGR_JOBS=1 run_stage "test-serial" 1200 "$TEKO" test .

echo "===== EXPERIMENTO B: default (paralelo) — so se A destravou ====="
run_stage "test-default" 1500 "$TEKO" test .

echo "===== FIM ====="
