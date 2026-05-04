#include "shell.h"
#include "deviceTree.h"
#include "uart.h"
#include "ramfs.h"
#include "mem_alloc.h"
#include "timer.h"
#include "thread.h"
#include "plic.h"

extern char _start[]; // from start.S
extern char _end[];   // from start.S
extern unsigned int CPU_FREQ; // from timer.c

unsigned long boot_cpu_hartid;
unsigned long boot_dtb_ptr;

void irq_enable(){
    // The 2nd bit of sstatus => SIE (Supervisor Interrupt Enable)
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    sstatus |= (1 << 1); 
    asm volatile("csrw sstatus, %0" : : "r"(sstatus));
}

void start_main(unsigned long hartid, unsigned long dtb_ptr) {
    boot_cpu_hartid = hartid;
    boot_dtb_ptr = dtb_ptr;
    
    plic_init(dtb_ptr);
    uart_init(dtb_ptr);
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
    
    asm volatile("move tp, %0" : : "r"(thread_create(idle)));
    thread_create(start_kernel_shell);
    idle();
    
    //start_kernel_shell();
}
