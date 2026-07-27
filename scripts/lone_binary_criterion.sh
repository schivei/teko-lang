#!/usr/bin/env sh
# scripts/lone_binary_criterion.sh — O CRITÉRIO DE PRONTO do expurgo do C (owner, 2026-07-26).
#
# "projeto isolado, só o binário, nenhum .c/.h em lugar nenhum, TK_RT_DIR desarmado, share/teko
# inexistente. Tem que sair `hello, teko` e exit 0."
#
# NÃO é a suíte de testes e NÃO é o gate: é um hello world num diretório vazio com um compilador
# sozinho. A suíte prova que o compilador está CERTO; isto prova que ele é AUTOSSUFICIENTE — que um
# programa Teko não depende de fontes de runtime em C estagiadas ao lado, de um `cc` instalado, nem
# dos headers de sistema desse `cc`.
#
# BASELINE MEDIDO com o seed 0.3.0.30 — o estado que o expurgo tem de tornar impossível:
#
#   emit C     bin/hello.c   ✓
#   cc         bin/hello.c:6:10: fatal error: teko_rt.h: No such file or directory
#              cc1: fatal error: src/runtime/teko_rt.c: No such file or directory
#              cc1: fatal error: src/assert/assert.c: No such file or directory
#              ✗  cc failed to build the generated C
#   BUILD_EXIT=1     FECHO: NAO
#
# DUAS CONDIÇÕES, e a segunda é a que se esquece: o build tem de passar E não pode ter emitido C.
# Numa máquina que por acaso tenha o share instalado, um compilador que ainda emite C passa no exit
# code e falha no propósito — por isso a emissão de qualquer `.c` é reprovação.
#
# A NEUTRALIZAÇÃO DO SHARE É OBRIGATÓRIA, e a restauração é garantida por `trap`: uma máquina de
# desenvolvedor que rodou o install.sh TEM `<prefix>/share/teko/runtime` populado, e medir sem
# neutralizá-lo mede a instalação, não o binário.
#
# Usage:   sh scripts/lone_binary_criterion.sh <teko-binary>
# POSIX sh only.
set -u

BIN="${1:?usage: lone_binary_criterion.sh <teko-binary>}"
[ -x "$BIN" ] || { echo "lone_binary: '$BIN' is not an executable" >&2; exit 1; }
BIN_ABS="$(CDPATH='' cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")"

log() { printf '%s\n' "lone_binary: $*" >&2; }

SHARE_DIRS="/usr/local/share/teko ${HOME:-/nonexistent}/.local/share/teko"
WORK=""

# restore — devolve todo share/teko que este script escondeu, em QUALQUER saída, e limpa o projeto
# temporário. Sem isto, uma falha no meio deixa a máquina sem a instalação que ela tinha.
restore() {
    for sd in $SHARE_DIRS; do
        [ -d "$sd.lone-hidden" ] && mv "$sd.lone-hidden" "$sd"
    done
    [ -n "$WORK" ] && [ -d "$WORK" ] && rm -rf "$WORK"
    return 0
}
trap restore EXIT INT TERM

WORK="$(mktemp -d)"
mkdir -p "$WORK/bin"
cp "$BIN_ABS" "$WORK/bin/teko"
printf 'teko::io::println("hello, teko")\n' > "$WORK/main.tks"
printf 'name = "hello"\nsource = "."\nversion = "0.0.1"\n\n[artifact]\nkind = "binary"\n' > "$WORK/hello.tkp"

log "the whole project:"
find "$WORK" -type f | sed "s|$WORK|.|;s|^|lone_binary:   |" >&2

STRAY="$(find "$WORK" \( -name '*.c' -o -name '*.h' \) | wc -l | tr -d ' ')"
if [ "$STRAY" != "0" ]; then
    log "FATAL: the isolated project already carries $STRAY C file(s) — the premise is broken"
    exit 1
fi

for sd in $SHARE_DIRS; do
    [ -d "$sd" ] && { mv "$sd" "$sd.lone-hidden"; log "neutralised $sd (restored on exit)"; }
done

# `--no-verify --release`, like every other build on the train (owner ruling 2026-07-27: *"O build
# sempre deve ser se o `--no-verify --release`, o teste já será executado na lane adequada"*). This
# call carried NEITHER, so it was the one place that still ran the test gate inside a build — the
# gate is the test layer's whole job, and paying for it here bought a second, slower copy of a
# verdict another lane already owns. `--release` because a build that is not an -O2 link is not the
# build this criterion claims to be exercising.
( cd "$WORK" && unset TK_RT_DIR && ./bin/teko . -o bin --no-verify --release )
BUILD_EXIT=$?

for sd in $SHARE_DIRS; do
    [ -d "$sd.lone-hidden" ] && mv "$sd.lone-hidden" "$sd"
done

if [ "$BUILD_EXIT" -ne 0 ]; then
    log "FECHO: NAO — the build failed (exit $BUILD_EXIT) with nothing but the binary on disk"
    exit 1
fi

EMITTED="$(find "$WORK" -name '*.c' | wc -l | tr -d ' ')"
if [ "$EMITTED" != "0" ]; then
    log "FECHO: NAO — the build succeeded but EMITTED C. Autonomy is not exit status 0:"
    find "$WORK" -name '*.c' | sed "s|$WORK|.|;s|^|lone_binary:   |" >&2
    exit 1
fi

OUT="$( (cd "$WORK" && ./bin/hello) 2>&1 )"
RUN_EXIT=$?
log "the program said: $OUT"
if [ "$RUN_EXIT" -ne 0 ] || [ "$OUT" != "hello, teko" ]; then
    log "FECHO: NAO — the program ran with exit $RUN_EXIT and said '$OUT'"
    exit 1
fi

log "FECHO: SIM — built and ran with nothing but the compiler binary, and emitted no C"
exit 0
