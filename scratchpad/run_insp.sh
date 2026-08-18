export TEKO_BACKEND=native TK_RT_DIR=/home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f/src/runtime
cd /home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f
exec setarch x86_64 -R gdb -batch -x scratchpad/insp.gdb --args ./out/teko . -o scratchpad/g2insp --no-verify --release < /dev/null
