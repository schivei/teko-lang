set pagination off
break tk_slice_elem_box
commands
silent
if $rdi != 0 && *(unsigned long*)((char*)$rdi+0x30) != 0x1c52f1d && $rsi >= 0x28 && $rsi <= 0x60
  # candidate boxes of aggregate size ~TypeDecl; log those whose alloc will be near the slot
end
continue
end
# Instead: watch the slot; when the corrupting write happens, we already know. Capture the box that PRODUCES td.
# Break at tk_slice_elem_box, print esz for every call, and stop after crash address known.
delete
break tk_alloc
commands
silent
continue
end
delete
run
