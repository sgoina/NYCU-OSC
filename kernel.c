#include "shell.h"
#include "uart.h"
#include "ramfs.h"

void start_new_kernel(unsigned long hartid, unsigned long dtb_ptr) {
    if (uart_init(dtb_ptr) != -1){
        uart_puts("New Kernel! New Kernel! New Kernel! New Kernel!\n");
        if (initrd_addr(dtb_ptr) != -1)
            uart_puts("File system initialization is successful!\n");
        else
            uart_puts("Can't initialize file system!\n");
    }
    start_kernel_shell(hartid, dtb_ptr);
}
