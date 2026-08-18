export TEKO_BACKEND=native
export TK_RT_DIR=/home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f/src/runtime
exec setarch x86_64 -R gdb -batch \
  -ex "set pagination off" \
  -ex "break tk_free_block if (unsigned long)\$rdi <= 0x555557db9410 && 0x555557db9410 < ((unsigned long)\$rdi + (unsigned long)\$rsi)" \
  -ex "commands" \
  -ex "silent" \
  -ex "printf \"=== PARK covering target: p=%p n=%lu ===\n\", \$rdi, \$rsi" \
  -ex "bt 14" \
  -ex "printf \"---\n\"" \
  -ex "continue" \
  -ex "end" \
  -ex "run . -o outN3 --no-verify --release" \
  -ex "printf \"=== PROGRAM ENDED ===\n\"" \
  ./outN/teko
