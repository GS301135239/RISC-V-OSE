.section .text.entry
.global _entry

_entry:
    
    la sp, stack_top

    la t0, sbss
    la t1, ebss
bss_zero_loop:
    bgeu t0, t1, bss_done
    sw zero, 0(t0)
    addi t0, t0, 4
    j bss_zero_loop
bss_done:

    # 跳转到 C 代码的 main 函数。
    call main

# 如果 main 函数返回，则进入无限循环。
spin:
    j spin
