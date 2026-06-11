#include "shell.h"
#include "uart.h"

// a0 = hartid, a1 = dtb_ptr
void start_main(unsigned long hartid, unsigned long dtb_ptr) {
    if (uart_init(dtb_ptr) != -1){
        uart_puts("\nOSC 314553022\n");
        uart_puts("This is a boot loader!!!\n");
    }
    start_bootLoader_shell(hartid, dtb_ptr);
}
