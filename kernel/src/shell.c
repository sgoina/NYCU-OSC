#include "shell.h"
#include "sbi.h"
#include "uart.h"
#include "string.h"
// Command length limit
#define MAX_CMD_LEN 128
#define KERNEL_LOAD_ADDR 0x20000000UL

void start_shell(){
    char buffer[MAX_CMD_LEN];
    int idx;
    char c;
    
    while (1) {
        uart_puts("OrangePi-RV2> ");
        idx = 0;
        buffer[idx] = '\0';

        while (1) {
            c = uart_getc();
            // enter
            if (c == '\n' || c == '\r') {
                buffer[idx] = '\0';
                uart_putc('\n');
                break; 
            }
            // other
            else if (idx < MAX_CMD_LEN - 1) {
                buffer[idx++] = c;
                uart_putc(c);
            }
        }
        // if command is ""
        if (idx == 0) 
            continue;
        // command "help"
        else if (strcmp(buffer, "help") == 0) {
            uart_puts("Available commands:\n");
            uart_puts("  help  - show all commands.\n");
            uart_puts("  hello - print Hello World.\n");
            uart_puts("  info  - print system info.\n");
        }
        // command "hello"
        else if (strcmp(buffer, "hello") == 0)
            uart_puts("Hello World!\n");
        // command "info"
        else if (strcmp(buffer, "info") == 0) {
            uart_puts("System information:\n");
            uart_puts("  OpenSBI specification version: ");
            uart_hex(sbi_get_spec_version());
            uart_putc('\n');
            uart_puts("  implementation ID: ");
            uart_hex(sbi_get_impl_id());
            uart_putc('\n');
            uart_puts("  implementation version: ");
            uart_hex(sbi_get_impl_version());
            uart_putc('\n');
        }
        // unknown command
        else {
            uart_puts("Unknown command: ");
            uart_puts(buffer);
            uart_putc('\n');
            uart_puts("Use help to get commands.\n");
        }
    }
}
