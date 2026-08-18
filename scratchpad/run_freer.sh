export TEKO_BACKEND=native
export TK_RT_DIR=/home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f/src/runtime
exec setarch x86_64 -R gdb -batch -x scratchpad/freer.gdb --args ./outN/teko . -o outN3 --no-verify --release < /dev/null
