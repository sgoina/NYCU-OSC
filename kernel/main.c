#include "shell.h"
#include "uart.h"

void new_kernel(unsigned long hartid, unsigned long dtb_ptr, unsigned int uart_reg) {
    uart_init(uart_reg);
    uart_puts("New Kernel! New Kernel! New Kernel! New Kernel!\n");
    start_shell();
}
