set pagination off
break tk_free_block if (unsigned long)$rdi <= 0x555569ae1ed0 && 0x555569ae1ed0 < ((unsigned long)$rdi + (unsigned long)$rsi)
commands
silent
printf "\n=== tk_free_block PARK covering slot: p=0x%lx bytes=%lu ===\n", $rdi, $rsi
bt 12
printf "=== end ===\n"
continue
end
break tk_arena_pop
commands
silent
printf "ARENA_POP hit\n"
continue
end
run
printf "=== ENDED ===\n"
