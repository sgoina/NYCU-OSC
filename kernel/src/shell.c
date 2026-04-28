#include "shell.h"
#include "sbi.h"
#include "uart.h"
#include "string.h"
#include "ramfs.h"
#include "mem_alloc.h"
#include "defint.h"
#include "timer.h"
#include "task.h"
#include "thread.h"

// Command length limit
#define MAX_CMD_LEN 128

void idle() {
    while (1) {
        kill_zombies();
        schedule();
    }
}

void foo() {
    for (int i = 0; i < 5; i++) {
        uart_puts("Process ID: ");
        uart_hex(get_current()->pid);
        uart_puts(" ");
        uart_hex(i);
        uart_puts("\n");
        for (int j = 0; j < 100000000; j++)
            ;
        schedule();
    }
    thread_exit(); 
}

void start_kernel_shell(){
    char buffer[MAX_CMD_LEN];
    int idx;
    char c;
    uart_puts("OrangePi-RV2> ");
    //add_timer(print_boot_time, NULL, 0);
    while (1) {
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
        // command "help"
        if (strcmp(buffer, "help") == 0) {
            uart_puts("Available commands:\n");
            uart_puts("  help  - show all commands.\n");
            uart_puts("  hello - print Hello World.\n");
            uart_puts("  info  - print system info.\n");
            uart_puts("  ls    - show all filenames.\n");
            uart_puts("  cat \"filename\"  - show the file content.\n");
            uart_puts("  exec  - execute a user program.\n");
            uart_puts("  settimeout \"x\" \"text\" - show \"text\" after \"x\" sec.\n");
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
        // command "allocate"
        else if (strcmp(buffer, "allocate") == 0){
            uart_puts("This command isn't supported. Please add alloc_test() into kernel.\n");
        }
        // command "exec"
        else if (strncmp(buffer, "exec ", 5) == 0){
            char *f = buffer + 5; // skip "exec "
            if (*f != '\0') {
                if (exec(f))
                    uart_puts("Failed to exec user program!\n");
            }
            else
                uart_puts("Failed to exec user program with no name!\n");
        }
        // command "settimeout"
        else if (strncmp(buffer, "settimeout ", 11) == 0){
            char *p = buffer + 11; // skip "settimeout "
            int sec = 0;
            while (*p >= '0' && *p <= '9') {
                sec = sec * 10 + (*p - '0');
                p++;
            }
            if (sec == 0) {
                uart_puts("Time setting failed! No number or seconds is 0.\n");
                continue;
            }
            if (*p != '\0')
                p++; // skip backspace
            char *msg = p;
            // timer setting
            if (*msg != '\0'){
                int msg_len = 0;
                while(msg[msg_len] != '\0'){
                    msg_len++;
                }
                char *msg_mm = (char*)allocate(msg_len + 1);
                
                for(int i = 0; i <= msg_len; i++) {
                    msg_mm[i] = msg[i];
                }
                add_timer(timeout_callback, (void*)msg_mm, sec);
            }
            else 
                uart_puts("Time setting failed! No message.\n");
        }
        else if (strcmp(buffer, "task") == 0){
            asm volatile("move tp, %0" : : "r"(thread_create(idle)));
            for (int i = 0; i < 3; i++) {
                thread_create(foo);
            }
            idle();
        }
        // unknown command (except type nothing)
        else if (idx != 0){
            uart_puts("Unknown command: ");
            uart_puts(buffer);
            uart_putc('\n');
            uart_puts("Use help to get commands.\n");
        }
        uart_puts("OrangePi-RV2> ");
    }
}
