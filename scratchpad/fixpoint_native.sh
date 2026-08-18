set -e
cd /home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f
RT=/home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f/src/runtime
GEN1=./out/teko
W=scratchpad/fp
rm -rf "$W"; mkdir -p "$W"
echo "=== BUILD gen2 (gen1 -> native) at $W/out ==="
setarch x86_64 -R env TK_RT_DIR="$RT" TEKO_BACKEND=native "$GEN1" . -o "$W/out" --no-verify --release > "$W/gen2.log" 2>&1 || echo "gen2 build exit=$?"
if [ ! -x "$W/out/teko" ]; then echo "GEN2 MISSING"; tail -5 "$W/gen2.log"; exit 1; fi
cp "$W/out/teko" "$W/gen2.teko"
echo "gen2 ok bytes=$(wc -c < "$W/gen2.teko")"
echo "=== BUILD gen3 (gen2 -> native) at SAME path $W/out ==="
GEN2="$W/gen2.teko"
rm -rf "$W/out"
setarch x86_64 -R env TK_RT_DIR="$RT" TEKO_BACKEND=native "$GEN2" . -o "$W/out" --no-verify --release > "$W/gen3.log" 2>&1 || echo "gen3 build exit=$?"
if [ ! -x "$W/out/teko" ]; then echo "GEN3 MISSING"; tail -8 "$W/gen3.log"; exit 1; fi
cp "$W/out/teko" "$W/gen3.teko"
echo "gen3 ok bytes=$(wc -c < "$W/gen3.teko")"
echo "=== COMPARE gen2 vs gen3 ==="
if cmp "$W/gen2.teko" "$W/gen3.teko"; then
  echo "FIXPOINT PASSED: gen2 == gen3 (byte-identical)"
  sha256sum "$W/gen2.teko" "$W/gen3.teko"
else
  echo "FIXPOINT FAILED: gen2 != gen3"
  sha256sum "$W/gen2.teko" "$W/gen3.teko"
fi
