export TEKO_BACKEND=native TK_RT_DIR=/home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f/src/runtime
export TEKO_NATIVE_REGION_CHECK=1 TEKO_NATIVE_TRACE_ITEMS=1
cd /home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f
exec setarch x86_64 -R gdb -batch -x scratchpad/gdb_g3d.gdb --args scratchpad/fp2/gen2.teko . -o scratchpad/fp2/outg --no-verify --release < /dev/null
