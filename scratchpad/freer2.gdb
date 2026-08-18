set pagination off
break tk_chunk_free if ((unsigned long)$rdi+24) <= 0x555569ae1ed0 && 0x555569ae1ed0 < ((unsigned long)$rdi+24+*(unsigned long*)((char*)$rdi+8))
commands
silent
printf "=== FREE covering slot ===\n"
bt 12
printf "---\n"
continue
end
run
printf "=== ENDED ===\n"
