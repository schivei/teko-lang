set pagination off
set can-use-hw-watchpoints 1
start
watch *(unsigned long*)0x555569ae1ed0
commands
silent
printf "WRITE val=0x%lx  pc=", *(unsigned long*)0x555569ae1ed0
x/i $pc
printf "  caller: "
info frame
continue
end
continue
