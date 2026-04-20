#include "shell.h"
#include "sbi.h"
#include "uart.h"
#include "string.h"
#include "ramfs.h"
#include "mem_alloc.h"

// Command length limit
#define MAX_CMD_LEN 128
#define KERNEL_LOAD_ADDR 0x00200000UL
#define RELOCATE_BASE 0x20000000UL
#define BEFORE_BASE 0x00200000UL

extern void relocate(unsigned long hartid, unsigned long dtb_ptr, void* continue_func); // relocate function in start.S

void load_kernel(unsigned long hartid, unsigned long dtb_ptr) {
    uart_puts("Relocation finished.\n");
    
    char* kernel_address = (char*)KERNEL_LOAD_ADDR;
    unsigned int magic = 0;
    unsigned int kernel_size = 0;
    uart_puts("Please run the Python code for loading kernel...\n");

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
    uart_puts("Kernel loaded successfully! Jump to kernel: ");
    uart_hex(KERNEL_LOAD_ADDR);
    uart_putc('\n');
    
    asm volatile(
        ".option push\n"             // save current setting of environment
        ".option arch, +zifencei\n"  // add extra instruction set "zifencei"
        "fence.i\n"                  // use "fence.i" to Flush D-Cache, Invalidate I-Cache, Pipeline Synchronization
        ".option pop\n"              // load origin setting of environment
        ::: "memory"                 // flush the data in cache to memory
    );
    
    // function pointer
    void (*kernel_entry)(unsigned long, unsigned long) = (void (*)(unsigned long, unsigned long))KERNEL_LOAD_ADDR;
    kernel_entry(hartid, dtb_ptr);     // jump to kernel
}

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
            uart_puts("Preparing to relocate Boot Loader...\n");
            unsigned long moving_offset = RELOCATE_BASE - BEFORE_BASE;
            void *continue_func = (void *)((unsigned long)load_kernel + moving_offset);
            relocate(hartid, dtb_ptr, continue_func); // relocate and return to load_kernel()
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
            uart_puts("  allocate - test allocation.\n");
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
        else if (strcmp(buffer, "allocate") == 0){
            alloc_test();
        }
        else if (strcmp(buffer, "exec") == 0){
            if (exec("prog.bin"))
                uart_puts("Failed to exec user program!\n");
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
