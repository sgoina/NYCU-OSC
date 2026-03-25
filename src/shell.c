#include "shell.h"
#include "sbi.h"
#include "uart.h"
#include "string.h"
#include "ramfs.h"

// Command length limit
#define MAX_CMD_LEN 128
#define KERNEL_LOAD_ADDR 0x20000000UL

void start_bootLoader_shell(unsigned long hartid, unsigned long dtb_ptr){
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
            uart_puts("  load  - load kernel image.\n");
        }
        // command "load"
        else if (strcmp(buffer, "load") == 0){
            char* kernel_address = (char*)KERNEL_LOAD_ADDR;
            unsigned int magic = 0;
            unsigned int kernel_size = 0;

            uart_puts("Please run the Python script for loading kernel...\n");

            // check magic
            for (int i = 0; i < 4; i++){
                ((char*)&magic)[i] = uart_getc_raw();
            }
            if(magic != 0x544F4F42){
                uart_puts("Error: magic number is not match.\n");
                return;
            }

            // read kernel size
            for (int i = 0; i < 4; i++){
                ((char*)&kernel_size)[i] = uart_getc_raw();
            }
            uart_puts("Loading kernel, the Kernel Size: ");
            uart_hex(kernel_size);
            uart_puts("\n");

            // Start to get kernel image
            for (int i = 0; i < kernel_size; i++){
                kernel_address[i] = uart_getc_raw();
            }
            uart_puts("Kernel loaded successfully! Jump to kernel");
            uart_hex(KERNEL_LOAD_ADDR);
            uart_putc('\n');

            // function pointer
            void (*kernel_entry)(unsigned long, unsigned long) = (void (*)(unsigned long, unsigned long))KERNEL_LOAD_ADDR;
            kernel_entry(hartid, dtb_ptr);     // jump to kernel
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

void start_kernel_shell(){
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
            uart_puts("  ls    - show all filenames.\n");
            uart_puts("  cat \"filename\"  - show the file content.\n");
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
        // command "ls"
        else if (strcmp(buffer, "ls") == 0){
            ls_filenames();
        }
        // command "cat"
        else if (strncmp(buffer, "cat ", 4) == 0){
            cat_file_content(&buffer[4]);
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
