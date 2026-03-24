.section ".text.boot"
.global _start

_start:
    la t0, __bss_start
    la t1, __bss_stop

clear_bss_loop:
    sd zero, 0(t0)    
    addi t0, t0, 8 
    blt t0, t1, clear_bss_loop

clear_bss_done: 
    la sp, _end
    tail new_kernel
