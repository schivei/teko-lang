set pagination off
# Stop at the corrupting memcpy via the value watch, then inspect the CALLER tk_slice_elem_box frame for esz + alloc base.
set can-use-hw-watchpoints 1
start
watch *(unsigned long*)0x555569ae1ed0 if *(unsigned long*)0x555569ae1ed0 == 0x1c52f1d
commands
silent
printf "\n=== corrupting write ===\n"
frame 1
info args
printf "rsi(esz at elem_box entry unknown here); regs:\n"
info registers rdi rsi rdx
printf "--- disas caller elem_box ---\n"
disassemble tk_slice_elem_box
end
continue
