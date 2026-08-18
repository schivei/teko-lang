export TEKO_BACKEND=native
export TK_RT_DIR=/home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f/src/runtime
exec setarch x86_64 -R gdb -batch \
  -ex "set pagination off" \
  -ex "break tk_chunk_free if (\$rdi+24) <= 0x555557db9410 && 0x555557db9410 < (\$rdi+24+*(unsigned long*)(\$rdi+8))" \
  -ex "commands" \
  -ex "silent" \
  -ex "printf \"=== FREE of chunk covering target ===\n\"" \
  -ex "bt 14" \
  -ex "printf \"---\n\"" \
  -ex "continue" \
  -ex "end" \
  -ex "run . -o outN3 --no-verify --release" \
  -ex "printf \"=== PROGRAM ENDED ===\n\"" \
  -ex "bt 6" \
  ./outN/teko
