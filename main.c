#include "shell.h"
#include "uart.h"

void start_kernel() {
    uart_puts("\nOSC 314553022\n");
    start_shell();
}
