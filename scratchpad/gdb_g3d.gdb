set pagination off
run
printf "\n=== DISAS ===\n"
disassemble $pc-70,$pc+8
printf "\n=== regs ===\n"
info registers rax rbx rcx rdx rsi rdi r8 r9 r10 r11 r12 r13 r14 r15
