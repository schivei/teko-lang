set -e
export TEKO_BACKEND=native
export TK_RT_DIR=/home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f/src/runtime
exec setarch x86_64 -R gdb -batch \
  -ex "run . -o outN3 --no-verify --release" \
  -ex "echo \n=== FAULT ===\n" \
  -ex "info registers rax rbx rcx rdx rsi rdi rbp rsp" \
  -ex "x/i \$pc" \
  -ex "echo \n=== BACKTRACE ===\n" \
  -ex "bt 15" \
  ./outN/teko
