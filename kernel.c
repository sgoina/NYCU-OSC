#include "shell.h"
#include "deviceTree.h"
#include "uart.h"
#include "ramfs.h"
#include "mem_alloc.h"
#include "timer.h"

extern char _start[];
extern char _end[];

void start_main(unsigned long hartid, unsigned long dtb_ptr) {
    if (uart_init(dtb_ptr) != -1){
        uart_puts("New Kernel! New Kernel! New Kernel! New Kernel!\n");
        if (initrd_addr(dtb_ptr) != -1)
            uart_puts("File system initialization is successful!\n");
        else
            uart_puts("Can't initialize file system!\n");
    }
    
    init_mem(dtb_ptr);
    timer_init(dtb_ptr);
    
    start_kernel_shell(hartid, dtb_ptr);
}
