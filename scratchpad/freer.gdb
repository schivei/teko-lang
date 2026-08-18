set pagination off
set width 0
break tk_chunk_free if ((unsigned long)$rdi+24) <= 0x555557db9410 && 0x555557db9410 < ((unsigned long)$rdi+24+*(unsigned long*)((char*)$rdi+8))
commands
silent
printf "=== FREE of chunk covering target ===\n"
bt 14
printf "---\n"
continue
end
run . -o outN3 --no-verify --release
printf "=== PROGRAM ENDED ===\n"
