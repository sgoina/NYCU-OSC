.section ".text.boot"
.global _start
.global relocate

.equ RELOC_DEST, 0x20000000

_start:
    la t0, __bss_start
    la t1, __bss_stop

clear_bss_loop:
    sd zero, 0(t0)    
    addi t0, t0, 8 
    blt t0, t1, clear_bss_loop

clear_bss_done: 
    la a2, handle_exception
    csrw stvec, a2
    la sp, _end
    tail start_main      
    
relocate:
    // save parameters
    mv s0, a0              // s0 = hartid
    mv s1, a1              // s1 = dtb_ptr
    mv s2, a2              // s2 = continue_func (high address)

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
    // .insn: Assembler
    // i    : I-type (Immediate)
    // 0x0F : Opcode (MISC-MEM)
    // 0x1  : Funct3 (fence.i)
    // x0   : rd (register destination)
    // x0   : rs1 (register source 1)
    // 0x000: Immediate number (12 bits)
    
    li   t0, RELOC_DEST
    la   t1, _load_start
    sub  t2, t0, t1          // Calculate the offset of relocation
    add  sp, sp, t2          // New stack pointer address

    mv   a0, s0              // a0 = hartid
    mv   a1, s1              // s1 = dtb_ptr
    jr   s2                  // return to continue_func(high)

.global handle_exception
.align 2
handle_exception:
    /*
     * Retrieve kernel sp from sscratch and
     * save user sp to sscratch
     */
    csrrw sp, sscratch, sp
    bnez sp, save_context

    csrr sp, sscratch
save_context:
    /* Save context */
    addi sp, sp, -8 * 35
    sd ra,  8 *  0(sp)
    sd gp,  8 *  2(sp)
    sd tp,  8 *  3(sp)
    sd t0,  8 *  4(sp)
    sd t1,  8 *  5(sp)
    sd t2,  8 *  6(sp)
    sd s0,  8 *  7(sp)
    sd s1,  8 *  8(sp)
    sd a0,  8 *  9(sp)
    sd a1,  8 * 10(sp)
    sd a2,  8 * 11(sp)
    sd a3,  8 * 12(sp)
    sd a4,  8 * 13(sp)
    sd a5,  8 * 14(sp)
    sd a6,  8 * 15(sp)
    sd a7,  8 * 16(sp)
    sd s2,  8 * 17(sp)
    sd s3,  8 * 18(sp)
    sd s4,  8 * 19(sp)
    sd s5,  8 * 20(sp)
    sd s6,  8 * 21(sp)
    sd s7,  8 * 22(sp)
    sd s8,  8 * 23(sp)
    sd s9,  8 * 24(sp)
    sd s10, 8 * 25(sp)
    sd s11, 8 * 26(sp)
    sd t3,  8 * 27(sp)
    sd t4,  8 * 28(sp)
    sd t5,  8 * 29(sp)
    sd t6,  8 * 30(sp)
    csrr s0, sscratch
    csrr s1, sepc
    csrr s2, sstatus
    csrr s3, scause
    csrr s4, stval
    sd s0,  8 *  1(sp)
    sd s1,  8 * 31(sp)
    sd s2,  8 * 32(sp)
    sd s3,  8 * 33(sp)
    sd s4,  8 * 34(sp)

    csrw sscratch, zero
    move a0, sp
    call do_trap

ret_from_exception:
    ld s0,  8 * 32(sp)
    andi s0, s0, (1 << 8)
    bnez s0, 1f

    addi s0, sp, 8 * 35
    csrw sscratch, s0
1:
    /* Restore context */
    ld s1,  8 * 31(sp)
    ld s2,  8 * 32(sp)
    csrw sepc, s1
    csrw sstatus, s2
    ld ra,  8 *  0(sp)
    ld gp,  8 *  2(sp)
    ld tp,  8 *  3(sp)
    ld t0,  8 *  4(sp)
    ld t1,  8 *  5(sp)
    ld t2,  8 *  6(sp)
    ld s0,  8 *  7(sp)
    ld s1,  8 *  8(sp)
    ld a0,  8 *  9(sp)
    ld a1,  8 * 10(sp)
    ld a2,  8 * 11(sp)
    ld a3,  8 * 12(sp)
    ld a4,  8 * 13(sp)
    ld a5,  8 * 14(sp)
    ld a6,  8 * 15(sp)
    ld a7,  8 * 16(sp)
    ld s2,  8 * 17(sp)
    ld s3,  8 * 18(sp)
    ld s4,  8 * 19(sp)
    ld s5,  8 * 20(sp)
    ld s6,  8 * 21(sp)
    ld s7,  8 * 22(sp)
    ld s8,  8 * 23(sp)
    ld s9,  8 * 24(sp)
    ld s10, 8 * 25(sp)
    ld s11, 8 * 26(sp)
    ld t3,  8 * 27(sp)
    ld t4,  8 * 28(sp)
    ld t5,  8 * 29(sp)
    ld t6,  8 * 30(sp)
    ld sp,  8 *  1(sp)
    sret
