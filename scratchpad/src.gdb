set pagination off
break tk_slice_elem_box if *(unsigned long*)((char*)$rdi+0x30) == 0x1c52f1d
commands
silent
printf "\n=== elem_box copying corrupt source: elem(r.decl)=0x%lx  [elem+0x30]=0x%lx esz=%lu ===\n", $rdi, *(unsigned long*)((char*)$rdi+0x30), $rsi
bt 12
printf "=== end ===\n"
continue
end
run
printf "=== ENDED ===\n"
