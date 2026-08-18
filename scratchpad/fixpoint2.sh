set -e
cd /home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f
RT=/home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f/src/runtime
W=scratchpad/fp2
ENVV="TK_RT_DIR=$RT TEKO_BACKEND=native TEKO_NATIVE_REGION_CHECK=1 TEKO_NATIVE_TRACE_ITEMS=1"
if [ ! -x "$W/out/teko" ]; then echo "gen2 missing at $W/out"; exit 1; fi
cp "$W/out/teko" "$W/gen2.teko"
echo "gen2 bytes=$(wc -c < "$W/gen2.teko") sha=$(sha256sum "$W/gen2.teko" | cut -d' ' -f1)"
echo "=== BUILD gen3 (gen2 -> native) at SAME path $W/out ==="
rm -rf "$W/out"
setarch x86_64 -R env $ENVV "$W/gen2.teko" . -o "$W/out" --no-verify --release > "$W/gen3.log" 2>&1 || echo "gen3 build exit=$?"
if [ ! -x "$W/out/teko" ]; then echo "GEN3 MISSING"; grep -viE "native-lowering item|items  " "$W/gen3.log" | tail -15; exit 1; fi
cp "$W/out/teko" "$W/gen3.teko"
echo "gen3 bytes=$(wc -c < "$W/gen3.teko") sha=$(sha256sum "$W/gen3.teko" | cut -d' ' -f1)"
echo "=== COMPARE ==="
if cmp "$W/gen2.teko" "$W/gen3.teko"; then echo "FIXPOINT PASSED: gen2 == gen3 (byte-identical)"; else echo "FIXPOINT FAILED: gen2 != gen3"; fi
