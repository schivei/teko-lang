set -eu
ROOT=/home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f
cd "$ROOT"
RT="$ROOT/src/runtime"
E="$ROOT/scratchpad/exp"
rm -rf "$E"; mkdir -p "$E"

echo "=== STEP 2: build gen0prime from out/teko.c (move-on-return codegen) ==="
if cc -std=c2x -w -O2 -I src/runtime -I src/assert out/teko.c src/runtime/teko_rt.c src/assert/assert.c -lm -o "$E/gen0prime" 2>"$E/gen0p_cc.log"; then
  echo "gen0prime built: $("$E/gen0prime" --version 2>&1 | head -1)"
else
  echo "GEN0PRIME CC FAILED"; tail -20 "$E/gen0p_cc.log"; exit 1
fi

echo "=== STEP 3: gen0prime compiles source (C route) -> gen1prime ==="
if ( cd "$ROOT" && TK_RT_DIR="$RT" TEKO_BACKEND=c "$E/gen0prime" . -o "$E/g1p" --no-verify --release ) >"$E/gen1p_build.log" 2>&1; then
  echo "gen1prime built: $("$E/g1p/teko" --version 2>&1 | head -1)"
else
  echo "GEN1PRIME BUILD (C route) exit=$?"; grep -viE "items|files|instances|consts" "$E/gen1p_build.log" | tail -15
  [ -x "$E/g1p/teko" ] || { echo "gen1prime MISSING"; exit 1; }
fi

echo "=== STEP 4: native fixpoint with gen1prime (ASLR off) ==="
echo "--- gen2p = gen1prime(source) native ---"
setarch x86_64 -R env TK_RT_DIR="$RT" TEKO_BACKEND=native "$E/g1p/teko" . -o "$E/g2" --no-verify --release >"$E/gen2p.log" 2>&1 || echo "gen2p build exit=$?"
if [ -x "$E/g2/teko" ]; then
  echo "gen2p BUILT ok bytes=$(wc -c < "$E/g2/teko")"
  cp "$E/g2/teko" "$E/gen2.teko"
  echo "--- gen3p = gen2p(source) native ---"
  setarch x86_64 -R env TK_RT_DIR="$RT" TEKO_BACKEND=native "$E/gen2.teko" . -o "$E/g3" --no-verify --release >"$E/gen3p.log" 2>&1 || echo "gen3p build exit=$?"
  if [ -x "$E/g3/teko" ]; then
    cp "$E/g3/teko" "$E/gen3.teko"
    echo "gen3p BUILT ok bytes=$(wc -c < "$E/gen3.teko")"
    if cmp "$E/gen2.teko" "$E/gen3.teko"; then echo "FIXPOINT PASSED: gen2p==gen3p"; else echo "FIXPOINT DIFF: gen2p != gen3p"; fi
  else
    echo "GEN3P MISSING - crash detail:"; grep -viE "items|files|instances|consts" "$E/gen3p.log" | tail -12
  fi
else
  echo "GEN2P MISSING - crash detail (does gen1prime crash like gen1 did?):"; grep -viE "items|files|instances|consts" "$E/gen2p.log" | tail -14
fi
echo "=== EXPERIMENT DONE ==="
