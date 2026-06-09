#include "shell.h"
#include "sbi.h"
#include "deviceTree.h"
#include "uart.h"
#include "ramfs.h"
#include "mem_alloc.h"
#include "timer.h"
#include "thread.h"
#include "plic.h"
#include "vm.h"
#include "vfs.h"

extern char _start[]; // from start.S
extern char _end[];   // from start.S
extern unsigned int CPU_FREQ; // from timer.c

unsigned long boot_cpu_hartid;
unsigned long boot_dtb_ptr;

void irq_enable(){
    // The 2nd bit of sstatus => SIE (Supervisor Interrupt Enable)
    // The 19th bit of sstatus => SUM (permit Supervisor User Memory access) if 0, supervisor mode can't access user virtual memory
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    sstatus |= (1 << 1); 
    sstatus |= (1 << 18);
    asm volatile("csrw sstatus, %0" : : "r"(sstatus));
}

void start_main(unsigned long hartid, unsigned long dtb_ptr) {
    dtb_ptr = phys_to_virt(dtb_ptr);
    boot_cpu_hartid = hartid;
    boot_dtb_ptr = dtb_ptr;
    
    plic_init(dtb_ptr);
    uart_init(dtb_ptr);
    irq_enable();
    
    uart_puts("New Kernel! New Kernel! New Kernel! New Kernel!\n");
    
    if (initrd_addr(dtb_ptr) != -1)
        uart_puts("File system initialization is successful!\n");
    else
        uart_puts("Can't initialize file system!\n");
    init_mem(dtb_ptr);
    init_vfs();
    
    // Set Thread Pointer
    asm volatile("move tp, %0" : : "r"(thread_create(idle)));
    timer_init(dtb_ptr); // Open timer interrupt (include heartbeat timer)
    thread_create(start_kernel_shell);
    idle();
}
