#include "shell.h"
#include "sbi.h"
#include "uart.h"
#include "string.h"
#include "ramfs.h"
#include "mem_alloc.h"
#include "defint.h"
#include "timer.h"
#include "task.h"

// Command length limit
#define MAX_CMD_LEN 128

void test_task_cb(char *arg) {
    uart_puts("[Task] Executing Priority ");
    uart_puts((char*)arg);
    uart_puts("\n");

    for (volatile int j = 0; j < 100000000; j++) {
        // do nothing
    }
    uart_puts("[Task] Executing Priority ");
    uart_puts((char*)arg);
    uart_puts("\n");
}

void start_kernel_shell(){
    char buffer[MAX_CMD_LEN];
    int idx;
    char c;
    uart_puts("OrangePi-RV2> ");
    add_timer(print_boot_time, NULL, 0);
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
        else if (strcmp(buffer, "exec") == 0){
            if (exec("prog.bin"))
                uart_puts("Failed to exec user program!\n");
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
            
            // 3. 註冊計時器
            if (*msg != '\0')
                add_timer(timeout_callback, msg, sec);
            else 
                uart_puts("Time setting failed! No message.\n");
        }
        else if (strcmp(buffer, "task") == 0){
            for (int i = 10; i < 30; i++) { // 先測 20 個就好，不然加了延遲會跑很久
                char prio_str[10];
                
                // 簡單的數字轉字串 (因為只有 10~29 兩位數)
                prio_str[0] = '0' + (i / 10);
                prio_str[1] = '0' + (i % 10);
                prio_str[2] = '\0';
                
                add_task(test_task_cb, prio_str, i);
            }
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
