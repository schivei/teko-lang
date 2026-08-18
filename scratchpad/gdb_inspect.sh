export TEKO_BACKEND=native
export TK_RT_DIR=/home/user/teko-lang/.claude/worktrees/agent-a94245ddf89de395f/src/runtime
exec setarch x86_64 -R gdb -batch \
  -ex "run . -o outN3 --no-verify --release" \
  -ex "echo \n=== DISAS around fault ===\n" \
  -ex "disassemble \$pc-80,\$pc+16" \
  -ex "echo \n=== regs ===\n" \
  -ex "info registers rax rbx rcx rdx rsi rdi r8 r9 r10 r11 r12 r13 r14 r15" \
  -ex "echo \n=== mem rbx (slice/struct?) ===\n" \
  -ex "x/8gx \$rbx" \
  -ex "echo \n=== mem rsi ===\n" \
  -ex "x/8gx \$rsi" \
  ./outN/teko
