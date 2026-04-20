#include "trap.h"
#include "uart.h"

void do_trap(struct pt_regs* regs) {
    // TODO: Implement this function
    // (1) Print the sepc and scause registers
    uart_puts("=== S-Mode trap ===\n");
    uart_puts("scause: ");
    uart_hex(regs->scause);
    uart_puts("\n");
    
    uart_puts("sepc: ");
    uart_hex(regs->sepc);
    uart_puts("\n");
    
    uart_puts("stval: ");
    uart_hex(regs->stval);
    uart_puts("\n");

    // (2) Increment the sepc register by 4 for traps
    regs->sepc += 4;
}
