set pagination off
set can-use-hw-watchpoints 1
start
watch *(unsigned long*)0x555569ae1ed0 if *(unsigned long*)0x555569ae1ed0 == 0x1c52f1d
commands
silent
printf "\n=== CORRUPTING WRITE: slot now 0x%lx ===\n", *(unsigned long*)0x555569ae1ed0
x/i $pc
printf "--- BT ---\n"
bt 18
printf "=== end write ===\n"
continue
end
continue
printf "=== PROGRAM ENDED ===\n"
