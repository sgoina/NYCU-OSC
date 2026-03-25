.section ".text.boot"
.global _start
.global relocate

.equ RELOC_DEST, 0x20000000
.equ LOAD_BASE,  0x00200000

_start:
    la t0, __bss_start
    la t1, __bss_stop

clear_bss_loop:
    sd zero, 0(t0)    
    addi t0, t0, 8 
    blt t0, t1, clear_bss_loop

clear_bss_done: 
    la sp, _end
    tail start_main      
    
relocate:
    // save parameters
    mv s0, a0              // s0 = hartid
    mv s1, a1              // s1 = dtb_ptr
    mv s2, a2              // s2 = continue_func(high)

    la t0, _load_start     // Old boot loader base
    la t1, _load_end       // Old boot loader end
    li t2, RELOC_DEST      // Destination

copy_loop:
    bgeu t0, t1, copy_done
    ld   t3, 0(t0)           // move data to tmp
    sd   t3, 0(t2)           // move tmp to new address
    addi t0, t0, 8
    addi t2, t2, 8
    j    copy_loop

copy_done:
    .insn i 0x0F, 0x1, x0, x0, 0x000
    
    li   t0, RELOC_DEST
    li   t1, LOAD_BASE
    sub  t2, t0, t1          // Calculate the offset of relocation
    add  sp, sp, t2          // New stack pointer address

    mv   a0, s0              // a0 = hartid
    mv   a1, s1              // s1 = dtb_ptr
    jr   s2                  // return to continue_func(high)
