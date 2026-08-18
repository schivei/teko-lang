set pagination off
run
printf "\n=== SLOT ===\n"
printf "r14=%p\n", $r14
set $rbxold = *(unsigned long*)($r14+8)
printf "rbx_old=[r14+8]=0x%lx\n", $rbxold
printf "slot=rbx_old+0x30=0x%lx\n", $rbxold+0x30
printf "val at slot=0x%lx\n", *(unsigned long*)($rbxold+0x30)
