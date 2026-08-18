set pagination off
run
printf "\n=== FAULT ===\n"
info registers rax rbx rcx rdx rsi rdi
x/i $pc
printf "\n=== BT ===\n"
bt 20
