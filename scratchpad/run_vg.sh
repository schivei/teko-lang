export TEKO_BACKEND=native TK_RT_DIR=/home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f/src/runtime
cd /home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f
exec setarch x86_64 -R valgrind --tool=memcheck --error-limit=no --num-callers=25 --error-exitcode=0 ./out/teko . -o scratchpad/g2vg --no-verify --release
