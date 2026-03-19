#include "shell.h"
#include "uart.h"

void new_kernel() {
    uart_puts("New Kernel! New Kernel! New Kernel! New Kernel!\n");
    start_shell();
}
