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
#include "vm.h"
#include "vfs.h"
#include "tmpfs.h"

// Command length limit
#define MAX_CMD_LEN 128

extern unsigned long CPU_FREQ; // from timer.c

#define MAX_FD 16
struct file* fdt[MAX_FD] = {0};
extern struct mount* rootfs; //vfs.h
extern struct filesystem fs_list[]; //vfs.h

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
                unsigned int filesize = 0;
                void* prog_va = find_program(f, &filesize);
                
                if (prog_va != NULL) { 
                    struct task_struct* child_task = user_process_create(filesize, prog_va);
                    
                    if (child_task != NULL) 
                        thread_wait(child_task->pid); // wait user process
                    else
                        uart_puts("Failed to create user process!\n");
                }
                else
                    uart_puts("Failed to exec: Program not found!\n");
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
                add_timer(timeout_callback, (void*)msg_mm, sec * CPU_FREQ);
            }
            else 
                uart_puts("Time setting failed! No message.\n");
        }
        else if (strcmp(buffer, "task") == 0){
            thread_create(idle);
            for (int i = 0; i < 3; i++) {
                thread_create(foo);
            }
            //idle(); // Comment for shell can do after testing.
        }
        else if (strcmp(buffer, "file") == 0){
            rootfs = allocate(sizeof(struct mount));
            struct filesystem fs = {.name = "tmpfs", .setup_mount = tmpfs_setup_mount};
            int id = register_filesystem(&fs);
            fs_list[id].setup_mount(&fs_list[id], rootfs);
            // Test 1: Normal
            int fd = -1;
            int length = 0;
            for (int i = 0; i < MAX_FD; i++) {
                if (fdt[i] == 0 && vfs_open("/file.txt", O_CREAT, &fdt[i]) == 0){
                    fd = i;
                    break;
                }
            }
            length = vfs_write(fdt[fd], "Operating Systems Capstone", 26);
            if (fdt[fd]){
                vfs_close(fdt[fd]);
                fdt[fd] = 0;
            }

            char buf[64] = {0};
            for (int i = 0; i < MAX_FD; i++) {
                if (fdt[i] == 0 && vfs_open("/file.txt", 0, &fdt[i]) == 0){
                    fd = i;
                    break;
                }
            }
            length = vfs_read(fdt[fd], buf, sizeof(buf) - 1);
            if (fdt[fd]){
                vfs_close(fdt[fd]);
                fdt[fd] = 0;
            }

            uart_puts((strcmp(buf, "Operating Systems Capstone") == 0) ? "Test passed. Nice work!\n" : "Test failed. Keep trying!\n");
            // Test 2: No O_CREAT
            fd = -1;
            for (int i = 0; i < MAX_FD; i++) {
                if (fdt[i] == 0 && vfs_open("/Fakefile.txt", 0, &fdt[i]) == 0){
                    fd = i;
                    break;
                }
            }
            uart_puts((fd == -1) ? "Test passed. Nice work!\n" : "Test failed. Keep trying!\n");
            // Test 3: Read/Write Over file size
            fd = -1;
            int write_len = 0;
            int read_len = 0;
            int total_written = 0;
            int total_read = 0;

            for (int i = 0; i < MAX_FD; i++) {
                if (fdt[i] == 0 && vfs_open("/Hugefile.txt", O_CREAT, &fdt[i]) == 0) {
                    fd = i;
                    break;
                }
            }

            if (fd != -1) {
                char chunk_write[100];
                for (int i = 0; i < 50; i++) {
                    for(int j = 0; j < 100; j++) {
                        chunk_write[j] = 'A' + ((i * 100 + j) % 26); 
                    }
                    
                    int w = vfs_write(fdt[fd], chunk_write, 100);
                    if (w > 0) {
                        total_written += w;
                    }
                }
                vfs_close(fdt[fd]);
                fdt[fd] = 0;
            }

            fd = -1;
            for (int i = 0; i < MAX_FD; i++) {
                if (fdt[i] == 0 && vfs_open("/Hugefile.txt", 0, &fdt[i]) == 0) {
                    fd = i;
                    break;
                }
            }

            int is_data_correct = 1;
            if (fd != -1) {
                char chunk_read[100];
                for (int i = 0; i < 50; i++) {
                    int r = vfs_read(fdt[fd], chunk_read, 100);
                    if (r > 0) {
                        for (int j = 0; j < r; j++) {
                            if (chunk_read[j] != 'A' + ((total_read + j) % 26)) {
                                is_data_correct = 0;
                            }
                        }
                        total_read += r;
                    } else {
                        break; 
                    }
                }
                vfs_close(fdt[fd]);
                fdt[fd] = 0;
            }

            if (total_written == 4096 && total_read == 4096 && is_data_correct) {
                uart_puts("Test passed. Nice work!\n");
            } else {
                uart_puts("Test failed. Keep trying!\n");
            }
            
            // Test 4: Recreate same file
            struct vnode* dir_node = NULL;
            struct vnode* new_target = NULL;

            if (vfs_lookup("/", &dir_node) == 0) {
                if (dir_node != NULL && dir_node->v_ops != NULL) {
                    int result = dir_node->v_ops->create(dir_node, &new_target, "file.txt");
                    uart_puts((result == -1) ? "Test passed. Nice work!\n" : "Test failed. Keep trying!\n");
                }
                else {
                    uart_puts("Error: dir_node or v_ops is NULL!\n");
                }
            }
            else {
                uart_puts("Error: vfs_lookup failed to find root directory!\n");
            }
            // ==========================================
            // Test 5: Multi-level VFS 綜合測試
            // ==========================================
            fd = -1;
            char buf2[64] = {0};

            // 1. 建立兩層目錄 /home 以及 /home/user
            int res1 = vfs_mkdir("/home");
            int res2 = vfs_mkdir("/home/user");
            uart_puts((res1 == 0 && res2 == 0) ? "Test 5.1 passed (mkdir)\n" : "Test 5.1 failed\n");

            // 2. 嘗試建立已經存在的目錄 (應該要失敗回傳 -1)
            int res3 = vfs_mkdir("/home/user");
            uart_puts((res3 == -1) ? "Test 5.2 passed (mkdir exist protection)\n" : "Test 5.2 failed\n");

            // 3. 在深層目錄建立檔案 /home/user/secret.txt
            for (int i = 0; i < MAX_FD; i++) {
                if (fdt[i] == 0 && vfs_open("/home/user/secret.txt", O_CREAT, &fdt[i]) == 0){
                    fd = i;
                    break;
                }
            }
            uart_puts((fd != -1) ? "Test 5.3 passed (open in deep dir)\n" : "Test 5.3 failed\n");

            // 4. 寫入資料並關閉
            if (fd != -1) {
                vfs_write(fdt[fd], "Hidden Treasure", 15);
                vfs_close(fdt[fd]);
                fdt[fd] = 0;
            }

            // 5. 重新開啟深層檔案並讀取，驗證路徑解析 (vfs_lookup) 是否正常穿越目錄
            fd = -1;
            for (int i = 0; i < MAX_FD; i++) {
                if (fdt[i] == 0 && vfs_open("/home/user/secret.txt", 0, &fdt[i]) == 0){
                    fd = i;
                    break;
                }
            }
            if (fd != -1) {
                vfs_read(fdt[fd], buf2, 15);
                vfs_close(fdt[fd]);
                fdt[fd] = 0;
            }
            uart_puts((strcmp(buf2, "Hidden Treasure") == 0) ? "Test 5.4 passed (read from deep dir)\n" : "Test 5.4 failed\n");
            
            // ==========================================
            // Test 6: File System Mounting Test
            // ==========================================
            fd = -1;

            // 1. 建立掛載點
            vfs_mkdir("/mnt");

            // 2. 掛載一個全新的 tmpfs 到 /mnt
            int mount_res = vfs_mount("/mnt", "tmpfs");
            uart_puts((mount_res == 0) ? "Test 6.1 passed (mount success)\n" : "Test 6.1 failed\n");

            // 3. 在原本的根目錄建立 a.txt
            for (int i = 0; i < MAX_FD; i++) {
                if (fdt[i] == 0 && vfs_open("/a.txt", O_CREAT, &fdt[i]) == 0){
                    fd = i; break;
                }
            }
            vfs_write(fdt[fd], "Root FS", 7);
            vfs_close(fdt[fd]);
            fdt[fd] = 0;

            // 4. 在掛載點 /mnt 建立同名的 a.txt
            fd = -1;
            for (int i = 0; i < MAX_FD; i++) {
                if (fdt[i] == 0 && vfs_open("/mnt/a.txt", O_CREAT, &fdt[i]) == 0){
                    fd = i; break;
                }
            }
            vfs_write(fdt[fd], "Mounted FS", 10);
            vfs_close(fdt[fd]);
            fdt[fd] = 0;

            // 5. 驗證兩者沒有互相覆蓋 (隔離成功)
            char buf_root[32] = {0};
            char buf_mnt[32] = {0};

            fd = -1;
            for (int i = 0; i < MAX_FD; i++) {
                if (fdt[i] == 0 && vfs_open("/a.txt", 0, &fdt[i]) == 0){
                    fd = i; break;
                }
            }
            vfs_read(fdt[fd], buf_root, 7);
            vfs_close(fdt[fd]);
            fdt[fd] = 0;

            fd = -1;
            for (int i = 0; i < MAX_FD; i++) {
                if (fdt[i] == 0 && vfs_open("/mnt/a.txt", 0, &fdt[i]) == 0){
                    fd = i; break;
                }
            }
            vfs_read(fdt[fd], buf_mnt, 10);
            vfs_close(fdt[fd]);
            fdt[fd] = 0;

            if (strcmp(buf_root, "Root FS") == 0 && strcmp(buf_mnt, "Mounted FS") == 0) {
                uart_puts("Test 6.2 passed (Filesystems are isolated!)\n");
            } else {
                uart_puts("Test 6.2 failed\n");
            }

            
            free(rootfs);
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
