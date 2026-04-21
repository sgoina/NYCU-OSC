#include "shell.h"
#include "deviceTree.h"
#include "uart.h"
#include "ramfs.h"
#include "mem_alloc.h"
#include "timer.h"
#include "plic.h"

extern char _start[];
extern char _end[];
extern unsigned int CPU_FREQ; // from timer.c

unsigned long boot_cpu_hartid;

void irq_enable(){
    // 開啟全域中斷 Supervisor Interrupt Enable (sstatus.SIE, bit 1)
    // (如果你的系統尚未開啟的話，必須設為 1 才能接收硬體中斷)
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    sstatus |= (1 << 1); 
    asm volatile("csrw sstatus, %0" : : "r"(sstatus));
}

void start_main(unsigned long hartid, unsigned long dtb_ptr) {
    boot_cpu_hartid = hartid;
    
    uart_init(dtb_ptr);
    plic_init(dtb_ptr);
    timer_init(dtb_ptr);
    irq_enable();
    
    uart_puts("New Kernel! New Kernel! New Kernel! New Kernel!\n");
    uart_puts("CPU frenquency: ");
    uart_dec(CPU_FREQ);
    uart_puts(" HZ\n");
    
    if (initrd_addr(dtb_ptr) != -1)
        uart_puts("File system initialization is successful!\n");
    else
        uart_puts("Can't initialize file system!\n");
    init_mem(dtb_ptr);
    
    start_kernel_shell(hartid, dtb_ptr);
}
