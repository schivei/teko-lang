set pagination off
run
printf "\n=== FAULT ===\n"
x/i $pc
printf "\n=== all regs ===\n"
info registers rax rbx rcx rdx rsi rdi rbp rsp r8 r9 r10 r11 r12 r13 r14 r15
printf "\n=== disas around pc ===\n"
disassemble $pc-90,$pc+12
printf "\n=== BT ===\n"
bt 18
